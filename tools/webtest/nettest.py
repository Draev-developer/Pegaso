"""Fresh profile: built-in servers, and "Connect" bringing up an ed2k server and Kad.

usage: python -I nettest.py

Uses the real ed2k and Kad networks: Pegaso connects to one of its built-in servers and
fetches a Kad contact list. Nothing is downloaded.
"""
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import btuitest  # noqa: E402

MP_HM_CON = 10209
MP_HM_SRVR = 10213
IDC_SERVLIST = 2091


def log_text():
    out = []
    for name in ('eMule.log', 'eMule_Verbose.log'):
        path = os.path.join(w.RUN, 'logs', name)
        if os.path.isfile(path):
            raw = open(path, 'rb').read()
            out.append(raw.decode('utf-16', 'replace') if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else raw.decode('utf-8', 'replace'))
    return '\n'.join(out)


def main():
    btuitest.deploy()
    w.reset_profile()
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('NetworkED2K=0\n', '').replace('NetworkKademlia=0\n', '')
    open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
    for name in ('server.met', 'nodes.dat', 'addresses.dat'):
        assert not os.path.exists(os.path.join(w.RUN, 'config', name))

    pegaso = w.start_pegaso()
    hwnd = u.main_window(pegaso.pid)
    mem = u.Remote(pegaso.pid)
    ok = True
    try:
        time.sleep(2)
        servers = btuitest.rows(u.find_child(hwnd, IDC_SERVLIST), mem)
        print('servers in the list before connecting: %d' % len(servers))
        for r in servers:
            print('  ', r[:2])
        ok &= len(servers) == 9

        u.user32.PostMessageW(hwnd, u.WM_COMMAND, MP_HM_CON, 0)
        ed2k = kad_fetch = kad_up = False
        t0 = time.time()
        while time.time() - t0 < 150 and not (ed2k and kad_up):
            time.sleep(5)
            log = log_text()
            ed2k = 'Connection established on' in log or 'New client ID' in log
            kad_fetch = 'Kad contact list fetched' in log
            title = u.window_text(hwnd) if hasattr(u, 'window_text') else ''
            kad_up = 'Kad' in log and ('Kademlia connected' in log or 'Kad Network: connected' in log or 'firewalled' in log.lower() and kad_fetch)
            print('  t=%3ds  ed2k=%s  kad list fetched=%s  kad up=%s %s' % (time.time() - t0, ed2k, kad_fetch, kad_up, title))
        lines = [l for l in log_text().splitlines() if l.strip()]
        print('--- log')
        for l in lines[-45:]:
            print('  ' + l[:190])
        print('screenshot:', u.screenshot(hwnd, 'shot_net.png'))
        ok &= ed2k and kad_fetch
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        mem.close()
        if not w.close_gracefully(pegaso, 60):
            pegaso.kill()


if __name__ == '__main__':
    main()
