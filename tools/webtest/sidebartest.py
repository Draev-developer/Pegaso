"""The main window: the bar on the left, the field above and the filters of the downloads.

usage: python -I sidebartest.py

One Pegaso, no network. The bars are painted by Pegaso itself, so the test clicks at the
places where their parts are for a window of 1280 x 760 at 100 % scaling; on a screen with
another scaling the clicks miss and the test says so.
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
import ratetest as r  # noqa: E402

ID_SIDEBAR, ID_FIELD, ID_TOPBAR = 0x5E00, 0x5E01, 0x5E02
WM_SETTEXT, WM_KEYDOWN, WM_LBUTTONDOWN, WM_LBUTTONUP, VK_RETURN = 0x000C, 0x0100, 0x0201, 0x0202, 0x0D
NAMES = [('Distribucion Linux.iso', 4700000000), ('Concierto benefico.mkv', 2100000000), ('Clase grabada.mkv', 1400000000)]


def resource_id(name):
    for line in open(os.path.join(HERE, '..', '..', 'srchybrid', 'Resource.h'), encoding='latin-1'):
        parts = line.split()
        if len(parts) >= 3 and parts[0] == '#define' and parts[1] == name:
            return int(parts[2], 0)
    raise KeyError(name)


def click(hwnd, x, y):
    lparam = (y << 16) | x
    u.user32.PostMessageW(hwnd, WM_LBUTTONDOWN, 1, lparam)
    u.user32.PostMessageW(hwnd, WM_LBUTTONUP, 0, lparam)
    time.sleep(1.5)


def visible(hwnd):
    return bool(u.user32.IsWindowVisible(hwnd))


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ok = True

    def check(what, cond, detail=''):
        nonlocal ok
        print('  %-70s %s %s' % (what, 'ok' if cond else 'FAIL', '' if cond else detail))
        ok &= bool(cond)

    dpi = ctypes.windll.user32.GetDpiForSystem()
    if dpi != 96:
        print('RESULT SKIPPED: the screen is scaled to %d %%, the clicks of this test are for 100 %%' % (dpi * 100 // 96))
        return
    r.make_instance(w.RUN, 4700)
    p = r.start(w.RUN)
    mem = None
    try:
        hwnd = u.main_window(p.pid)
        u.user32.MoveWindow(hwnd, 60, 40, 1280, 760, True)
        time.sleep(1)
        side, top = u.find_child(hwnd, ID_SIDEBAR), u.find_child(hwnd, ID_TOPBAR)
        field = u.find_child(hwnd, ID_FIELD)
        check('the bar on the left, the bar above and its field exist', side and top and field)
        check('the toolbar of eMule is not shown', not visible(u.user32.FindWindowExW(hwnd, 0, 'ToolbarWindow32', None)))
        lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
        uploads = u.find_child(hwnd, resource_id('IDC_UPLOADLIST'))
        mem = u.Remote(p.pid)

        # ---- the sections
        click(side, 100, 87)  # downloads
        check('"Descargas" shows the downloads alone', visible(lv) and not visible(uploads))
        click(side, 100, 213)  # uploads
        check('"Subidas" shows the uploads alone', visible(uploads) and not visible(lv))
        click(side, 100, 129)  # search
        check('"Buscar" leaves the transfers', not visible(uploads) and not visible(lv))
        click(side, 100, 87)
        check('back to the downloads', visible(lv))

        # ---- the field: a link is offered for download
        for name, size in NAMES[:2]:
            r.send_link(hwnd, 'ed2k://|file|%s|%d|%s|/' % (name, size, hashlib.md5(name.encode()).hexdigest()))
            time.sleep(0.5)
        name, size = NAMES[2]
        link = ctypes.create_unicode_buffer('ed2k://|file|%s|%d|%s|/' % (name, size, hashlib.md5(name.encode()).hexdigest()))
        u.user32.SendMessageW(field, WM_SETTEXT, 0, ctypes.addressof(link))
        u.user32.PostMessageW(field, WM_KEYDOWN, VK_RETURN, 0)
        time.sleep(3)
        dialogs = [(h, t) for h, t in u.top_windows(p.pid) if h != hwnd and t]
        print('    windows after Enter:', [t for _, t in dialogs])
        check('Enter with a link in the field opens the window for links', len(dialogs) == 1, dialogs)
        if dialogs:
            u.user32.PostMessageW(dialogs[0][0], u.WM_COMMAND, 1, 0)  # accept
            time.sleep(3)
        print('    windows after accepting:', [t for h, t in u.top_windows(p.pid) if h != hwnd and t])
        rows = btuitest.rows(lv, mem)
        check('the link became a download', len(rows) == 3 and any(name in row[0] for row in rows), [row[0] for row in rows])

        # ---- the filters (the bar above is 1068 wide here; the pills end at its right edge)
        u.select_item(lv, mem, 0)
        u.user32.PostMessageW(lv, u.WM_COMMAND, resource_id_menu('MP_PAUSE'), 0)
        time.sleep(3)
        print('    screenshot:', u.screenshot(hwnd, 'shot_sidebar.png'))
        click(top, 850, 83)   # paused
        check('"En pausa" shows the paused download alone', len(btuitest.rows(lv, mem)) == 1, btuitest.rows(lv, mem))
        click(top, 735, 83)   # downloading
        check('"Descargando" shows the other two', len(btuitest.rows(lv, mem)) == 2, btuitest.rows(lv, mem))
        click(top, 620, 83)   # all
        check('"Todas" shows the three', len(btuitest.rows(lv, mem)) == 3)
        check('still running', p.poll() is None)
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        if mem:
            mem.close()
        if not w.close_gracefully(p, 40):
            p.kill()


def resource_id_menu(name):
    for line in open(os.path.join(HERE, '..', '..', 'srchybrid', 'MenuCmds.h'), encoding='latin-1'):
        parts = line.split()
        if len(parts) >= 3 and parts[0] == '#define' and parts[1] == name:
            return int(parts[2], 0)
    raise KeyError(name)


if __name__ == '__main__':
    main()
