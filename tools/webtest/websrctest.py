"""A web address as a source of an ed2k download ("Añadir fuente web..." in the menu).

usage: python -I websrctest.py

Pegaso gets an ed2k link nobody shares, joins Kad (eMule asks its sources only while it
is connected), and is given the address of a local web server which has the same file.
The data has to arrive from that server and pass the ed2k hash check.
"""
import ctypes
import os
import re
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import btuitest  # noqa: E402
import nettest  # noqa: E402
import ratetest as r  # noqa: E402

MP_PEGASO_ADDWEBSRC = 10983
WM_SETTEXT = 0x000C
NAME = 'Manual.de.prueba.pdf'
SIZE = 5 * 1024 * 1024
DATA = os.urandom(SIZE)
requests = []


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def do_GET(self):
        rng = self.headers.get('Range')
        requests.append(rng)
        start, end = 0, SIZE - 1
        m = re.match(r'bytes=(\d+)-(\d*)', rng or '')
        if m:
            start = int(m.group(1))
            end = int(m.group(2)) if m.group(2) else SIZE - 1
        body = DATA[start:end + 1]
        self.send_response(206 if m else 200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(len(body)))
        if m:
            self.send_header('Content-Range', 'bytes %d-%d/%d' % (start, end, SIZE))
        self.send_header('Accept-Ranges', 'bytes')
        self.end_headers()
        try:
            self.wfile.write(body)
        except OSError:
            pass


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ip = r.lan_ip()
    server = ThreadingHTTPServer((ip, 8771), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = 'http://%s:8771/%s' % (ip, NAME)
    link = 'ed2k://|file|%s|%d|%s|/' % (NAME, SIZE, w.ed2k_hash(DATA))

    r.make_instance(w.RUN, 4700)
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('NetworkKademlia=0\n', '')
    open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
    pegaso = r.start(w.RUN)
    mem = None
    ok = True
    try:
        hwnd = u.main_window(pegaso.pid)
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, nettest.MP_HM_CON, 0)
        t0 = time.time()
        while time.time() - t0 < 90 and 'external Kad Port' not in r.log_of(w.RUN):
            time.sleep(3)
        time.sleep(5)
        lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
        mem = u.Remote(pegaso.pid)
        r.send_link(hwnd, link)
        time.sleep(3)
        print('download list:', [row[:4] for row in btuitest.rows(lv, mem)])

        u.select_item(lv, mem, 0)
        u.user32.PostMessageW(lv, u.WM_COMMAND, MP_PEGASO_ADDWEBSRC, 0)
        time.sleep(2)
        box = None
        for h, title in u.top_windows(pegaso.pid):
            if 'fuente web' in title:
                box = h
        print('input box:', box is not None)
        ok &= box is not None
        if box:
            edits = []

            def cb(child, _):
                cls = ctypes.create_unicode_buffer(32)
                u.user32.GetClassNameW(child, cls, 32)
                if cls.value == 'Edit':
                    edits.append(child)
                return True

            u.user32.EnumChildWindows(box, u.EnumProc(cb), 0)
            buf = ctypes.create_unicode_buffer(url)
            u.user32.SendMessageW(edits[0], WM_SETTEXT, 0, ctypes.addressof(buf))
            print('screenshot:', u.screenshot(box, 'shot_websrc.png'))
            u.user32.PostMessageW(box, u.WM_COMMAND, 1, 0)

        path = os.path.join(w.RUN, 'Incoming', NAME)
        t0 = time.time()
        while time.time() - t0 < 150 and not os.path.isfile(path):
            time.sleep(5)
            rows = btuitest.rows(lv, mem)
            print('  t=%3ds %s  requests to the web server: %d' % (time.time() - t0, rows[0][1:4] if rows else None, len(requests)))
        time.sleep(3)
        same = os.path.isfile(path) and open(path, 'rb').read() == DATA
        print('finished: %s, identical: %s, range requests: %d (e.g. %s)' % (os.path.isfile(path), same, len(requests), requests[:2]))
        ok &= same and len(requests) > 0
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        if mem:
            mem.close()
        if not w.close_gracefully(pegaso, 60):
            pegaso.kill()
        server.shutdown()


if __name__ == '__main__':
    main()
