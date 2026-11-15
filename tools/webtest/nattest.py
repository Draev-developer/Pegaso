"""Low ID to low ID: two clients which cannot call each other transfer a file over QUIC.

usage: python -I nattest.py

A pretend eD2K server (played here) gives both Pegasos a low ID and says it introduces low
ID clients (login flag 0x20000, the protocol of ed2k-server 0.9.84 and later). A shares a
file; B asks for sources and is given A by its low ID. Stock eMule drops such a source
("LowID->LowID"). Here B asks the server for A (TCP 0x60), the server tells each of them the
UDP address of the other (0x61) - it knows them from the keep-alives (UDP 0x9F) - both punch,
and B calls A with QUIC. The file has to arrive, identical.
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
import ip6test  # noqa: E402

NAME = 'Video.prueba.avi'
SIZE = 8 * 1024 * 1024
SERVER_PORT = 4661


class Server:
    def __init__(self, ip, file_hash, source_port):
        self.ip = ip
        self.file_hash = file_hash
        self.source_port = source_port  # TCP port of the client which has the file
        self.sessions = {}   # low ID -> dict(conn, hash, port)
        self.udp_seen = {}   # user hash -> (ip, port)
        self.log = []
        self.lock = threading.Lock()
        self.next_id = 0x00100001
        self.tcp = socket.socket()
        self.tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.tcp.bind((ip, SERVER_PORT))
        self.tcp.listen(4)
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.bind((ip, SERVER_PORT + 4))
        threading.Thread(target=self.accept_loop, daemon=True).start()
        threading.Thread(target=self.udp_loop, daemon=True).start()

    def udp_loop(self):
        while True:
            try:
                data, addr = self.udp.recvfrom(2048)
            except OSError:
                return
            if len(data) >= 18 and data[0] == 0xE3 and data[1] == 0x9F:
                with self.lock:
                    self.udp_seen[data[2:18]] = addr
                    self.log.append(('keepalive', addr[1]))

    def accept_loop(self):
        while True:
            try:
                conn, _ = self.tcp.accept()
            except OSError:
                return
            threading.Thread(target=self.session, args=(conn,), daemon=True).start()

    def session(self, conn):
        my_id = None
        try:
            while True:
                proto, op, body = ip6test.read_packet(conn)
                if proto is None:
                    return
                if proto != 0xE3:
                    continue
                if op == 0x01:  # login
                    with self.lock:
                        my_id = self.next_id
                        self.next_id += 1
                        self.sessions[my_id] = dict(conn=conn, hash=body[:16], port=struct.unpack('<H', body[20:22])[0], login=body)
                    conn.sendall(ip6test.packet(0x40, struct.pack('<II', my_id, 0x20001)))  # a low ID; zlib + hole punch
                elif op in (0x19, 0x23) and body[:16] == self.file_hash:  # sources: the one who has the file, by its low ID
                    with self.lock:
                        others = [(i, s) for i, s in self.sessions.items() if i != my_id and s['port'] == self.source_port]
                    if others:
                        records = b''.join(struct.pack('<IH', i, s['port']) for i, s in others)
                        conn.sendall(ip6test.packet(0x42, self.file_hash + bytes([len(others)]) + records))
                        self.log.append(('sources', my_id, [i for i, _ in others]))
                elif op == 0x60:  # introduce me to that low ID
                    target, udp_port = struct.unpack('<IH', body[:6])
                    with self.lock:
                        me, peer = self.sessions.get(my_id), self.sessions.get(target)
                        mine = self.udp_seen.get(me['hash']) if me else None
                        theirs = self.udp_seen.get(peer['hash']) if peer else None
                        self.log.append(('request', my_id, target, mine, theirs))
                    if not (mine and theirs):
                        continue  # asked again in ten seconds
                    for dest, other, other_udp, other_id, role in ((me, peer, theirs, target, 0), (peer, me, mine, my_id, 1)):
                        answer = socket.inet_aton(other_udp[0]) + struct.pack('<HH', other['port'], other_udp[1]) + other['hash'] + bytes([role])
                        dest['conn'].sendall(ip6test.packet(0x61, answer + struct.pack('<I', other_id)))
        except OSError:
            pass

    def close(self):
        self.tcp.close()
        self.udp.close()


def connect(proc):
    hwnd = u.main_window(proc.pid)
    u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
    u.user32.PostMessageW(hwnd, u.WM_COMMAND, nettest.MP_HM_CON, 0)
    return hwnd


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ok = True

    def check(what, cond, detail=''):
        nonlocal ok
        print('  %-72s %s %s' % (what, 'ok' if cond else 'FAIL', '' if cond else detail))
        ok &= bool(cond)

    ip = r.lan_ip()
    data = os.urandom(SIZE)
    file_hash = bytes.fromhex(w.ed2k_hash(data))
    link = 'ed2k://|file|%s|%d|%s|/' % (NAME, SIZE, file_hash.hex())
    extra = 'CryptLayerRequested=0\nIPv6Enabled=0\n'
    a_folder = w.RUN + '-a1'
    for folder, port in ((a_folder, 4720), (w.RUN, 4700)):
        r.make_instance(folder, port, extra + ('MaxUpload=300\n' if folder == a_folder else ''))
        open(os.path.join(folder, 'config', 'staticservers.dat'), 'w', encoding='ascii').write('%s:%d,0,Servidor de prueba\n' % (ip, SERVER_PORT))
    open(os.path.join(a_folder, 'Incoming', NAME), 'wb').write(data)

    server = Server(ip, file_hash, 4720)
    a = r.start(a_folder)
    b = r.start(w.RUN)
    mem = None
    try:
        time.sleep(10)  # A hashes its file
        connect(a)
        hwnd = connect(b)
        t0 = time.time()
        while time.time() - t0 < 60 and len(server.sessions) < 2:
            time.sleep(1)
        check('both log in to the server and get a low ID', len(server.sessions) == 2)
        logins = [s['login'] for s in server.sessions.values()]
        if logins:
            tags = ip6test.tags_of(logins[0], 22)
            check('the login tells the UDP ports (tag 0xF9)', 0xF9 in tags, tags.keys())
        time.sleep(6)
        check('both sent a UDP keep-alive (0x9F), so the server knows their UDP address', len(server.udp_seen) == 2, server.udp_seen)
        lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
        mem = u.Remote(b.pid)
        r.send_link(hwnd, link)
        path = os.path.join(w.RUN, 'Incoming', NAME)
        t0 = time.time()
        while time.time() - t0 < 150 and not os.path.isfile(path):
            time.sleep(5)
            rows = btuitest.rows(lv, mem)
            print('    t=%3ds %s' % (time.time() - t0, rows[0][1:6] if rows else None))
        time.sleep(3)
        print('    server:', [x for x in server.log if x[0] != 'keepalive'][:6])
        check('B was given A as a low ID source', any(x[0] == 'sources' for x in server.log))
        check('B asked the server to be introduced to A (0x60)', any(x[0] == 'request' for x in server.log))
        check('B (low ID) downloaded the file from A (low ID)', os.path.isfile(path) and open(path, 'rb').read() == data)
        log_a, log_b = r.log_of(a_folder), r.log_of(w.RUN)
        check('B called A over QUIC', 'is up (we called)' in log_b)
        check('A was called over QUIC', 'is up (the peer called)' in log_a)
        for line in (log_a + log_b).splitlines():
            if 'NAT traversal' in line or 'QUIC' in line:
                print('    log:', line.strip()[21:160])
        check('both still running', a.poll() is None and b.poll() is None)
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        if mem:
            mem.close()
        for proc in (b, a):
            if not w.close_gracefully(proc, 60):
                proc.kill()
        server.close()


if __name__ == '__main__':
    main()
