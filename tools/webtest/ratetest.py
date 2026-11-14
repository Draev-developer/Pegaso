"""Verdicts on files, end to end, with three Pegaso instances on this computer.

usage: python -I ratetest.py

  B  (pegaso-run)     downloads a file and rates it through the dialog
  A1, A2 (pegaso-run-a1 / -a2) share that file, each with the verdict "fake"

B gets the file from A1 and A2 (direct connections over the LAN address, no server, no
Kad), receives their verdicts and has to warn about the file. No outside network is used.
"""
import ctypes
import glob
import os
import shutil
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import btuitest  # noqa: E402
import btfilestest  # noqa: E402
import nettest  # noqa: E402

MP_PEGASO_RATE = 10982
IDC_RT_MISMATCH, IDC_RT_WHAT, IDC_RT_LANG, IDC_RT_TEXT, IDC_RT_OTHERS = 5054, 5057, 5059, 5061, 5062
BM_CLICK, CB_SETCURSEL, WM_SETTEXT, WM_GETTEXT = 0x00F5, 0x014E, 0x000C, 0x000D
NAME = 'Pelicula.de.prueba.(Castellano).avi'
SIZE = 6 * 1024 * 1024


def lan_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('192.0.2.1', 9))  # no packet is sent
        return s.getsockname()[0]
    finally:
        s.close()


def make_instance(folder, port, extra=''):
    for sub in ('config', 'Incoming', 'logs', 'Temp'):
        shutil.rmtree(os.path.join(folder, sub), ignore_errors=True)
    os.makedirs(os.path.join(folder, 'config'), exist_ok=True)
    os.makedirs(os.path.join(folder, 'Incoming'), exist_ok=True)
    shutil.copy2(os.path.join(btuitest.RELEASE, 'Pegaso.exe'), folder)
    lang = os.path.join(folder, 'lang')
    os.makedirs(lang, exist_ok=True)
    for dll in glob.glob(os.path.join(btuitest.RELEASE, 'lang', '*.dll')):
        shutil.copy2(dll, lang)
    text = w.PREFS.replace('Language=1033', 'Language=1034')
    text += 'Port=%d\nUDPPort=%d\nFilterBadIPs=0\nTorrentEnabled=0\n%s' % (port, port + 10, extra)
    open(os.path.join(folder, 'config', 'preferences.ini'), 'w', encoding='ascii', newline='\r\n').write(text)


def start(folder):
    proc = subprocess.Popen([os.path.join(folder, 'Pegaso.exe')], cwd=folder)
    time.sleep(10)
    if proc.poll() is not None:
        sys.exit('Pegaso exited at startup in ' + folder)
    return proc


class COPYDATASTRUCT(ctypes.Structure):
    _fields_ = [('dwData', ctypes.c_size_t), ('cbData', ctypes.c_uint32), ('lpData', ctypes.c_void_p)]


def send_link(hwnd, link):
    """Hands a link to one particular instance (Pegaso.exe <link> picks any running one)."""
    buf = ctypes.create_unicode_buffer(link)
    cds = COPYDATASTRUCT(12000, ctypes.sizeof(buf), ctypes.addressof(buf))  # OP_ED2KLINK
    u.user32.SendMessageW(hwnd, 0x004A, 0, ctypes.addressof(cds))  # WM_COPYDATA


def rate_dialog(pid):
    for hwnd, title in u.top_windows(pid):
        if title == 'Valorar archivo':
            return hwnd, title
    return None, None


def text_of(hwnd):
    buf = ctypes.create_unicode_buffer(1024)
    u.user32.SendMessageW(hwnd, WM_GETTEXT, 1024, ctypes.addressof(buf))
    return buf.value


