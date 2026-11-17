"""A torrent as one more source of an eD2K download, picked instead of typed.

usage: python -I hybridpicktest.py [file] [listed]

"Add a torrent of this same file..." offers what Pegaso has found by itself:
  file    a .torrent file lying in the download folder which has a file of the same size
          (and not another one which has not);
  listed  a torrent which is in the download list already, finished.
In both cases the eD2K download, which has no source at all, has to complete from the torrent
and pass the eD2K hash check. The torrent gets its data from a web seed of this test; Pegaso
is tied to a connected adapter and the web server is called at the address of that adapter.
"""
import ctypes
import os
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
from proxytest import wipe  # noqa: E402
from privtest import adapters, wait_for, stop  # noqa: E402
from autotest import torrent_of, downloads  # noqa: E402

WEB_PORT = 8778
MP_PEGASO_ADDTORRENT = 10984
IDC_AT_LINK, IDC_AT_LIST = 5192, 5195
LB_GETCOUNT, LB_GETTEXT, LB_GETCURSEL, LB_SETCURSEL = 0x018B, 0x0189, 0x0188, 0x0186
SEEDED, WANTED = 'Curso.por.torrent.mkv', 'Curso.por.eMule.mkv'
ok = True


def check(what, cond, detail=''):
    global ok
    print('  %-78s %s %s' % (what, 'ok' if cond else 'FAIL', '' if cond else detail))
    ok &= bool(cond)


def instance(tied):
    btuitest.deploy()
    for sub in ('config', 'Incoming', 'Temp', 'logs'):
        wipe(os.path.join(w.RUN, sub))
    r.make_instance(w.RUN, 4700, 'ApiEnabled=1\nApiPort=%d\nApiToken=%s\nPegasoBindAdapter=%s\n' % (apitest.PORT, apitest.TOKEN, quote(tied)))
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('TorrentEnabled=0', 'TorrentEnabled=1\nTorrentPort=4720')
    open(ini, 'w', encoding='ascii', newline='\r\n').write(text)


def offered(pegaso, hwnd, mem):
    """Opens the window on the eD2K download; gives back the window and what its list offers."""
    u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
    time.sleep(1.5)
    lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
    rows = btuitest.rows(lv, mem)
    index = next((i for i, row in enumerate(rows) if row and row[0] == WANTED), None)
    if index is None:
        return None, [r_[:1] for r_ in rows]
    u.select_item(lv, mem, index)
    u.user32.PostMessageW(lv, u.WM_COMMAND, MP_PEGASO_ADDTORRENT, 0)
    time.sleep(3)
    dlg = vpntest.prefs_window(pegaso.pid, hwnd)
    if not dlg:
        return None, []
    box = u.find_child(dlg, IDC_AT_LIST)
    texts = []
    for i in range(u.user32.SendMessageW(box, LB_GETCOUNT, 0, 0)):
        buf = ctypes.create_unicode_buffer(600)
        u.user32.SendMessageW(box, LB_GETTEXT, i, ctypes.addressof(buf))
        texts.append(buf.value)
    return dlg, texts


def completes(data):
    path = os.path.join(w.RUN, 'Incoming', WANTED)
    return wait_for(lambda: os.path.isfile(path) and os.path.getsize(path) == len(data) and open(path, 'rb').read() == data, 150)


def run(kind, tied, ip):
    print('---- the torrent is', 'a .torrent file in the download folder' if kind == 'file' else 'in the download list')
    data = os.urandom(3 * 1024 * 1024 + 12345)
    ws.FILES.clear()
    ws.FILES[SEEDED] = data
    instance(tied)
    incoming = os.path.join(w.RUN, 'Incoming')
    url = 'http://%s:%d/%s' % (ip, WEB_PORT, SEEDED)
    where = os.path.join(incoming, 'el.del.curso.torrent') if kind == 'file' else os.path.join(w.RUN, 'el.del.curso.torrent')
    torrent_of(SEEDED, data, url, where)
    if kind == 'file':   # another one, of another size: not to be offered
        torrent_of('Otra.cosa.mkv', os.urandom(500000), None, os.path.join(incoming, 'otra.cosa.torrent'))
    link = 'ed2k://|file|%s|%d|%s|/' % (WANTED, len(data), w.ed2k_hash(data).upper())
    p = r.start(w.RUN)
    mem = None
    try:
        hwnd = u.main_window(p.pid)
        mem = u.Remote(p.pid)
        status, added = apitest.call('POST', '/api/v1/downloads', {'links': [link] + ([where] if kind == 'listed' else [])})
        check('the downloads are taken', status == 200 and added.get('added') == (2 if kind == 'listed' else 1), added)
        if kind == 'listed':
            check('the torrent of the list finishes on its own', wait_for(lambda: any(d.get('name') == SEEDED and d.get('complete') for d in downloads()), 90), downloads())
            time.sleep(6)
        check('the eD2K download has nothing: nobody has it', not os.path.exists(os.path.join(incoming, WANTED)))
        dlg, texts = offered(p, hwnd, mem)
        print('    offered:', texts)
        check('the window opens on the eD2K download', dlg is not None, texts)
        if dlg:
            expected = 'el.del.curso.torrent' if kind == 'file' else SEEDED
            check('it offers the torrent which has this same file, and only that one', len(texts) == 1 and texts[0].startswith(expected), texts)
            box = u.find_child(dlg, IDC_AT_LIST)
            check('the only one offered is chosen already', u.user32.SendMessageW(box, LB_GETCURSEL, 0, 0) == 0)
            print('    screenshot:', u.screenshot(dlg, 'shot_addtorrent_%s.png' % kind))
            u.user32.PostMessageW(dlg, u.WM_COMMAND, 1, 0)  # OK
            time.sleep(2)
            check('accepted, the window closes', vpntest.prefs_window(p.pid, hwnd) is None)
            check('the eD2K download completes from the torrent and passes its own hash check', completes(data), (os.listdir(incoming), r.log_of(w.RUN)[-700:]))
        if kind == 'listed':
            check('the torrent of the list is still in the list, complete', any(d.get('name') == SEEDED and d.get('complete') for d in downloads()), downloads())
            check('and its own file is still where it was', os.path.isfile(os.path.join(incoming, SEEDED)), os.listdir(incoming))
        else:
            check('the torrent used as a source has no row of its own', not any(d.get('kind') == 'torrent' for d in downloads()), downloads())
        check('still running', p.poll() is None)
    finally:
        if mem:
            mem.close()
        check('closes', stop(p))


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    found = sorted(adapters(), key=lambda a: 'vpn' in a[0].lower())
    if not found:
        print('skipped: no connected adapter')
        return
    tied, ip = found[0]
    web = ThreadingHTTPServer((ip, WEB_PORT), ws.Handler)
    threading.Thread(target=web.serve_forever, daemon=True).start()
    for kind in (sys.argv[1:] or ['file', 'listed']):
        run(kind, tied, ip)
    print('RESULT', 'PASS' if ok else 'FAIL')


if __name__ == '__main__':
    main()
