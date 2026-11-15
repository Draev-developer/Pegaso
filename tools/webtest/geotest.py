"""Country flags: the server list of a Pegaso which never connects.

usage: python geotest.py        (needs Pillow, so not -I)

The server list is filled from staticservers.dat with addresses of known countries (taken
from the data file itself, so the test does not depend on who owns which address) and one
private address. The picture of the list is saved, and each row is checked for a flag: the
16x11 pixels after the server icon are compared with that flag in geo\\flags.bmp.
"""
import bisect
import ctypes
import os
import socket
import struct
import sys
import time

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import btuitest  # noqa: E402
import ratetest as r  # noqa: E402

MP_HM_SRVR = 10210
IDC_SERVLIST = 2091
WANTED = ['ES', 'FR', 'DE', 'IT', 'PT', 'US', 'JP', 'BR', 'CH']


class Geo:
    def __init__(self, folder):
        d = open(os.path.join(folder, 'ip-country.dat'), 'rb').read()
        _, n, c = struct.unpack('<III', d[4:16])
        self.codes = [d[16 + 2 * i:18 + 2 * i].decode() for i in range(c)]
        self.starts = struct.unpack('<%dI' % n, d[16 + 2 * c:16 + 2 * c + 4 * n])
        self.owners = d[16 + 2 * c + 4 * n:]
        self.flags = Image.open(os.path.join(folder, 'flags.bmp')).convert('RGB')

    def address_of(self, code):
        index = self.codes.index(code)
        i = self.owners.index(bytes([index]))
        return socket.inet_ntoa(struct.pack('>I', self.starts[i] + 1))

    def flag(self, code):
        i = self.codes.index(code)
        return self.flags.crop((16 * i, 0, 16 * i + 16, 11))


def find_flag(shot, flag):
    """Position of the flag in the picture (exact pixels where the flag is opaque), or None."""
    mask = [(x, y) for y in range(11) for x in range(16) if flag.getpixel((x, y)) != (255, 0, 255)]
    want = [flag.getpixel(p) for p in mask]
    px = shot.load()
    first = want[0]
    fx, fy = mask[0]
    for y in range(shot.height - 11):
        for x in range(shot.width - 16):
            if px[x + fx, y + fy] != first:
                continue
            if all(px[x + mx, y + my] == v for (mx, my), v in zip(mask, want)):
                return x, y
    return None


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    geo = Geo(os.path.join(btuitest.RELEASE, 'geo'))
    r.make_instance(w.RUN, 4700)
    servers = [(code, geo.address_of(code)) for code in WANTED] + [('private', '192.168.50.1')]
    open(os.path.join(w.RUN, 'config', 'staticservers.dat'), 'w', encoding='ascii').write(
        ''.join('%s:4661,0,Servidor de prueba %s\n' % (ip, code) for code, ip in servers))
    pegaso = r.start(w.RUN)
    ok = True
    try:
        hwnd = u.main_window(pegaso.pid)
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, MP_HM_SRVR, 0)
        time.sleep(3)
        path = u.screenshot(hwnd, 'shot_geo_servers.png')
        print('screenshot:', path)
        shot = Image.open(path).convert('RGB')
        rows = {}
        for code, ip in servers:
            if code == 'private':
                continue
            pos = find_flag(shot, geo.flag(code))
            rows[code] = pos
            print('  %-3s %-16s flag %s' % (code, ip, 'at %s' % (pos,) if pos else 'NOT FOUND'))
            ok &= pos is not None
        # one flag per row, all in one column of the list
        xs = {pos[0] for pos in rows.values() if pos}
        ys = [pos[1] for pos in rows.values() if pos]
        print('flags in one column: %s; on %d different rows' % (len(xs) == 1, len(set(ys))))
        ok &= len(xs) == 1 and len(set(ys)) == len(WANTED) and pegaso.poll() is None

        # ---- the sources of a download: a link which names sources in three countries
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
        time.sleep(1)
        sources = [(code, geo.address_of(code)) for code in ('ES', 'JP', 'BR')]
        r.send_link(hwnd, 'ed2k://|file|Video.prueba.avi|%d|%s|/|sources,%s|/' % (
            20 * 1024 * 1024, w.ed2k_hash(os.urandom(1024)), ','.join('%s:4662' % ip for _, ip in sources)))
        time.sleep(3)
        lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
        mem = u.Remote(pegaso.pid)
        try:
            u.select_item(lv, mem, 0)
            where = (30 << 16) | 400  # the first row, away from the name: a double click unfolds the sources
            for msg in (0x0201, 0x0202, 0x0203, 0x0202):  # button down, up, double click, up
                u.user32.PostMessageW(lv, msg, 1 if msg != 0x0202 else 0, where)
            time.sleep(2)
            count = u.user32.SendMessageW(lv, u.LVM_GETITEMCOUNT, 0, 0)
        finally:
            mem.close()
        path = u.screenshot(hwnd, 'shot_geo_sources.png')
        print('screenshot:', path, '- rows in the download list:', count)
        shot = Image.open(path).convert('RGB')
        for code, ip in sources:
            pos = find_flag(shot, geo.flag(code))
            print('  source %-3s %-16s flag %s' % (code, ip, 'at %s' % (pos,) if pos else 'NOT FOUND'))
            ok &= pos is not None
        ok &= pegaso.poll() is None
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        if not w.close_gracefully(pegaso, 60):
            pegaso.kill()


if __name__ == '__main__':
    main()
