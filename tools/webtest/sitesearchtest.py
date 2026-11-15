"""Search in the sites of the user: a web page as a place to search in.

usage: python -I sitesearchtest.py

Two pretend sites run on 127.0.0.1:8767 (no real site is contacted): "Mi web" answers a
search with an ordinary web page, "Otra web" with another one. The list of sites of the
user holds both, each with the address of its search page and %s where the words go. The
search window is driven through the real interface (Spanish): choose a site, search, look at
what was found, download.
"""
import ctypes
import os
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

MP_PEGASO_WEBSEARCH = 10981
IDC_WS_QUERY, IDC_WS_SEARCH, IDC_WS_LIST, IDC_WS_STATUS, IDC_WS_DOWNLOAD, IDC_WS_SITE = 5030, 5031, 5033, 5034, 5035, 5036
WM_SETTEXT, WM_GETTEXT, CB_GETCOUNT, CB_SETCURSEL, CB_GETCURSEL = 0x000C, 0x000D, 0x0146, 0x014E, 0x0147
HASH = '0123456789abcdef0123456789abcdef'
seen = []

PAGE = '''<!doctype html><html><head><meta charset="utf-8"><title>Resultados</title></head><body>
<nav><a href="/">Inicio</a> <a href="/ayuda.html">Ayuda</a> <a href='/buscar?q=otra'>Otra b&uacute;squeda</a></nav>
<img src="/logo.png"><a href="/logo.png">logo</a>
<table>
<tr><td><a class="x" href="magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&amp;dn=Documental+de+prueba+%281080p%29&amp;tr=udp%3A%2F%2Ft.invalid">Magnet</a></td></tr>
<tr><td><a href="ed2k://|file|Curso%20de%20fotograf%C3%ADa.mkv|1400000000|''' + HASH + '''|/">eD2K</a></td></tr>
<tr><td><a href = '/descargas/Imagen.prueba.torrent?id=7'><b>Imagen</b> de   prueba</a></td></tr>
<tr><td><a href=archivos/Manual.de.usuario.zip>Manual</a></td></tr>
<tr><td><a href="//127.0.0.1:8767/otro/Distribucion.Linux.iso">ISO</a></td></tr>
<tr><td><a href="magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567&amp;dn=Documental+de+prueba+%281080p%29&amp;tr=udp%3A%2F%2Ft.invalid">Repetido</a></td></tr>
<tr><td><a href="javascript:void(0)">Nada</a> <a href="mailto:x@example.invalid">Correo</a> <abbr title="x">a</abbr></td></tr>
</table></body></html>'''

FILE = os.urandom(120000)
OTHER = '<html><body><a href="magnet:?xt=urn:btih:89abcdef0123456789abcdef0123456789abcdef&dn=De+la+otra+web">x</a></body></html>'


# a site without a search: a list of pages, the downloads inside each page
LIST = ('<html><body><a href=index.html>Inicio</a>'
        '<a href=ficha.html?id=1>Paseo por el campo</a><a href=ficha.html?id=2>Paseo por la ciudad</a>'
        '<a href=sin.html>Enlace protegido</a><a href=ficha.html?id=3>Otra cosa del campo</a><a href="http://127.0.0.1:8768/ficha.html?id=9">Paseo por el campo ajeno</a>'
        + ''.join('<a href=ficha.html?id=%d>Documento %d</a>' % (n, n) for n in range(20, 32)) + '</body></html>')
CARD = ('<html><body><a href=lista.html>Volver</a><a \nhref=http://127.0.0.1:8767/bajar.php?tipo=torrent&f=Cosa_%s.avi.torrent>Formato grande 700 MB</a>'
        '<a href=/bajar.php?tipo=torrent&f=Cosa_%s_mini.avi.torrent>Formato peque\u00f1o 90 MB</a></body></html>')


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def do_GET(self):
        url = urlparse(self.path)
        q = parse_qs(url.query)
        seen.append((url.path, q, self.headers.get('User-Agent', '')))
        if url.path == '/sitio/buscar':
            body, status = PAGE.encode('utf-8'), 200
        elif url.path == '/otra/s':
            body, status = OTHER.encode('utf-8'), 200
        elif url.path == '/portada/':
            body, status = PAGE.encode('utf-8'), 200
        elif url.path == '/fichas/sin.html':
            body, status = b'<html><body><a href="http://127.0.0.1:8768/ir?a=1">Descargar</a></body></html>', 200
        elif url.path == '/fichas/lista.html':
            body, status = LIST.encode('utf-8'), 200
        elif url.path == '/fichas/ficha.html':
            body, status = (CARD % (q['id'][0], q['id'][0])).encode('utf-8'), 200
        elif url.path == '/sitio/archivos/Manual.de.usuario.zip':
            body, status = FILE, 200
        else:
            body, status = b'no', 404
        self.send_response(status)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def text_of(hwnd):
    buf = ctypes.create_unicode_buffer(1024)
    u.user32.SendMessageW(hwnd, WM_GETTEXT, 1024, ctypes.addressof(buf))
    return buf.value


