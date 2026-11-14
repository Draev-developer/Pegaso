"""Web search through a Torznab provider, and the Internet mark on finished downloads.

usage: python -I searchtest.py

A fake Torznab provider runs on 127.0.0.1:8766 (no real site is contacted). The search
dialog is driven through the real interface (Spanish): query, results, download.
"""
import ctypes
import hashlib
import os
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, quote, urlparse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import btuitest  # noqa: E402
import btfilestest  # noqa: E402

MP_PEGASO_WEBSEARCH = 10981
IDC_WS_QUERY, IDC_WS_SEARCH, IDC_WS_LIST, IDC_WS_STATUS, IDC_WS_DOWNLOAD = 5030, 5031, 5033, 5034, 5035
WM_SETTEXT, WM_GETTEXT = 0x000C, 0x000D
KEY = 'clave secreta/1'
seen = []
TORRENT = os.path.join(HERE, 'prueba.torrent')

RSS = '''<?xml version="1.0" encoding="UTF-8"?>
<rss version="2.0" xmlns:torznab="http://torznab.com/schemas/2015/feed">
<channel><title>Fake</title>
<item><title>Prueba Pegaso (.torrent)</title><size>302897</size>
  <link>http://127.0.0.1:8766/prueba.torrent</link>
  <enclosure url="http://127.0.0.1:8766/prueba.torrent" length="302897" type="application/x-bittorrent" />
  <torznab:attr name="seeders" value="5" /><torznab:attr name="peers" value="7" /></item>
<item><title><![CDATA[Documental: Ñandú & cía (1080p)]]></title>
  <torznab:attr name="size" value="1500000000" />
  <torznab:attr name="magneturl" value="magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&amp;dn=Documental" />
  <torznab:attr name="seeders" value="99" /><torznab:attr name="peers" value="120" /></item>
<item><title>Sin semillas &amp; sin tama&#241;o</title>
  <link>magnet:?xt=urn:btih:89abcdef0123456789abcdef0123456789abcdef</link></item>
<item><title>Enlace que Pegaso no sabe usar</title><link>ftp://example.invalid/x.torrent</link>
  <torznab:attr name="seeders" value="500" /></item>
</channel></rss>'''


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def do_GET(self):
        url = urlparse(self.path)
        q = parse_qs(url.query)
        seen.append((url.path, q))
        if url.path == '/prueba.torrent':
            body, ctype, status = open(TORRENT, 'rb').read(), 'application/x-bittorrent', 200
        elif url.path == '/api' and q.get('apikey') == [KEY] and q.get('t') == ['search']:
            body, ctype, status = RSS.encode('utf-8'), 'application/rss+xml', 200
        else:
            body, ctype, status = b'denied', 'text/plain', 401
        self.send_response(status)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def text_of(hwnd):
    buf = ctypes.create_unicode_buffer(1024)
    u.user32.SendMessageW(hwnd, WM_GETTEXT, 1024, ctypes.addressof(buf))
    return buf.value


def main():
    btuitest.deploy()
    w.reset_profile()
    btfilestest.make_torrent(TORRENT)
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('Language=1033', 'Language=1034')
    text += 'TorznabCount=1\nTorznab0Name=Mi%20Jackett\nTorznab0Url=http://127.0.0.1:8766/api\nTorznab0Key=' + quote(KEY, safe='') + '\n'
    open(ini, 'w', encoding='ascii', newline='\r\n').write(text)

    fake = ThreadingHTTPServer(('127.0.0.1', 8766), Handler)
    threading.Thread(target=fake.serve_forever, daemon=True).start()
    server = w.start_server()
    pegaso = w.start_pegaso()
    hwnd = u.main_window(pegaso.pid)
    u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
    time.sleep(1)
    lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
    mem = u.Remote(pegaso.pid)
    ok = True
    try:
        # ---- search dialog
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, MP_PEGASO_WEBSEARCH, 0)
        time.sleep(2)
        dlg, title = btfilestest.dialog(pegaso.pid, hwnd)
        print('dialog:', repr(title))
        ok &= dlg is not None
        query = ctypes.create_unicode_buffer('ñandú 1080p & más')
        u.user32.SendMessageW(u.find_child(dlg, IDC_WS_QUERY), WM_SETTEXT, 0, ctypes.addressof(query))
        u.user32.PostMessageW(dlg, u.WM_COMMAND, IDC_WS_SEARCH, 0)
        time.sleep(3)
        results = u.find_child(dlg, IDC_WS_LIST)
        rows = btuitest.rows(results, mem)
        for r in rows:
            print('  ', r)
        print('status:', repr(text_of(u.find_child(dlg, IDC_WS_STATUS))))
        api = [q for p, q in seen if p == '/api']
        print('provider received: q=%r apikey ok=%s' % (api[0].get('q') if api else None, bool(api) and api[0].get('apikey') == [KEY]))
        ok &= bool(api) and api[0].get('q') == ['ñandú 1080p & más']
        ok &= [r[0] for r in rows] == ['Documental: Ñandú & cía (1080p)', 'Prueba Pegaso (.torrent)', 'Sin semillas & sin tamaño']
        ok &= rows[0][1:] == ['1.40 GB', '99', '120', 'Mi Jackett']
        print('screenshot:', u.screenshot(dlg, 'shot_search.png'))

        # ---- download the .torrent result
        u.select_item(results, mem, 1)
        u.user32.PostMessageW(dlg, u.WM_COMMAND, IDC_WS_DOWNLOAD, 0)
        time.sleep(4)
        u.user32.PostMessageW(dlg, u.WM_COMMAND, 2, 0)
        time.sleep(2)
        dl = btuitest.rows(lv, mem)
        print('download list:', [r[0] for r in dl])
        ok &= [r[0] for r in dl] == ['Prueba Pegaso']
        ok &= any(p == '/prueba.torrent' for p, _ in seen)

        # ---- Internet mark on a finished web download
        w.add_url(w.BASE + '/small.bin')
        times = w.wait_files({'small.bin': '/small.bin'}, 60)
        path = os.path.join(w.RUN, 'Incoming', 'small.bin')
        try:
            mark = open(path + ':Zone.Identifier').read()
        except OSError as e:
            mark = 'MISSING (%s)' % e
        print('Zone.Identifier of small.bin:', repr(mark))
        ok &= times['small.bin'] is not None and 'ZoneId=3' in mark and 'HostUrl=http' in mark
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        mem.close()
        if not w.close_gracefully(pegaso, 60):
            pegaso.kill()
        server.kill()
        fake.shutdown()


if __name__ == '__main__':
    main()
