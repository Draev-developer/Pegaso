"""What 0.7 added: the watched folder, the folders by type, playing a torrent while it
downloads, the import from eMule and qBittorrent, and the names looked up through the adapter.

usage: python -I autotest.py [folders] [play] [import] [dns]

Nothing leaves this computer: the web server runs here. "folders" and "dns" tie Pegaso to a
connected adapter and call the web server at the address of that adapter.
"""
import ctypes
import hashlib
import os
import shutil
import socket
import sys
import threading
import time
from http.server import ThreadingHTTPServer
from urllib.parse import quote

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import ratetest as r  # noqa: E402
import btuitest  # noqa: E402
import apitest  # noqa: E402
import vpntest  # noqa: E402
import webseedtest as ws  # noqa: E402
from proxytest import bencode, wipe  # noqa: E402
from privtest import adapters, ini_value, wait_for, stop  # noqa: E402

WEB_PORT = 8776
PIECE = 65536
MP_PEGASO_IMPORT = 10997
IDC_IM_EMULE, IDC_IM_EMULE_GO, IDC_IM_QB, IDC_IM_QB_GO, IDC_IM_RESULT = 5181, 5183, 5186, 5188, 5190
WM_SETTEXT, WM_GETTEXT, BM_CLICK = 0x000C, 0x000D, 0x00F5
ok = True


def check(what, cond, detail=''):
    global ok
    print('  %-80s %s %s' % (what, 'ok' if cond else 'FAIL', '' if cond else detail))
    ok &= bool(cond)


def instance(extra, torrents=True):
    btuitest.deploy()
    for sub in ('config', 'Incoming', 'Temp', 'logs', 'vigilada', 'por-tipo', 'de-emule', 'de-qb'):
        wipe(os.path.join(w.RUN, sub))
    r.make_instance(w.RUN, 4700, 'ApiEnabled=1\nApiPort=%d\nApiToken=%s\nPlayDryRun=1\n%s' % (apitest.PORT, apitest.TOKEN, extra))
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    if torrents:
        text = open(ini).read().replace('TorrentEnabled=0', 'TorrentEnabled=1\nTorrentPort=4720')
        open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
    return ini


def torrent_of(name, data, url=None, path=None):
    info = {'name': name, 'piece length': PIECE, 'length': len(data),
            'pieces': b''.join(hashlib.sha1(data[i:i + PIECE]).digest() for i in range(0, len(data), PIECE))}
    meta = {'info': info, 'announce': 'http://127.0.0.1:8770/announce'}
    if url:
        meta['url-list'] = url
    path = path or os.path.join(w.RUN, name + '.torrent')
    open(path, 'wb').write(bencode(meta))
    return path, hashlib.sha1(bencode(info)).hexdigest()


def downloads():
    status, listed = apitest.call('GET', '/api/v1/downloads')
    return listed.get('downloads', []) if status == 200 else []


def shared_names():
    status, listed = apitest.call('GET', '/api/v1/shared')
    return [f.get('name') for f in listed.get('files', [])] if status == 200 else []


