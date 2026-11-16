"""Many search results downloaded at once: Pegaso has to stay with the user.

usage: python -I manytest.py [number of results, 600 by default]

A stored search with that many results is restored at startup; all of them are selected and
"Download" is pressed. A few are suspicious: there has to be one question for all of them,
not one window each. Measured: how long it takes until every download is in the list, and
the longest time the window did not answer.
"""
import ctypes
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import ratetest as r  # noqa: E402
import trusttest as t  # noqa: E402

MB = 1024 * 1024
SUSPICIOUS = 4
u.user32.SendMessageTimeoutW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t, ctypes.c_uint, ctypes.c_uint,
                                         ctypes.POINTER(ctypes.c_size_t)]


def write_search(folder, count):
    results = []
    for i in range(count):
        name = 'Leccion.%04d.mkv' % i if i >= SUSPICIOUS else 'Leccion.%04d.avi.exe' % i
        results.append(t.result('muchos-%d' % i, name, (40 + i % 50) * MB, 3 + i % 9))
    body = b'\x0f\x01' + struct.pack('<H', 1)
    body += struct.pack('<IBB', 0, 1, 0) + t.string('') + t.string('leccion') + t.string('')
    body += struct.pack('<I', len(results)) + b''.join(results)
    open(os.path.join(folder, 'config', 'StoredSearches.met'), 'wb').write(body)


def answers(hwnd, ms):
    """Whether the window takes a message within that time."""
    result = ctypes.c_size_t()
    return bool(u.user32.SendMessageTimeoutW(hwnd, 0, 0, 0, 0x0002, ms, ctypes.byref(result)))  # WM_NULL, SMTO_ABORTIFHUNG


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    count = int(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1].isdigit() else 600
    ok = True

    def check(what, cond, detail=''):
        nonlocal ok
        print('  %-72s %s %s' % (what, 'ok' if cond else 'FAIL', '' if cond else detail))
        ok &= bool(cond)

    r.make_instance(w.RUN, 4700, 'AddNewFilesPaused=%d\n' % (0 if 'active' in sys.argv else 1))   # "active": as most people have it
    for name in os.listdir(os.path.join(w.RUN, 'Temp')) if os.path.isdir(os.path.join(w.RUN, 'Temp')) else []:
        os.remove(os.path.join(w.RUN, 'Temp', name))
    write_search(w.RUN, count)
    temp = os.path.join(w.RUN, 'Temp')
    pegaso = r.start(w.RUN)
    try:
        hwnd = u.main_window(pegaso.pid)
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, t.MP_HM_SEARCH, 0)
        time.sleep(3)
        lv = u.find_child(hwnd, t.IDC_SEARCHLIST)
        listed = u.user32.SendMessageW(lv, u.LVM_GETITEMCOUNT, 0, 0)
        check('the %d results are listed' % count, listed == count, listed)
        mem = u.Remote(pegaso.pid)
        item = u.LVITEM(stateMask=u.LVIS_SELECTED, state=u.LVIS_SELECTED)
        mem.write(item)
        u.user32.SendMessageW(lv, u.LVM_SETITEMSTATE, ctypes.c_size_t(-1).value, mem.addr)  # all of them
        start = time.time()
        u.user32.PostMessageW(lv, u.WM_COMMAND, u.MP_RESUME, 0)
        questions = 0
        worst = 0.0
        silent_since = None
        done = None
        while time.time() - start < 600:
            box = t.message_box(pegaso.pid)
            if box:
                questions += 1
                u.user32.PostMessageW(box, u.WM_COMMAND, 6, 0)  # Yes
                time.sleep(0.5)
                continue
            if answers(hwnd, 500):
                silent_since = None
            else:
                silent_since = silent_since or time.time()
                worst = max(worst, time.time() - silent_since + 0.5)
            parts = len([n for n in os.listdir(temp) if n.endswith('.part.met')]) if os.path.isdir(temp) else 0
            if parts >= count and silent_since is None:
                done = time.time() - start
                break
            time.sleep(0.3)
        print('    %d downloads in %s s; questions asked: %d; longest time without an answer from the window: %.1f s'
              % (count, '%.1f' % done if done is not None else '> 600', questions, worst))
        check('every download is in the list', done is not None)
        check('one question for all the suspicious results, not one each', questions == 1, questions)
        check('it took less than a minute', done is not None and done < 60, done)
        dl = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
        time.sleep(2)
        check('the window answers afterwards', answers(hwnd, 3000))
        check('still running', pegaso.poll() is None)
    finally:
        if not w.close_gracefully(pegaso, 180):
            pegaso.kill()
            check('closes', False)
    print('RESULT', 'PASS' if ok else 'FAIL')


if __name__ == '__main__':
    main()
