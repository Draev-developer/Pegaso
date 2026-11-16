"""Making torrents of the user's own files, and series of web downloads written as one address.

usage: python -I createtest.py

Series: "…/parte[01-05].bin" asks for the five files, with the zeros as written.
Torrents: the window makes the torrent of a folder and of a single file. The .torrent file is
read back here and checked on its own: names, sizes, tracker, and the hash of every piece
against the data. The torrents are seeded at once and still after Pegaso is opened again.
"""
import ctypes
import hashlib
import os
import stat
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import ratetest as r  # noqa: E402
import btuitest  # noqa: E402
import apitest  # noqa: E402

MP_PEGASO_CREATE = 10993
IDC_CR_PATH, IDC_CR_TRACKERS, IDC_CR_STATUS, IDC_CR_MAGNET = 5121, 5125, 5129, 5131
WM_SETTEXT, WM_GETTEXT = 0x000C, 0x000D
FILES_PORT = 8773
asked = []


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def do_GET(self):
        path = unquote(self.path.split('?')[0])
        if self.command == 'GET' and path not in asked:
            asked.append(path)
        body = (path.encode('utf-8') * 4000)[:60000]
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        if self.command == 'GET':
            self.wfile.write(body)

    do_HEAD = do_GET


def bdecode(data, i=0):
    c = data[i:i + 1]
    if c == b'i':
        end = data.index(b'e', i)
        return int(data[i + 1:end]), end + 1
    if c == b'l':
        out, i = [], i + 1
        while data[i:i + 1] != b'e':
            v, i = bdecode(data, i)
            out.append(v)
        return out, i + 1
    if c == b'd':
        out, i = {}, i + 1
        while data[i:i + 1] != b'e':
            k, i = bdecode(data, i)
            v, i = bdecode(data, i)
            out[k] = v
        return out, i + 1
    colon = data.index(b':', i)
    n = int(data[i:colon])
    return data[colon + 1:colon + 1 + n], colon + 1 + n


def pieces_ok(info, parent):
    """every piece of the torrent, hashed here from the data"""
    name = info[b'name'].decode('utf-8')
    if b'files' in info:
        paths = [os.path.join(parent, name, *[p.decode('utf-8') for p in f[b'path']]) for f in info[b'files']]
    else:
        paths = [os.path.join(parent, name)]
    data = b''.join(open(p, 'rb').read() for p in paths)
    size = info[b'piece length']
    mine = b''.join(hashlib.sha1(data[i:i + size]).digest() for i in range(0, len(data), size))
    return mine == info[b'pieces'], len(data)


def wipe(folder):
    for root, dirs, files in os.walk(folder, topdown=False):
        for name in files:
            os.chmod(os.path.join(root, name), stat.S_IWRITE)
            os.remove(os.path.join(root, name))
        for name in dirs:
            os.chmod(os.path.join(root, name), stat.S_IWRITE)
            os.rmdir(os.path.join(root, name))


def text_of(hwnd):
    buf = ctypes.create_unicode_buffer(4096)
    u.user32.SendMessageW(hwnd, WM_GETTEXT, 4096, ctypes.addressof(buf))
    return buf.value


def set_text(hwnd, text):
    buf = ctypes.create_unicode_buffer(text)
    u.user32.SendMessageW(hwnd, WM_SETTEXT, 0, ctypes.addressof(buf))


def torrents():
    status, listed = apitest.call('GET', '/api/v1/downloads')
    return {d['name']: d for d in listed['downloads'] if d.get('kind') == 'torrent'} if status == 200 else {}