def run_folders(found):
    print('---- watched folder, folders by type, playing')
    if not found:
        print('  skipped: no connected adapter')
        return
    tied, ip = found[0]
    ws.FILES.clear()
    ws.FILES.update({'Clase.grabada.mkv': os.urandom(700000), 'Apuntes.del.curso.pdf': os.urandom(300000),
                     'Datos.sueltos.bin': os.urandom(200000), 'Charla.con.semilla.mkv': os.urandom(3 * 1024 * 1024)})
    web = ThreadingHTTPServer((ip, WEB_PORT), ws.Handler)
    threading.Thread(target=web.serve_forever, daemon=True).start()
    base = 'http://%s:%d/' % (ip, WEB_PORT)
    watched = os.path.join(w.RUN, 'vigilada')
    videos = os.path.join(w.RUN, 'por-tipo', 'videos')
    docs = os.path.join(w.RUN, 'por-tipo', 'documentos')
    ini = instance('PegasoBindAdapter=%s\nWatchFolder=%s\nTypeFolderVideo=%s\nTypeFolderDoc=%s\n' % (quote(tied), quote(watched), quote(videos), quote(docs)))
    os.makedirs(watched, exist_ok=True)
    incoming = os.path.join(w.RUN, 'Incoming')
    p = r.start(w.RUN)
    try:
        u.main_window(p.pid)
        open(os.path.join(watched, 'lista.txt'), 'w', encoding='utf-8').write(
            base + 'Clase.grabada.mkv\n\n' + base + 'Apuntes.del.curso.pdf\n' + base + 'Datos.sueltos.bin\nesto no es un enlace\n')
        torrent_of('Charla.con.semilla.mkv', ws.FILES['Charla.con.semilla.mkv'], base + 'Charla.con.semilla.mkv', os.path.join(watched, 'charla.torrent'))
        check('what was left in the watched folder is taken and put away',
              wait_for(lambda: sorted(os.listdir(os.path.join(watched, 'added'))) == ['charla.torrent', 'lista.txt'] if os.path.isdir(os.path.join(watched, 'added')) else False, 40),
              os.listdir(watched))
        check('and nothing of it is left behind', [n for n in os.listdir(watched) if n != 'added'] == [], os.listdir(watched))
        check('the web video lands in the folder of the videos', wait_for(lambda: os.path.isfile(os.path.join(videos, 'Clase.grabada.mkv')), 60), r.log_of(w.RUN)[-600:])
        check('the document in the folder of the documents', wait_for(lambda: os.path.isfile(os.path.join(docs, 'Apuntes.del.curso.pdf')), 40), os.listdir(incoming))
        check('a file of a type without a folder stays in the downloads folder', wait_for(lambda: os.path.isfile(os.path.join(incoming, 'Datos.sueltos.bin')), 40), os.listdir(incoming))
        moved = os.path.join(videos, 'Charla.con.semilla.mkv')
        check('the torrent, once finished, goes whole to the folder of the videos',
              wait_for(lambda: os.path.isfile(moved) and os.path.getsize(moved) == len(ws.FILES['Charla.con.semilla.mkv']), 120), (os.listdir(incoming), r.log_of(w.RUN)[-600:]))
        check('with its content intact', os.path.isfile(moved) and open(moved, 'rb').read() == ws.FILES['Charla.con.semilla.mkv'])
        check('and it is not left in the downloads folder', not os.path.exists(os.path.join(incoming, 'Charla.con.semilla.mkv')), os.listdir(incoming))
        check('the web video is shared from its new folder', wait_for(lambda: 'Clase.grabada.mkv' in shared_names(), 40), shared_names())
        check('and so is the file of the torrent', wait_for(lambda: 'Charla.con.semilla.mkv' in shared_names(), 60), shared_names())
        torrent = [d for d in downloads() if d.get('name') == 'Charla.con.semilla.mkv']
        check('the torrent is still in the list', len(torrent) == 1, downloads())
        if torrent:
            status, played = apitest.call('POST', '/api/v1/downloads/%s/play' % torrent[0]['hash'])
            check('played, the finished torrent opens at once', status == 200 and played.get('opened') is True, (status, played))
            check('from the folder it was moved to', wait_for(lambda: 'Play: would open ' + moved in r.log_of(w.RUN), 10), r.log_of(w.RUN)[-400:])
    finally:
        check('closes', stop(p))
    p = r.start(w.RUN)
    try:
        u.main_window(p.pid)
        check('opened again, the web video is still shared', wait_for(lambda: 'Clase.grabada.mkv' in shared_names(), 40), shared_names())
        check('and the torrent is still there, complete', wait_for(lambda: any(d.get('name') == 'Charla.con.semilla.mkv' and d.get('complete') for d in downloads()), 40), downloads())
    finally:
        check('closes again', stop(p))
    web.shutdown()


def run_play():
    print('---- playing a torrent which has nothing yet')
    ini = instance('')
    config = os.path.join(w.RUN, 'config')
    path, _ = torrent_of('Documental.sin.fuentes.mkv', os.urandom(4 * 1024 * 1024))
    p = r.start(w.RUN)
    try:
        u.main_window(p.pid)
        status, added = apitest.call('POST', '/api/v1/downloads', {'links': [path]})
        check('the torrent is taken', status == 200 and added.get('added') == 1, added)
        check('it is listed', wait_for(lambda: len(downloads()) == 1, 20), downloads())
        listed = downloads()
        if listed:
            status, played = apitest.call('POST', '/api/v1/downloads/%s/play' % listed[0]['hash'])
            print('    ', played)
            check('played, it says that it will open when the beginning is there', status == 200 and played.get('opened') is False and played.get('message'), (status, played))
        time.sleep(4)
        check('nothing was opened', 'Play: would open' not in r.log_of(w.RUN))
    finally:
        check('closes', stop(p))
    resume = b''.join(open(os.path.join(config, 'torrents', n), 'rb').read() for n in os.listdir(os.path.join(config, 'torrents')) if n.endswith('.fastresume'))
    check('and the torrent is fetched in order from then on', b'19:sequential_downloadi1e' in resume, resume[:200])


