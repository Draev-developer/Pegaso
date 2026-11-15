"""Builds geo/ip-country.dat and geo/flags.bmp, which Pegaso uses to show country flags.

    python tools/geo/make_geo.py <dbip-country-lite-YYYY-MM.csv.gz> <folder with xx.png flags>

Sources (neither is part of this repository):
  - "IP to Country Lite" by DB-IP, https://db-ip.com/db/download/ip-to-country-lite
    Creative Commons Attribution 4.0 - attribution to DB-IP.com is required (see geo/README.txt).
  - Flag icons by Mark James, http://www.famfamfam.com - public domain, 16x11 PNG files named
    by ISO 3166-1 alpha-2 code.

ip-country.dat (little endian):
    "PGEO"  uint32 version (1)  uint32 ranges  uint32 countries
    countries x 2 bytes     ISO codes; the position is the country index and the place of
                            its flag in flags.bmp; index 0 is "unknown"
    ranges x uint32         first address of each range, ascending; a range ends where the
                            next one starts
    ranges x uint8          country index of each range
Only IPv4: eD2K and Kad know no other addresses.
"""
import csv
import gzip
import ipaddress
import os
import struct
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, 'geo')
FLAG_W, FLAG_H = 16, 11
MASK = (255, 0, 255)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    ranges = []  # (first, last, code)
    with gzip.open(sys.argv[1], 'rt', encoding='ascii', newline='') as f:
        for first, last, code in csv.reader(f):
            if ':' in first:
                continue
            ranges.append((int(ipaddress.IPv4Address(first)), int(ipaddress.IPv4Address(last)), code.upper()))
    ranges.sort()
    codes = ['ZZ'] + sorted({code for _, _, code in ranges if code != 'ZZ'})
    if len(codes) > 255:
        sys.exit('too many countries for one byte')
    index = {code: i for i, code in enumerate(codes)}

    starts, owners = [], []

    def add(first, owner):
        if owners and owners[-1] == owner:
            return  # the previous range simply goes on
        starts.append(first)
        owners.append(owner)

    expected = 0
    for first, last, code in ranges:
        if first < expected:
            sys.exit('overlapping ranges at %s' % ipaddress.IPv4Address(first))
        if first > expected:
            add(expected, 0)  # nobody's addresses
        add(first, index[code])
        expected = last + 1
    if expected <= 0xFFFFFFFF:
        add(expected, 0)

    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, 'ip-country.dat'), 'wb') as f:
        f.write(b'PGEO' + struct.pack('<III', 1, len(starts), len(codes)))
        f.write(''.join(codes).encode('ascii'))
        f.write(struct.pack('<%dI' % len(starts), *starts))
        f.write(bytes(owners))

    strip = Image.new('RGB', (FLAG_W * len(codes), FLAG_H), MASK)
    missing = []
    for i, code in enumerate(codes[1:], 1):
        path = os.path.join(sys.argv[2], code.lower() + '.png')
        if not os.path.isfile(path):
            missing.append(code)
            continue
        flag = Image.open(path).convert('RGBA')
        if flag.width > FLAG_W or flag.height > FLAG_H:  # a pixel too tall or too wide
            flag = flag.resize((min(flag.width, FLAG_W), min(flag.height, FLAG_H)), Image.LANCZOS)
        if flag.size != (FLAG_W, FLAG_H):  # a flag of another shape (Switzerland, Nepal) sits in the middle
            whole = Image.new('RGBA', (FLAG_W, FLAG_H), (0, 0, 0, 0))
            whole.paste(flag, ((FLAG_W - flag.width) // 2, (FLAG_H - flag.height) // 2))
            flag = whole
        cell = Image.new('RGB', flag.size, MASK)
        cell.paste(flag, mask=flag.split()[3].point(lambda a: 255 if a >= 128 else 0))
        # a flag never holds the mask colour itself
        px = cell.load()
        alpha = flag.split()[3].load()
        for y in range(FLAG_H):
            for x in range(FLAG_W):
                if alpha[x, y] >= 128 and px[x, y] == MASK:
                    px[x, y] = (254, 0, 254)
        strip.paste(cell, (FLAG_W * i, 0))
    strip.save(os.path.join(OUT, 'flags.bmp'))
    print('%d ranges (from %d), %d countries, without a flag: %s' % (len(starts), len(ranges), len(codes) - 1, ' '.join(missing) or '-'))
    print('ip-country.dat: %d bytes' % os.path.getsize(os.path.join(OUT, 'ip-country.dat')))


if __name__ == '__main__':
    main()
