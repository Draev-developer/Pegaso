"""Opening the ports on the router with NAT-PMP and PCP.

usage: python -I portmaptest.py

A pretend gateway listens here on UDP 5351 and Pegaso is told to ask it ("PortMapGateway" in
preferences.ini, which also leaves the real router alone). Four gateways are played:

  natpmp   gives the ports asked for
  other    gives another port: of no use, it has to be refused and given back
  cgnat    has no public address itself
  pcp      speaks only PCP: says so to the NAT-PMP question and answers the PCP one
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
import ratetest as r  # noqa: E402

TCP, UDP = 4700, 4710
PUBLIC = '203.0.113.7'


class Gateway(threading.Thread):
    def __init__(self, ip, mode):
        super().__init__(daemon=True)
        self.mode = mode
        self.seen = []  # (protocol, 'tcp'/'udp'/'address', internal port, wanted external, lifetime)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind((ip, 5351))
        self.sock.settimeout(0.5)
        self.running = True

    def run(self):
        while self.running:
            try:
                data, addr = self.sock.recvfrom(1100)
            except socket.timeout:
                continue
            except OSError:
                break
            answer = self.answer(data)
            if answer:
                self.sock.sendto(answer, addr)

    def answer(self, data):
        epoch = int(time.time()) & 0xffffffff
        if data[0] == 0:  # NAT-PMP
            op = data[1]
            if self.mode == 'pcp':
                self.seen.append(('natpmp', 'refused', 0, 0, 0))
                return struct.pack('>BBHI', 0, 128 + op, 1, epoch)  # unsupported version
            if op == 0:
                self.seen.append(('natpmp', 'address', 0, 0, 0))
                ip = '100.64.1.2' if self.mode == 'cgnat' else PUBLIC
                return struct.pack('>BBHI', 0, 128, 0, epoch) + socket.inet_aton(ip)
            internal, external, lifetime = struct.unpack('>HHI', data[4:12])
            self.seen.append(('natpmp', 'tcp' if op == 2 else 'udp', internal, external, lifetime))
            granted = external + 1 if self.mode == 'other' and lifetime else external
            return struct.pack('>BBHIHHI', 0, 128 + op, 0, epoch, internal, granted if lifetime else 0, lifetime)
        if data[0] == 2 and len(data) >= 60 and data[1] == 1:  # PCP MAP
            lifetime = struct.unpack('>I', data[4:8])[0]
            proto = data[36]
            internal, external = struct.unpack('>HH', data[40:44])
            self.seen.append(('pcp', 'tcp' if proto == 6 else 'udp', internal, external, lifetime))
            head = struct.pack('>BBBBII', 2, 0x81, 0, 0, lifetime, epoch) + b'\0' * 12
            ext_ip = b'\0' * 10 + b'\xff\xff' + socket.inet_aton(PUBLIC)
            return head + data[24:40] + struct.pack('>HH', internal, external) + ext_ip
        return None

    def stop(self):
        self.running = False
        self.join()
        self.sock.close()


def run(mode):
    ip = r.lan_ip()
    gateway = Gateway(ip, mode)
    gateway.start()
    r.make_instance(w.RUN, TCP, 'PortMapGateway=%s\n' % ip)
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('EnableUPnP=0', 'EnableUPnP=1')
    open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
    pegaso = r.start(w.RUN)
    try:
        time.sleep(6)
        log = r.log_of(w.RUN)
        running = [s for s in gateway.seen]
    finally:
        if not w.close_gracefully(pegaso, 60):
            pegaso.kill()
        time.sleep(1)
        gateway.stop()
    after = gateway.seen[len(running):]
    lines = [line.strip()[21:] for line in log.splitlines()
             if any(word in line for word in ('NAT-PMP', 'PCP', 'CGNAT', 'UPnP'))]
    return running, after, lines


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    ok = True

    def check(what, cond):
        nonlocal ok
        print('   %-62s %s' % (what, 'ok' if cond else 'FAIL'))
        ok &= bool(cond)

    for mode in ('natpmp', 'other', 'cgnat', 'pcp'):
        running, after, lines = run(mode)
        print('== gateway "%s"' % mode)
        print('   asked while running:', running)
        print('   asked at exit:      ', after)
        for line in lines:
            print('   log:', line[:200])
        maps = [s for s in running if s[1] in ('tcp', 'udp') and s[4] > 0]
        releases = [s for s in running + after if s[1] in ('tcp', 'udp') and s[4] == 0]
        text = '\n'.join(lines)
        if mode == 'natpmp':
            check('both ports asked for with NAT-PMP, the same port outside',
                  sorted(m[:4] for m in maps) == [('natpmp', 'tcp', TCP, TCP), ('natpmp', 'udp', UDP, UDP)])
            check('reported with the public address', 'mediante NAT-PMP: TCP %d, UDP %d' % (TCP, UDP) in text and PUBLIC in text)
            check('given back at exit', sorted(x[1] for x in after if x[4] == 0) == ['tcp', 'udp'])
        elif mode == 'other':
            check('the other port is refused and the user told', 'el puerto %d en lugar del %d' % (TCP + 1, TCP) in text)
            check('not reported as open', 'Puertos abiertos' not in text)
            check('given back right away', any(x[1] == 'tcp' for x in releases))
        elif mode == 'cgnat':
            check('no mapping is asked for', not maps)
            check('the user is told about the missing public address', 'CGNAT' in text and '100.64.1.2' in text)
        else:
            check('NAT-PMP refused, then both ports asked for with PCP',
                  running[0][0] == 'natpmp' and sorted(m[:4] for m in maps) == [('pcp', 'tcp', TCP, TCP), ('pcp', 'udp', UDP, UDP)])
            check('reported', 'mediante PCP: TCP %d, UDP %d' % (TCP, UDP) in text and PUBLIC in text)
            check('given back at exit', sorted(x[1] for x in after if x[4] == 0) == ['tcp', 'udp'])
    print('RESULT', 'PASS' if ok else 'FAIL')


if __name__ == '__main__':
    main()
