"""Hybrid swarm: one download fed by an eMule source and by a torrent at once.

usage: python -I hybridtest.py

  A1 (pegaso-test-a1) shares two files on eMule, slowly (40 KB/s)
  A2 (pegaso-test-a2) seeds two torrents: one with the first file, and one with a file of
                     the same size as the second but other content (the "wrong" torrent)
  B  (pegaso-test)    downloads both files from A1 and is given the torrents as sources

Expected: the first file arrives quickly and correct (most of it through the torrent,
checked with the ed2k hashes B got from A1); the wrong torrent is detected and dropped.
B joins Kad, because eMule asks its sources only while connected; a fake tracker on
127.0.0.1:8770 introduces the torrent peers. Nothing else of the outside network is used.
"""
import ctypes
import hashlib
import os
import sys
import threading
import time
from http.server import ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import btuitest  # noqa: E402
import btfilestest  # noqa: E402
import nettest  # noqa: E402
import ratetest as r  # noqa: E402
import btseedtest as bs  # noqa: E402

MP_PEGASO_ADDTORRENT = 10984
WM_SETTEXT = 0x000C
SIZE = 24 * 1024 * 1024
PIECE = 256 * 1024
GOOD, OTHER, WRONG = 'Documental.hibrido.mkv', 'Otro.documental.mkv', 'Contenido.distinto.mkv'


def make_torrent(name, data):
    pieces = b''.join(hashlib.sha1(data[i:i + PIECE]).digest() for i in range(0, len(data), PIECE))
    info = {'name': name, 'piece length': PIECE, 'pieces': pieces, 'length': len(data)}
    path = os.path.join(HERE, name + '.torrent')
    open(path, 'wb').write(btfilestest.bencode({'info': info, 'announce': 'http://127.0.0.1:8770/announce'}))
    return path


