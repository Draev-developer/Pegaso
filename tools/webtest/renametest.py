"""Names for finished videos from the film and series databases, and moving them.

usage: python -I renametest.py

The databases are played by a server of this test (preferences.ini: RenameTestBase), so
nothing is asked on the Internet. Checked: what is read from the names of the files, the
name each database gives, one request for all the episodes of a series, the new names on
disk, the move to another folder, and that the files stay shared on eD2K with the same
hash and seeded on BitTorrent - also after Pegaso is closed and opened again.
"""
import ctypes
import gzip
import hashlib
import json
import os
import shutil
import stat
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import ratetest as r  # noqa: E402
import btuitest  # noqa: E402
import apitest  # noqa: E402

MP_PEGASO_RENAME = 10991
IDC_RN_LIST, IDC_RN_SERIES_DB, IDC_RN_MOVIES_DB, IDC_RN_LOOKUP = 5080, 5082, 5084, 5085
IDC_RN_MOVIEDIR, IDC_RN_SERIESDIR, IDC_RN_APPLY, IDC_RN_STATUS = 5087, 5090, 5093, 5094
CB_SETCURSEL, WM_SETTEXT, WM_GETTEXT = 0x014E, 0x000C, 0x000D
DB_PORT = 8769
seen = []

TITLES = ('# list of titles\n1|1|x-jat|Tesuto no Anime\n1|4|en|The Test Anime\n1|4|es|El anime de prueba\n'
          '2|1|x-jat|Betsu no Anime\n2|4|en|Another Anime\n')


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def answer(self, status, body, kind='application/json'):
        if not isinstance(body, bytes):
            body = json.dumps(body).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', 0)))
        seen.append(('POST', self.path, body.decode('utf-8'), ''))
        if self.path == '/tvdb/login' and json.loads(body).get('apikey') == 'clave-tvdb':
            return self.answer(200, {'status': 'success', 'data': {'token': 'ficha-123'}})
        self.answer(401, {'status': 'failure'})

    def do_GET(self):
        url = urlparse(self.path)
        q = parse_qs(url.query)
        seen.append(('GET', url.path, q, self.headers.get('Authorization', '')))
        if url.path == '/tvmaze/singlesearch/shows':
            for known, proper in (('otra serie', 'Otra serie de prueba'), ('tercera serie', 'Tercera serie de prueba'), ('cuarta serie', 'Cuarta serie de prueba')):
                if known in q['q'][0].lower():
                    return self.answer(200, {'id': 10, 'url': 'x', 'name': proper, 'type': 'Scripted'})
            if 'serie de prueba' in q['q'][0].lower():
                return self.answer(200, {'id': 7, 'url': 'x', 'name': 'La serie de prueba', 'type': 'Scripted', 'network': {'name': 'Cadena'}})
            if 'documental' in q['q'][0].lower():
                return self.answer(200, {'id': 8, 'url': 'x', 'name': 'Documental: la última "frontera"?', 'type': 'Documentary'})
            if 'grabacion de prueba' in q['q'][0].lower():
                return self.answer(200, {'id': 9, 'url': 'x', 'name': 'Grabaci\u00f3n de prueba', 'type': 'Scripted'})
            return self.answer(404, {'name': 'Not Found', 'status': 404})
        if url.path == '/tmdb/search/movie':
            if q.get('api_key') != ['clave-tmdb']:
                return self.answer(401, {'status_code': 7})
            if 'viaje' in q['query'][0].lower() and q.get('year') == ['2010'] and q.get('language') == ['es-ES']:
                return self.answer(200, {'page': 1, 'results': [
                    {'adult': False, 'original_title': 'The Test Journey', 'overview': 'Un "title" dentro', 'release_date': '2010-07-15', 'title': 'El viaje de prueba'},
                    {'adult': False, 'original_title': 'Other', 'release_date': '1999-01-01', 'title': 'Otro viaje'}]})
            return self.answer(200, {'page': 1, 'results': []})
        if url.path == '/omdb/':
            if q.get('apikey') != ['clave-omdb']:
                return self.answer(401, {'Response': 'False', 'Error': 'Invalid API key!'})
            if 'viaje' in q['t'][0].lower():
                return self.answer(200, {'Title': 'The Test Journey', 'Year': '2010', 'Response': 'True'})
            return self.answer(200, {'Response': 'False', 'Error': 'Movie not found!'})
        if url.path == '/tvdb/search':
            if self.headers.get('Authorization') != 'Bearer ficha-123':
                return self.answer(401, {'status': 'failure'})
            if 'serie de prueba' in q['query'][0].lower():
                return self.answer(200, {'status': 'success', 'data': [{'objectID': 'series-7', 'name': 'The Test Series',
                                         'overviews': {'spa': 'Resumen'}, 'translations': {'eng': 'The Test Series', 'spa': 'La serie de prueba (TVDB)'}}]})
            return self.answer(200, {'status': 'success', 'data': []})
        if url.path == '/anidb/anime-titles.dat.gz':
            return self.answer(200, gzip.compress(TITLES.encode('utf-8')), 'application/gzip')
        self.answer(404, {})


