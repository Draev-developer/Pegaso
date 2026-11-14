"""Web seeds of torrents: a web server as the source of a torrent.

usage: python -I webseedtest.py

Two torrents without any peer (the fake tracker knows nobody else):
  1. one which names a web server itself (the "url-list" of the .torrent file);
  2. one without it, which gets the address through "Añadir fuente web..." in the menu.
Both have to complete from the local web server, checked by the hashes of the torrent.
"""
import ctypes
import hashlib
import os
import re
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import btuitest  # noqa: E402
import btfilestest  # noqa: E402
import ratetest as r  # noqa: E402
import btseedtest as bs  # noqa: E402

MP_PEGASO_ADDWEBSRC = 10983
WM_SETTEXT = 0x000C
PIECE = 256 * 1024
FILES = {'Curso.con.semilla.web.mkv': os.urandom(9 * 1024 * 1024), 'Curso.sin.semilla.mkv': os.urandom(7 * 1024 * 1024)}
requests = []


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def do_GET(self):
        name = unquote(self.path.lstrip('/'))
        data = FILES.get(name)
        rng = self.headers.get('Range')
        requests.append((name, rng))
        if data is None:
            self.send_response(404)
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        start, end = 0, len(data) - 1
        m = re.match(r'bytes=(\d+)-(\d*)', rng or '')
        if m:
            start = int(m.group(1))
            end = min(int(m.group(2)) if m.group(2) else len(data) - 1, len(data) - 1)
        body = data[start:end + 1]
        self.send_response(206 if m else 200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(len(body)))
        if m:
            self.send_header('Content-Range', 'bytes %d-%d/%d' % (start, end, len(data)))
        self.send_header('Accept-Ranges', 'bytes')
        self.end_headers()
        try:
            self.wfile.write(body)
        except OSError:
            pass


def make_torrent(name, data, url=None):
    pieces = b''.join(hashlib.sha1(data[i:i + PIECE]).digest() for i in range(0, len(data), PIECE))
    meta = {'info': {'name': name, 'piece length': PIECE, 'pieces': pieces, 'length': len(data)},
            'announce': 'http://127.0.0.1:8770/announce'}
    if url:
        meta['url-list'] = url
    path = os.path.join(HERE, name + '.torrent')
    open(path, 'wb').write(btfilestest.bencode(meta))
    return path


def wait_file(name, timeout):
    path = os.path.join(w.RUN, 'Incoming', name)
    t0 = time.time()
    while time.time() - t0 < timeout:
        if os.path.isfile(path) and os.path.getsize(path) == len(FILES[name]) and open(path, 'rb').read() == FILES[name]:
            return time.time() - t0
        time.sleep(2)
    return None


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ip = r.lan_ip()
    web = ThreadingHTTPServer((ip, 8772), Handler)
    threading.Thread(target=web.serve_forever, daemon=True).start()
    tracker = ThreadingHTTPServer(('127.0.0.1', 8770), bs.Tracker)
    threading.Thread(target=tracker.serve_forever, daemon=True).start()
    names = list(FILES)
    base = 'http://%s:8772/' % ip
    t_with = make_torrent(names[0], FILES[names[0]], base + names[0])
    t_without = make_torrent(names[1], FILES[names[1]])

    r.make_instance(w.RUN, 4700, 'TorrentPort=6881\n')
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('TorrentEnabled=0\n', '')
    open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
    pegaso = r.start(w.RUN)
    mem = None
    ok = True
    try:
        hwnd = u.main_window(pegaso.pid)
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
        time.sleep(1)
        lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
        mem = u.Remote(pegaso.pid)

        # ---- 1. the torrent names its web seed
        r.send_link(hwnd, t_with)
        took = wait_file(names[0], 90)
        print('1. torrent with url-list: finished=%s after %s s; requests to the web server: %d'
              % (took is not None, None if took is None else round(took), sum(1 for n, _ in requests if n == names[0])))
        ok &= took is not None

        # ---- 2. the web seed is added from the menu
        r.send_link(hwnd, t_without)
        time.sleep(6)
        rows = btuitest.rows(lv, mem)
        row = next((i for i, x in enumerate(rows) if x[0] == names[1]), None)
        print('2. rows:', [x[:3] for x in rows])
        before = os.path.isfile(os.path.join(w.RUN, 'Incoming', names[1])) and wait_file(names[1], 1) is not None
        ok &= row is not None and not before
        if row is not None:
            u.select_item(lv, mem, row)
            u.user32.PostMessageW(lv, u.WM_COMMAND, MP_PEGASO_ADDWEBSRC, 0)
            time.sleep(2)
            box = None
            for h, title in u.top_windows(pegaso.pid):
                if 'fuente web' in title:
                    box = h
            print('   input box:', box is not None)
            if box:
                edits = []

                def cb(child, _):
                    cls = ctypes.create_unicode_buffer(32)
                    u.user32.GetClassNameW(child, cls, 32)
                    if cls.value == 'Edit':
                        edits.append(child)
                    return True

                u.user32.EnumChildWindows(box, u.EnumProc(cb), 0)
                buf = ctypes.create_unicode_buffer(base + names[1])
                u.user32.SendMessageW(edits[0], WM_SETTEXT, 0, ctypes.addressof(buf))
                u.user32.PostMessageW(box, u.WM_COMMAND, 1, 0)
            took = wait_file(names[1], 90)
            print('   finished=%s after %s s; requests to the web server: %d'
                  % (took is not None, None if took is None else round(took), sum(1 for n, _ in requests if n == names[1])))
            ok &= took is not None
        for line in r.log_of(w.RUN).splitlines():
            if 'orrent' in line and 'listening' not in line:
                print('  LOG ' + line[:200])
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        if mem:
            mem.close()
        if not w.close_gracefully(pegaso, 60):
            pegaso.kill()
        web.shutdown()
        tracker.shutdown()


if __name__ == '__main__':
    main()
