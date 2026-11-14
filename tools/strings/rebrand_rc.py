"""Replaces the application name "eMule" by "Pegaso" in the texts of the resource scripts.

    python tools/strings/rebrand_rc.py

Covers srchybrid/emule.rc and the languages shipped with Pegaso. Texts which talk about
the eMule project, network or protocol rather than about this program are left alone
(see KEEP). Running it again changes nothing.
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, 'srchybrid')
FILES = ['emule.rc'] + [os.path.join('lang', n + '.rc') for n in
                        ('es_ES_T', 'fr_FR', 'it_IT', 'ru_RU', 'pt_PT', 'pt_BR', 'de_DE')]

# string identifiers whose text is about eMule itself: its web pages, its users, its history, its protocol
KEEP = ('IDS_HM_LINKHP', 'IDS_HM_LINKFAQ', 'IDS_HM_LINKVC', 'IDS_CMT_AIDE', 'IDS_AICH_WARNUSER', 'IDS_LD_EMULEHASH',
        'IDS_IRC_', 'IDS_BETANAG')
NAME = re.compile(r'(?<![A-Za-z])eMule(?![A-Za-z_])')
IDENT = re.compile(r'\b(ID[A-Z]_[A-Z0-9_]+)\b')


def convert(path):
    raw = open(path, 'rb').read()
    if raw[:2] in (b'\xff\xfe', b'\xfe\xff'):
        enc = 'utf-16'
    elif raw[:3] == b'\xef\xbb\xbf':
        enc = 'utf-8-sig'
    else:
        enc = 'latin-1'
    text = raw.decode(enc)
    eol = '\r\n' if '\r\n' in text else '\n'
    out = []
    current = ''
    changed = 0
    for line in text.split(eol):
        stripped = line.lstrip()
        if stripped.startswith('IDS_') or stripped.startswith('IDD_') or stripped.startswith('IDR_'):
            current = IDENT.search(stripped).group(1)
        # only quoted text, never identifiers or file names of resources
        if '"' in line and 'eMule' in line and not current.startswith(KEEP) and not re.search(r'"res\\\\|\.ico"|\.bmp"|\.gif"|\.png"|\.jpg"', line):
            head, sep, tail = line.partition('"')
            new_tail, n = NAME.subn('Pegaso', tail)
            if n:
                line = head + sep + new_tail
                changed += n
        out.append(line)
    if changed:
        data = eol.join(out).encode('utf-16-le' if enc == 'utf-16' else enc)
        if enc == 'utf-16':
            data = b'\xff\xfe' + data
        open(path, 'wb').write(data)
    return changed


if __name__ == '__main__':
    for name in FILES:
        print('%-22s %d' % (name, convert(os.path.join(SRC, name))))
