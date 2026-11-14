"""Verdicts on torrents, carried by Kad between Pegaso users.

usage: python -I btratetest.py

  A1, A2 (pegaso-test-a1 / -a2) add a torrent and mark it as fake through the menu
  B      (pegaso-test)          adds the same torrent later

B has to learn from Kad that the torrent is called a fake.

Kad keeps one note per IP address for a file (and Pegaso counts one per address), so from a
single Internet connection only one of the two verdicts can arrive: the test passes when B
is told about one. The warning itself needs two users on different connections; that part
is the same code as for eMule downloads (ratetest.py).

Uses the real Kad network: the three instances join it and two small notes about a torrent
made up for this test (random content, a tracker which does not exist) are stored on it;
they expire by themselves within a day. Nothing is downloaded.
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
import nettest  # noqa: E402
import ratetest as r  # noqa: E402

MP_PEGASO_RATE = 10982
IDC_RT_FAKE, IDC_RT_OTHERS = 5053, 5062
BM_CLICK = 0x00F5


def make_torrent():
    name = 'Pelicula.de.prueba.%s.mkv' % os.urandom(4).hex()
    data = os.urandom(400000)
    pieces = b''.join(hashlib.sha1(data[i:i + 16384]).digest() for i in range(0, len(data), 16384))
    info = {'name': name, 'piece length': 16384, 'pieces': pieces, 'length': len(data)}
    path = os.path.join(HERE, 'valorar.torrent')
    open(path, 'wb').write(btfilestest.bencode({'info': info, 'announce': 'http://127.0.0.1:9/announce'}))
    return name, path


def bring_up(folder):
    """Starts an instance, joins Kad, shows the transfers page. Returns (process, window, list, memory)."""
    proc = r.start(folder)
    hwnd = u.main_window(proc.pid)
    u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
    u.user32.PostMessageW(hwnd, u.WM_COMMAND, nettest.MP_HM_CON, 0)
    t0 = time.time()
    while time.time() - t0 < 90 and 'external Kad Port' not in r.log_of(folder):
        time.sleep(3)
    time.sleep(5)
    return proc, hwnd, u.find_child(hwnd, u.IDC_DOWNLOADLIST), u.Remote(proc.pid)


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    name, torrent = make_torrent()
    folders = [w.RUN + '-a1', w.RUN + '-a2', w.RUN]
    for i, folder in enumerate(folders):
        r.make_instance(folder, 4720 + 20 * i, 'TorrentPort=%d\n' % (6891 + 10 * i))
        ini = os.path.join(folder, 'config', 'preferences.ini')
        text = open(ini).read().replace('TorrentEnabled=0\n', '').replace('NetworkKademlia=0\n', '')
        open(ini, 'w', encoding='ascii', newline='\r\n').write(text)

    running = []
    ok = True
    try:
        # ---- A1 and A2 mark the torrent as fake
        for folder in folders[:2]:
            proc, hwnd, lv, mem = bring_up(folder)
            running.append((proc, mem))
            r.send_link(hwnd, torrent)
            time.sleep(5)
            u.select_item(lv, mem, 0)
            u.user32.PostMessageW(lv, u.WM_COMMAND, MP_PEGASO_RATE, 0)
            time.sleep(2)
            dlg, _ = r.rate_dialog(proc.pid)
            print('%s: rows=%s, rating window=%s' % (os.path.basename(folder), [x[0] for x in btuitest.rows(lv, mem)], dlg is not None))
            ok &= dlg is not None
            if dlg:
                u.user32.SendMessageW(u.find_child(dlg, IDC_RT_FAKE), BM_CLICK, 0, 0)
                time.sleep(1)
                u.user32.PostMessageW(dlg, u.WM_COMMAND, 1, 0)
        # the notes travel to the Kad nodes close to the key
        t0 = time.time()
        published = 0
        while time.time() - t0 < 150 and published < 2:
            time.sleep(10)
            published = sum('Publishing the verdict' in r.log_of(f) for f in folders[:2])
        print('published by %d of 2 after %ds' % (published, time.time() - t0))
        ok &= published == 2
        time.sleep(60)

        # ---- B adds the torrent and has to be warned
        proc, hwnd, lv, mem = bring_up(folders[2])
        running.append((proc, mem))
        r.send_link(hwnd, torrent)
        t0 = time.time()
        others = ''
        while time.time() - t0 < 180 and 'falso' not in others:
            time.sleep(15)
            u.select_item(lv, mem, 0)
            u.user32.PostMessageW(lv, u.WM_COMMAND, MP_PEGASO_RATE, 0)
            time.sleep(2)
            dlg, _ = r.rate_dialog(proc.pid)
            others = r.text_of(u.find_child(dlg, IDC_RT_OTHERS)) if dlg else ''
            if dlg:
                u.user32.PostMessageW(dlg, u.WM_COMMAND, 2, 0)
            print('  t=%3ds B is told: %r' % (time.time() - t0, others))
        warned = 'es falso o no cumple' in r.log_of(folders[2])
        print('warning shown (needs two users on different connections):', warned)
        for line in r.log_of(folders[2]).splitlines():
            if 'Aviso' in line:
                print('  LOG ' + line[:230])
        ok &= 'falso' in others
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        for proc, mem in running:
            mem.close()
            if not w.close_gracefully(proc, 60):
                proc.kill()


if __name__ == '__main__':
    main()