def bencode(x):
    if isinstance(x, int):
        return b'i%de' % x
    if isinstance(x, bytes):
        return b'%d:%s' % (len(x), x)
    if isinstance(x, str):
        return bencode(x.encode('utf-8'))
    if isinstance(x, list):
        return b'l' + b''.join(bencode(v) for v in x) + b'e'
    if isinstance(x, dict):
        return b'd' + b''.join(bencode(k) + bencode(v) for k, v in sorted(x.items())) + b'e'
    raise TypeError(x)


def make_torrent(path, torrent):
    data = open(path, 'rb').read()
    piece = 262144
    pieces = b''.join(hashlib.sha1(data[i:i + piece]).digest() for i in range(0, len(data), piece))
    open(torrent, 'wb').write(bencode({'announce': 'http://127.0.0.1:9/announce',
                                       'info': {'length': len(data), 'name': os.path.basename(path), 'piece length': piece, 'pieces': pieces}}))


def make_folder_torrent(folder, torrent):
    """a torrent of all the files of a folder, the folder itself being its name"""
    entries, data = [], b''
    for name in sorted(os.listdir(folder)):
        content = open(os.path.join(folder, name), 'rb').read()
        entries.append({'length': len(content), 'path': [name.encode('utf-8')]})
        data += content
    piece = 262144
    pieces = b''.join(hashlib.sha1(data[i:i + piece]).digest() for i in range(0, len(data), piece))
    open(torrent, 'wb').write(bencode({'announce': 'http://127.0.0.1:9/announce',
                                       'info': {'files': entries, 'name': os.path.basename(folder), 'piece length': piece, 'pieces': pieces}}))


def wipe(folder):
    """removes a folder of the test with all it holds; folders marked read-only included"""
    for root, dirs, files in os.walk(folder, topdown=False):
        for name in files:
            os.chmod(os.path.join(root, name), stat.S_IWRITE)
            os.remove(os.path.join(root, name))
        for name in dirs:
            os.chmod(os.path.join(root, name), stat.S_IWRITE)
            os.rmdir(os.path.join(root, name))
    if os.path.isdir(folder):
        os.chmod(folder, stat.S_IWRITE)
        os.rmdir(folder)


def text_of(hwnd):
    buf = ctypes.create_unicode_buffer(1024)
    u.user32.SendMessageW(hwnd, WM_GETTEXT, 1024, ctypes.addressof(buf))
    return buf.value


def set_text(hwnd, text):
    buf = ctypes.create_unicode_buffer(text)
    u.user32.SendMessageW(hwnd, WM_SETTEXT, 0, ctypes.addressof(buf))


def dialog(pid, hwnd):
    for h, t in u.top_windows(pid):
        if h != hwnd and t == 'Renombrar vídeos':
            return h
    return None


