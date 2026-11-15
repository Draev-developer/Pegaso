"""The "origin" column of the download list: E (eMule), T (torrent), W (web) and mixtures.

usage: python -I origintest.py

One download of every kind is added to a Pegaso which stays offline (no server, no Kad;
the trackers and web addresses do not exist), and the column is read back.
"""
import hashlib
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import btuitest  # noqa: E402
import btfilestest  # noqa: E402
import ratetest as r  # noqa: E402
import hybridtest as h  # noqa: E402

SIZE = 3 * 1024 * 1024
PIECE = 256 * 1024


def torrent(name, data, web=None):
    pieces = b''.join(hashlib.sha1(data[i:i + PIECE]).digest() for i in range(0, len(data), PIECE))
    meta = {'info': {'name': name, 'piece length': PIECE, 'pieces': pieces, 'length': len(data)},
            'announce': 'http://127.0.0.1:9/announce'}
    if web:
        meta['url-list'] = web
    path = os.path.join(HERE, name + '.torrent')
    open(path, 'wb').write(btfilestest.bencode(meta))
    return path


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ip = r.lan_ip()
    dead = 'http://%s:9/' % ip  # nothing listens there
    files = {n: os.urandom(SIZE) for n in ('Documento.prueba.pdf', 'Video.prueba.avi', 'Imagen.prueba.iso',
                                           'Video.prueba.2.mkv', 'Video.prueba.5.mkv')}
    names = list(files)
    ed2k = {n: 'ed2k://|file|%s|%d|%s|/' % (n.replace(' ', '%20'), SIZE, w.ed2k_hash(files[n])) for n in names}
    expected = {
        names[0]: 'E',                       # eMule only
        names[1]: 'E+W',                     # eMule with a web source
        names[2]: 'E+T',                     # eMule with a torrent of the same file
        names[3]: 'E+T+W',                   # all three
        'Video.prueba.3.mkv': 'T',
        'Video.prueba.4.mkv': 'T+W',          # torrent with a web seed
        'slow.bin': 'W',                     # web download
    }

    r.make_instance(w.RUN, 4700, 'TorrentPort=6881\n')
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('TorrentEnabled=0\n', '')
    open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
    server = w.start_server()
    pegaso = r.start(w.RUN)
    mem = None
    try:
        hwnd = u.main_window(pegaso.pid)
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
        time.sleep(1)
        lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
        mem = u.Remote(pegaso.pid)

        r.send_link(hwnd, ed2k[names[0]])
        # a web source goes inside the link, before its closing "|/"
        r.send_link(hwnd, ed2k[names[1]][:-1] + 's=%s%s|/' % (dead, 'video.avi'))
        r.send_link(hwnd, ed2k[names[2]])
        r.send_link(hwnd, ed2k[names[3]][:-1] + 's=%s%s|/' % (dead, 'video2.mkv'))
        r.send_link(hwnd, torrent('Video.prueba.3.mkv', os.urandom(SIZE)))
        r.send_link(hwnd, torrent('Video.prueba.4.mkv', os.urandom(SIZE), dead + 'video4.mkv'))
        r.send_link(hwnd, w.BASE + '/slow.bin')
        time.sleep(6)
        for n in (names[2], names[3]):
            rows = btuitest.rows(lv, mem)
            row = next(i for i, x in enumerate(rows) if x[0] == n)
            h.link_torrent(pegaso.pid, lv, mem, row, torrent(n, files[n]))
        time.sleep(8)

        ok = True
        rows = btuitest.rows(lv, mem)
        print('%-32s %-8s %s' % ('download', 'column', 'expected'))
        for name, want in expected.items():
            row = next((x for x in rows if x[0] == name), None)
            got = row[-2] if row else None  # the last column is the trust (fake detector)
            print('%-32s %-8s %s' % (name, got, want))
            ok &= got == want
        ok &= len(rows) == len(expected)
        print('screenshot:', u.screenshot(hwnd, 'shot_origin.png'))

        # ---- after a restart: the same rows (no row of its own for the torrents which are
        # sources of another download), the same letters
        mem.close()
        mem = None
        if not w.close_gracefully(pegaso, 60):
            pegaso.kill()
        time.sleep(3)
        pegaso = r.start(w.RUN)
        hwnd = u.main_window(pegaso.pid)
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
        time.sleep(8)
        lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
        mem = u.Remote(pegaso.pid)
        rows = btuitest.rows(lv, mem)
        after = {x[0]: x[-2] for x in rows}
        want_after = {n: v for n, v in expected.items() if n in after or v != 'W'}  # a finished web download leaves the list
        print('after a restart: %d rows, %s' % (len(rows), after))
        ok &= len(rows) == len(after) and after == want_after and pegaso.poll() is None
        print('screenshot:', u.screenshot(hwnd, 'shot_origin2.png'))
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        if mem:
            mem.close()
        if not w.close_gracefully(pegaso, 60):
            pegaso.kill()
        server.kill()


if __name__ == '__main__':
    main()