def link_torrent(pid, lv, mem, row, torrent):
    u.select_item(lv, mem, row)
    u.user32.PostMessageW(lv, u.WM_COMMAND, MP_PEGASO_ADDTORRENT, 0)
    time.sleep(2)
    box = None
    for h, title in u.top_windows(pid):
        if 'torrent de este mismo' in title:
            box = h
    if not box:
        return False
    edits = []

    def cb(child, _):
        cls = ctypes.create_unicode_buffer(32)
        u.user32.GetClassNameW(child, cls, 32)
        if cls.value == 'Edit':
            edits.append(child)
        return True

    u.user32.EnumChildWindows(box, u.EnumProc(cb), 0)
    buf = ctypes.create_unicode_buffer(torrent)
    u.user32.SendMessageW(edits[0], WM_SETTEXT, 0, ctypes.addressof(buf))
    u.user32.PostMessageW(box, u.WM_COMMAND, 1, 0)
    time.sleep(1)
    return True


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ip = r.lan_ip()
    good, other, wrong = os.urandom(SIZE), os.urandom(SIZE), os.urandom(SIZE)
    t_good = make_torrent(GOOD, good)
    t_wrong = make_torrent(WRONG, wrong)
    tracker = ThreadingHTTPServer(('127.0.0.1', 8770), bs.Tracker)
    threading.Thread(target=tracker.serve_forever, daemon=True).start()

    a1, a2 = w.RUN + '-a1', w.RUN + '-a2'
    r.make_instance(a1, 4720, 'MaxUpload=40\n')
    r.make_instance(a2, 4740, 'TorrentPort=6891\nTorrentSeedRatio=0\n')
    r.make_instance(w.RUN, 4700, 'TorrentPort=6881\n')
    for folder, kad in ((a2, False), (w.RUN, True)):
        ini = os.path.join(folder, 'config', 'preferences.ini')
        text = open(ini).read().replace('TorrentEnabled=0\n', '')
        if kad:
            text = text.replace('NetworkKademlia=0\n', '')
        open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
    # "Connect" brings up a server and Kad. Logged in to a server with a low ID, eMule drops
    # sources which have its own local address (all instances here): B gets a server list
    # with one dead entry, so that only Kad comes up.
    open(os.path.join(w.RUN, 'config', 'staticservers.dat'), 'w', encoding='ascii').write('%s:9,0,Nadie\n' % ip)
    open(os.path.join(a1, 'Incoming', GOOD), 'wb').write(good)
    open(os.path.join(a1, 'Incoming', OTHER), 'wb').write(other)
    open(os.path.join(a2, 'Incoming', GOOD), 'wb').write(good)
    open(os.path.join(a2, 'Incoming', WRONG), 'wb').write(wrong)

    procs = [r.start(a1), r.start(a2), r.start(w.RUN)]
    mem = None
    ok = True
    try:
        hwnd_a2 = u.main_window(procs[1].pid)
        r.send_link(hwnd_a2, t_good)
        r.send_link(hwnd_a2, t_wrong)
        b = procs[2]
        hwnd = u.main_window(b.pid)
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, nettest.MP_HM_CON, 0)
        t0 = time.time()
        while time.time() - t0 < 90 and 'external Kad Port' not in r.log_of(w.RUN):
            time.sleep(3)
        time.sleep(12)  # A1 hashing its files, A2 checking its torrents
        lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
        mem = u.Remote(b.pid)

        # ---- 1. the right torrent
        src = '|sources,%s:4720|/' % ip
        r.send_link(hwnd, 'ed2k://|file|%s|%d|%s|/' % (GOOD, SIZE, w.ed2k_hash(good)) + src)
        t0 = time.time()
        rows = []
        while time.time() - t0 < 60:
            time.sleep(3)
            rows = btuitest.rows(lv, mem)
            if rows and 'KB' in rows[0][2]:
                break
        print('from eMule alone after %ds: %s' % (time.time() - t0, rows[0][1:5] if rows else None))
        t1 = time.time()
        linked = link_torrent(b.pid, lv, mem, 0, t_good)
        print('torrent given as a source:', linked)
        ok &= linked
        path = os.path.join(w.RUN, 'Incoming', GOOD)
        shown = ''
        shot = False
        while time.time() - t1 < 120 and not os.path.isfile(path):
            time.sleep(1)
            rows = btuitest.rows(lv, mem)
            for cell in (rows[0] if rows else []):
                if '+ BT' in cell:
                    shown = cell
            if shown and not shot:
                shot = True
                print('screenshot:', u.screenshot(hwnd, 'shot_hybrid.png'))
            if len(rows) != 1:
                print('  unexpected rows:', [x[0] for x in rows])
                ok = False
        took = time.time() - t1
        time.sleep(4)
        same = os.path.isfile(path) and open(path, 'rb').read() == good
        left = os.listdir(os.path.join(w.RUN, 'Temp', 'hybrid')) if os.path.isdir(os.path.join(w.RUN, 'Temp', 'hybrid')) else []
        print('finished in %ds (eMule alone at 40 KB/s would need about %d min); identical: %s' % (took, SIZE / 40000 / 60, same))
        print('sources column showed: %r; helper data left behind: %s' % (shown, left))
        # between two local instances the torrent can deliver everything before the list is read once
        ok &= same and took < 100 and ('+ BT' in shown or took < 8) and not left

        # ---- 2. a torrent with a file of the same size but other content
        r.send_link(hwnd, 'ed2k://|file|%s|%d|%s|/' % (OTHER, SIZE, w.ed2k_hash(other)) + src)
        t0 = time.time()
        row = None
        while time.time() - t0 < 60:
            time.sleep(3)
            rows = btuitest.rows(lv, mem)
            row = next((i for i, x in enumerate(rows) if x[0].endswith(OTHER)), None)
            if row is not None and 'KB' in rows[row][2]:
                break
        linked = row is not None and link_torrent(b.pid, lv, mem, row, t_wrong)
        print('wrong torrent given as a source:', linked)
        ok &= bool(linked)
        t1 = time.time()
        dropped = False
        while time.time() - t1 < 150 and not dropped:
            time.sleep(3)
            dropped = 'not the same file' in r.log_of(w.RUN)
        rows = btuitest.rows(lv, mem)
        print('dropped after %ds: %s; rows now: %s' % (time.time() - t1, dropped, [x[:3] for x in rows]))
        bad = os.path.isfile(os.path.join(w.RUN, 'Incoming', OTHER))
        ok &= dropped and not bad
        for line in r.log_of(w.RUN).splitlines():
            if 'Hybrid' in line or 'corrupt' in line.lower() or 'orrup' in line:
                print('  LOG ' + line[:210])
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        if mem:
            mem.close()
        for proc in procs:
            if not w.close_gracefully(proc, 60):
                proc.kill()
        tracker.shutdown()


if __name__ == '__main__':
    main()
