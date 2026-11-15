"""Publishes a release on GitHub with the installer attached.

    python tools/installer/publish_release.py <tag> [<title>]

The tag has to exist on GitHub already. The text of the release is docs/release-<version>.md
and the file is tools/installer/output/Pegaso-<version>-setup.exe. The GitHub credential is
the one git itself uses for this repository (asked from git's credential helper); it is only
held in memory and never printed.
"""
import json
import os
import subprocess
import sys
import urllib.error
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REPO = 'Draev-developer/Pegaso'
VERSION = '0.2'


def token():
    out = subprocess.run(['git', '-C', ROOT, 'credential', 'fill'], input='protocol=https\nhost=github.com\n\n',
                         capture_output=True, text=True, env=dict(os.environ, GIT_TERMINAL_PROMPT='0'), timeout=60).stdout
    for line in out.splitlines():
        if line.startswith('password='):
            return line[len('password='):]
    sys.exit('git has no stored credential for github.com')


def call(method, url, tok, data=None, content_type='application/json'):
    req = urllib.request.Request(url, data=data, method=method)
    req.add_header('Authorization', 'Bearer ' + tok)
    req.add_header('Accept', 'application/vnd.github+json')
    req.add_header('X-GitHub-Api-Version', '2022-11-28')
    req.add_header('User-Agent', 'pegaso-release-script')
    if data is not None:
        req.add_header('Content-Type', content_type)
    try:
        with urllib.request.urlopen(req, timeout=600) as resp:
            return json.loads(resp.read().decode('utf-8'))
    except urllib.error.HTTPError as e:
        sys.exit('GitHub answered %d: %s' % (e.code, e.read().decode('utf-8', 'replace')[:400]))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    tag = sys.argv[1]
    title = sys.argv[2] if len(sys.argv) > 2 else 'Pegaso ' + VERSION
    body = open(os.path.join(ROOT, 'docs', 'release-%s.md' % VERSION), encoding='utf-8').read()
    body = body.split('\n', 1)[1].lstrip()  # the first line is the title
    installer = os.path.join(ROOT, 'tools', 'installer', 'output', 'Pegaso-%s-setup.exe' % VERSION)
    data = open(installer, 'rb').read()
    tok = token()
    release = call('POST', 'https://api.github.com/repos/%s/releases' % REPO, tok,
                   json.dumps({'tag_name': tag, 'name': title, 'body': body, 'draft': False, 'prerelease': False}).encode('utf-8'))
    print('release created:', release['html_url'])
    upload = release['upload_url'].split('{')[0] + '?name=' + os.path.basename(installer)
    asset = call('POST', upload, tok, data, 'application/octet-stream')
    print('installer attached: %s (%d bytes)' % (asset['browser_download_url'], asset['size']))


if __name__ == '__main__':
    main()