def create(pid, hwnd, path, trackers):
    """makes a torrent with the window; gives back what the window says and the magnet link"""
    u.user32.PostMessageW(hwnd, u.WM_COMMAND, MP_PEGASO_CREATE, 0)
    time.sleep(2)
    dlg = [h for h, t in u.top_windows(pid) if t == 'Crear torrent'][0]
    set_text(u.find_child(dlg, IDC_CR_PATH), path)
    set_text(u.find_child(dlg, IDC_CR_TRACKERS), trackers)
    u.user32.PostMessageW(dlg, u.WM_COMMAND, 1, 0)
    t0 = time.time()
    status = ''
    while time.time() - t0 < 60:
        time.sleep(1)
        status = text_of(u.find_child(dlg, IDC_CR_STATUS))
        if 'Torrent creado' in status or (status and 'Leyendo' not in status):
            break
    magnet = text_of(u.find_child(dlg, IDC_CR_MAGNET))
    shot = u.screenshot(dlg, 'shot_create.png')
    u.user32.PostMessageW(dlg, u.WM_COMMAND, 2, 0)
    time.sleep(1)
    return status, magnet, shot


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ok = True

    def check(what, cond, detail=''):
        nonlocal ok
        print('  %-70s %s %s' % (what, 'ok' if cond else 'FAIL', '' if cond else detail))
        ok &= bool(cond)

    btuitest.deploy()
    for sub in ('config', 'Incoming', 'Temp', 'Mis cosas'):
        wipe(os.path.join(w.RUN, sub))
    r.make_instance(w.RUN, 4700, 'ApiEnabled=1\nApiPort=%d\nApiToken=%s\n' % (apitest.PORT, apitest.TOKEN))
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('TorrentEnabled=0', 'TorrentEnabled=1\nTorrentPort=4720')
    open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
    mine = os.path.join(w.RUN, 'Mis cosas')
    folder = os.path.join(mine, 'Curso de prueba')
    os.makedirs(os.path.join(folder, 'Anexos'), exist_ok=True)
    open(os.path.join(folder, 'Leccion 1.mkv'), 'wb').write(os.urandom(700000))
    open(os.path.join(folder, 'Leccion 2.mkv'), 'wb').write(os.urandom(450000))
    open(os.path.join(folder, 'Anexos', 'Apuntes.pdf'), 'wb').write(os.urandom(90000))
    single = os.path.join(mine, 'Grabacion suelta.mkv')
    open(single, 'wb').write(os.urandom(520000))

    files = ThreadingHTTPServer(('127.0.0.1', FILES_PORT), Handler)
    threading.Thread(target=files.serve_forever, daemon=True).start()
    base = 'http://127.0.0.1:%d' % FILES_PORT
    incoming = os.path.join(w.RUN, 'Incoming')
    p = r.start(w.RUN)
    try:
        hwnd = u.main_window(p.pid)
        # ---- series of web downloads
        status, added = apitest.call('POST', '/api/v1/downloads', {'links': [base + '/serie/parte[01-05].bin', base + '/otra/suelto[8-10].bin', base + '/rara/letras[a-c].bin']})
        check('the addresses are taken', status == 200 and added.get('added') == 3, added)
        t0 = time.time()
        want = ['parte%02d.bin' % n for n in range(1, 6)] + ['suelto%d.bin' % n for n in range(8, 11)]
        while time.time() - t0 < 60 and not all(os.path.isfile(os.path.join(incoming, n)) for n in want):
            time.sleep(2)
        got = sorted(os.listdir(incoming))
        print('    downloaded:', got)
        check('"[01-05]" stands for five files, with their zeros', all(n in got for n in want[:5]), got)
        check('"[8-10]" for three, without zeros', all(n in got for n in want[5:]) and 'suelto08.bin' not in got, got)
        check('what is not a range of numbers is asked for as it is written', '/rara/letras[a-c].bin' in asked, asked)
        check('nothing else was asked for', sorted(a for a in asked if 'letras' not in a) == sorted(
            ['/serie/' + n for n in want[:5]] + ['/otra/' + n for n in want[5:]]), asked)

        # ---- a torrent of a folder
        status, magnet, shot = create(p.pid, hwnd, folder, 'http://127.0.0.1:9/announce\r\nesto no es un rastreador\r\nudp://127.0.0.1:9/announce')
        print('    status:', status)
        print('    magnet:', magnet[:90])
        print('    screenshot:', shot)
        torrent = folder + '.torrent'
        check('the window makes the torrent, next to the folder', 'Torrent creado' in status and os.path.isfile(torrent), status)
        meta = bdecode(open(torrent, 'rb').read())[0] if os.path.isfile(torrent) else {}
        info = meta.get(b'info', {})
        names = sorted('/'.join(x.decode() for x in f[b'path']) for f in info.get(b'files', []))
        check('it holds the files of the folder, those of its folders too', info.get(b'name') == b'Curso de prueba'
              and names == ['Anexos/Apuntes.pdf', 'Leccion 1.mkv', 'Leccion 2.mkv'], (info.get(b'name'), names))
        good, total = pieces_ok(info, mine) if info else (False, 0)
        check('the hash of every piece is the one of the data', good and total == 700000 + 450000 + 90000, total)
        trackers = [t.decode() for tier in meta.get(b'announce-list', []) for t in tier] or [meta.get(b'announce', b'').decode()]
        check('the trackers are in it; what is not one is left out', trackers == ['http://127.0.0.1:9/announce', 'udp://127.0.0.1:9/announce'], trackers)
        check('the magnet link is given', magnet.startswith('magnet:?xt=urn:btih:') and hashlib.sha1(
            open(torrent, 'rb').read()[open(torrent, 'rb').read().index(b'4:info') + 6:-1]).hexdigest() in magnet.lower(), magnet[:80])
        check('"Sembrando" is told', 'sembrando' in status.lower(), status)

        # ---- a torrent of one file
        status, magnet, _ = create(p.pid, hwnd, single, '')
        meta = bdecode(open(single + '.torrent', 'rb').read())[0] if os.path.isfile(single + '.torrent') else {}
        info = meta.get(b'info', {})
        good, total = pieces_ok(info, mine) if info else (False, 0)
        check('a single file makes a torrent as well, without trackers', info.get(b'name') == b'Grabacion suelta.mkv' and good and total == 520000
              and b'announce' not in meta, (status, list(meta)))
        time.sleep(4)
        seeding = torrents()
        print('    torrents:', [(n, d.get('complete'), d.get('status')) for n, d in seeding.items()])
        check('both are in the list of Pegaso, complete and offered', all(n in seeding and seeding[n].get('complete') for n in ('Curso de prueba', 'Grabacion suelta.mkv')), seeding)
        check('the data stayed where it was', os.path.isfile(os.path.join(folder, 'Leccion 1.mkv')) and os.path.isfile(single)
              and not os.path.exists(os.path.join(incoming, 'Curso de prueba')))
        check('still running', p.poll() is None)
    finally:
        if not w.close_gracefully(p, 60):
            p.kill()

    # ---- closed and opened again
    p = r.start(w.RUN)
    try:
        u.main_window(p.pid)
        t0 = time.time()
        while time.time() - t0 < 40 and len([d for d in torrents().values() if d.get('complete')]) < 2:
            time.sleep(2)
        seeding = torrents()
        print('    torrents after a restart:', [(n, d.get('complete'), d.get('status')) for n, d in seeding.items()])
        check('after a restart both are seeded still', all(n in seeding and seeding[n].get('complete') for n in ('Curso de prueba', 'Grabacion suelta.mkv')), seeding)
    finally:
        if not w.close_gracefully(p, 60):
            p.kill()
    print('RESULT', 'PASS' if ok else 'FAIL')


if __name__ == '__main__':
    main()
