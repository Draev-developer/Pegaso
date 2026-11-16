"""What 0.6.2 added for the privacy of the user.

usage: python -I privtest.py

 - keys and tokens of preferences.ini are written encrypted;
 - the check for a new version asks only when it is turned on, and says what it found;
 - the files of a torrent can be saved without the folder it brings;
 - the local traces are cleared when Pegaso closes, if so set;
 - web requests are made by Pegaso itself, from the adapter it is tied to;
 - a schedule can pause everything.

Nothing leaves this computer: the web server runs here.
"""
import hashlib
import json
import os
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import quote, urlparse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import ratetest as r  # noqa: E402
import btuitest  # noqa: E402
import apitest  # noqa: E402
from proxytest import bencode, wipe  # noqa: E402

WEB_PORT = 8775
hits = []           # (path, address of the caller, its Connection header)
lock = threading.Lock()
ok = True


def check(what, cond, detail=''):
    global ok
    print('  %-78s %s %s' % (what, 'ok' if cond else 'FAIL', '' if cond else detail))
    ok &= bool(cond)


class Web(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def do_GET(self):
        url = urlparse(self.path)
        with lock:
            hits.append((url.path, self.client_address[0], self.headers.get('Connection', '')))
        if url.path == '/latest':
            body = json.dumps({'tag_name': 'v9.9.9', 'name': 'Pegaso 9.9.9'}).encode()
        else:
            body = (url.path.encode() * 3000)[:50000]
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        if self.command == 'GET':
            self.wfile.write(body)

    do_HEAD = do_GET


def adapters():
    """Connected adapters with an IPv4 address: [(name, address)]"""
    out = subprocess.run(['powershell', '-NoProfile', '-Command',
                          "Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*'"
                          " -and $_.AddressState -eq 'Preferred' } | ForEach-Object { $_.InterfaceAlias + '|' + $_.IPAddress }"],
                         capture_output=True, text=True, encoding='utf-8', errors='replace').stdout
    return [tuple(line.strip().split('|')) for line in out.splitlines() if '|' in line]


def instance(extra, sections=''):
    btuitest.deploy()
    for sub in ('config', 'Incoming', 'Temp', 'logs'):
        wipe(os.path.join(w.RUN, sub))
    r.make_instance(w.RUN, 4700, 'ApiEnabled=1\nApiPort=%d\nApiToken=%s\n%s%s' % (apitest.PORT, apitest.TOKEN, extra, sections))
    return os.path.join(w.RUN, 'config', 'preferences.ini')


def ini_value(ini, key):
    for line in open(ini, encoding='latin-1').read().splitlines():
        if line.startswith(key + '='):
            return line[len(key) + 1:]
    return None


def wait_for(cond, seconds):
    t0 = time.time()
    while time.time() - t0 < seconds:
        if cond():
            return True
        time.sleep(1)
    return cond()


def stop(p):
    if not w.close_gracefully(p, 60):
        p.kill()
        return False
    return True


def run_local():
    print('---- keys, version check, torrent without folder, traces cleared on exit')
    del hits[:]
    ini = instance('RenameKeyTmdb=clave-de-prueba\nClearTracesOnExit=1\nTorrentNoFolder=1\nPegasoUpdateCheck=1\n'
                   'UpdateCheckUrl=http://127.0.0.1:%d/latest\n' % WEB_PORT,
                   '[Proxy]\nProxyEnableProxy=0\nProxyEnablePassword=1\nProxyName=proxy.invalid\nProxyPort=1080\nProxyUser=alguien\nProxyPassword=secreto-de-prueba\n')
    text = open(ini).read().replace('TorrentEnabled=0', 'TorrentEnabled=1\nTorrentPort=4720')
    open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
    config = os.path.join(w.RUN, 'config')
    open(os.path.join(config, 'AC_SearchStrings.dat'), 'wb').write('﻿busqueda de prueba\r\notra busqueda\r\n'.encode('utf-16-le'))
    # a torrent of two files in a folder; nobody has it
    data = [os.urandom(40000), os.urandom(50000)]
    piece = 16384
    whole = b''.join(data)
    info = {'name': 'Curso de prueba', 'piece length': piece,
            'pieces': b''.join(hashlib.sha1(whole[i:i + piece]).digest() for i in range(0, len(whole), piece)),
            'files': [{'length': len(data[0]), 'path': ['Leccion 1.mkv']}, {'length': len(data[1]), 'path': ['extras', 'Leccion 2.mkv']}]}
    torrent = os.path.join(w.RUN, 'curso.torrent')
    open(torrent, 'wb').write(bencode({'announce': 'http://127.0.0.1:%d/announce' % WEB_PORT, 'info': info}))

    p = r.start(w.RUN)
    try:
        u.main_window(p.pid)
        check('the key of a video database is encrypted at startup', (ini_value(ini, 'RenameKeyTmdb') or '').startswith('dpapi:'), ini_value(ini, 'RenameKeyTmdb'))
        check('and so is the token of the API', (ini_value(ini, 'ApiToken') or '').startswith('dpapi:'), ini_value(ini, 'ApiToken'))
        status, state = apitest.call('GET', '/api/v1/status')
        check('the API still takes the token it was given', status == 200, (status, state))
        check('the version check asks where it was told to', wait_for(lambda: any(h[0] == '/latest' for h in hits), 40), hits)
        check('and says that there is a new version', wait_for(lambda: 'new version of Pegaso is available: 9.9.9' in r.log_of(w.RUN), 10), r.log_of(w.RUN)[-400:])
        status, added = apitest.call('POST', '/api/v1/downloads', {'links': [torrent]})
        check('the torrent is taken', status == 200 and added.get('added') == 1, added)
        status, conn = apitest.call('GET', '/api/v1/connection')
        report = conn.get('report', '') if status == 200 else ''
        print('    ' + report.replace('\r\n', '\n    ').rstrip())
        check('"Check my connection" names the adapter of each protocol and warns that nothing hides the address',
              report.count('(') >= 4 and 'Pegaso' in report.splitlines()[-1], report)
        time.sleep(8)
        log_dir = os.path.join(w.RUN, 'logs')
        check('there is a log on disk while Pegaso runs', any(n.endswith('.log') for n in os.listdir(log_dir)), os.listdir(log_dir))
    finally:
        check('closes', stop(p))
    time.sleep(2)
    resume = b''.join(open(os.path.join(config, 'torrents', n), 'rb').read() for n in os.listdir(os.path.join(config, 'torrents')) if n.endswith('.fastresume'))
    check('the files of the torrent are kept without its folder',
          b'12:mapped_filesl13:Leccion 1.mkv20:extras\\Leccion 2.mkve' in resume,
          resume[resume.find(b'mapped_files') - 20:resume.find(b'mapped_files') + 160])
    left = open(os.path.join(config, 'AC_SearchStrings.dat'), 'rb').read().decode('utf-16', 'replace') if os.path.isfile(os.path.join(config, 'AC_SearchStrings.dat')) else ''
    check('closing cleared the search history', 'busqueda' not in left, left)
    logs = [n for n in os.listdir(log_dir) if n.endswith('.log')] if os.path.isdir(log_dir) else []
    check('and the logs', not logs, logs)
    check('the password of the proxy is saved encrypted', (ini_value(ini, 'ProxyPassword') or '').startswith('dpapi:'), ini_value(ini, 'ProxyPassword'))
    # opened again, what was encrypted is read back: the proxy still has its password and the API its token
    p = r.start(w.RUN)
    try:
        u.main_window(p.pid)
        status, state = apitest.call('GET', '/api/v1/status')
        check('opened again, the API takes the same token', status == 200, (status, state))
    finally:
        check('closes again', stop(p))
    check('and the password of the proxy is still there, encrypted', (ini_value(ini, 'ProxyPassword') or '').startswith('dpapi:'), ini_value(ini, 'ProxyPassword'))
    # saving the preferences of eMule used to put back what the file held when Pegaso started
    check('after closing, the token and the key are still encrypted in the file',
          (ini_value(ini, 'ApiToken') or '').startswith('dpapi:') and (ini_value(ini, 'RenameKeyTmdb') or '').startswith('dpapi:'),
          (ini_value(ini, 'ApiToken'), ini_value(ini, 'RenameKeyTmdb')))


def run_off():
    print('---- the version check is off unless asked for')
    del hits[:]
    instance('UpdateCheckUrl=http://127.0.0.1:%d/latest\n' % WEB_PORT)
    p = r.start(w.RUN)
    try:
        u.main_window(p.pid)
        time.sleep(25)
        check('nobody was asked about new versions', not any(h[0] == '/latest' for h in hits), hits)
    finally:
        check('closes', stop(p))


def run_bound(found):
    print('---- web requests leave from the adapter Pegaso is tied to')
    if not found:
        print('  skipped: no connected adapter')
        return
    tied, tied_ip = found[0]
    print('    tied to "%s" (%s)' % (tied, tied_ip))
    del hits[:]
    instance('PegasoBindAdapter=%s\nPegasoKillSwitch=1\n' % quote(tied))
    p = r.start(w.RUN)
    try:
        u.main_window(p.pid)
        status, added = apitest.call('POST', '/api/v1/downloads', {'links': ['http://%s:%d/web/atado.bin' % (tied_ip, WEB_PORT)]})
        check('the download is taken', status == 200 and added.get('added') == 1, added)
        check('it arrives', wait_for(lambda: os.path.isfile(os.path.join(w.RUN, 'Incoming', 'atado.bin')), 40), r.log_of(w.RUN)[-500:])
        mine = [h for h in hits if h[0] == '/web/atado.bin']
        check('every request for it came from the address of that adapter', mine and all(h[1] == tied_ip for h in mine), mine)
        # Windows alone keeps its connections open; the bridge of Pegaso, which is the one that
        # can be tied to an adapter, asks for one connection per request
        check('and was made by the bridge of Pegaso, not by Windows on its own', mine and all(h[2].lower() == 'close' for h in mine), mine)
        status, conn = apitest.call('GET', '/api/v1/connection')
        report = conn.get('report', '') if status == 200 else ''
        print('    ' + report.replace('\r\n', '\n    ').rstrip())
        check('"Check my connection" names that adapter for the web too', report.count(tied_ip) >= 4, report)
    finally:
        check('closes', stop(p))


def run_schedule(found):
    print('---- a schedule pauses everything')
    del hits[:]
    target = '127.0.0.1'    # with a VPN connected, Windows may refuse a call to an address of its own adapters
    midnight = int(time.mktime(time.localtime()[:3] + (0, 0, 0, 0, 0, -1)))
    instance('EnableScheduler=1\n', '[Scheduler]\nCount=1\n[Schedule#0]\nTitle=Pausa de prueba\nDay=0\nEnabled=1\nStartTime=%d\nEndTime=%d\n'
             'Actions=8,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\nValues=1,,,,,,,,,,,,,,,\n'
             % (midnight, midnight + (time.localtime().tm_hour * 60 + time.localtime().tm_min + 3) * 60))   # it ends in two to three minutes
    p = r.start(w.RUN)
    try:
        u.main_window(p.pid)
        check('the schedule comes on and says so', wait_for(lambda: 'Scheduler: everything is paused' in r.log_of(w.RUN), 75), r.log_of(w.RUN)[-500:])
        status, added = apitest.call('POST', '/api/v1/downloads', {'links': ['http://%s:%d/web/en-pausa.bin' % (target, WEB_PORT)]})
        check('a download can be added meanwhile', status == 200 and added.get('added') == 1, added)
        time.sleep(15)
        check('but nothing is asked from the web', not any(h[0] == '/web/en-pausa.bin' for h in hits), hits)
        status, conn = apitest.call('GET', '/api/v1/connection')
        report = conn.get('report', '') if status == 200 else ''
        print('    ' + report.replace('\r\n', '\n    ').rstrip())
        check('"Check my connection" shows every protocol stopped', report.count('detenido') >= 4, report)
        tcp = [line.split()[1] for line in subprocess.run(['netstat', '-ano', '-p', 'TCP'], capture_output=True, text=True).stdout.splitlines()
               if line.split()[-1:] == [str(p.pid)] and 'LISTENING' not in line and 'ESCUCHANDO' not in line and not line.split()[2].startswith('127.0.0.1:')]
        check('Pegaso holds no connection to another computer', not tcp, tcp)
        check('when the time of the schedule is over, Pegaso says so', wait_for(lambda: 'Scheduler: the pause is over' in r.log_of(w.RUN), 200), r.log_of(w.RUN)[-500:])
        check('and the download which was waiting arrives', wait_for(lambda: os.path.isfile(os.path.join(w.RUN, 'Incoming', 'en-pausa.bin')), 60), r.log_of(w.RUN)[-500:])
        check('still running', p.poll() is None)
    finally:
        check('closes', stop(p))


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    web = ThreadingHTTPServer(('0.0.0.0', WEB_PORT), Web)
    threading.Thread(target=web.serve_forever, daemon=True).start()
    found = sorted(adapters(), key=lambda a: 'vpn' in a[0].lower())
    which = sys.argv[1:] or ['local', 'off', 'bound', 'schedule']
    if 'local' in which:
        run_local()
    if 'off' in which:
        run_off()
    if 'bound' in which:
        run_bound(found)
    if 'schedule' in which:
        run_schedule(found)
    print('RESULT', 'PASS' if ok else 'FAIL')


if __name__ == '__main__':
    main()
