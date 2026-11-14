"""BitTorrent scenario for Pegaso against a real, legal swarm (Debian netinst image).

usage: python -I bttest.py <file.torrent> <expected sha256 of the payload> [minutes]

Steps: add the .torrent through the command line, let it download for a while, close
Pegaso, start it again and check that the download continues instead of starting over,
wait for completion and compare the SHA-256 of the result with the published one.
"""
import glob
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402

UNITS = {'Bytes': 1, 'KB': 1024, 'MB': 1024 ** 2, 'GB': 1024 ** 3}


def read_log(name):
    path = os.path.join(w.RUN, 'logs', name)
    if not os.path.isfile(path):
        return ''
    raw = open(path, 'rb').read()
    return raw.decode('utf-16', 'replace') if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else raw.decode('utf-8', 'replace')


def torrent_lines():
    text = read_log('eMule.log') + '\n' + read_log('eMule_Verbose.log')
    return sorted(l.strip() for l in text.splitlines() if 'orrent' in l)


def last_progress():
    """(done bytes, line) from the newest status line of the verbose log."""
    best = (0, '')
    for line in read_log('eMule_Verbose.log').splitlines():
        m = re.search(r'Torrent ".*": (\w[\w ]*), ([\d.]+) (Bytes|KB|MB|GB) of ', line)
        if m:
            best = (int(float(m.group(2)) * UNITS[m.group(3)]), line.strip())
    return best


def payload_bytes(name):
    """Bytes really present on disk for the payload (the file is not pre-allocated by default)."""
    path = os.path.join(w.RUN, 'Incoming', name)
    return os.path.getsize(path) if os.path.isfile(path) else 0


def main():
    torrent, expected = sys.argv[1], sys.argv[2].lower()
    minutes = float(sys.argv[3]) if len(sys.argv) > 3 else 20
    ok = True
    w.reset_profile()
    pegaso = w.start_pegaso()
    try:
        w.add_url(torrent)

        # phase 1: until at least 40 MB are done (or 4 minutes)
        t0 = time.time()
        done = 0
        while time.time() - t0 < 240:
            time.sleep(5)
            done, line = last_progress()
            if done >= 40 * 1024 ** 2:
                break
        print('phase 1: %.1f MB after %.0f s' % (done / 1048576, time.time() - t0))
        print('  ' + last_progress()[1][-170:])
        ok &= done > 0

        closed = w.close_gracefully(pegaso, 60)
        print('graceful close:', closed)
        if not closed:
            pegaso.kill()
        state = sorted(os.path.basename(p) for p in glob.glob(os.path.join(w.RUN, 'config', 'torrents', '*')))
        print('state files:', state)
        ok &= any(n.endswith('.fastresume') for n in state)
        for l in torrent_lines()[-6:]:
            print('  log: ' + l[:170])

        # phase 2: restart, the torrent must come back by itself and keep its data
        pegaso = w.start_pegaso()
        time.sleep(25)
        done2, line2 = last_progress()
        print('after restart: %.1f MB' % (done2 / 1048576))
        print('  ' + line2[-170:])
        ok &= done2 >= done * 0.9

        t0 = time.time()
        finished = False
        while time.time() - t0 < minutes * 60:
            time.sleep(10)
            if any('Torrent finished' in l for l in torrent_lines()):
                finished = True
                break
        d, line = last_progress()
        print('finished: %s after %.0f s more (last status: %s)' % (finished, time.time() - t0, line[-120:]))
        ok &= finished
        if finished:
            time.sleep(3)
            files = [p for p in glob.glob(os.path.join(w.RUN, 'Incoming', '*')) if os.path.isfile(p)]
            for p in files:
                digest = w.sha(p)
                print('  %s  %d bytes  sha256 %s' % (os.path.basename(p), os.path.getsize(p), 'matches' if digest == expected else 'DIFFERS ' + digest))
                ok &= digest == expected
            ok &= len(files) == 1
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        if pegaso.poll() is None and not w.close_gracefully(pegaso, 60):
            pegaso.kill()


if __name__ == '__main__':
    main()
