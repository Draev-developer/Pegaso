"""File chooser of a torrent and the options page, through the real interface (Spanish).

usage: python -I btfilestest.py

A small multi-file torrent is generated on the fly (no peers needed): the test only
looks at the file list, the default selection and what changes when a file is unticked.
"""
import ctypes
import hashlib
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import btuitest  # noqa: E402

LVM_GETITEMSTATE = 0x102C
LVIS_STATEIMAGEMASK = 0xF000
MP_TORRENT_FILES = 10980
MP_HM_PREFS = 10217
TVM_GETNEXTITEM, TVM_SELECTITEM = 0x110A, 0x110B
TVGN_ROOT, TVGN_NEXT, TVGN_CARET = 0, 1, 9
IDC_PG_FILES_LIST = 5018


def bencode(x):
    if isinstance(x, int):
        return b'i%de' % x
    if isinstance(x, str):
        x = x.encode('utf-8')
    if isinstance(x, bytes):
        return b'%d:%s' % (len(x), x)
    if isinstance(x, list):
        return b'l' + b''.join(bencode(i) for i in x) + b'e'
    if isinstance(x, dict):
        return b'd' + b''.join(bencode(k) + bencode(v) for k, v in sorted((k.encode() if isinstance(k, str) else k, v) for k, v in x.items())) + b'e'
    raise TypeError(type(x))


def make_torrent(path):
    files = [('Pelicula.mkv', os.urandom(300000)), ('Leeme.txt', b'hola\n' * 200),
             ('Visita nuestra web.URL', b'[InternetShortcut]\nURL=http://example.invalid/\n'),
             ('extras/Subtitulos.srt', b'1\n00:00:01,000 --> 00:00:02,000\nhola\n' * 50)]
    piece = 16384
    blob = b''.join(d for _, d in files)
    pieces = b''.join(hashlib.sha1(blob[i:i + piece]).digest() for i in range(0, len(blob), piece))
    info = {'name': 'Prueba Pegaso', 'piece length': piece, 'pieces': pieces,
            'files': [{'length': len(d), 'path': n.split('/')} for n, d in files]}
    open(path, 'wb').write(bencode({'info': info, 'announce': 'http://127.0.0.1:9/announce'}))
    return files


def list_rows(lv, mem):
    n = u.user32.SendMessageW(lv, u.LVM_GETITEMCOUNT, 0, 0)
    out = []
    for i in range(n):
        state = u.user32.SendMessageW(lv, LVM_GETITEMSTATE, i, LVIS_STATEIMAGEMASK)
        out.append((u.row_text(lv, mem, i), (state >> 12) == 2))
    return out


def set_check(lv, mem, index, checked):
    item = u.LVITEM(stateMask=LVIS_STATEIMAGEMASK, state=(2 if checked else 1) << 12)
    mem.write(item)
    u.user32.SendMessageW(lv, u.LVM_SETITEMSTATE, index, mem.addr)


def dialog(pid, main):
    for hwnd, title in u.top_windows(pid):
        if hwnd != main:
            return hwnd, title
    return None, None


def main():
    btuitest.deploy()
    w.reset_profile()
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('Language=1033', 'Language=1034')
    open(ini, 'w', newline='\r\n').write(text)
    torrent = os.path.join(HERE, 'prueba.torrent')
    files = make_torrent(torrent)
    total = sum(len(d) for _, d in files)
    url_size = len(files[2][1])

    pegaso = w.start_pegaso()
    hwnd = u.main_window(pegaso.pid)
    u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
    time.sleep(1)
    lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
    mem = u.Remote(pegaso.pid)
    ok = True
    try:
        w.add_url(torrent)
        time.sleep(6)
        row = btuitest.rows(lv, mem)
        print('row:', row)
        ok &= len(row) == 1 and row[0][0] == 'Prueba Pegaso'

        # the file chooser, as opened from the context menu
        u.select_item(lv, mem, 0)
        u.user32.PostMessageW(lv, u.WM_COMMAND, MP_TORRENT_FILES, 0)
        time.sleep(2)
        dlg, title = dialog(pegaso.pid, hwnd)
        print('dialog:', repr(title))
        ok &= dlg is not None
        flist = u.find_child(dlg, IDC_PG_FILES_LIST)
        rows = list_rows(flist, mem)
        for text, checked in rows:
            print('  [%s] %s' % ('x' if checked else ' ', text))
        print('screenshot:', u.screenshot(dlg, 'shot_files.png'))
        names = [t[0] for t, _ in rows]
        ok &= len(rows) == 4
        ok &= all(checked != name.lower().endswith('.url') for (name, *_), checked in [(t, c) for t, c in rows])

        # untick the subtitles too and accept
        sub = [i for i, n in enumerate(names) if n.lower().endswith('.srt')][0]
        set_check(flist, mem, sub, False)
        u.user32.PostMessageW(dlg, u.WM_COMMAND, 1, 0)  # IDOK
        time.sleep(4)
        row = btuitest.rows(lv, mem)
        print('row after unticking the subtitles:', row)

        u.user32.PostMessageW(lv, u.WM_COMMAND, MP_TORRENT_FILES, 0)
        time.sleep(2)
        dlg, _ = dialog(pegaso.pid, hwnd)
        rows2 = list_rows(u.find_child(dlg, IDC_PG_FILES_LIST), mem)
        print('reopened:', [(t[0], c) for t, c in rows2])
        ok &= [c for _, c in rows2] == [not (n.lower().endswith('.url') or n.lower().endswith('.srt')) for n in names]
        u.user32.PostMessageW(dlg, u.WM_COMMAND, 2, 0)  # IDCANCEL
        time.sleep(1)
        print('payload %d bytes, link file %d bytes' % (total, url_size))

        # options page
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, MP_HM_PREFS, 0)
        time.sleep(3)
        prefs, ptitle = dialog(pegaso.pid, hwnd)
        print('options window:', repr(ptitle))
        tree = []

        def cb(h, _):
            buf = ctypes.create_unicode_buffer(64)
            u.user32.GetClassNameW(h, buf, 64)
            if buf.value == 'SysTreeView32':
                tree.append(h)
            return True

        u.user32.EnumChildWindows(prefs, u.EnumProc(cb), 0)
        item = u.user32.SendMessageW(tree[0], TVM_GETNEXTITEM, TVGN_ROOT, 0)
        for _ in range(7):  # General, Display, Connection, Proxy, Server, Directories, Files -> Pegaso
            item = u.user32.SendMessageW(tree[0], TVM_GETNEXTITEM, TVGN_NEXT, item)
        u.user32.SendMessageW(tree[0], TVM_SELECTITEM, TVGN_CARET, item)
        time.sleep(1.5)
        print('screenshot:', u.screenshot(prefs, 'shot_options.png'))
        port = u.find_child(prefs, 5013)
        ok &= bool(port)
        u.user32.PostMessageW(prefs, u.WM_COMMAND, 2, 0)
        time.sleep(1)
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        mem.close()
        if not w.close_gracefully(pegaso, 60):
            pegaso.kill()


if __name__ == '__main__':
    main()
