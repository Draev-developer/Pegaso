"""Local HTTP test server for the Pegaso web download engine.

Serves deterministic pseudo-random files so the downloaded copy can be verified:
  /big.bin      60 MB, range requests supported
  /small.bin    3 MB, range requests supported
  /norange.bin  12 MB, Range header ignored (always 200 + whole file)
  /named        5 MB, name given by Content-Disposition ("named file.dat")
  /slow.bin     40 MB, throttled to ~300 KB/s per connection (for pause/resume tests)
Every request is logged to stdout as: METHOD path range status
"""
import hashlib
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def content(seed, size):
    out = bytearray()
    block = hashlib.sha256(seed.encode()).digest()
    while len(out) < size:
        block = hashlib.sha256(block).digest()
        out += block * 1024  # 32 KB runs: cheap to generate, still position dependent
    return bytes(out[:size])


MB = 1024 * 1024
FILES = {
    '/big.bin': dict(data=content('big', 60 * MB), ranges=True),
    '/small.bin': dict(data=content('small', 3 * MB), ranges=True),
    '/norange.bin': dict(data=content('norange', 12 * MB), ranges=False),
    '/named': dict(data=content('named', 5 * MB), ranges=True, name='named file.dat'),
    '/slow.bin': dict(data=content('slow', 40 * MB), ranges=True, rate=300 * 1024),
}
lock = threading.Lock()


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def do_GET(self):
        entry = FILES.get(self.path.split('?')[0])
        rng = self.headers.get('Range')
        if entry is None:
            self.send_response(404)
            self.send_header('Content-Length', '0')
            self.end_headers()
            self.note(rng, 404)
            return
        data = entry['data']
        start, end, status = 0, len(data) - 1, 200
        if rng and entry['ranges'] and rng.startswith('bytes='):
            a, _, b = rng[6:].partition('-')
            start = int(a)
            end = min(int(b), len(data) - 1) if b else len(data) - 1
            status = 206
        self.send_response(status)
        self.send_header('Content-Length', str(end - start + 1))
        if status == 206:
            self.send_header('Content-Range', 'bytes %d-%d/%d' % (start, end, len(data)))
        if entry['ranges']:
            self.send_header('Accept-Ranges', 'bytes')
        if 'name' in entry:
            self.send_header('Content-Disposition', 'attachment; filename="%s"' % entry['name'])
        self.end_headers()
        self.note(rng, status)
        rate = entry.get('rate')
        try:
            pos = start
            while pos <= end:
                n = min(64 * 1024, end - pos + 1)
                self.wfile.write(data[pos:pos + n])
                pos += n
                if rate:
                    time.sleep(n / rate)
        except (ConnectionError, OSError):
            pass
        with lock:
            print('SENT %s %d' % (self.path, pos - start), flush=True)

    def note(self, rng, status):
        with lock:
            print('GET %s %s %d' % (self.path, rng or '-', status), flush=True)


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'hashes':
        for path, entry in FILES.items():
            print(path, len(entry['data']), hashlib.sha256(entry['data']).hexdigest())
    else:
        ThreadingHTTPServer(('127.0.0.1', 8765), Handler).serve_forever()
