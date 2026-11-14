"""Drives a Pegaso instance against the local range server and verifies the results.

usage: python -I webtest.py <scenario>
  basic    download big/small/norange/named, compare SHA-256 with the served data
"""
import hashlib
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
RUN = r'C:\xampp\htdocs\Emule\pegaso-test'
EXE = os.path.join(RUN, 'Pegaso.exe')
BASE = 'http://127.0.0.1:8765'
SERVER_LOG = os.path.join(HERE, 'server.log')

sys.path.insert(0, HERE)
import rangeserver  # noqa: E402  (same directory as this script)

PREFS = """[eMule]
AppVersion=0.72a
Language=1033
Nick=PegasoTest
Autoconnect=0
Reconnect=0
NetworkED2K=0
NetworkKademlia=0
UpdateNotifyTestClient=0
ConfirmExit=0
Splashscreen=0
BringToFront=0
Verbose=1
SaveLogToDisk=1
SaveDebugToDisk=1
DownloadCapacity=1000000
MaxDownload=-1
"""


def reset_profile(keep_temp=False):
    for sub in ('config', 'Incoming', 'logs') + (() if keep_temp else ('Temp',)):
        shutil.rmtree(os.path.join(RUN, sub), ignore_errors=True)
    os.makedirs(os.path.join(RUN, 'config'), exist_ok=True)
    with open(os.path.join(RUN, 'config', 'preferences.ini'), 'w', encoding='ascii', newline='\r\n') as f:
        f.write(PREFS)


def start_server():
    log = open(SERVER_LOG, 'w')
    proc = subprocess.Popen([sys.executable, '-I', os.path.join(HERE, 'rangeserver.py')], stdout=log, stderr=subprocess.STDOUT)
    import socket
    for _ in range(120):  # content generation takes a few seconds
        try:
            socket.create_connection(('127.0.0.1', 8765), 0.5).close()
            return proc
        except OSError:
            time.sleep(0.5)
    sys.exit('test server did not start')


def start_pegaso():
    proc = subprocess.Popen([EXE], cwd=RUN)
    time.sleep(10)
    if proc.poll() is not None:
        sys.exit('Pegaso exited at startup, code %s' % proc.returncode)
    return proc


def add_url(url):
    subprocess.run([EXE, url], cwd=RUN, timeout=30)


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def wait_files(expected, timeout):
    """expected: {file name in Incoming: server path}. Returns {name: seconds or None}."""
    incoming = os.path.join(RUN, 'Incoming')
    done = {}
    t0 = time.time()
    while len(done) < len(expected) and time.time() - t0 < timeout:
        for name in expected:
            if name not in done and os.path.isfile(os.path.join(incoming, name)):
                done[name] = round(time.time() - t0, 1)
        time.sleep(0.5)
    time.sleep(1)
    return {name: done.get(name) for name in expected}


def check(expected, times):
    ok = True
    for name, path in expected.items():
        full = os.path.join(RUN, 'Incoming', name)
        data = rangeserver.FILES[path]['data']
        if times[name] is None:
            print('FAIL %-16s not completed' % name)
            ok = False
            continue
        good = os.path.getsize(full) == len(data) and sha(full) == hashlib.sha256(data).hexdigest()
        print('%s %-16s %9d bytes  %5.1fs' % ('ok  ' if good else 'FAIL', name, os.path.getsize(full), times[name]))
        ok &= good
    return ok


def server_summary():
    counts = {}
    for line in open(SERVER_LOG):
        parts = line.split()
        if len(parts) == 4:
            key = (parts[1], parts[3])
            counts[key] = counts.get(key, 0) + 1
    for (path, status), n in sorted(counts.items()):
        print('  server: %-14s status %s x%d' % (path, status, n))


def tail_log(n=25):
    path = os.path.join(RUN, 'logs', 'eMule.log')
    for name in ('eMule.log', 'eMule_Verbose.log'):
        path = os.path.join(RUN, 'logs', name)
        if os.path.isfile(path):
            raw = open(path, 'rb').read()
            text = raw.decode('utf-16', 'replace') if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else raw.decode('utf-8', 'replace')
            lines = [l for l in text.splitlines() if 'eb ' in l or 'rror' in l or 'ailed' in l or 'orrupt' in l or 'ash' in l]
            print('--- %s (%d matching lines)' % (name, len(lines)))
            for l in lines[-n:]:
                print('  ' + l[:200])


