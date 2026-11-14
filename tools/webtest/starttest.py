"""Opening a .torrent file from outside: with Pegaso closed, and with Pegaso open.

usage: python -I starttest.py

  1. Pegaso is started with a .torrent file as its argument (what a double click on the
     file does while Pegaso is closed): the torrent has to be in the list.
  2. With Pegaso open, it is started again with another .torrent file: no second Pegaso
     may stay open, and the torrent has to appear in the one which is running.

The torrents point to a tracker which does not exist; nothing is downloaded.
"""
import hashlib
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import btuitest  # noqa: E402
import btfilestest  # noqa: E402


def make_torrent(name):
    data = os.urandom(300000)
    pieces = b''.join(hashlib.sha1(data[i:i + 16384]).digest() for i in range(0, len(data), 16384))
    info = {'name': name, 'piece length': 16384, 'pieces': pieces, 'length': len(data)}
    path = os.path.join(HERE, name + ' [prueba] (1).torrent')
    open(path, 'wb').write(btfilestest.bencode({'info': info, 'announce': 'http://127.0.0.1:9/announce'}))
    return path


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    btuitest.deploy()
    w.reset_profile()
    first, second = make_torrent('Arranque en frio.mkv'), make_torrent('Con Pegaso abierto.mkv')
    pegaso = subprocess.Popen([w.EXE, first], cwd=w.RUN)
    time.sleep(12)
    ok = pegaso.poll() is None
    mem = None
    try:
        hwnd = u.main_window(pegaso.pid)
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
        time.sleep(2)
        lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
        mem = u.Remote(pegaso.pid)
        names = sorted(r[0] for r in btuitest.rows(lv, mem))
        print('1. started with a torrent: list =', names)
        ok &= names == ['Arranque en frio.mkv']

        again = subprocess.Popen([w.EXE, second], cwd=w.RUN)
        time.sleep(8)
        still = again.poll() is None
        names = sorted(r[0] for r in btuitest.rows(lv, mem))
        print('2. opened another torrent: a second Pegaso stays open = %s, list = %s' % (still, names))
        if still:
            again.kill()
        ok &= not still and names == ['Arranque en frio.mkv', 'Con Pegaso abierto.mkv']
        ok &= os.path.isdir(os.path.join(w.RUN, 'logs'))  # it really worked on the test folder
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        if mem:
            mem.close()
        if not w.close_gracefully(pegaso, 60):
            pegaso.kill()


if __name__ == '__main__':
    main()
