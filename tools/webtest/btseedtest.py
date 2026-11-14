"""Torrents between two Pegaso instances: piece map, visible seeding, seed ratio.

usage: python -I btseedtest.py

  A (pegaso-test-a1) has the file and seeds it, seed ratio 100 %
  B (pegaso-test)    downloads it

A fake tracker on 127.0.0.1:8770 introduces them; no outside network is used.
"""
import hashlib
import os
import socket
import struct
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import btuitest  # noqa: E402
import btfilestest  # noqa: E402
import ratetest as r  # noqa: E402

NAME = 'Concierto.de.prueba.mkv'
SIZE = 320 * 1024 * 1024
PIECE = 256 * 1024
TORRENT = os.path.join(HERE, 'concierto.torrent')
peers = {}


class Tracker(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        q = parse_qs(urlparse(self.path).query)
        port = int(q.get('port', ['0'])[0])
        peers[port] = time.time()
        compact = b''.join(socket.inet_aton('127.0.0.1') + struct.pack('>H', p) for p in peers if p != port)
        body = btfilestest.bencode({'interval': 10, 'min interval': 5, 'peers': compact})
        self.send_response(200)
        self.send_header('Content-Type', 'text/plain')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    data = os.urandom(SIZE)
    pieces = b''.join(hashlib.sha1(data[i:i + PIECE]).digest() for i in range(0, SIZE, PIECE))
    info = {'name': NAME, 'piece length': PIECE, 'pieces': pieces, 'length': SIZE}
    open(TORRENT, 'wb').write(btfilestest.bencode({'info': info, 'announce': 'http://127.0.0.1:8770/announce'}))

    tracker = ThreadingHTTPServer(('127.0.0.1', 8770), Tracker)
    threading.Thread(target=tracker.serve_forever, daemon=True).start()

    a_dir = w.RUN + '-a1'
    r.make_instance(w.RUN, 4700, 'TorrentPort=6881\n')
    r.make_instance(a_dir, 4720, 'TorrentPort=6891\nMaxUpload=400\nTorrentSeedRatio=100\n')
    for folder in (w.RUN, a_dir):
        ini = os.path.join(folder, 'config', 'preferences.ini')
        text = open(ini).read().replace('TorrentEnabled=0\n', '')
        open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
    open(os.path.join(a_dir, 'Incoming', NAME), 'wb').write(data)

    procs = [r.start(a_dir), r.start(w.RUN)]
    mems = []
    ok = True
    try:
        wins, lists = [], []
        for proc in procs:
            hwnd = u.main_window(proc.pid)
            u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
            wins.append(hwnd)
            mems.append(u.Remote(proc.pid))
        time.sleep(1)
        for hwnd in wins:
            lists.append(u.find_child(hwnd, u.IDC_DOWNLOADLIST))

        r.send_link(wins[0], TORRENT)
        time.sleep(8)
        print('A:', btuitest.rows(lists[0], mems[0]))
        r.send_link(wins[1], TORRENT)

        shot = False
        seeding_text = ''
        t0 = time.time()
        done = False
        while time.time() - t0 < 240 and not done:
            time.sleep(1.5)
            ra = btuitest.rows(lists[0], mems[0])
            rb = btuitest.rows(lists[1], mems[1])
            print('  t=%3ds  B=%s' % (time.time() - t0, rb[0][1:5] if rb else None))
            print('          A=%s' % (ra[0][1:] if ra else None))
            if ra and any('subida' in c for c in ra[0]):
                seeding_text = [c for c in ra[0] if 'subida' in c][0]
            path = os.path.join(w.RUN, 'Incoming', NAME)
            have = os.path.getsize(path) if os.path.isfile(path) else 0
            if not shot and rb and len(rb[0]) > 2 and ('MB' in rb[0][2]) and float(rb[0][2].split()[0]) >= 60:
                print('screenshot:', u.screenshot(wins[1], 'shot_pieces.png'))
                shot = True
            done = bool(rb) and any('compartiendo' in c for c in rb[0]) and have == SIZE
        time.sleep(8)
        ra = btuitest.rows(lists[0], mems[0])
        rb = btuitest.rows(lists[1], mems[1])
        print('A at the end:', ra)
        print('B at the end:', rb)
        print('screenshot:', u.screenshot(wins[0], 'shot_seeding.png'))
        same = done and open(os.path.join(w.RUN, 'Incoming', NAME), 'rb').read() == data
        log_a, log_b = r.log_of(a_dir), r.log_of(w.RUN)
        for line in (log_a + '\n' + log_b).splitlines():
            if any(k in line for k in ('seeding stopped', 'Antivirus', 'Torrent finished', 'hash de archivo', 'Hashing file')):
                print('  LOG ' + line[:200])
        print('B finished: %s, content identical: %s, A showed while seeding: %r' % (done, same, seeding_text))
        ok &= done and same and shot
        ok &= 'subida' in seeding_text and 'compartido' in seeding_text
        ok &= 'seeding stopped' in log_a
        ok &= 'Antivirus check passed' in log_b
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        for mem in mems:
            mem.close()
        for proc in procs:
            if not w.close_gracefully(proc, 60):
                proc.kill()
        tracker.shutdown()


if __name__ == '__main__':
    main()
