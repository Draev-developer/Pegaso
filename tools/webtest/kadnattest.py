"""Low ID to low ID through Kad: a firewalled client downloads from a firewalled source.

usage: python -I kadnattest.py direct     (about two minutes)
       python -I kadnattest.py buddy      (about nine minutes: eMule waits before it looks for a buddy)

Two Pegasos, A with a file and B which wants it. Neither can be called: no server, and Kad
never tells them that their TCP port is open. The only Kad node either of them knows is
played here; no outside network is used.

direct  B asks Kad for sources and is given A as a firewalled source whose UDP port is open
        (type 6) and which says "nat" = QUIC. Stock eMule drops it. B sends A a direct
        callback request with the QUIC bit, A punches and waits, B calls with QUIC.

buddy   A finds a buddy (played here: a Kad answer, then the TCP connection A keeps with it)
        and publishes itself with it. B is given A as a firewalled source with that buddy
        (type 3). B sends the buddy a reask with an empty file hash and OP_RENDEZVOUS inside;
        the buddy passes it on over TCP as any stock buddy does and - as one which knows the
        dialect - tells B where A is. A punches towards B, B calls with QUIC.

In both cases the file has to arrive, identical.
"""
import os
import socket
import struct
import sys
import threading
import time
import zlib

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
KAD_PORT = 4672          # the pretend Kad node (UDP) and buddy (UDP and TCP)
A_PORT, B_PORT = 4720, 4700
A_HASH = bytes.fromhex('a1a2a3a4a50ea7a8a9aaabacadae6fb0')
NODE_ID = bytes.fromhex('00112233445566778899aabbccddeeff')
BUDDY_HASH = bytes.fromhex('b1b2b3b4b50eb7b8b9babbbcbdbe6fc0')


def kad(h):
    """A 128 bit number as Kad writes it: four 32 bit words, each little endian."""
    return b''.join(h[i:i + 4][::-1] for i in range(0, 16, 4))


def tag(name, kind, value):
    head = bytes([kind]) + struct.pack('<H', len(name)) + name
    if kind == 2:
        return head + struct.pack('<H', len(value)) + value
    return head + struct.pack({3: '<I', 8: '<H', 9: '<B'}[kind], value)


def read_tags(data, pos):
    tags = {}
    count = data[pos]
    pos += 1
    for _ in range(count):
        kind = data[pos]
        n = struct.unpack_from('<H', data, pos + 1)[0]
        name = data[pos + 3:pos + 3 + n]
        pos += 3 + n
        if kind == 2:
            n = struct.unpack_from('<H', data, pos)[0]
            value = data[pos + 2:pos + 2 + n]
            pos += 2 + n
        else:
            size = {3: 4, 8: 2, 9: 1, 0x0B: 8, 4: 4}[kind]
            value = int.from_bytes(data[pos:pos + size], 'little')
            pos += size
        tags[name] = value
    return tags


