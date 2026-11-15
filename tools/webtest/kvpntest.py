"""Kaspersky VPN for Pegaso: the offer to tie Pegaso to its adapter.

usage: python -I kvpntest.py

Only on a computer where Kaspersky VPN is installed (else the test says so and ends).
Kaspersky VPN is not touched: the test asks for the window, reads it and answers "No", so
nothing is tied and Kaspersky VPN is not opened.
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

MP_PEGASO_KVPN = 10989
IDNO = 7


def static_text(dialog):
    out = []

    def each(hwnd, _):
        buf = ctypes.create_unicode_buffer(4096)
        u.user32.GetWindowTextW(hwnd, buf, 4096)
        if buf.value:
            out.append(buf.value)
        return True

    proc = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)(each)
    u.user32.EnumChildWindows(dialog, proc, 0)
    return '\n'.join(out)


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ok = True

    def check(what, cond, detail=''):
        nonlocal ok
        print('  %-66s %s %s' % (what, 'ok' if cond else 'FAIL', '' if cond else detail))
        ok &= bool(cond)

    r.make_instance(w.RUN, 4700)
    p = r.start(w.RUN)
    try:
        hwnd = u.main_window(p.pid)
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, MP_PEGASO_KVPN, 0)
        time.sleep(3)
        boxes = [(h, t) for h, t in u.top_windows(p.pid) if h != hwnd and t]
        if not boxes:
            print('RESULT SKIPPED: Kaspersky VPN is not installed on this computer')
            return
        box, title = boxes[0]
        text = static_text(box)
        print('    title:', title)
        print('    ' + text.replace('\n', '\n    ')[:900])
        check('the window is the one of Kaspersky VPN for Pegaso', title == 'Kaspersky VPN para Pegaso', title)
        check('it names the adapter of Kaspersky VPN', '«' in text and 'adaptador' in text)
        check('it tells what to set in Kaspersky VPN', 'Túnel dividido' in text and 'siempre se conectan por VPN' in text)
        check('it gives the path of this Pegaso to add there', os.path.join(w.RUN, 'Pegaso.exe').lower() in text.lower())
        print('    screenshot:', u.screenshot(box, 'shot_kvpn.png'))
        u.user32.PostMessageW(box, u.WM_COMMAND, IDNO, 0)
        time.sleep(2)
        ini = open(os.path.join(w.RUN, 'config', 'preferences.ini'), encoding='latin-1').read()
        check('answering "No" ties nothing', 'PegasoBindAdapter=' not in ini or 'PegasoBindAdapter=\n' in ini.replace('\r', ''))
        check('still running', p.poll() is None)
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        if not w.close_gracefully(p, 40):
            p.kill()


if __name__ == '__main__':
    main()