def set_text(hwnd, text):
    buf = ctypes.create_unicode_buffer(text)
    u.user32.SendMessageW(hwnd, WM_SETTEXT, 0, ctypes.addressof(buf))


def get_text(hwnd):
    buf = ctypes.create_unicode_buffer(4096)
    u.user32.SendMessageW(hwnd, WM_GETTEXT, 4096, ctypes.addressof(buf))
    return buf.value


def run_import():
    print('---- import from eMule and qBittorrent')
    # 1. another program's state: made with Pegaso itself, which keeps both in the formats of eMule and of the torrent engine
    instance('')
    config = os.path.join(w.RUN, 'config')
    first, first_hash = torrent_of('Curso.importado.mkv', os.urandom(2 * 1024 * 1024))
    second, second_hash = torrent_of('Manual.importado.pdf', os.urandom(1024 * 1024))
    link = 'ed2k://|file|Descarga.de.eMule.iso|3145728|%s|/' % hashlib.md5(b'descarga de prueba').hexdigest().upper()
    p = r.start(w.RUN)
    try:
        u.main_window(p.pid)
        status, added = apitest.call('POST', '/api/v1/downloads', {'links': [link, first]})
        check('a download of each kind is started', status == 200 and added.get('added') == 2, added)
        check('both are listed', wait_for(lambda: len(downloads()) == 2, 20), downloads())
        time.sleep(4)
    finally:
        check('closes', stop(p))
    temp = os.path.join(w.RUN, 'Temp')
    parts = [n for n in os.listdir(temp) if n.endswith('.part.met')]
    check('the first run left an unfinished eD2K download behind', len(parts) == 1, os.listdir(temp))
    keep = os.path.join(os.path.dirname(w.RUN), 'pegaso-test-import')
    wipe(keep)
    os.makedirs(os.path.join(keep, 'emule'), exist_ok=True)
    os.makedirs(os.path.join(keep, 'qb'), exist_ok=True)
    for n in os.listdir(temp):
        if os.path.isfile(os.path.join(temp, n)):
            shutil.copy2(os.path.join(temp, n), os.path.join(keep, 'emule', n))
    shutil.copy2(os.path.join(config, 'torrents', first_hash + '.fastresume'), os.path.join(keep, 'qb', first_hash + '.fastresume'))
    shutil.copy2(first, os.path.join(keep, 'qb', first_hash + '.torrent'))
    shutil.copy2(second, os.path.join(keep, 'qb', second_hash + '.torrent'))    # this one without what the other program knew

    # 2. a Pegaso which knows nothing of them
    ini = instance('')
    emule = os.path.join(w.RUN, 'de-emule')
    qb = os.path.join(w.RUN, 'de-qb')
    shutil.copytree(os.path.join(keep, 'emule'), emule, dirs_exist_ok=True)
    shutil.copytree(os.path.join(keep, 'qb'), qb, dirs_exist_ok=True)
    wipe(keep)
    os.rmdir(keep)
    p = r.start(w.RUN)
    try:
        main_hwnd = u.main_window(p.pid)
        check('it starts with an empty list', downloads() == [], downloads())
        u.user32.PostMessageW(main_hwnd, u.WM_COMMAND, MP_PEGASO_IMPORT, 0)
        time.sleep(3)
        dlg = vpntest.prefs_window(p.pid, main_hwnd)
        check('the import window opens', dlg is not None)
        if dlg:
            print('    title:', get_text(dlg), '| guesses:', bool(get_text(u.find_child(dlg, IDC_IM_EMULE))), bool(get_text(u.find_child(dlg, IDC_IM_QB))))
            set_text(u.find_child(dlg, IDC_IM_EMULE), emule)
            u.user32.SendMessageW(u.find_child(dlg, IDC_IM_EMULE_GO), BM_CLICK, 0, 0)
            time.sleep(2)
            result = get_text(u.find_child(dlg, IDC_IM_RESULT))
            print('    ' + result)
            check('eMule: one download added', result.startswith('eMule: 1 '), result)
            set_text(u.find_child(dlg, IDC_IM_QB), qb)
            u.user32.SendMessageW(u.find_child(dlg, IDC_IM_QB_GO), BM_CLICK, 0, 0)
            time.sleep(2)
            result = get_text(u.find_child(dlg, IDC_IM_RESULT))
            print('    ' + result)
            check('qBittorrent: two torrents added', result.startswith('qBittorrent: 2 '), result)
            u.user32.SendMessageW(u.find_child(dlg, IDC_IM_QB_GO), BM_CLICK, 0, 0)
            time.sleep(2)
            result = get_text(u.find_child(dlg, IDC_IM_RESULT))
            check('asked again, they are known to be there already', result.startswith('qBittorrent: 0 ') and ' 2 ' in result, result)
            print('    screenshot:', u.screenshot(dlg, 'shot_import.png'))
            u.user32.PostMessageW(dlg, u.WM_COMMAND, 2, 0)
            time.sleep(1)
        names = sorted(d.get('name') for d in downloads())
        check('the three downloads are in the list', wait_for(lambda: sorted(d.get('name') for d in downloads()) == ['Curso.importado.mkv', 'Descarga.de.eMule.iso', 'Manual.importado.pdf'], 20), names)
    finally:
        check('closes', stop(p))
    check('the folder of eMule is kept as one more temporary folder', 'de-emule' in (ini_value(ini, 'TempDirs') or ''), ini_value(ini, 'TempDirs'))
    p = r.start(w.RUN)
    try:
        u.main_window(p.pid)
        names = sorted(d.get('name') for d in downloads())
        check('opened again, the three downloads are still there', wait_for(lambda: len(downloads()) == 3, 20), names)
    finally:
        check('closes again', stop(p))


