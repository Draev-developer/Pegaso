"""Web search started from the search tab (method "Web (torrents)").

usage: python -I searchtabtest.py

Same fake Torznab provider as searchtest.py; no real site is contacted.
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
import btuitest  # noqa: E402
import btfilestest  # noqa: E402
import searchtest as st  # noqa: E402

MP_HM_SEARCH = 10212
IDC_METH, IDC_SEARCHNAME, IDC_STARTS = 2175, 2183, 2189  # IDC_COMBO1 is the method combo
CB_SETCURSEL, CB_GETCOUNT, CBN_SELCHANGE, WM_SETTEXT = 0x014E, 0x0146, 1, 0x000C


def find_visible(parent, ctrl_id):
    """Like uitest.find_child, but several windows may carry the identifier: the visible one."""
    found = []

    def cb(hwnd, _):
        if u.user32.GetDlgCtrlID(hwnd) == ctrl_id:
            cls = ctypes.create_unicode_buffer(64)
            u.user32.GetClassNameW(hwnd, cls, 64)
            found.append((bool(u.user32.IsWindowVisible(hwnd)), hwnd, cls.value))
        return True

    u.user32.EnumChildWindows(parent, u.EnumProc(cb), 0)
    print('   id %d: %s' % (ctrl_id, [(v, c) for v, _, c in found]))
    for visible, hwnd, _ in found:
        if visible:
            return hwnd
    return found[0][1] if found else None


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    btuitest.deploy()
    w.reset_profile()
    btfilestest.make_torrent(st.TORRENT)
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('Language=1033', 'Language=1034')
    text += 'TorznabCount=1\nTorznab0Name=Mi%20Jackett\nTorznab0Url=http://127.0.0.1:8766/api\nTorznab0Key=' + quote(st.KEY, safe='') + '\n'
    open(ini, 'w', encoding='ascii', newline='\r\n').write(text)

    fake = ThreadingHTTPServer(('127.0.0.1', 8766), st.Handler)
    threading.Thread(target=fake.serve_forever, daemon=True).start()
    pegaso = w.start_pegaso()
    hwnd = u.main_window(pegaso.pid)
    mem = u.Remote(pegaso.pid)
    ok = True
    try:
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, MP_HM_SEARCH, 0)
        time.sleep(2)
        combo = find_visible(hwnd, IDC_METH)
        name = find_visible(hwnd, IDC_SEARCHNAME)
        start = find_visible(hwnd, IDC_STARTS)
        count = u.user32.SendMessageW(combo, CB_GETCOUNT, 0, 0)
        print('search methods: %d' % count)
        ok &= count == 6
        u.user32.SendMessageW(combo, CB_SETCURSEL, 5, 0)
        parent = u.user32.GetParent(combo)
        u.user32.SendMessageW(parent, u.WM_COMMAND, (CBN_SELCHANGE << 16) | IDC_METH, combo)
        query = ctypes.create_unicode_buffer('documental 1080p')
        u.user32.SendMessageW(name, WM_SETTEXT, 0, ctypes.addressof(query))
        time.sleep(1)
        print('screenshot:', u.screenshot(hwnd, 'shot_searchtab.png'))
        u.user32.PostMessageW(u.user32.GetParent(start), u.WM_COMMAND, IDC_STARTS, start)
        time.sleep(5)
        dlg = None
        for h, title in u.top_windows(pegaso.pid):
            if title == 'Búsqueda web':
                dlg = h
        print('web search window opened:', dlg is not None)
        ok &= dlg is not None
        if dlg:
            rows = btuitest.rows(u.find_child(dlg, st.IDC_WS_LIST), mem)
            for row in rows:
                print('  ', row)
            api = [q for p, q in st.seen if p == '/api']
            print('provider received:', api[0].get('q') if api else None)
            ok &= len(rows) == 3 and bool(api) and api[0].get('q') == ['documental 1080p']
            u.user32.PostMessageW(dlg, u.WM_COMMAND, 2, 0)
            time.sleep(1)
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        mem.close()
        if not w.close_gracefully(pegaso, 60):
            pegaso.kill()
        fake.shutdown()


if __name__ == '__main__':
    main()
