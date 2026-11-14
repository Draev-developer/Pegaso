"""First start without a configured language: Pegaso takes the language of Windows.

usage: python -I langtest.py

Starts Pegaso on a fresh test profile which names no language and reports the language
it chose (written to preferences.ini when it closes), next to what Windows says.
"""
import ctypes
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import btuitest  # noqa: E402
import nettest  # noqa: E402

SHIPPED = {0x09: 1033, 0x0A: 1034, 0x0C: 1036, 0x10: 1040, 0x19: 1049, 0x07: 1031}  # primary language -> translation


def expected(lid):
    if lid & 0x3FF == 0x16:  # Portuguese: Brazil has its own translation
        return 1046 if lid == 0x0416 else 2070
    return SHIPPED.get(lid & 0x3FF)


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ui = ctypes.windll.kernel32.GetUserDefaultUILanguage()
    fmt = ctypes.windll.kernel32.GetUserDefaultLCID() & 0xFFFF
    want = expected(ui) or expected(fmt) or 1033
    print('Windows: display language 0x%04X, regional format 0x%04X -> expected translation %d' % (ui, fmt, want))
    btuitest.deploy()
    w.reset_profile()
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('Language=1033\n', '')
    open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
    pegaso = w.start_pegaso()
    hwnd = u.main_window(pegaso.pid)
    boxes = [t for h, t in u.top_windows(pegaso.pid) if h != hwnd]
    print('extra windows at start (a "language not available" notice would be one):', boxes)
    if not w.close_gracefully(pegaso, 60):
        pegaso.kill()
    raw = open(ini, 'rb').read().decode('latin-1')
    got = re.search(r'^Language=(\d+)', raw, re.M)
    got = int(got.group(1)) if got else None
    ready = [l for l in nettest.log_text().splitlines() if 'Pegaso' in l and ('ready' in l or 'lista' in l or 'bereit' in l)]
    print('Pegaso chose language %s; log says: %s' % (got, ready[:1]))
    print('RESULT', 'PASS' if got == want and not boxes else 'FAIL')


if __name__ == '__main__':
    main()