def run_dns(found):
    print('---- names looked up on the DNS servers of the adapter Pegaso is tied to')
    if not found:
        print('  skipped: no connected adapter')
        return
    tied, ip = found[0]
    ws.FILES.clear()
    ws.FILES.update({'Por.nombre.bin': os.urandom(250000)})
    web = ThreadingHTTPServer((ip, WEB_PORT + 1), ws.Handler)
    threading.Thread(target=web.serve_forever, daemon=True).start()
    instance('PegasoBindAdapter=%s\n' % quote(tied), torrents=False)
    p = r.start(w.RUN)
    try:
        u.main_window(p.pid)
        status, conn = apitest.call('GET', '/api/v1/connection')
        report = conn.get('report', '') if status == 200 else ''
        lines = [line for line in report.splitlines() if 'DNS' in line]
        print('    ' + '\n    '.join(lines))
        if not lines or ip not in lines[0]:
            print('  skipped: this adapter has no DNS servers of its own, so Windows looks the names up')
            return
        check('"Check my connection" says the names are asked through that adapter', True)
        # the name of this computer: answered without a question leaving it
        status, added = apitest.call('POST', '/api/v1/downloads', {'links': ['http://%s:%d/Por.nombre.bin' % (socket.gethostname(), WEB_PORT + 1)]})
        check('a download by name is taken', status == 200 and added.get('added') == 1, added)
        check('and arrives', wait_for(lambda: os.path.isfile(os.path.join(w.RUN, 'Incoming', 'Por.nombre.bin')), 90), r.log_of(w.RUN)[-300:].replace(socket.gethostname(), '<name>'))
    finally:
        check('closes', stop(p))
        web.shutdown()


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    found = sorted(adapters(), key=lambda a: 'vpn' in a[0].lower())
    which = sys.argv[1:] or ['folders', 'play', 'import', 'dns']
    if 'folders' in which:
        run_folders(found)
    if 'play' in which:
        run_play()
    if 'import' in which:
        run_import()
    if 'dns' in which:
        run_dns(found)
    print('RESULT', 'PASS' if ok else 'FAIL')


if __name__ == '__main__':
    main()