class Node:
    """The one Kad node A and B know. Also the buddy of A."""

    def __init__(self, ip, mode, file_hash):
        self.ip, self.mode, self.file_hash = ip, mode, file_hash
        self.log = []
        self.published = None      # tags of A's source record
        self.buddy_id = None       # as A asked for it (Kad order)
        self.buddy_conn = None     # the TCP connection of A
        self.envelope = None       # what B sent the buddy
        self.envelope_from = None
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.bind((ip, KAD_PORT))
        self.tcp = socket.socket()
        self.tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.tcp.bind((ip, KAD_PORT))
        self.tcp.listen(2)
        threading.Thread(target=self.udp_loop, daemon=True).start()

    def send(self, opcode, payload, addr):
        self.udp.sendto(bytes([0xE4, opcode]) + payload, addr)

    def source_record(self):
        ip_host = struct.unpack('>I', socket.inet_aton(self.ip))[0]
        tags = [tag(b'\xFE', 3, ip_host), tag(b'\xFD', 8, A_PORT), tag(b'\xFC', 8, A_PORT + 10)]
        if self.mode == 'direct':
            tags += [tag(b'\xFF', 9, 6), tag(b'\xF3', 9, 0x08), tag(b'nat', 9, 1)]
        else:
            tags += [tag(b'\xFF', 9, 3), tag(b'\xFB', 3, struct.unpack('<I', socket.inet_aton(self.ip))[0]),
                     tag(b'\xFA', 8, KAD_PORT), tag(b'\xF8', 2, self.buddy_id.hex().upper().encode()),
                     tag(b'\xF3', 9, 0), tag(b'nat', 9, 1)]
        return kad(A_HASH) + bytes([len(tags)]) + b''.join(tags)

    def udp_loop(self):
        while True:
            try:
                data, addr = self.udp.recvfrom(4096)
            except ConnectionResetError:
                continue
            except OSError:
                return
            try:
                self.on_datagram(data, addr)
            except Exception as e:  # noqa: BLE001
                self.log.append(('error', repr(e), data[:8].hex()))

    def on_datagram(self, data, addr):
        if len(data) < 2:
            return
        if data[0] == 0xE5:
            data = bytes([0xE4, data[1]]) + zlib.decompress(data[2:])
        op, body = data[1], data[2:]
        if data[0] == 0xC5:  # eMule client UDP
            if op == 0x94 and self.mode == 'buddy':  # OP_REASKCALLBACKUDP: B, through the buddy
                self.log.append(('envelope', addr[1], len(body)))
                self.envelope, self.envelope_from = body, addr
                self.relay(body, addr)
            else:
                self.log.append(('client udp', hex(op), addr[1]))
            return
        if data[0] != 0xE4:
            self.log.append(('other', data[:2].hex(), addr[1]))
            return
        self.log.append(('kad', hex(op), addr[1]))
        if op == 0x11:    # hello
            self.send(0x19, kad(NODE_ID) + struct.pack('<HBB', KAD_PORT, 5, 0), addr)
        elif op == 0x01:  # bootstrap
            self.send(0x09, kad(NODE_ID) + struct.pack('<HBH', KAD_PORT, 5, 0), addr)
        elif op == 0x60:  # ping
            self.send(0x61, struct.pack('<H', addr[1]), addr)
        elif op == 0x21:  # look-up: nobody is closer than us
            self.send(0x29, body[1:17] + b'\x00', addr)
        elif op == 0x34:  # sources of a file
            target = body[:16]
            records = b''
            if addr[1] == B_PORT + 10 and target == kad(self.file_hash) and (self.mode == 'direct' or self.buddy_conn is not None):
                records = self.source_record()
                self.log.append(('gave the source', self.mode))
            self.send(0x3B, kad(NODE_ID) + target + struct.pack('<H', 1 if records else 0) + records, addr)
        elif op == 0x44:  # A publishes itself as a source
            self.published = read_tags(body, 32)
            self.send(0x4B, body[:16] + b'\x01', addr)
        elif op == 0x51 and self.mode == 'buddy' and addr[1] == A_PORT + 10:  # A looks for a buddy
            self.buddy_id = body[:16]
            self.log.append(('find buddy', body[16:32].hex()))
            threading.Thread(target=self.buddy_accept, daemon=True).start()
            self.send(0x5A, body[:16] + kad(BUDDY_HASH) + struct.pack('<H', KAD_PORT), addr)

    # ---- the buddy
    def buddy_accept(self):
        conn, _ = self.tcp.accept()
        proto, op, body = ip6test.read_packet(conn)
        self.log.append(('buddy tcp', hex(op) if op is not None else None))
        # the answer to its hello: <hash 16><id 4><port 2><tag count 4><server ip 4><server port 2>
        answer = BUDDY_HASH + socket.inet_aton(self.ip) + struct.pack('<H', KAD_PORT)
        tags = [b'\x02\x01\x00\x01' + struct.pack('<H', 5) + b'Buddy',                      # name
                b'\x03\x01\x00\x11' + struct.pack('<I', 0x3C),                               # version
                b'\x03\x01\x00\xF9' + struct.pack('<I', (KAD_PORT << 16) | KAD_PORT),        # UDP ports
                b'\x03\x01\x00\xFA' + struct.pack('<I', (4 << 12) | (4 << 8) | (3 << 4)),    # misc options 1: UDP version 4
                b'\x03\x01\x00\xFE' + struct.pack('<I', 8)]                                  # misc options 2: Kad version 8
        answer += struct.pack('<I', len(tags)) + b''.join(tags) + b'\x00' * 6
        conn.sendall(ip6test.packet(0x4C, answer))
        self.buddy_conn = conn
        threading.Thread(target=self.buddy_read, args=(conn,), daemon=True).start()

    def buddy_read(self, conn):
        try:
            while True:
                proto, op, body = ip6test.read_packet(conn)
                if proto is None:
                    self.log.append(('buddy tcp closed',))
                    return
                if proto == 0xC5 and op == 0x9F:  # buddy ping
                    conn.sendall(ip6test.packet(0xA0, b'', 0xC5))
        except OSError:
            pass

    def relay(self, body, addr):
        """What a stock buddy does with a reask, and the hint of one which knows the dialect."""
        if self.buddy_conn is None or len(body) < 17:
            return
        tcp_body = socket.inet_aton(addr[0]) + struct.pack('<H', addr[1]) + body[16:]
        self.buddy_conn.sendall(ip6test.packet(0x9A, tcp_body, 0xC5))  # OP_REASKCALLBACKTCP
        if len(body) >= 66 and body[16:32] == bytes(16) and body[32] == 0xA0 and body[49] & 0x20:
            hint = b'\x01' + A_HASH + body[:16] + body[50:66] + socket.inet_aton(self.ip) + struct.pack('<H', A_PORT + 10) + b'\x00'
            self.udp.sendto(b'\xC5\xAA' + hint, addr)
            self.log.append(('hint sent',))

    def close(self):
        self.udp.close()
        self.tcp.close()


