"""The proxy of the preferences carries everything: web downloads and BitTorrent as well as eD2K.

usage: python -I proxytest.py

A SOCKS5 proxy and a web server run here. The server knows which of its visitors came through
the proxy (by the port they called from). The tracker of the torrent and one of the files have
names which only the proxy can turn into an address: reaching them at all proves that the name
was handed to the proxy and not looked up on this computer.
"""
import hashlib
import os
import select
import socket
import stat
import struct
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import ratetest as r  # noqa: E402
import btuitest  # noqa: E402
import apitest  # noqa: E402

PROXY_PORT, WEB_PORT = 1085, 8774
ONLY_PROXY = 'solo-por-el-proxy.invalid'   # a name no resolver knows
PEER = ('10.254.1.2', 6881)                # where the tracker says a peer is: nobody
asked = []          # what the proxy was asked for: (host, port)
through = set()     # the ports the proxy called the web server from
hits = []           # what the web server was asked: (path, port of the caller, query, headers)
lock = threading.Lock()


def bencode(v):
    if isinstance(v, int):
        return b'i%de' % v
    if isinstance(v, bytes):
        return b'%d:%s' % (len(v), v)
    if isinstance(v, str):
        return bencode(v.encode('utf-8'))
    if isinstance(v, list):
        return b'l' + b''.join(bencode(x) for x in v) + b'e'
    return b'd' + b''.join(bencode(k) + bencode(v[k]) for k in sorted(v)) + b'e'


