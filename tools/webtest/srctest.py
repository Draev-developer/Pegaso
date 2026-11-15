"""The sources of a download survive a restart (config\\sources.dat).

usage: python -I srctest.py

A shares a file and uploads slowly. B is told about A once (a link with a source), receives
some data and is closed: A has to be in B's sources.dat. B starts again - nobody tells it
about A this time, and A is on no server and publishes nothing B could find in time - and
has to go on downloading from A.
"""
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import btuitest  # noqa: E402
import nettest  # noqa: E402
import ratetest as r  # noqa: E402

NAME = 'Video.prueba.avi'
SIZE = 40 * 1024 * 1024
A_PORT = 4720


def connect(hwnd):
    u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
    u.user32.PostMessageW(hwnd, u.WM_COMMAND, nettest.MP_HM_CON, 0)
    t0 = time.time()
    while time.time() - t0 < 90 and 'external Kad Port' not in r.log_of(w.RUN):
        time.sleep(3)
    time.sleep(5)


def part_bytes():
    path = os.path.join(w.RUN, 'Temp', '001.part.met')
    return os.path.getmtime(path) if os.path.isfile(path) else 0


def wait_transfer(lv, mem, timeout):
    """Waits until the download receives data; returns the row."""
    t0 = time.time()
    row = None
    while time.time() - t0 < timeout:
        time.sleep(5)
        rows = btuitest.rows(lv, mem)
        row = rows[0] if rows else None
        print('  t=%3ds %s' % (time.time() - t0, row[1:6] if row else None))
        if row and any('kB/s' in cell or 'KB/s' in cell for cell in row):
            return row
    return None


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ip = r.lan_ip()
    data = os.urandom(SIZE)
    link = 'ed2k://|file|%s|%d|%s|/' % (NAME, SIZE, w.ed2k_hash(data))
    a_folder = w.RUN + '-a1'
    r.make_instance(w.RUN, 4700)
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('NetworkKademlia=0\n', '')
    open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
    r.make_instance(a_folder, A_PORT, 'MaxUpload=40\n')
    open(os.path.join(a_folder, 'Incoming', NAME), 'wb').write(data)

    a = r.start(a_folder)
    b = r.start(w.RUN)
    mem = None
    ok = True
    try:
        time.sleep(12)  # A hashes its file
        hwnd = u.main_window(b.pid)
        connect(hwnd)
        lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
        mem = u.Remote(b.pid)
        r.send_link(hwnd, link + '|sources,%s:%d|/' % (ip, A_PORT))
        row = wait_transfer(lv, mem, 120)
        print('B downloads from A:', row is not None)
        ok &= row is not None
        time.sleep(10)

        # ---- B closes: A is remembered
        mem.close()
        mem = None
        if not w.close_gracefully(b, 60):
            b.kill()
        time.sleep(3)
        stored = os.path.join(w.RUN, 'config', 'sources.dat')
        lines = open(stored).read().split() if os.path.isfile(stored) else []
        print('sources.dat:', lines)
        ok &= len(lines) == 1 and ';%d;' % A_PORT in lines[0]

        # ---- B starts again and is told nothing
        b = r.start(w.RUN)
        hwnd = u.main_window(b.pid)
        lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
        mem = u.Remote(b.pid)
        connect(hwnd)
        row = wait_transfer(lv, mem, 120)
        log = r.log_of(w.RUN)
        remembered = [line.strip()[-70:] for line in log.splitlines() if 'Sources remembered' in line]
        print('log:', remembered)
        print('B goes on downloading from A after the restart:', row is not None)
        ok &= row is not None and bool(remembered) and b.poll() is None
        print('screenshot:', u.screenshot(hwnd, 'shot_sources.png'))

        # ---- an entry older than a day is not used: B is closed, the entry aged, B started
        mem.close()
        mem = None
        if not w.close_gracefully(b, 60):
            b.kill()
        time.sleep(3)
        fields = open(stored).read().split()[0].split(';')
        fields[1] = str(int(time.time()) - 25 * 3600)
        open(stored, 'w').write(';'.join(fields) + '\n')
        b = r.start(w.RUN)
        hwnd = u.main_window(b.pid)
        connect(hwnd)
        time.sleep(10)
        log = r.log_of(w.RUN)
        again = sum('Sources remembered' in line for line in log.splitlines())
        print('entries used in the log after ageing the entry: %d (expected %d)' % (again, len(remembered)))
        ok &= again == len(remembered)
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        if mem:
            mem.close()
        for proc in (b, a):
            if not w.close_gracefully(proc, 60):
                proc.kill()


if __name__ == '__main__':
    main()