def log_of(folder):
    saved, w.RUN = w.RUN, folder
    try:
        return nettest.log_text()
    finally:
        w.RUN = saved


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ip = lan_ip()
    data = os.urandom(SIZE)
    file_hash = w.ed2k_hash(data)
    link = 'ed2k://|file|%s|%d|%s|/' % (NAME, SIZE, file_hash)
    print('LAN address %s, file hash %s' % (ip, file_hash))

    a = [w.RUN + '-a1', w.RUN + '-a2']
    make_instance(w.RUN, 4700)
    # eMule asks sources for data only while it is connected to a network: B joins Kad.
    # (Not a server: logged in to a server with a low ID, eMule drops sources which have its
    # own local address - and here all three instances share one address.)
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('NetworkKademlia=0\n', '')
    open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
    for i, folder in enumerate(a):
        make_instance(folder, 4720 + 20 * i, 'MaxUpload=60\n')
        open(os.path.join(folder, 'Incoming', NAME), 'wb').write(data)

    procs = []
    ok = True
    b = start(w.RUN)
    procs.append(b)
    mem = None
    try:
        hwnd = u.main_window(b.pid)
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
        time.sleep(1)
        lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
        mem = u.Remote(b.pid)

        u.user32.PostMessageW(hwnd, u.WM_COMMAND, nettest.MP_HM_CON, 0)
        t0 = time.time()
        while time.time() - t0 < 90 and 'external Kad Port' not in log_of(w.RUN):
            time.sleep(3)
        time.sleep(5)
        print('B connected to a server after %ds' % (time.time() - t0))

        # ---- 1. B rates its download through the dialog
        subprocess.run([w.EXE, link], cwd=w.RUN, timeout=30)
        time.sleep(3)
        print('download list:', [r[0] for r in btuitest.rows(lv, mem)])
        u.select_item(lv, mem, 0)
        u.user32.PostMessageW(lv, u.WM_COMMAND, MP_PEGASO_RATE, 0)
        time.sleep(2)
        dlg, title = rate_dialog(b.pid)
        print('dialog:', repr(title))
        ok &= dlg is not None and title == 'Valorar archivo'
        u.user32.SendMessageW(u.find_child(dlg, IDC_RT_MISMATCH), BM_CLICK, 0, 0)
        combo = u.find_child(dlg, IDC_RT_LANG)
        u.user32.SendMessageW(combo, CB_SETCURSEL, 3, 0)  # English
        note = ctypes.create_unicode_buffer('el audio está en inglés')
        u.user32.SendMessageW(u.find_child(dlg, IDC_RT_TEXT), WM_SETTEXT, 0, ctypes.addressof(note))
        time.sleep(1)
        print('others line:', repr(text_of(u.find_child(dlg, IDC_RT_OTHERS))))
        print('screenshot:', u.screenshot(dlg, 'shot_rate.png'))
        u.user32.PostMessageW(dlg, u.WM_COMMAND, 1, 0)
        time.sleep(2)
        stored = None
        for path in glob.glob(os.path.join(w.RUN, 'config', '*.ini')):
            raw = open(path, 'rb').read()
            if b'MISMATCH' in raw:
                stored = path
                print('stored in %s:' % os.path.basename(path))
                print('   ' + raw.decode('latin-1').strip().replace('\r\n', '\n   '))
        ok &= stored is not None
        if stored is None:
            print('RESULT FAIL (verdict not stored)')
            return
        raw = open(stored, 'rb').read().decode('latin-1')
        ok &= '[MISMATCH: language=en] el audio est' in raw and 'Rate=2' in raw
        section = raw[raw.index('['):raw.index(']') + 1]

        # ---- 2. A1 and A2 share the file, each calling it a fake
        for i, folder in enumerate(a):
            open(os.path.join(folder, 'config', os.path.basename(stored)), 'w', encoding='utf-8', newline='\r\n').write(
                '%s\nComment=[FAKE] no es la pelicula, es otra cosa (%d)\nRate=1\n' % (section, i + 1))
            procs.append(start(folder))
        time.sleep(15)  # hashing of the shared file

        # ---- 3. B learns the sources: it has to receive the verdicts and warn
        sources = ','.join('%s:%d' % (ip, 4720 + 20 * i) for i in range(len(a)))
        send_link(hwnd, link + '|sources,%s|/' % sources)
        warned = False
        t0 = time.time()
        while time.time() - t0 < 120 and not warned:
            time.sleep(5)
            warned = 'es falso o no cumple' in log_of(w.RUN)
            rows = btuitest.rows(lv, mem)
            print('  t=%3ds warned=%s row=%s' % (time.time() - t0, warned, rows[0][:4] if rows else None))
        u.select_item(lv, mem, 0)
        time.sleep(1)
        print('screenshot:', u.screenshot(hwnd, 'shot_rate_list.png'))
        for line in log_of(w.RUN).splitlines():
            if 'Aviso' in line or 'falso' in line:
                print('  LOG ' + line[:230])
        ok &= warned

        # the dialog now tells what the others say
        u.user32.PostMessageW(lv, u.WM_COMMAND, MP_PEGASO_RATE, 0)
        time.sleep(2)
        dlg, title = rate_dialog(b.pid)
        if dlg:
            others = text_of(u.find_child(dlg, IDC_RT_OTHERS))
            print('others line:', repr(others))
            ok &= '2 falso' in others
            u.user32.PostMessageW(dlg, u.WM_COMMAND, 2, 0)
        else:
            ok = False

        # ---- 4. the download finishes: checked by the antivirus
        t0 = time.time()
        done = False
        while time.time() - t0 < 150 and not done:
            time.sleep(5)
            done = os.path.isfile(os.path.join(w.RUN, 'Incoming', NAME))
        time.sleep(6)
        log = log_of(w.RUN)
        for line in log.splitlines():
            if 'Antivirus' in line:
                print('  LOG ' + line[:230])
        print('finished: %s, content identical: %s' % (done, done and open(os.path.join(w.RUN, 'Incoming', NAME), 'rb').read() == data))
        ok &= done and 'Antivirus check passed' in log
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        if mem:
            mem.close()
        for proc in procs:
            if not w.close_gracefully(proc, 40):
                proc.kill()


if __name__ == '__main__':
    main()