class Web(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def do_GET(self):
        url = urlparse(self.path)
        with lock:
            hits.append((url.path, self.client_address[1], parse_qs(url.query), dict(self.headers)))
        if url.path == '/announce':
            body = bencode({'interval': 1800, 'peers': socket.inet_aton(PEER[0]) + struct.pack('>H', PEER[1])})
        else:
            body = (url.path.encode() * 3000)[:50000]
        self.send_response(200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        if self.command == 'GET':
            self.wfile.write(body)

    do_HEAD = do_GET


def recv_all(s, n):
    data = b''
    while len(data) < n:
        part = s.recv(n - len(data))
        if not part:
            raise ConnectionError()
        data += part
    return data


def serve_socks(client):
    up = None
    try:
        ver, n = recv_all(client, 2)
        if ver != 5:
            with lock:
                asked.append(('not SOCKS5: version %d' % ver, 0))
            return
        recv_all(client, n)
        client.sendall(b'\x05\x00')
        ver, cmd, _, atyp = recv_all(client, 4)
        if atyp == 1:
            host = socket.inet_ntoa(recv_all(client, 4))
        elif atyp == 3:
            host = recv_all(client, recv_all(client, 1)[0]).decode('ascii', 'replace')
        else:
            host = socket.inet_ntop(socket.AF_INET6, recv_all(client, 16))
        port = struct.unpack('>H', recv_all(client, 2))[0]
        with lock:
            asked.append((host if cmd == 1 else 'command %d to %s' % (cmd, host), port))
        target = '127.0.0.1' if host == ONLY_PROXY else host
        if cmd != 1 or target != '127.0.0.1':
            client.sendall(b'\x05\x07\x00\x01' + bytes(6) if cmd != 1 else b'\x05\x04\x00\x01' + bytes(6))
            return
        up = socket.create_connection((target, port), timeout=10)
        with lock:
            through.add(up.getsockname()[1])
        client.sendall(b'\x05\x00\x00\x01' + bytes(6))
        while True:
            ready, _, _ = select.select([client, up], [], [], 60)
            if not ready:
                break
            for s in ready:
                data = s.recv(65536)
                if not data:
                    return
                (up if s is client else client).sendall(data)
    except (OSError, ConnectionError):
        pass
    finally:
        client.close()
        if up is not None:
            up.close()


def relay(client, up):
    while True:
        ready, _, _ = select.select([client, up], [], [], 60)
        if not ready:
            return
        for s in ready:
            data = s.recv(65536)
            if not data:
                return
            (up if s is client else client).sendall(data)


def serve_http(client):
    """an HTTP proxy: "GET http://host/path" is passed on, "CONNECT host:port" opens a tunnel"""
    up = None
    try:
        head = b''
        while b'\r\n\r\n' not in head:
            part = client.recv(65536)
            if not part:
                return
            head += part
        head, rest = head.split(b'\r\n\r\n', 1)
        lines = head.decode('latin-1').split('\r\n')
        method, target, _ = lines[0].split(' ', 2)
        if method == 'CONNECT':
            host, port = target.rsplit(':', 1)
            path = None
        else:
            url = urlparse(target)
            host, port = url.hostname, url.port or 80
            path = (url.path or '/') + ('?' + url.query if url.query else '')
        port = int(port)
        with lock:
            asked.append((host, port))
        if host not in (ONLY_PROXY, '127.0.0.1'):
            client.sendall(b'HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n')
            return
        up = socket.create_connection(('127.0.0.1', port), timeout=10)
        with lock:
            through.add(up.getsockname()[1])
        if path is None:
            client.sendall(b'HTTP/1.1 200 Connection established\r\n\r\n')
        else:
            kept = [line for line in lines[1:] if line.split(':')[0].lower() not in ('connection', 'proxy-connection')]
            up.sendall(('\r\n'.join(['%s %s HTTP/1.1' % (method, path)] + kept + ['Connection: close']) + '\r\n\r\n').encode('latin-1') + rest)
        relay(client, up)
    except (OSError, ConnectionError, ValueError):
        pass
    finally:
        client.close()
        if up is not None:
            up.close()


def proxy_server(serve):
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', PROXY_PORT))
    srv.listen(50)
    while True:
        try:
            client, _ = srv.accept()
        except OSError:
            return
        threading.Thread(target=serve, args=(client,), daemon=True).start()


def wipe(folder):
    for root, dirs, files in os.walk(folder, topdown=False):
        for name in files:
            os.chmod(os.path.join(root, name), stat.S_IWRITE)
            os.remove(os.path.join(root, name))
        for name in dirs:
            os.chmod(os.path.join(root, name), stat.S_IWRITE)
            os.rmdir(os.path.join(root, name))


def make_torrent(path):
    data = os.urandom(300000)
    piece = 32768
    info = {'name': 'Charla grabada.mkv', 'length': len(data), 'piece length': piece,
            'pieces': b''.join(hashlib.sha1(data[i:i + piece]).digest() for i in range(0, len(data), piece))}
    open(path, 'wb').write(bencode({'announce': 'http://%s:%d/announce' % (ONLY_PROXY, WEB_PORT), 'info': info}))


def run(kind):
    """kind: 'socks5' or 'http' - the proxy of the preferences"""
    global PROXY_PORT
    print('---- the proxy is', kind)
    PROXY_PORT += 1
    del asked[:], hits[:]
    through.clear()
    ok = True

    def check(what, cond, detail=''):
        nonlocal ok
        print('  %-74s %s %s' % (what, 'ok' if cond else 'FAIL', '' if cond else detail))
        ok &= bool(cond)

    btuitest.deploy()
    for sub in ('config', 'Incoming', 'Temp'):
        wipe(os.path.join(w.RUN, sub))
    r.make_instance(w.RUN, 4700, 'ApiEnabled=1\nApiPort=%d\nApiToken=%s\n[Proxy]\nProxyEnableProxy=1\nProxyType=%d\nProxyName=127.0.0.1\nProxyPort=%d\n'
                    % (apitest.PORT, apitest.TOKEN, 3 if kind == 'socks5' else 4, PROXY_PORT))
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('TorrentEnabled=0', 'TorrentEnabled=1\nTorrentPort=4720')
    open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
    torrent = os.path.join(w.RUN, 'charla.torrent')
    make_torrent(torrent)

    threading.Thread(target=proxy_server, args=(serve_socks if kind == 'socks5' else serve_http,), daemon=True).start()
    incoming = os.path.join(w.RUN, 'Incoming')
    p = r.start(w.RUN)
    try:
        u.main_window(p.pid)
        status, added = apitest.call('POST', '/api/v1/downloads', {'links': [
            'http://127.0.0.1:%d/web/por-direccion.bin' % WEB_PORT,
            'http://%s:%d/web/por-nombre.bin' % (ONLY_PROXY, WEB_PORT),
            torrent]})
        check('the downloads are taken', status == 200 and added.get('added') == 3, added)
        t0 = time.time()
        while time.time() - t0 < 60:
            time.sleep(2)
            with lock:
                done = (any(h[0] == '/announce' for h in hits) and any(a == PEER for a in asked)
                        and os.path.isfile(os.path.join(incoming, 'por-nombre.bin')))
            if done:
                break
        time.sleep(6)
        with lock:
            got_hits, got_asked, got_through = list(hits), list(asked), set(through)
        print('    the proxy was asked for:', sorted(set(got_asked)))
        print('    the web server was asked:', sorted(set((h[0], h[1] in got_through) for h in got_hits)))
        # an address of this computer itself is reached directly by Windows whatever the proxy: it never leaves it
        direct = [h[0] for h in got_hits if h[1] not in got_through and h[0] != '/web/por-direccion.bin']
        check('nothing reached the web server without the proxy', got_hits and not direct, direct)
        # Windows cannot speak SOCKS: the web requests go through the bridge of Pegaso, which can
        check('a web download arrives through the proxy, its name looked up by the proxy',
              os.path.isfile(os.path.join(incoming, 'por-nombre.bin')) and (ONLY_PROXY, WEB_PORT) in got_asked, (os.listdir(incoming), got_asked))
        status, conn = apitest.call('GET', '/api/v1/connection')
        report = conn.get('report', '') if status == 200 else ''
        print('    ' + report.replace('\r\n', '\n    ').rstrip())
        check('"Check my connection" says that everything goes through the proxy and Kad is not used',
              report.count('127.0.0.1:%d)' % PROXY_PORT) >= 3 and [line for line in report.splitlines() if 'Kad' in line and '127.0.0.1' not in line], report)
        stored = [line for line in open(ini, encoding='latin-1').read().splitlines() if line.startswith('ApiToken=')]
        check('the token of the API is no longer in clear in preferences.ini', stored and stored[0].startswith('ApiToken=dpapi:'), stored)
        announces = [h for h in got_hits if h[0] == '/announce']
        check('the tracker is asked through the proxy, by a name only the proxy knows', announces and (ONLY_PROXY, WEB_PORT) in got_asked, got_asked)
        check('the peer the tracker names is called through the proxy', PEER in got_asked, got_asked)
        if announces:
            query, headers = announces[0][2], announces[0][3]
            print('    announce:', {k: v for k, v in query.items() if k not in ('info_hash', 'peer_id')}, '| User-Agent:', repr(headers.get('User-Agent')))
            check('the announce names no address of this computer', 'ip' not in query and 'ipv4' not in query and 'ipv6' not in query, query)
        check('no SOCKS command but "connect" to a place outside was served', all(not str(a[0]).startswith('not SOCKS5') for a in got_asked), got_asked)
        # eD2K and Kad also speak UDP, which no proxy of the preferences carries: it must not leave
        udp = [line.split()[1] for line in subprocess.run(['netstat', '-ano', '-p', 'UDP'], capture_output=True, text=True).stdout.splitlines()
               if line.split()[-1:] == [str(p.pid)] and line.split()[0] == 'UDP']
        print('    UDP sockets of Pegaso:', udp)
        # the one of BitTorrent stays open for a SOCKS5 proxy which carries UDP; libtorrent sends
        # nothing from it but through the proxy (udp_socket::send refuses everything else)
        ed2k = [a for a in udp if not a.endswith(':4720')]
        check('the UDP sockets of eD2K and Kad are tied to this computer: nothing of them leaves', ed2k and all(a.startswith('127.0.0.1:') for a in ed2k), udp)
        apitest.call('POST', '/api/v1/connect')
        time.sleep(4)
        status, state = apitest.call('GET', '/api/v1/status')
        check('Kad, all UDP, is not started, and Pegaso says why', 'Kad is not started' in r.log_of(w.RUN), state)
        check('still running', p.poll() is None)
    finally:
        if not w.close_gracefully(p, 60):
            p.kill()
    return ok


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    web = ThreadingHTTPServer(('127.0.0.1', WEB_PORT), Web)
    threading.Thread(target=web.serve_forever, daemon=True).start()
    ok = run('socks5')
    ok = run('http') and ok
    print('RESULT', 'PASS' if ok else 'FAIL')


if __name__ == '__main__':
    main()
