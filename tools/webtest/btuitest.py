"""Torrent rows in the download list: visible, pause, resume, cancel. Spanish interface.

usage: python -I btuitest.py <file.torrent>
"""
import glob
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402

RELEASE = r'C:\xampp\htdocs\Emule\eMule0.72a-Sources\eMule0.72a-Sources\srchybrid\x64\Release'


def deploy():
    shutil.copy2(os.path.join(RELEASE, 'Pegaso.exe'), w.EXE)
    lang = os.path.join(w.RUN, 'lang')
    os.makedirs(lang, exist_ok=True)
    for dll in glob.glob(os.path.join(RELEASE, 'lang', '*.dll')):
        shutil.copy2(dll, lang)


def rows(lv, mem):
    n = u.user32.SendMessageW(lv, u.LVM_GETITEMCOUNT, 0, 0)
    return [[t for t in u.row_text(lv, mem, i) if t] for i in range(n)]


def main():
    torrent = sys.argv[1]
    deploy()
    w.reset_profile()
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('Language=1033', 'Language=1034')
    open(ini, 'w', newline='\r\n').write(text)
    pegaso = w.start_pegaso()
    hwnd = u.main_window(pegaso.pid)
    title = [t for h, t in u.top_windows(pegaso.pid) if h == hwnd][0]
    print('window title:', repr(title))
    ok = 'eMule' not in title
    u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
    time.sleep(1)
    lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
    mem = u.Remote(pegaso.pid)
    try:
        w.add_url(torrent)
        time.sleep(25)
        r = rows(lv, mem)
        print('rows:', r)
        ok &= len(r) == 1 and 'debian' in r[0][0]
        print('screenshot:', u.screenshot(hwnd, 'shot_torrent.png'))

        u.select_item(lv, mem, 0)
        u.user32.PostMessageW(lv, u.WM_COMMAND, u.MP_PAUSE, 0)
        time.sleep(6)
        paused = rows(lv, mem)
        print('paused:', paused)
        payload = glob.glob(os.path.join(w.RUN, 'Incoming', '*.iso'))
        size_a = os.path.getsize(payload[0]) if payload else -1
        from bttest import last_progress
        done_a = last_progress()[0]
        time.sleep(18)
        done_b = last_progress()[0]
        print('while paused: %.1f MB -> %.1f MB' % (done_a / 1048576, done_b / 1048576))
        ok &= done_b == done_a or done_b - done_a < 2 * 1048576

        u.user32.PostMessageW(lv, u.WM_COMMAND, u.MP_RESUME, 0)
        time.sleep(20)
        resumed = rows(lv, mem)
        print('resumed:', resumed)
        done_c = last_progress()[0]
        print('after resume: %.1f MB' % (done_c / 1048576))
        ok &= done_c > done_b

        u.user32.PostMessageW(lv, u.WM_COMMAND, u.MP_CANCEL, 0)
        time.sleep(2)
        boxes = [h for h, t in u.top_windows(pegaso.pid) if h != hwnd]
        print('confirmation windows:', len(boxes))
        for box in boxes:
            u.user32.PostMessageW(box, u.WM_COMMAND, 6, 0)  # IDYES
        time.sleep(6)
        left = rows(lv, mem)
        files = os.listdir(os.path.join(w.RUN, 'Incoming'))
        state = [os.path.basename(p) for p in glob.glob(os.path.join(w.RUN, 'config', 'torrents', '*.fastresume'))]
        print('after cancel: rows %s, Incoming %s, resume files %s' % (left, files, state))
        ok &= not left and not files and not state
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        mem.close()
        if not w.close_gracefully(pegaso, 60):
            pegaso.kill()


if __name__ == '__main__':
    main()