def list_dir(sub):
    d = os.path.join(RUN, sub)
    if os.path.isdir(d):
        for name in sorted(os.listdir(d)):
            print('  %s\\%s  %d' % (sub, name, os.path.getsize(os.path.join(d, name))))


def scenario_basic():
    reset_profile()
    server = start_server()
    pegaso = start_pegaso()
    try:
        expected = {'big.bin': '/big.bin', 'small.bin': '/small.bin', 'norange.bin': '/norange.bin', 'named file.dat': '/named'}
        for path in expected.values():
            add_url(BASE + path)
        times = wait_files(expected, 120)
        ok = check(expected, times)
        server_summary()
        list_dir('Temp')
        tail_log()
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        pegaso.kill()
        server.kill()


def close_gracefully(proc, timeout=40):
    """Asks the main window to close, as the user would, and waits for the process."""
    import ctypes
    from ctypes import wintypes
    user32 = ctypes.windll.user32
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(hwnd, _):
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(hwnd, buf, 256)
        if pid.value == proc.pid and buf.value.startswith('Pegaso v'):
            found.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    if not found:
        return False
    user32.PostMessageW(found[0], 0x0010, 0, 0)  # WM_CLOSE
    try:
        proc.wait(timeout)
        return True
    except subprocess.TimeoutExpired:
        return False


def sent_bytes(path):
    return sum(int(l.split()[2]) for l in open(SERVER_LOG) if l.startswith('SENT ' + path + ' '))


def md4_cng(data):
    """MD4 through Windows CNG (Python's OpenSSL no longer ships it)."""
    import ctypes
    bcrypt = ctypes.windll.bcrypt
    alg = ctypes.c_void_p()
    if bcrypt.BCryptOpenAlgorithmProvider(ctypes.byref(alg), ctypes.c_wchar_p('MD4'), None, 0):
        raise OSError('MD4 provider')
    out = ctypes.create_string_buffer(16)
    status = bcrypt.BCryptHash(alg, None, 0, ctypes.c_char_p(data), len(data), out, 16)
    bcrypt.BCryptCloseAlgorithmProvider(alg, 0)
    if status:
        raise OSError('BCryptHash %x' % (status & 0xffffffff))
    return out.raw


def ed2k_hash(data):
    """ed2k file hash: MD4 of the MD4 hashes of the 9.28 MB parts."""
    part = 9728000
    md4 = md4_cng
    if len(data) < part:
        return md4(data).hex()
    hashes = b''.join(md4(data[i:i + part]) for i in range(0, len(data), part))
    if len(data) % part == 0:
        hashes += md4(b'')
    return md4(hashes).hex()


def scenario_resume():
    reset_profile()
    server = start_server()
    pegaso = start_pegaso()
    try:
        add_url(BASE + '/slow.bin')
        time.sleep(7)
        closed = close_gracefully(pegaso)
        print('graceful close:', closed)
        if not closed:
            pegaso.kill()
        time.sleep(1)
        before = sent_bytes('/slow.bin')
        print('served before restart: %.1f MB' % (before / 1048576))
        list_dir('Temp')
        met = os.path.join(RUN, 'Temp', '001.part.met')
        print('url stored in part.met:', os.path.isfile(met) and b'PegasoWebUrl' in open(met, 'rb').read())
        print('file in Incoming before restart:', os.path.isfile(os.path.join(RUN, 'Incoming', 'slow.bin')))

        pegaso = start_pegaso()
        expected = {'slow.bin': '/slow.bin'}
        times = wait_files(expected, 90)
        ok = check(expected, times)
        total = sent_bytes('/slow.bin')
        size = len(rangeserver.FILES['/slow.bin']['data'])
        print('served after restart: %.1f MB; in total %.1f MB for a %.1f MB file' % ((total - before) / 1048576, total / 1048576, size / 1048576))
        ok &= total - before < size - before / 2  # most of the first session was kept
        # the completed file must be known under its real ed2k hash
        closed = close_gracefully(pegaso)
        known = open(os.path.join(RUN, 'config', 'known.met'), 'rb').read()
        real = bytes.fromhex(ed2k_hash(rangeserver.FILES['/slow.bin']['data']))
        placeholder = md4_cng(b'pegaso-web:' + (BASE + '/slow.bin').encode())
        print('known.met has the real ed2k hash:', real in known, '| placeholder hash:', placeholder in known)
        ok &= real in known and placeholder not in known
        tail_log(12)
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        if pegaso.poll() is None:
            pegaso.kill()
        server.kill()


if __name__ == '__main__':
    globals()['scenario_' + sys.argv[1]]()
