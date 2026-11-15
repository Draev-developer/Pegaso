"""The fake detector: the "trust" column of the search results and of the download list.

usage: python -I trusttest.py [search|downloads]

search     A stored search (config\\StoredSearches.met, written here) is restored at startup;
           its results carry misleading names, several names for one file, an impossible
           duration and ratings. The column is read back, and downloading a suspicious
           result has to ask first.
downloads  Downloads whose first bytes come from a local web server: a program named as a
           video, a video in another container, an archive named as a document... plus a
           disguised name and a torrent with a disguised program inside. Needs Kad (eMule
           asks its sources only while it is connected).
"""
import hashlib
import os
import re
import struct
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import btuitest  # noqa: E402
import btfilestest  # noqa: E402
import nettest  # noqa: E402
import ratetest as r  # noqa: E402

IDC_SEARCHLIST = 2172
MP_HM_SEARCH = 10212
MB = 1024 * 1024
FT_FILENAME, FT_FILESIZE, FT_SOURCES, FT_COMPLETE, FT_LENGTH, FT_RATING = 0x01, 0x02, 0x15, 0x30, 0xD3, 0xF7


# ---------------------------------------------------------------- the stored search
def tag(name_id, value):
    if isinstance(value, str):
        raw = value.encode('utf-8')
        return struct.pack('<BBH', 0x82, name_id, len(raw)) + raw  # TAGTYPE_STRING
    return struct.pack('<BBI', 0x83, name_id, value)  # TAGTYPE_UINT32


def result(seed, name, size, sources, extra=()):
    tags = [tag(FT_FILENAME, name), tag(FT_FILESIZE, size), tag(FT_SOURCES, sources), tag(FT_COMPLETE, sources)]
    tags += [tag(k, v) for k, v in extra]
    return hashlib.md5(seed.encode()).digest() + struct.pack('<IHI', 0, 0, len(tags)) + b''.join(tags)


def string(text):
    raw = text.encode('utf-8')
    return struct.pack('<H', len(raw)) + raw


SEARCH = [
    # seed (one per file), name, size, sources, extra tags -> what the column has to say
    ('a', 'Video.prueba.avi', 700 * MB, 20, (), 'Sin avisos'),
    ('b', 'Video.prueba.2.avi.exe', 2 * MB, 12, (), 'Probable falso (85)'),
    ('c', 'Video.prueba.3.mkv', 300 * MB, 10, (), 'Probable falso (95)'),
    ('c', 'Programa.instalador.exe', 300 * MB, 8, (), None),
    ('c', 'Otra.cosa.distinta.mp3', 300 * MB, 6, (), None),
    ('d', 'Video.prueba.4.avi', 30 * MB, 9, ((FT_LENGTH, 3 * 3600),), 'Precaución (40)'),
    ('e', 'Video.prueba.5.mkv', 400 * MB, 7, ((FT_RATING, 1 * 51),), 'Precaución (30)'),
    ('f', 'Video.prueba.6.mkv', 400 * MB, 7, ((FT_RATING, 5 * 51),), 'Parece correcto'),
    ('g', 'Video.prueba.7.1080p.x264.zip', 2 * MB, 5, (), 'Precaución (35)'),
    ('h', 'Video.prueba.8.mkv', 500 * MB, 4, (), 'Sin avisos'),
    ('h', 'Video.prueba.8.(otro.nombre).mkv', 500 * MB, 3, (), None),
    ('i', 'Documento.prueba.pdf.scr', 1 * MB, 3, (), 'Probable falso (85)'),
]


def write_stored_search(folder):
    body = b'\x0f\x01' + struct.pack('<H', 1)
    body += struct.pack('<IBB', 0, 1, 0) + string('') + string('prueba') + string('')  # an eD2K server search
    body += struct.pack('<I', len(SEARCH)) + b''.join(result(s, n, size, src, extra) for s, n, size, src, extra, _ in SEARCH)
    open(os.path.join(folder, 'config', 'StoredSearches.met'), 'wb').write(body)


def message_box(pid):
    for hwnd, title in u.top_windows(pid):
        cls = u.ctypes.create_unicode_buffer(32)
        u.user32.GetClassNameW(hwnd, cls, 32)
        if cls.value == '#32770' and u.find_child(hwnd, 6):  # has a "Yes" button
            return hwnd
    return None