def write_nodes(folder, ip):
    """nodes.dat, version 2, with the pretend node as the only contact - an old one (Kad
    version 5), which is talked to without obfuscation."""
    ip_host = struct.unpack('>I', socket.inet_aton(ip))[0]
    contacts = [kad(NODE_ID) + struct.pack('<IHHB', ip_host, KAD_PORT, KAD_PORT, 5) + bytes(8) + b'\x01']
    # With nothing but nodes of the local network eMule runs Kad in "LAN mode", where nobody
    # counts as firewalled. Nine contacts which do not exist keep it out of that mode: eMule
    # checks one of them a minute and drops it two minutes later. Their addresses are from the
    # range set aside for benchmarks (198.18.0.0/15), which no router passes on.
    for i in range(9):
        dead_ip = struct.unpack('>I', socket.inet_aton('198.18.%d.1' % (i + 1)))[0]
        contacts.append(os.urandom(16) + struct.pack('<IHHB', dead_ip, KAD_PORT, KAD_PORT, 5) + bytes(8) + b'\x01')
    open(os.path.join(folder, 'config', 'nodes.dat'), 'wb').write(struct.pack('<III', 0, 2, len(contacts)) + b''.join(contacts))


def connect(proc):
    hwnd = u.main_window(proc.pid)
    u.user32.PostMessageW(hwnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
    u.user32.PostMessageW(hwnd, u.WM_COMMAND, nettest.MP_HM_CON, 0)
    return hwnd


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    mode = sys.argv[1] if len(sys.argv) > 1 else 'direct'
    ok = True

    def check(what, cond, detail=''):
        nonlocal ok
        print('  %-76s %s %s' % (what, 'ok' if cond else 'FAIL', '' if cond else detail))
        ok &= bool(cond)

    ip = r.lan_ip()
    data = os.urandom(SIZE)
    file_hash = bytes.fromhex(w.ed2k_hash(data))
    link = 'ed2k://|file|%s|%d|%s|/' % (NAME, SIZE, file_hash.hex())
    extra = 'CryptLayerRequested=0\nIPv6Enabled=0\n'
    a_folder = w.RUN + '-a1'
    for folder, port in ((a_folder, A_PORT), (w.RUN, B_PORT)):
        r.make_instance(folder, port, extra + ('MaxUpload=300\n' if folder == a_folder else ''))
        ini = os.path.join(folder, 'config', 'preferences.ini')
        text = open(ini).read().replace('NetworkKademlia=0\n', '')
        open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
        write_nodes(folder, ip)
    open(os.path.join(a_folder, 'config', 'preferences.dat'), 'wb').write(b'\x14' + A_HASH + bytes(111))
    open(os.path.join(a_folder, 'Incoming', NAME), 'wb').write(data)

    node = Node(ip, mode, file_hash)
    a = r.start(a_folder)
    b = None
    mem = None
    try:
        time.sleep(8)  # A hashes its file
        connect(a)
        if mode == 'buddy':
            node.send(0x60, b'', (ip, A_PORT + 10))  # a first sign of life: A counts as connected to Kad
            t0 = time.time()
            while time.time() - t0 < 560 and node.buddy_conn is None:
                time.sleep(10)
                if int(time.time() - t0) % 60 < 10:
                    print('    t=%3ds waiting for A to look for a buddy' % (time.time() - t0))
            check('A, firewalled, looked for a buddy and connected to it', node.buddy_conn is not None, node.log[-6:])
            t0 = time.time()
            while time.time() - t0 < 200 and node.published is None:
                time.sleep(5)
            check('A published itself as a firewalled source (type 3) with its buddy',
                  node.published is not None and node.published.get(b'\xFF') in (3, 5) and b'\xF8' in node.published, node.published)
            check('the record says that A takes QUIC calls ("nat" = 1)', node.published is not None and node.published.get(b'nat') == 1)
        b = r.start(w.RUN)
        hwnd = connect(b)
        time.sleep(2)
        node.send(0x60, b'', (ip, B_PORT + 10))
        lv = u.find_child(hwnd, u.IDC_DOWNLOADLIST)
        mem = u.Remote(b.pid)
        time.sleep(3)
        r.send_link(hwnd, link)
        path = os.path.join(w.RUN, 'Incoming', NAME)
        t0 = time.time()
        while time.time() - t0 < 240 and not os.path.isfile(path):
            time.sleep(5)
            rows = btuitest.rows(lv, mem)
            print('    t=%3ds %s' % (time.time() - t0, rows[0][1:6] if rows else None))
        time.sleep(3)
        print('    node:', [x for x in node.log if x[0] != 'kad'][:12])
        print('    kad opcodes seen:', sorted({x[1] for x in node.log if x[0] == 'kad'}))
        check('B asked Kad for sources and was given A, firewalled', any(x[0] == 'gave the source' for x in node.log))
        log_a, log_b = r.log_of(a_folder), r.log_of(w.RUN)
        if mode == 'buddy':
            env = node.envelope or b''
            check('B sent the buddy a reask with an empty file hash and OP_RENDEZVOUS',
                  len(env) in (66, 73) and env[16:32] == bytes(16) and env[32] == 0xA0, env.hex())
            check('... naming the buddy ID A published, its own hash, the hint option and the file',
                  len(env) >= 66 and kad(env[:16]) == node.buddy_id and env[49] & 0x20 and env[50:66] == file_hash, env.hex())
            check('A was told by its buddy', 'our buddy says somebody wants to get through to us' in log_a)
        else:
            check('A got the direct request with the QUIC bit', 'a firewalled client asks straight for a QUIC call' in log_a)
        check('B (firewalled) downloaded the file from A (firewalled)', os.path.isfile(path) and open(path, 'rb').read() == data)
        check('B called A over QUIC', 'is up (we called)' in log_b)
        check('A was called over QUIC', 'is up (the peer called)' in log_a)
        for line in (log_a + log_b).splitlines():
            if 'NAT traversal' in line or 'QUIC' in line:
                print('    log:', line.strip()[21:170])
        check('both still running', a.poll() is None and b.poll() is None)
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        if mem:
            mem.close()
        for proc in (b, a):
            if proc is not None and not w.close_gracefully(proc, 60):
                proc.kill()
        node.close()


if __name__ == '__main__':
    main()
