"""IPv6: a client with a low ID downloads from a source it only knows by its IPv6 address.

usage: python -I ip6test.py

A shares a file and listens on IPv6. B logs in to a pretend eD2K server (played here), which
gives it a low ID and, when asked for sources, one record in the IPv6 form of the published
extension: client ID 0xFFFFFFFF, the port, then the 16 bytes of the address. B has to call A
over IPv6 and download the file. Everything runs on this computer over ::1; both Pegasos are
told that this is their address ("IPv6Address"), which the extension allows for networks
where the program cannot know it.

Checked on the wire: the login of B carries CT_MOD_IP_V6; the hello B sends to a second,
pretend source carries CT_MOD_MISCOPTIONS (IPv6 bit), CT_MOD_YOUR_IP and CT_MOD_IP_V6.
"""
import os
import socket
import struct
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import btuitest  # noqa: E402
import nettest  # noqa: E402
import ratetest as r  # noqa: E402

NAME = 'Video.prueba.avi'
SIZE = 6 * 1024 * 1024
A_PORT = 4720
SERVER_PORT = 4661
PEER_PORT = 4733
LOOP6 = socket.inet_pton(socket.AF_INET6, '::1')
LOW_ID = 0x00123456


def read_packet(conn):
    head = b''
    while len(head) < 5:
        chunk = conn.recv(5 - len(head))
        if not chunk:
            return None, None, None
        head += chunk
    proto, size = head[0], struct.unpack('<I', head[1:5])[0]
    body = b''
    while len(body) < size:
        chunk = conn.recv(size - len(body))
        if not chunk:
            return None, None, None
        body += chunk
    return proto, body[0], body[1:]


def packet(opcode, payload=b'', proto=0xE3):
    return struct.pack('<BIB', proto, len(payload) + 1, opcode) + payload


class Server(threading.Thread):
    """Enough of an eD2K server: login, a low ID, an ident with the IPv6 verdict, sources."""

    def __init__(self, ip, file_hash):
        super().__init__(daemon=True)
        self.file_hash = file_hash
        self.login = None
        self.asked = 0
        self.sock = socket.socket()
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((ip, SERVER_PORT))
        self.sock.listen(4)

    def run(self):
        while True:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                return
            threading.Thread(target=self.session, args=(conn,), daemon=True).start()

    def session(self, conn):
        try:
            while True:
                proto, op, body = read_packet(conn)
                if proto is None:
                    return
                if proto != 0xE3:
                    continue
                if op == 0x01:  # OP_LOGINREQUEST
                    self.login = body
                    conn.sendall(packet(0x40, struct.pack('<II', LOW_ID, 0x4001)))  # OP_IDCHANGE: low ID, zlib + IPv6
                    # OP_SERVERIDENT with ST_IPV6_STATUS = HAVE | REACHABLE | PROBED, old tag format
                    ident = b'\x11' * 16 + socket.inet_aton('127.0.0.1') + struct.pack('<HI', SERVER_PORT, 1)
                    ident += bytes([0x03, 0x01, 0x00, 0xAB]) + struct.pack('<I', 7)
                    conn.sendall(packet(0x41, ident))
                elif op in (0x19, 0x23):  # OP_GETSOURCES / _OBFU
                    self.asked += 1
                    if body[:16] != self.file_hash:
                        continue
                    records = struct.pack('<IH', 0xFFFFFFFF, A_PORT) + LOOP6      # A, by its IPv6
                    records += struct.pack('<IH', 0xFFFFFFFF, PEER_PORT) + LOOP6  # the pretend source
                    conn.sendall(packet(0x42, self.file_hash + bytes([2]) + records))
        except OSError:
            pass


class Peer(threading.Thread):
    """A pretend source on IPv6 which only records the first thing it is told (the hello)."""

    def __init__(self):
        super().__init__(daemon=True)
        self.hello = None
        self.sock = socket.socket(socket.AF_INET6)
        self.sock.bind(('::1', PEER_PORT))
        self.sock.listen(2)

    def run(self):
        while True:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                return
            conn.settimeout(10)
            try:
                proto, op, body = read_packet(conn)
                if proto == 0xE3 and op == 0x01 and self.hello is None:
                    self.hello = body
            except OSError:
                pass
            conn.close()