def scenario_search():
    r.make_instance(w.RUN, 4700)
    write_stored_search(w.RUN)
    pegaso = r.start(w.RUN)
    mem = None
    ok = True
    try:
        hwnd = u.main_window(pegaso.pid)
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, MP_HM_SEARCH, 0)
        time.sleep(2)
        lv = u.find_child(hwnd, IDC_SEARCHLIST)
        mem = u.Remote(pegaso.pid)
        rows = btuitest.rows(lv, mem)
        got = {row[0]: row[-1] for row in rows}
        print('%-34s %-22s %s' % ('result', 'column', 'expected'))
        for _, name, _, _, _, want in SEARCH:
            if want is None:
                continue
            print('%-34s %-22s %s' % (name, got.get(name), want))
            ok &= got.get(name) == want
        print('screenshot:', u.screenshot(hwnd, 'shot_trust_search.png'))

        # ---- downloading a suspicious result asks first
        dl = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
        index = next(i for i, row in enumerate(rows) if row[0] == 'Video.prueba.2.avi.exe')
        for answer, expected in ((7, 0), (6, 1)):  # IDNO, IDYES
            u.select_item(lv, mem, index)
            u.user32.PostMessageW(lv, u.WM_COMMAND, u.MP_RESUME, 0)
            time.sleep(2)
            box = message_box(pegaso.pid)
            if box and answer == 7:
                print('screenshot:', u.screenshot(box, 'shot_trust_ask.png'))
            print('asked before downloading:', box is not None)
            ok &= box is not None
            if box:
                u.user32.PostMessageW(box, u.WM_COMMAND, answer, 0)
            time.sleep(2)
            count = u.user32.SendMessageW(dl, u.LVM_GETITEMCOUNT, 0, 0)
            print('  answer %s -> downloads: %d (expected %d)' % ('no' if answer == 7 else 'yes', count, expected))
            ok &= count == expected
        # a result with nothing against it is downloaded without a question
        index = next(i for i, row in enumerate(rows) if row[0] == 'Video.prueba.avi')
        item = u.LVITEM(stateMask=u.LVIS_SELECTED | u.LVIS_FOCUSED, state=0)
        mem.write(item)
        u.user32.SendMessageW(lv, u.LVM_SETITEMSTATE, -1, mem.addr)
        u.select_item(lv, mem, index)
        u.user32.PostMessageW(lv, u.WM_COMMAND, u.MP_RESUME, 0)
        time.sleep(2)
        box = message_box(pegaso.pid)
        count = u.user32.SendMessageW(dl, u.LVM_GETITEMCOUNT, 0, 0)
        print('a clean result: asked %s, downloads: %d (expected 2)' % (box is not None, count))
        ok &= box is None and count == 2
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
        time.sleep(12)  # the detector looks at the downloads every 10 s
        print('download list:', [(row[0], row[-1]) for row in btuitest.rows(dl, mem)])
        log = r.log_of(w.RUN)
        warned = 'Video.prueba.2.avi.exe": Probable falso' in log
        print('warning in the log:', warned)
        ok &= warned and pegaso.poll() is None
    finally:
        if mem:
            mem.close()
        if not w.close_gracefully(pegaso, 60):
            pegaso.kill()
    return ok


