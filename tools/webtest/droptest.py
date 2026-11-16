"""Files dragged from Explorer into the window of Pegaso: .torrent files become downloads.

usage: python -I droptest.py

The drag is a real one (dragdrop.ps1): the mouse is moved for about two seconds each time
and put back afterwards. Do not touch the mouse meanwhile.
"""
import ctypes
import ctypes.wintypes
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import btuitest  # noqa: E402


def drag(hwnd, files):
    """drops the files on the middle of the window; tells the effect the target answered with"""
    u.user32.SetForegroundWindow(hwnd)
    time.sleep(1)
    rc = ctypes.wintypes.RECT()
    u.user32.GetWindowRect(hwnd, ctypes.byref(rc))
    x, y = (rc.left + rc.right) // 2, (rc.top + rc.bottom) // 2
    out = subprocess.run(['powershell', '-NoProfile', '-STA', '-ExecutionPolicy', 'Bypass', '-File', os.path.join(HERE, 'dragdrop.ps1'),
                          str(x), str(y)] + files, capture_output=True, text=True, timeout=60)
    return (out.stdout + out.stderr).strip().splitlines()[-1] if (out.stdout + out.stderr).strip() else ''


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ok = True

    def check(what, cond, detail=''):
        nonlocal ok
        print('  %-66s %s %s' % (what, 'ok' if cond else 'FAIL', '' if cond else detail))
        ok &= bool(cond)

    ctypes.windll.shcore.SetProcessDpiAwareness(2)
    btuitest.deploy()
    w.reset_profile()
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('Language=1033', 'Language=1034')
    open(ini, 'w', newline='\r\n').write(text)
    pegaso = w.start_pegaso()
    hwnd = u.main_window(pegaso.pid)
    u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
    time.sleep(1)
    lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
    mem = u.Remote(pegaso.pid)
    try:
        u.user32.GetPropW.restype = ctypes.c_void_p
        u.user32.GetPropW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p]
        # the wizard of a first start keeps the window from taking drops until it is closed
        for h, t in u.top_windows(pegaso.pid):
            if h != hwnd and t:
                u.user32.PostMessageW(h, u.WM_COMMAND, 2, 0)
        time.sleep(4)
        check('the window of Pegaso takes drops', bool(u.user32.GetPropW(hwnd, 'OleDropTargetInterface')), [t for h, t in u.top_windows(pegaso.pid)])
        one = os.path.join(HERE, 'Video.prueba.2.mkv.torrent')
        effect = drag(hwnd, [one])
        time.sleep(6)
        names = [row[0] for row in btuitest.rows(lv, mem)]
        print('    %s; downloads: %s' % (effect, names))
        check('a .torrent file is accepted while it is dragged over Pegaso', 'Copy' in effect, effect)
        check('... and dropped, it becomes a download', len(names) == 1, names)

        two = [os.path.join(HERE, 'Clase grabada.mkv.torrent'), os.path.join(HERE, 'Presentacion del proyecto.mkv.torrent')]
        effect = drag(hwnd, two)
        time.sleep(8)
        names = [row[0] for row in btuitest.rows(lv, mem)]
        print('    %s; downloads: %s' % (effect, names))
        u.user32.FindWindowExW.restype = ctypes.c_void_p
        u.user32.FindWindowExW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_wchar_p]
        bar = u.user32.FindWindowExW(hwnd, None, 'msctls_statusbar32', None)
        n = u.user32.SendMessageW(bar, 0x40D, 0, mem.addr + 512) & 0xffff
        print('    status bar:', mem.read(n * 2, 512).decode('utf-16-le', 'replace'))
        check('several files dropped at once are all taken', len(names) == 3, names)

        effect = drag(hwnd, [os.path.join(HERE, 'droptest.py')])
        time.sleep(3)
        print('    %s' % effect)
        check('a file which Pegaso does not open is not accepted', 'None' in effect, effect)
        check('... and nothing is added', len(btuitest.rows(lv, mem)) == 3)
        print('    screenshot:', u.screenshot(hwnd, 'shot_drop.png'))
        check('still running', pegaso.poll() is None)
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        mem.close()
        if not w.close_gracefully(pegaso, 40):
            pegaso.kill()


if __name__ == '__main__':
    main()