def shared():
    status, listed = apitest.call('GET', '/api/v1/shared')
    return {f['name']: f for f in listed['files']} if status == 200 else {}


def torrents():
    status, listed = apitest.call('GET', '/api/v1/downloads')
    return [d for d in listed['downloads'] if d.get('kind') == 'torrent'] if status == 200 else []


def hidden_seeds():
    status, state = apitest.call('GET', '/api/v1/status')
    return state.get('hiddenSeeds') if status == 200 else None


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ok = True

    def check(what, cond, detail=''):
        nonlocal ok
        print('  %-70s %s %s' % (what, 'ok' if cond else 'FAIL', '' if cond else detail))
        ok &= bool(cond)

    btuitest.deploy()
    for sub in ('config', 'Incoming', 'Temp', 'Series ordenadas'):
        wipe(os.path.join(w.RUN, sub))
    r.make_instance(w.RUN, 4700, 'ApiEnabled=1\nApiPort=%d\nApiToken=%s\nRenameTestBase=http://127.0.0.1:%d\n'
                    'RenameKeyTmdb=clave-tmdb\nRenameKeyOmdb=clave-omdb\nRenameKeyTvdb=clave-tvdb\n' % (apitest.PORT, apitest.TOKEN, DB_PORT))
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('TorrentEnabled=0', 'TorrentEnabled=1\nTorrentPort=4720')
    open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
    incoming = os.path.join(w.RUN, 'Incoming')
    os.makedirs(incoming, exist_ok=True)
    series_dir = os.path.join(w.RUN, 'Series ordenadas')
    wipe(series_dir)
    names = ['La.Serie.De.Prueba.S01E05.720p.HDTV.x264.mkv', 'la serie de prueba 1x06 [HDTV][Castellano].avi', 'El.Viaje.De.Prueba.2010.1080p.BluRay.x264.mkv',
             'Documental [HDTV 720p][Cap.203].avi', '[Grupo] Tesuto no Anime - 07 [1080p].mkv', 'Cosa.Desconocida.S03E01.mkv', 'Grabacion.De.Prueba.S02E03.WEB-DL.mkv']
    for n, name in enumerate(names):
        open(os.path.join(incoming, name), 'wb').write(os.urandom(300000 + 1000 * n))
    seeded = os.path.join(incoming, names[-1])
    torrent = os.path.join(w.RUN, 'sembrado.torrent')
    make_torrent(seeded, torrent)
    # folders inside the download folder: one of a torrent with something else in it, one with
    # a video alone, one with two videos of which only one will be moved
    pack = os.path.join(incoming, 'Paquete.De.Prueba')
    alone = os.path.join(incoming, 'Carpeta suelta', 'Dentro')
    pair = os.path.join(incoming, 'Dos videos')
    for folder in (pack, alone, pair):
        os.makedirs(folder)
    open(os.path.join(pack, 'Otra.Serie.De.Prueba.S01E01.mkv'), 'wb').write(os.urandom(310000))
    open(os.path.join(pack, 'informacion.nfo'), 'wb').write(os.urandom(2000))
    open(os.path.join(alone, 'Tercera.Serie.De.Prueba.S01E02.mkv'), 'wb').write(os.urandom(311000))
    open(os.path.join(pair, 'Cuarta.Serie.De.Prueba.S01E03.mkv'), 'wb').write(os.urandom(312000))
    open(os.path.join(pair, 'Algo.Sin.Nombre.Conocido.S09E09.mkv'), 'wb').write(os.urandom(313000))
    pack_torrent = os.path.join(w.RUN, 'paquete.torrent')
    make_folder_torrent(pack, pack_torrent)

    # nothing of an earlier run: a torrent left behind would come back with the engine
    state = os.path.join(w.RUN, 'config', 'torrents')
    for left in (os.listdir(state) if os.path.isdir(state) else []):
        os.remove(os.path.join(state, left))
    check('the profile of the test starts clean', not os.path.isdir(state) or not os.listdir(state))
    fake = ThreadingHTTPServer(('127.0.0.1', DB_PORT), Handler)
    threading.Thread(target=fake.serve_forever, daemon=True).start()
    p = r.start(w.RUN)
    mem = None
    try:
        hwnd = u.main_window(p.pid)
        for h, t in u.top_windows(p.pid):
            if h != hwnd and t.startswith('Asistente'):
                u.user32.PostMessageW(h, u.WM_COMMAND, 2, 0)
        mem = u.Remote(p.pid)
        subprocess.run([w.EXE, torrent], timeout=60)   # handed over to the copy which is running
        subprocess.run([w.EXE, pack_torrent], timeout=60)
        t0 = time.time()
        while time.time() - t0 < 90 and (len(shared()) < len(names) or len([t for t in torrents() if t.get('complete')]) < 2):
            time.sleep(2)
        before = shared()
        check('the videos of the incoming folder are shared on eD2K', all(n in before for n in names), sorted(before))
        seeding = torrents()
        print('    torrent:', [(t['name'], t.get('complete')) for t in seeding])
        check('the torrents find their files and seed them', len(seeding) == 2 and all(t.get('complete') for t in seeding), seeding)

        u.user32.PostMessageW(hwnd, u.WM_COMMAND, MP_PEGASO_RENAME, 0)
        time.sleep(3)
        dlg = dialog(p.pid, hwnd)
        check('the window opens', dlg is not None, u.top_windows(p.pid))
        lv = u.find_child(dlg, IDC_RN_LIST)
        rows = btuitest.rows(lv, mem)
        for row in rows:
            print('    ', row)
        read = {row[1]: row[0] for row in rows}
        check('what the names of the files say: series, season and episode', read.get(names[0]) == 'La Serie De Prueba - 1x05.mkv'
              and read.get(names[1]) == 'la serie de prueba - 1x06.avi' and read.get(names[3]) == 'Documental - 2x03.avi', read)
        check('... a film and its year', read.get(names[2]) == 'El Viaje De Prueba (2010).mkv', read.get(names[2]))
        check('... an anime with its group in front', read.get(names[4]) == 'Tesuto no Anime - 1x07.mkv', read.get(names[4]))

        # ---- TVmaze for the series, TheMovieDB for the films
        u.user32.SendMessageW(u.find_child(dlg, IDC_RN_SERIES_DB), CB_SETCURSEL, 0, 0)
        u.user32.SendMessageW(u.find_child(dlg, IDC_RN_MOVIES_DB), CB_SETCURSEL, 0, 0)
        u.user32.PostMessageW(dlg, u.WM_COMMAND, IDC_RN_LOOKUP, 0)
        time.sleep(6)
        rows = btuitest.rows(lv, mem)
        for row in rows:
            print('    ', row)
        print('    status:', text_of(u.find_child(dlg, IDC_RN_STATUS)))
        found = {row[1]: row for row in rows}
        check('TVmaze names the series; both episodes get it', found[names[0]][0] == 'La serie de prueba - 1x05.mkv' and found[names[1]][0] == 'La serie de prueba - 1x06.avi')
        asked = [q['q'][0] for m, path, q, _ in seen if path == '/tvmaze/singlesearch/shows']
        check('... with one request for the two', len([a for a in asked if a.lower() == 'la serie de prueba']) == 1, asked)
        check('TheMovieDB names the film, in Spanish, first result', found[names[2]][0] == 'El viaje de prueba (2010).mkv' and found[names[2]][2] == 'TheMovieDB', found[names[2]])
        check('what a file name cannot hold is taken out', found[names[3]][0] == "Documental - la última 'frontera' - 2x03.avi", found[names[3]][0])
        check('a series the database does not know is told so', 'no encontrado' in found[names[5]][2], found[names[5]])

        # ---- the other databases
        u.user32.SendMessageW(u.find_child(dlg, IDC_RN_SERIES_DB), CB_SETCURSEL, 2, 0)   # TheTVDB
        u.user32.SendMessageW(u.find_child(dlg, IDC_RN_MOVIES_DB), CB_SETCURSEL, 1, 0)   # OMDb
        del seen[:]
        u.user32.PostMessageW(dlg, u.WM_COMMAND, IDC_RN_LOOKUP, 0)
        time.sleep(6)
        found = {row[1]: row for row in btuitest.rows(lv, mem)}
        check('TheTVDB: the key is exchanged for a token, the name comes in Spanish', found[names[0]][0] == 'La serie de prueba (TVDB) - 1x05.mkv'
              and len([s for s in seen if s[0] == 'POST']) == 1, (found[names[0]], seen[:3]))
        check('OMDb names the film', found[names[2]][0] == 'The Test Journey (2010).mkv' and found[names[2]][2] == 'OMDb', found[names[2]])
        u.user32.SendMessageW(u.find_child(dlg, IDC_RN_SERIES_DB), CB_SETCURSEL, 3, 0)   # AniDB
        u.user32.PostMessageW(dlg, u.WM_COMMAND, IDC_RN_LOOKUP, 0)
        time.sleep(6)
        found = {row[1]: row for row in btuitest.rows(lv, mem)}
        check('AniDB: the list of titles is fetched and the official Spanish title given', found[names[4]][0] == 'El anime de prueba - 1x07.mkv', found[names[4]])
        check('... and kept for the day', os.path.isfile(os.path.join(w.RUN, 'config', 'anidb-titles.dat.gz')))

        # ---- rename: TVmaze and TheMovieDB again, the series to another folder
        u.user32.SendMessageW(u.find_child(dlg, IDC_RN_SERIES_DB), CB_SETCURSEL, 0, 0)
        u.user32.SendMessageW(u.find_child(dlg, IDC_RN_MOVIES_DB), CB_SETCURSEL, 0, 0)
        u.user32.PostMessageW(dlg, u.WM_COMMAND, IDC_RN_LOOKUP, 0)
        time.sleep(6)
        set_text(u.find_child(dlg, IDC_RN_SERIESDIR), series_dir)
        print('    screenshot:', u.screenshot(dlg, 'shot_rename.png'))
        check('Pegaso can be used while the window is open', bool(u.user32.IsWindowEnabled(hwnd)))
        u.user32.PostMessageW(dlg, u.WM_COMMAND, IDC_RN_APPLY, 0)
        t0 = time.time()
        while time.time() - t0 < 90 and 'renombrados' not in text_of(u.find_child(dlg, IDC_RN_STATUS)):
            time.sleep(1)
        print('    moved in %.0f s' % (time.time() - t0))
        rows = btuitest.rows(lv, mem)
        print('    status:', text_of(u.find_child(dlg, IDC_RN_STATUS)))
        for row in rows:
            print('    ', row)
        check('the series are renamed and moved to the folder chosen, which is made', os.path.isfile(os.path.join(series_dir, 'La serie de prueba - 1x05.mkv'))
              and os.path.isfile(os.path.join(series_dir, 'La serie de prueba - 1x06.avi')), os.listdir(series_dir) if os.path.isdir(series_dir) else 'no folder')
        check('the film is renamed where it was', os.path.isfile(os.path.join(incoming, 'El viaje de prueba (2010).mkv')), os.listdir(incoming))
        check('what was not found is left alone', os.path.isfile(os.path.join(incoming, names[5])) and os.path.isfile(os.path.join(incoming, names[4])))
        check('the old names are gone', not os.path.exists(os.path.join(incoming, names[0])) and not os.path.exists(os.path.join(incoming, names[2])))
        u.user32.PostMessageW(dlg, u.WM_COMMAND, 2, 0)
        time.sleep(3)

        # ---- still shared, the same files for the network
        after = shared()
        print('    shared:', sorted(after))
        same = all(new in after and after[new]['hash'] == before[old]['hash'] for old, new in (
            (names[0], 'La serie de prueba - 1x05.mkv'), (names[1], 'La serie de prueba - 1x06.avi'), (names[2], 'El viaje de prueba (2010).mkv')))
        check('eD2K: the files are shared under their new names with the same hash', same, sorted(after))

        # ---- the folders the videos were in
        check('a folder of a torrent goes once its video is moved away', os.path.isfile(os.path.join(series_dir, 'Otra serie de prueba - 1x01.mkv'))
              and not os.path.exists(pack), os.listdir(incoming))
        kept = [os.path.join(d, f) for d, _, fs in os.walk(os.path.join(w.RUN, 'config', 'torrents', 'files')) for f in fs]
        check('... what the torrent still offers is kept for it elsewhere', [os.path.basename(k) for k in kept] == ['informacion.nfo'], kept)
        check('a folder left empty goes, and the empty one above it', os.path.isfile(os.path.join(series_dir, 'Tercera serie de prueba - 1x02.mkv'))
              and not os.path.exists(os.path.join(incoming, 'Carpeta suelta')), os.listdir(incoming))
        check('a folder with another video in it stays', os.path.isfile(os.path.join(series_dir, 'Cuarta serie de prueba - 1x03.mkv'))
              and os.listdir(pair) == ['Algo.Sin.Nombre.Conocido.S09E09.mkv'], os.listdir(pair) if os.path.isdir(pair) else 'gone')
        check('the download folder itself stays', os.path.isdir(incoming))
        check('... those moved out of the shared folder too', os.path.dirname(after.get('La serie de prueba - 1x05.mkv', {}).get('path', '')) in ('', series_dir) and
              'La serie de prueba - 1x05.mkv' in after)
        moved = os.path.join(series_dir, 'Grabación de prueba - 2x03.mkv')
        check('BitTorrent: the file of the torrent is renamed and moved as well', os.path.isfile(moved) and not os.path.exists(seeded),
              os.listdir(series_dir) if os.path.isdir(series_dir) else 'no folder')
        check('... shared on eD2K with the same hash', after.get('Grabación de prueba - 2x03.mkv', {}).get('hash') == before[names[-1]]['hash'])
        time.sleep(4)
        seeding = torrents()
        print('    torrent:', [(t['name'], t.get('complete')) for t in seeding])
        check('... the torrents whose videos were moved leave the download list', seeding == [], seeding)
        check('... and go on being seeded, out of it', hidden_seeds() == 2, hidden_seeds())
    finally:
        if mem:
            mem.close()
        if not w.close_gracefully(p, 60):
            p.kill()

    # ---- closed and opened again
    p = r.start(w.RUN)
    try:
        u.main_window(p.pid)
        t0 = time.time()
        moved_names = [n for n in after if after[n].get('path', '').lower().startswith(series_dir.lower())]
        while time.time() - t0 < 60 and (not all(n in shared() for n in moved_names) or hidden_seeds() != 2):
            time.sleep(2)
        again = shared()
        print('    shared after a restart:', sorted(again))
        check('after a restart the moved files are shared still, with the same hash', len(moved_names) >= 4
              and all(n in again and again[n]['hash'] == after[n]['hash'] for n in moved_names), (moved_names, sorted(again)))
        seeding = torrents()
        print('    torrent:', [(t['name'], t.get('complete')) for t in seeding])
        check('... and the torrents seed from the new places, out of the download list: nothing is downloaded again', seeding == [] and hidden_seeds() == 2
              and not os.path.exists(seeded) and os.path.isfile(moved) and not os.path.exists(pack), (seeding, hidden_seeds()))
        check('still running', p.poll() is None)
    finally:
        if not w.close_gracefully(p, 60):
            p.kill()

    if os.environ.get('RENAME_OTHER_DISK'):
        print('    on the other disk:', sorted(os.listdir(series_dir)))
        wipe(series_dir)   # the folder of the test, nothing else
    print('RESULT', 'PASS' if ok else 'FAIL')


if __name__ == '__main__':
    main()