# ---------------------------------------------------------------- downloads
SIZE = 6 * MB
RATE = 96 * 1024  # bytes per second of the web servers: the column is read while the downloads run
MKV = b'\x1a\x45\xdf\xa3'
DOWNLOADS = [
    # name, first bytes, expected column
    ('Video.prueba.avi', b'MZ\x90\x00\x03\x00\x00\x00', 'Probable falso (90)'),
    ('Video.prueba.2.mkv', MKV, 'Parece correcto'),
    ('Video.prueba.3.avi', MKV, 'Sin avisos'),
    ('Documento.prueba.pdf', b'PK\x03\x04', 'Sospechoso (50)'),
    ('Video.prueba.5.avi', b'\x00' * 16, 'Precaución (40)'),
]
contents = {}
requests = []


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def do_GET(self):
        data = contents.get(self.path.rsplit('/', 1)[-1])  # eMule sends the whole address
        m = re.match(r'bytes=(\d+)-(\d*)', self.headers.get('Range') or '')
        start = int(m.group(1)) if m else 0
        end = int(m.group(2)) if m and m.group(2) else SIZE - 1
        requests.append((self.path, start, end))
        if data is None:
            self.send_error(404)
            return
        body = data[start:end + 1]
        self.send_response(206 if m else 200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(len(body)))
        if m:
            self.send_header('Content-Range', 'bytes %d-%d/%d' % (start, end, SIZE))
        self.send_header('Accept-Ranges', 'bytes')
        self.end_headers()
        try:
            for i in range(0, len(body), RATE // 4):
                self.wfile.write(body[i:i + RATE // 4])
                time.sleep(0.25)
        except OSError:
            pass


def scenario_downloads():
    ip = r.lan_ip()
    servers = []
    links = []
    for i, (name, head, _) in enumerate(DOWNLOADS):
        data = head + os.urandom(SIZE - len(head))
        contents[name] = data
        server = ThreadingHTTPServer((ip, 8781 + i), Handler)  # one address per file
        threading.Thread(target=server.serve_forever, daemon=True).start()
        servers.append(server)
        links.append('ed2k://|file|%s|%d|%s|s=http://%s:%d/%s|/' % (name, SIZE, w.ed2k_hash(data), ip, 8781 + i, name))
    # a disguised name, without any source
    links.append('ed2k://|file|Video.prueba.6.avi.exe|%d|%s|/' % (SIZE, w.ed2k_hash(os.urandom(SIZE))))
    # a torrent with a disguised program inside
    piece = 256 * 1024
    files = [('Video.prueba.7.mkv', os.urandom(piece)), ('Video.prueba.7.mkv.exe', os.urandom(2 * piece))]
    blob = b''.join(d for _, d in files)
    meta = {'announce': 'http://127.0.0.1:9/announce',
            'info': {'name': 'Video.prueba.7', 'piece length': piece,
                     'pieces': b''.join(hashlib.sha1(blob[i:i + piece]).digest() for i in range(0, len(blob), piece)),
                     'files': [{'length': len(d), 'path': [n]} for n, d in files]}}
    torrent = os.path.join(HERE, 'Video.prueba.7.torrent')
    open(torrent, 'wb').write(btfilestest.bencode(meta))
    expected = {name: want for name, _, want in DOWNLOADS}
    expected['Video.prueba.6.avi.exe'] = 'Probable falso (85)'
    expected['Video.prueba.7'] = 'Probable falso (85)'

    r.make_instance(w.RUN, 4700, 'TorrentPort=6881\n')
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('NetworkKademlia=0\n', '').replace('TorrentEnabled=0\n', '')
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
        for link in links + [torrent]:
            r.send_link(hwnd, link)
        t0 = time.time()
        got = {}  # the last thing the column said about each download while it was in the list
        shot = None
        while time.time() - t0 < 150:
            time.sleep(4)
            now = {row[0]: row[-1] for row in btuitest.rows(lv, mem)}
            got.update(now)
            print('  t=%3ds %d requests; %s' % (time.time() - t0, len(requests), sorted(now.values())))
            if shot is None and now == expected:
                shot = u.screenshot(hwnd, 'shot_trust_downloads.png')
            if got == expected and shot:
                break
        print('%-28s %-22s %s' % ('download', 'column', 'expected'))
        for name, want in expected.items():
            print('%-28s %-22s %s' % (name, got.get(name), want))
        ok &= got == expected and pegaso.poll() is None
        print('screenshot:', shot)
        log = r.log_of(w.RUN)
        for line in log.splitlines():
            if 'Probable falso' in line or 'Sospechoso' in line:
                print('  log:', line.strip()[:200])
    finally:
        if mem:
            mem.close()
        if not w.close_gracefully(pegaso, 60):
            pegaso.kill()
        for server in servers:
            server.shutdown()
    return ok


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    which = sys.argv[1] if len(sys.argv) > 1 else 'search'
    ok = scenario_search() if which == 'search' else scenario_downloads()
    print('RESULT', 'PASS' if ok else 'FAIL')


if __name__ == '__main__':
    main()