def tags_of(data, offset):
    """Old format tags from the offset of the tag count; returns {name id: (type, raw value)}."""
    count = struct.unpack('<I', data[offset:offset + 4])[0]
    pos = offset + 4
    found = {}
    for _ in range(count):
        kind = data[pos]
        name_len = struct.unpack('<H', data[pos + 1:pos + 3])[0]
        name = data[pos + 3:pos + 3 + name_len]
        pos += 3 + name_len
        if kind == 0x02:
            n = struct.unpack('<H', data[pos:pos + 2])[0]
            value, pos = data[pos + 2:pos + 2 + n], pos + 2 + n
        elif kind == 0x03:
            value, pos = data[pos:pos + 4], pos + 4
        elif kind == 0x01:
            value, pos = data[pos:pos + 16], pos + 16
        elif kind == 0x08:
            value, pos = data[pos:pos + 2], pos + 2
        elif kind == 0x09:
            value, pos = data[pos:pos + 1], pos + 1
        else:
            raise ValueError('tag type %#x' % kind)
        found[name[0] if name_len == 1 else name] = (kind, value)
    return found


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ok = True

    def check(what, cond, detail=''):
        nonlocal ok
        print('  %-70s %s %s' % (what, 'ok' if cond else 'FAIL', '' if cond else detail))
        ok &= bool(cond)

    ip = r.lan_ip()
    data = os.urandom(SIZE)
    file_hash = bytes.fromhex(w.ed2k_hash(data))
    link = 'ed2k://|file|%s|%d|%s|/' % (NAME, SIZE, file_hash.hex())
    six = 'IPv6Address=::1\nIPv6AssumeReachable=1\nCryptLayerRequested=0\n'
    a_folder = w.RUN + '-a1'
    r.make_instance(a_folder, A_PORT, six)
    open(os.path.join(a_folder, 'Incoming', NAME), 'wb').write(data)
    r.make_instance(w.RUN, 4700, six)
    open(os.path.join(w.RUN, 'config', 'staticservers.dat'), 'w', encoding='ascii').write('%s:%d,0,Servidor de prueba\n' % (ip, SERVER_PORT))

    server = Server(ip, file_hash)
    server.start()
    peer = Peer()
    peer.start()
    a = r.start(a_folder)
    b = r.start(w.RUN)
    mem = None
    try:
        time.sleep(10)  # A hashes its file
        hwnd = u.main_window(b.pid)
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, nettest.MP_HM_CON, 0)
        t0 = time.time()
        while time.time() - t0 < 60 and server.login is None:
            time.sleep(1)
        check('B logs in to the server', server.login is not None)
        if server.login:
            print('    login bytes:', server.login.hex())
            tags = tags_of(server.login, 22)
            print('    login tags:', {('%#x' % k if isinstance(k, int) else k): v[1].hex() for k, v in tags.items()})
            check('the login carries CT_MOD_IP_V6 (hash type, our address)', tags.get(0xAE) == (0x01, LOOP6))
        time.sleep(4)
        lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
        mem = u.Remote(b.pid)
        r.send_link(hwnd, link)
        path = os.path.join(w.RUN, 'Incoming', NAME)
        t0 = time.time()
        while time.time() - t0 < 150 and not os.path.isfile(path):
            time.sleep(5)
            rows = btuitest.rows(lv, mem)
            print('    t=%3ds %s  asked the server: %d' % (time.time() - t0, rows[0][1:6] if rows else None, server.asked))
        time.sleep(3)
        check('the server was asked for sources', server.asked > 0)
        check('B (low ID) downloaded the file from A over IPv6', os.path.isfile(path) and open(path, 'rb').read() == data)
        log_a, log_b = r.log_of(a_folder), r.log_of(w.RUN)
        check('B took the verdict of the server on its IPv6', 'IPv6: the server reached this computer' in log_b)
        check('A saw a connection come in over IPv6', 'IPv6: a connection got in' in log_a)
        check('B has a low ID', 'Cliente es %d' % LOW_ID in log_b and LOW_ID < 16777216)
        check('the pretend source was called over IPv6 and got a hello', peer.hello is not None)
        if peer.hello:
            tags = tags_of(peer.hello, 1 + 16 + 4 + 2)
            print('    hello tags:', {('%#x' % k if isinstance(k, int) else k): v[1].hex() for k, v in tags.items()})
            check('hello: CT_MOD_MISCOPTIONS, uint32, IPv6 bit', tags.get(0xAA, (0, b''))[0] == 0x03
                  and struct.unpack('<I', tags[0xAA][1])[0] & 0x04)
            check('hello: CT_MOD_YOUR_IP is the address of the peer (::1)', tags.get(0xAD) == (0x01, LOOP6))
            check('hello: CT_MOD_IP_V6 is our address', tags.get(0xAE) == (0x01, LOOP6))
        for line in (log_a + log_b).splitlines():
            if 'IPv6' in line:
                print('    log:', line.strip()[21:150])
        check('both still running', a.poll() is None and b.poll() is None)
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        if mem:
            mem.close()
        for proc in (b, a):
            if not w.close_gracefully(proc, 60):
                proc.kill()
        server.sock.close()
        peer.sock.close()


if __name__ == '__main__':
    main()
