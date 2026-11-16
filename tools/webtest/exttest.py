"""The browser extension and what the local API does for it.

usage: python -I exttest.py

First the API alone: a browser extension is let in (with the key), a web page is not, and
the web address of a .torrent file becomes a torrent. Then the extension itself, loaded in
a real browser (Edge, without a window, with a profile of its own which is thrown away):
its background script hands links over to Pegaso with its own code.
Needs: browser-extension/build.py run before (dist/extension), Node 22 or later, Edge.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import ratetest as r  # noqa: E402
import btuitest  # noqa: E402
import apitest  # noqa: E402

EXTENSION = os.environ.get('PEGASO_EXTENSION') or r'C:\xampp\htdocs\Emule\dist\extension\pegaso-chromium'
EDGE = r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
FILES_PORT, DEBUG_PORT = 8771, 9377


def bencode(x):
    if isinstance(x, int):
        return b'i%de' % x
    if isinstance(x, bytes):
        return b'%d:%s' % (len(x), x)
    if isinstance(x, str):
        return bencode(x.encode('utf-8'))
    return b'd' + b''.join(bencode(k) + bencode(v) for k, v in sorted(x.items())) + b'e'


def torrent(name):
    return bencode({'announce': 'http://127.0.0.1:9/announce',
                    'info': {'length': 3 * 262144, 'name': name, 'piece length': 262144, 'pieces': hashlib.sha1(name.encode()).digest() * 3}})


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def do_GET(self):
        path = self.path.split('?')[0]
        if path == '/uno.torrent':
            body = torrent('Torrent.por.direccion.mkv')
        elif path == '/bajar':
            body = torrent('Torrent.sin.extension.mkv')
        elif path == '/desde-el-navegador.torrent':
            body = torrent('Torrent.desde.la.extension.mkv')
        else:
            body = b'x' * 200000
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    do_HEAD = do_GET


def names():
    status, listed = apitest.call('GET', '/api/v1/downloads')
    return sorted(d['name'] for d in listed['downloads']) if status == 200 else []


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ok = True

    def check(what, cond, detail=''):
        nonlocal ok
        print('  %-72s %s %s' % (what, 'ok' if cond else 'FAIL', '' if cond else detail))
        ok &= bool(cond)

    btuitest.deploy()
    r.make_instance(w.RUN, 4700, 'ApiEnabled=1\nApiPort=%d\nApiToken=%s\n' % (apitest.PORT, apitest.TOKEN))
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('TorrentEnabled=0', 'TorrentEnabled=1\nTorrentPort=4720')
    open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
    files = ThreadingHTTPServer(('127.0.0.1', FILES_PORT), Handler)
    threading.Thread(target=files.serve_forever, daemon=True).start()
    base = 'http://127.0.0.1:%d' % FILES_PORT
    p = r.start(w.RUN)
    browser = None
    profile = tempfile.mkdtemp(prefix='pegaso-exttest-')
    try:
        u.main_window(p.pid)
        # ---- the API
        check('a browser extension is let in (Chrome, Edge)', apitest.call('GET', '/api/v1/status', headers={'Origin': 'chrome-extension://abcdefghijklmnopabcdefghijklmnop'})[0] == 200)
        check('... and one of Firefox', apitest.call('GET', '/api/v1/status', headers={'Origin': 'moz-extension://0a1b2c3d-0000-4000-8000-000000000000'})[0] == 200)
        check('... but not without the key', apitest.call('GET', '/api/v1/status', token=None, headers={'Origin': 'chrome-extension://abcdefghijklmnopabcdefghijklmnop'})[0] == 401)
        check('a web page is still turned away', apitest.call('GET', '/api/v1/status', headers={'Origin': 'https://example.org'})[0] == 403)
        check('... also one which only looks like an extension', apitest.call('GET', '/api/v1/status', headers={'Origin': 'https://chrome-extension.example.org'})[0] == 403)
        status, added = apitest.call('POST', '/api/v1/downloads', {'links': [base + '/uno.torrent?id=7']})
        check('the web address of a .torrent file is taken', status == 200 and added.get('added') == 1, added)
        status, added = apitest.call('POST', '/api/v1/downloads', {'links': [base + '/bajar?que=2'], 'kind': 'torrent'})
        check('... also one which does not look like it, when told so', status == 200 and added.get('added') == 1, added)
        time.sleep(6)
        got = names()
        print('    downloads:', got)
        check('both became torrents, with the names their files give', 'Torrent.por.direccion.mkv' in got and 'Torrent.sin.extension.mkv' in got, got)

        # ---- the extension, in a browser
        if not (os.path.isdir(EXTENSION) and os.path.isfile(EDGE) and shutil.which('node')):
            print('  SKIPPED: the extension is not built, or Edge or Node is missing')
        else:
            browser = subprocess.Popen([EDGE, '--headless=new', '--disable-gpu', '--no-first-run', '--user-data-dir=' + profile,
                                        '--remote-debugging-port=%d' % DEBUG_PORT, '--load-extension=' + EXTENSION,
                                        '--disable-extensions-except=' + EXTENSION, 'about:blank'],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            links = [base + '/Archivo.de.la.extension.bin', base + '/desde-el-navegador.torrent']
            run = subprocess.run(['node', os.path.join(HERE, 'exttest.mjs'), str(DEBUG_PORT), str(apitest.PORT), apitest.TOKEN, json.dumps(links)],
                                 capture_output=True, text=True, timeout=120)
            print('    ' + (run.stdout.strip() or run.stderr.strip()[-400:]))
            out = json.loads(run.stdout) if run.stdout.strip().startswith('{') else {}
            check('the extension loads and its background script runs', out.get('extension', '').startswith('chrome-extension://'), run.stderr[-300:])
            check('without a key it says so and sends nothing', out.get('withoutKey') == 'needKey', out.get('withoutKey'))
            check('with the key it reaches Pegaso', out.get('status') == btuitest_version(), out.get('status'))
            check('it hands over a file and a torrent', isinstance(out.get('sent'), dict) and out['sent'].get('added') == 2, out.get('sent'))
            check('a wrong key is told apart', out.get('wrongKey') == 'badKey', out.get('wrongKey'))
            check('links are picked out of a text', out.get('extract') == ['magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&dn=a', 'https://example.org/a.zip'], out.get('extract'))
            check('its menu on links is there', out.get('menus') == 'ok', out.get('menus'))
            time.sleep(6)
            got = names()
            print('    downloads:', got)
            # the file is small: it may be finished, and out of the list, already
            arrived = os.path.isfile(os.path.join(w.RUN, 'Incoming', 'Archivo.de.la.extension.bin'))
            check('what the extension sent is in Pegaso: the torrent in its list, the file downloaded',
                  ('Archivo.de.la.extension.bin' in got or arrived) and 'Torrent.desde.la.extension.mkv' in got, (got, arrived))
        check('still running', p.poll() is None)
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        if browser:
            browser.terminate()
            time.sleep(2)
        shutil.rmtree(profile, ignore_errors=True)
        if not w.close_gracefully(p, 60):
            p.kill()


def btuitest_version():
    import re
    text = open(os.path.join(os.path.dirname(os.path.dirname(HERE)), 'tools', 'installer', 'pegaso.iss'), encoding='utf-8-sig').read()
    return re.search(r'#define AppVersion "([^"]+)"', text).group(1)


if __name__ == '__main__':
    main()