def search(dlg, mem, words):
    query = ctypes.create_unicode_buffer(words)
    u.user32.SendMessageW(u.find_child(dlg, IDC_WS_QUERY), WM_SETTEXT, 0, ctypes.addressof(query))
    u.user32.PostMessageW(dlg, u.WM_COMMAND, IDC_WS_SEARCH, 0)
    time.sleep(3)
    return btuitest.rows(u.find_child(dlg, IDC_WS_LIST), mem)


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ok = True

    def check(what, cond, detail=''):
        nonlocal ok
        print('  %-72s %s %s' % (what, 'ok' if cond else 'FAIL', '' if cond else detail))
        ok &= bool(cond)

    btuitest.deploy()
    w.reset_profile()
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('Language=1033', 'Language=1034')
    text += ('TorznabCount=4\nTorznab3Name=Fichas\nTorznab3Url=http://127.0.0.1:8767/fichas/lista.html\nTorznab3Key=\n'
             'Torznab2Name=Sin%20marca\nTorznab2Url=http://127.0.0.1:8767/portada/\nTorznab2Key=\n'
             'Torznab0Name=Mi%20web\nTorznab0Url=http://127.0.0.1:8767/sitio/buscar?q=%s&orden=fecha\nTorznab0Key=\n'
             'Torznab1Name=Otra%20web\nTorznab1Url=http://127.0.0.1:8767/otra/s?palabras=%s\nTorznab1Key=\n')
    open(ini, 'w', encoding='ascii', newline='\r\n').write(text)

    fake = ThreadingHTTPServer(('127.0.0.1', 8767), Handler)
    threading.Thread(target=fake.serve_forever, daemon=True).start()
    pegaso = w.start_pegaso()
    hwnd = u.main_window(pegaso.pid)
    u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
    time.sleep(1)
    lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
    mem = u.Remote(pegaso.pid)
    try:
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, MP_PEGASO_WEBSEARCH, 0)
        time.sleep(2)
        dlg, title = btfilestest.dialog(pegaso.pid, hwnd)
        check('the search window opens', dlg is not None, title)
        combo = u.find_child(dlg, IDC_WS_SITE)
        check('it offers all the sites and each one of them', u.user32.SendMessageW(combo, CB_GETCOUNT, 0, 0) == 5)

        # ---- all the sites
        rows = search(dlg, mem, 'ñandú & cía')
        for row in rows:
            print('    ', row)
        asked = [(p, q) for p, q, _ in seen]
        check('both sites were asked, the words in the place of %s', ('/sitio/buscar', {'q': ['ñandú & cía'], 'orden': ['fecha']}) in asked
              and ('/otra/s', {'palabras': ['ñandú & cía']}) in asked, asked)
        names = sorted(row[0] for row in rows)
        check('the download links of both pages are listed, each once', names == sorted([
            'Documental de prueba (1080p)', 'Curso de fotografía.mkv', 'Imagen.prueba.torrent', 'Manual.de.usuario.zip',
            'Distribucion.Linux.iso', 'De la otra web']), names)
        check('menus, pictures and other pages are left out', not any('logo' in n or 'ayuda' in n.lower() or 'Inicio' in n for n in names))
        ed2k = [row for row in rows if row[0] == 'Curso de fotografía.mkv']
        check('an ed2k link tells its size', bool(ed2k) and '1.30 GB' in ed2k[0], ed2k)
        check('each result names its site', all(('Mi web' in row) != ('Otra web' in row) for row in rows), rows)
        status = text_of(u.find_child(dlg, IDC_WS_STATUS))
        print('    status:', status)
        check('a page without %s where nothing holds the words: it tells what to do', 'Sin marca' in status and 'buscador' in status and '%s' in status, status)
        print('    screenshot:', u.screenshot(dlg, 'shot_sitesearch.png'))

        # ---- one site alone
        del seen[:]
        u.user32.SendMessageW(combo, CB_SETCURSEL, 2, 0)
        rows = search(dlg, mem, 'documental')
        check('with "Otra web" chosen only that site is asked', [p for p, _, _ in seen] == ['/otra/s'], seen)
        check('... and only its results are shown', [row[0] for row in rows] == ['De la otra web'], rows)

        # ---- a site without a search: its list is searched, the pages which match are opened
        del seen[:]
        u.user32.SendMessageW(combo, CB_SETCURSEL, 4, 0)
        rows = search(dlg, mem, 'campo PASEO')
        for row in rows:
            print('    ', row)
        cards = sorted(q['id'][0] for p, q, _ in seen if p == '/fichas/ficha.html')
        check('of the list, only the page whose name holds all the words is opened', cards == ['1'], seen)
        check('its downloads are listed under the name of the page', sorted(row[0] for row in rows) == [
            'Paseo por el campo - Formato grande 700 MB', 'Paseo por el campo - Formato pequeño 90 MB'], rows)
        del seen[:]
        rows = search(dlg, mem, 'documento')
        status = text_of(u.find_child(dlg, IDC_WS_STATUS))
        print('    status:', status)
        cards = [q['id'][0] for p, q, _ in seen if p == '/fichas/ficha.html']
        check('no more than 8 pages are opened for one search', len(cards) == 8, cards)
        pages = [row for row in rows if 'Página web' in row]
        check('the pages left are listed as pages to open, after the downloads',
              [row[0] for row in pages] == ['Documento %d' % n for n in range(28, 32)] and rows[-4:] == pages, rows)

        # ---- a page whose download is not in it: the page itself is offered
        del seen[:]
        rows = search(dlg, mem, 'enlace protegido')
        print('    ', rows)
        check('a page without a download in it is listed as a page', len(rows) == 1 and rows[0][0] == 'Enlace protegido' and 'Página web' in rows[0], rows)
        u.select_item(u.find_child(dlg, IDC_WS_LIST), mem, 0)
        time.sleep(1)
        check('the button then says it opens the page', text_of(u.find_child(dlg, IDC_WS_DOWNLOAD)) == 'Abrir la web')
        check('... and it tells how many were left', '4 páginas más' in status, status)

        # ---- download: an ed2k link and a file
        u.user32.SendMessageW(combo, CB_SETCURSEL, 1, 0)
        rows = search(dlg, mem, 'curso')
        results = u.find_child(dlg, IDC_WS_LIST)
        for wanted in ('Curso de fotografía.mkv', 'Manual.de.usuario.zip'):
            index = [row[0] for row in rows].index(wanted)
            u.select_item(results, mem, index)
            u.user32.PostMessageW(dlg, u.WM_COMMAND, IDC_WS_DOWNLOAD, 0)
            time.sleep(3)
        u.user32.PostMessageW(dlg, u.WM_COMMAND, 2, 0)
        time.sleep(2)
        downloads = [row[0] for row in btuitest.rows(lv, mem)]
        print('    downloads:', downloads)
        check('the ed2k link became a download', 'Curso de fotografía.mkv' in downloads)
        path = os.path.join(w.RUN, 'Incoming', 'Manual.de.usuario.zip')
        t0 = time.time()
        while time.time() - t0 < 40 and not os.path.isfile(path):
            time.sleep(2)
        check('the file was downloaded from the site, identical', os.path.isfile(path) and open(path, 'rb').read() == FILE)
        check('Pegaso says who it is when it asks for the pages', all(ua.startswith('Pegaso/') for _, _, ua in seen))
        print('    user agent:', sorted({ua for _, _, ua in seen}))

        # ---- the choice is remembered
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, MP_PEGASO_WEBSEARCH, 0)
        time.sleep(2)
        dlg, title = btfilestest.dialog(pegaso.pid, hwnd)
        check('the site chosen last is chosen again', dlg is not None and u.user32.SendMessageW(u.find_child(dlg, IDC_WS_SITE), CB_GETCURSEL, 0, 0) == 1)
        if dlg:
            u.user32.PostMessageW(dlg, u.WM_COMMAND, 2, 0)
        check('still running', pegaso.poll() is None)
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        mem.close()
        if not w.close_gracefully(pegaso, 40):
            pegaso.kill()
        fake.shutdown()


if __name__ == '__main__':
    main()
