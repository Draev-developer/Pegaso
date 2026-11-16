"""The windows 0.6.2 added, as they look: the options page and "Check my connection".

usage: python privuitest.py        (takes screenshots; needs Pillow, so without -I)
"""
import ctypes
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import ratetest as r  # noqa: E402
import btuitest  # noqa: E402
import apitest  # noqa: E402
import vpntest  # noqa: E402
from proxytest import wipe  # noqa: E402

MP_PEGASO_CONN = 10994
IDC_PG_BT_NOFOLDER, IDC_PG_CLEAR_EXIT, IDC_PG_CLEAR_NOW, IDC_PG_CONN, IDC_CC_TEXT = 5026, 5028, 5029, 5143, 5140
BM_GETCHECK, BM_CLICK = 0x00F0, 0x00F5


def text_of(hwnd):
    n = u.user32.GetWindowTextLengthW(hwnd)
    buf = ctypes.create_unicode_buffer(n + 2)
    u.user32.GetWindowTextW(hwnd, buf, n + 1)
    return buf.value


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ok = True

    def check(what, cond, detail=''):
        nonlocal ok
        print('  %-70s %s %s' % (what, 'ok' if cond else 'FAIL', '' if cond else detail))
        ok &= bool(cond)

    btuitest.deploy()
    for sub in ('config', 'Incoming', 'Temp', 'logs'):
        wipe(os.path.join(w.RUN, sub))
    r.make_instance(w.RUN, 4700, 'ApiEnabled=1\nApiPort=%d\nApiToken=%s\nTorrentNoFolder=1\n' % (apitest.PORT, apitest.TOKEN))
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('TorrentEnabled=0', 'TorrentEnabled=1\nTorrentPort=4720')
    open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
    p = r.start(w.RUN)
    try:
        main_hwnd = u.main_window(p.pid)
        # ---- "Check my connection"
        u.user32.PostMessageW(main_hwnd, u.WM_COMMAND, MP_PEGASO_CONN, 0)
        time.sleep(3)
        dlg = vpntest.prefs_window(p.pid, main_hwnd)
        check('"Check my connection" opens from the menu command', dlg is not None)
        if dlg:
            print('    title:', text_of(dlg))
            buf = ctypes.create_unicode_buffer(8192)  # the system marshals WM_GETTEXT for standard controls
            u.user32.SendMessageW(u.find_child(dlg, IDC_CC_TEXT), 0x000D, 8192, ctypes.addressof(buf))
            report = buf.value
            check('and shows the report', 'eD2K (TCP)' in report and 'BitTorrent DHT' in report, report)
            print('    screenshot:', u.screenshot(dlg, 'shot_conn.png'))
            u.user32.PostMessageW(dlg, u.WM_COMMAND, 2, 0)  # Close
            time.sleep(1)
        # ---- the options page
        u.user32.PostMessageW(main_hwnd, u.WM_COMMAND, vpntest.MP_HM_PREFS, 0)
        time.sleep(3)
        prefs = vpntest.prefs_window(p.pid, main_hwnd)
        trees = []

        def cb(h, _):
            buf = ctypes.create_unicode_buffer(64)
            u.user32.GetClassNameW(h, buf, 64)
            if buf.value == 'SysTreeView32':
                trees.append(h)
            return True

        u.user32.EnumChildWindows(prefs, u.EnumProc(cb), 0)
        item = u.user32.SendMessageW(trees[0], vpntest.TVM_GETNEXTITEM, 0, 0)
        for _ in range(7):
            item = u.user32.SendMessageW(trees[0], vpntest.TVM_GETNEXTITEM, 1, item)
        u.user32.SendMessageW(trees[0], vpntest.TVM_SELECTITEM, 9, item)
        time.sleep(1.5)
        for ctrl, what in ((IDC_PG_BT_NOFOLDER, 'torrent without folder'), (IDC_PG_CLEAR_EXIT, 'clear traces on exit'),
                           (IDC_PG_CLEAR_NOW, 'clear traces now'), (IDC_PG_CONN, 'check my connection')):
            h = u.find_child(prefs, ctrl)
            check('the page has "%s": %s' % (what, text_of(h) if h else ''), bool(h) and bool(text_of(h)))
        check('"without folder" shows what preferences.ini says', u.user32.SendMessageW(u.find_child(prefs, IDC_PG_BT_NOFOLDER), BM_GETCHECK, 0, 0) == 1)
        print('    screenshot:', u.screenshot(prefs, 'shot_options.png'))
        # an option changed in the window has to be there after Pegaso is closed
        u.user32.SendMessageW(u.find_child(prefs, IDC_PG_CLEAR_EXIT), BM_CLICK, 0, 0)
        u.user32.SendMessageW(u.find_child(prefs, IDC_PG_BT_NOFOLDER), BM_CLICK, 0, 0)
        time.sleep(0.5)
        u.user32.PostMessageW(prefs, u.WM_COMMAND, 1, 0)  # OK
        time.sleep(2)
        check('still running', p.poll() is None)
    finally:
        if not w.close_gracefully(p, 60):
            p.kill()
    time.sleep(1)
    saved = {line.split('=')[0]: line.split('=', 1)[1] for line in open(ini, encoding='latin-1').read().splitlines() if '=' in line}
    check('"clear traces on exit", ticked in the window, is kept after closing', saved.get('ClearTracesOnExit') == '1', saved.get('ClearTracesOnExit'))
    check('"without folder", unticked in the window, is kept after closing', saved.get('TorrentNoFolder') == '0', saved.get('TorrentNoFolder'))
    check('the token of the API stays encrypted after closing', saved.get('ApiToken', '').startswith('dpapi:'), saved.get('ApiToken'))
    print('RESULT', 'PASS' if ok else 'FAIL')


if __name__ == '__main__':
    main()
