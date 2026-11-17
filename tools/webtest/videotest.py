"""Videos published as a list of pieces (HLS, DASH): quality, language and name are chosen.

usage: python -I videotest.py

A web server of this test publishes made-up videos (their pieces are random bytes, which is
all the joining needs): an HLS one with two qualities and two languages, named by a page; one
with the sound inside; an encrypted one and a live one, which Pegaso has to refuse; a page
with no video; and a DASH one. Then the same with a stand-in for ffmpeg, to see how it is
called. Nothing leaves this computer.
"""
import ctypes
import hashlib
import os
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import quote, urlparse

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import ratetest as r  # noqa: E402
import btuitest  # noqa: E402
import apitest  # noqa: E402
import vpntest  # noqa: E402
from proxytest import wipe  # noqa: E402
from privtest import wait_for, stop  # noqa: E402
from autotest import set_text, get_text  # noqa: E402

PORT = 8779
BASE = 'http://127.0.0.1:%d' % PORT
MP_PEGASO_VIDEO = 10999
IDC_URL, IDC_LOOK, IDC_QUALITY, IDC_LANG, IDC_NAME, IDC_STATUS, IDC_GO = 5201, 5202, 5204, 5206, 5208, 5209, 5211
CB_GETCOUNT, CB_GETCURSEL, CB_GETLBTEXT, CB_SETCURSEL = 0x0146, 0x0147, 0x0148, 0x014E
BM_CLICK = 0x00F5
asked = []
ok = True


def check(what, cond, detail=''):
    global ok
    print('  %-80s %s %s' % (what, 'ok' if cond else 'FAIL', '' if cond else detail))
    ok &= bool(cond)


def piece(path):
    """What the server gives for a piece: bytes which depend on its address alone."""
    seed = hashlib.sha256(path.encode()).digest()
    return (seed * 2000)[:20000 + len(path) * 97]


def media(prefix, count, ext='ts', extra=''):
    lines = ['#EXTM3U', '#EXT-X-VERSION:3', '#EXT-X-TARGETDURATION:4'] + ([extra] if extra else [])
    for i in range(count):
        lines += ['#EXTINF:4.0,', '%s_%d.%s' % (prefix, i, ext)]
    return '\n'.join(lines)


LISTS = {
    '/pagina.html': '<html><head><title>Curso de prueba: lecci&oacute;n 1</title></head><body><div id="player"></div>'
                    '<script>var config = {"sources":[{"type":"hls","src":"http:\\/\\/127.0.0.1:%d\\/hls\\/master.m3u8?v=2"}]};</script></body></html>' % PORT,
    '/nada.html': '<html><head><title>Sin nada</title></head><body><p>Aqui no hay ningun video.</p></body></html>',
    '/hls/master.m3u8': '\n'.join([
        '#EXTM3U',
        '#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="sonido",NAME="Español",LANGUAGE="es",DEFAULT=YES,URI="audio_es.m3u8"',
        '#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID="sonido",NAME="English",LANGUAGE="en",URI="audio_en.m3u8"',
        '#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360,CODECS="avc1.4d401e,mp4a.40.2",AUDIO="sonido"',
        'v360.m3u8',
        '#EXT-X-STREAM-INF:BANDWIDTH=2500000,RESOLUTION=1280x720,CODECS="avc1.4d401f,mp4a.40.2",AUDIO="sonido"',
        'v720.m3u8']),
    '/hls/v360.m3u8': media('seg/v360', 6) + '\n#EXT-X-ENDLIST\n',
    '/hls/v720.m3u8': media('seg/v720', 6) + '\n#EXT-X-ENDLIST\n',
    '/hls/audio_es.m3u8': media('seg/a_es', 6, 'aac') + '\n#EXT-X-ENDLIST\n',
    '/hls/audio_en.m3u8': media('seg/a_en', 6, 'aac') + '\n#EXT-X-ENDLIST\n',
    '/hls/junto.m3u8': media('seg/junto', 5) + '\n#EXT-X-ENDLIST\n',
    '/hls/cifrado.m3u8': media('seg/cifrado', 3, extra='#EXT-X-KEY:METHOD=AES-128,URI="clave.bin"') + '\n#EXT-X-ENDLIST\n',
    '/hls/directo.m3u8': media('seg/directo', 3) + '\n',
    '/dash/video.mpd': '''<?xml version="1.0" encoding="UTF-8"?>
<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="static" mediaPresentationDuration="PT20S" minBufferTime="PT2S">
 <Period>
  <AdaptationSet contentType="video" mimeType="video/mp4">
   <SegmentTemplate timescale="1000" duration="4000" startNumber="1" initialization="d/$RepresentationID$/init.mp4" media="d/$RepresentationID$/$Number%03d$.m4s"/>
   <Representation id="v480" bandwidth="900000" width="854" height="480"/>
   <Representation id="v1080" bandwidth="4000000" width="1920" height="1080"/>
  </AdaptationSet>
  <AdaptationSet contentType="audio" mimeType="audio/mp4" lang="es">
   <SegmentTemplate timescale="1000" initialization="d/$RepresentationID$/init.mp4" media="d/$RepresentationID$/$Time$.m4s">
    <SegmentTimeline><S t="0" d="4000" r="4"/></SegmentTimeline>
   </SegmentTemplate>
   <Representation id="a_es" bandwidth="128000"><BaseURL>sonido/</BaseURL></Representation>
  </AdaptationSet>
  <AdaptationSet contentType="audio" mimeType="audio/mp4" lang="en">
   <SegmentTemplate timescale="1000" initialization="d/$RepresentationID$/init.mp4" media="d/$RepresentationID$/$Time$.m4s">
    <SegmentTimeline><S t="0" d="4000" r="4"/></SegmentTimeline>
   </SegmentTemplate>
   <Representation id="a_en" bandwidth="128000"><BaseURL>sonido/</BaseURL></Representation>
  </AdaptationSet>
 </Period>
</MPD>''',
}


class Web(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, *args):
        pass

    def do_GET(self):
        path = urlparse(self.path).path
        asked.append(path)
        if path in LISTS:
            body = LISTS[path].encode('utf-8')
            kind = 'text/html' if path.endswith('.html') else ('application/dash+xml' if path.endswith('.mpd') else 'application/vnd.apple.mpegurl')
        elif path.endswith(('.ts', '.aac', '.m4s', '.mp4')):
            body = piece(path)
            kind = 'application/octet-stream'
        else:
            body, kind = b'', None
        self.send_response(200 if kind else 404)
        self.send_header('Content-Type', kind or 'text/plain')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        if self.command == 'GET':
            self.wfile.write(body)

    do_HEAD = do_GET


def hls(name, count, ext='ts'):
    return b''.join(piece('/hls/seg/%s_%d.%s' % (name, i, ext)) for i in range(count))


def dash_video(rep):
    return piece('/dash/d/%s/init.mp4' % rep) + b''.join(piece('/dash/d/%s/%03d.m4s' % (rep, n)) for n in range(1, 6))


def dash_audio(rep):
    return piece('/dash/sonido/d/%s/init.mp4' % rep) + b''.join(piece('/dash/sonido/d/%s/%d.m4s' % (rep, t)) for t in range(0, 20000, 4000))


def combo(hwnd):
    texts = []
    for i in range(u.user32.SendMessageW(hwnd, CB_GETCOUNT, 0, 0)):
        buf = ctypes.create_unicode_buffer(300)
        u.user32.SendMessageW(hwnd, CB_GETLBTEXT, i, ctypes.addressof(buf))
        texts.append(buf.value)
    return texts, u.user32.SendMessageW(hwnd, CB_GETCURSEL, 0, 0)


class Window:
    def __init__(self, pegaso, main_hwnd):
        self.pegaso, self.main = pegaso, main_hwnd
        self.dlg = None

    def find(self):
        self.dlg = vpntest.prefs_window(self.pegaso.pid, self.main)
        return self.dlg

    def child(self, ctrl):
        return u.find_child(self.dlg, ctrl)

    def status(self):
        return get_text(self.child(IDC_STATUS))

    def look(self, url):
        set_text(self.child(IDC_URL), url)
        u.user32.SendMessageW(self.child(IDC_LOOK), BM_CLICK, 0, 0)
        wait_for(lambda: 'Mirando' in self.status(), 3)     # what the window said before is not the answer
        return self.looked()

    def looked(self):
        wait_for(lambda: 'Mirando' not in self.status() and self.status() and 'pulsa «Mirar»' not in self.status(), 30)
        return self.status()

    def choose(self, ctrl, index):
        box = self.child(ctrl)
        u.user32.SendMessageW(box, CB_SETCURSEL, index, 0)
        u.user32.SendMessageW(self.dlg, u.WM_COMMAND, (1 << 16) | ctrl, box)  # CBN_SELCHANGE

    def download(self, name):
        set_text(self.child(IDC_NAME), name)
        before = self.status()
        u.user32.SendMessageW(self.child(IDC_GO), BM_CLICK, 0, 0)
        wait_for(lambda: self.status() != before and (self.status().startswith('Hecho') or 'piece' in self.status() or 'HTTP' in self.status()), 120)
        return self.status()


def instance(extra=''):
    btuitest.deploy()
    for sub in ('config', 'Incoming', 'Temp', 'logs'):
        wipe(os.path.join(w.RUN, sub))
    r.make_instance(w.RUN, 4700, 'ApiEnabled=1\nApiPort=%d\nApiToken=%s\n%s' % (apitest.PORT, apitest.TOKEN, extra))


def same(name, data):
    path = os.path.join(w.RUN, 'Incoming', name)
    return os.path.isfile(path) and open(path, 'rb').read() == data


def run_plain():
    print('---- without ffmpeg')
    instance()
    incoming = os.path.join(w.RUN, 'Incoming')
    p = r.start(w.RUN)
    try:
        hwnd = u.main_window(p.pid)
        win = Window(p, hwnd)
        # ---- a page given as a link to download
        status, added = apitest.call('POST', '/api/v1/downloads', {'links': [BASE + '/pagina.html']})
        check('the address of a page is taken as a link', status == 200 and added.get('added') == 1, added)
        check('it opens the video window instead of downloading the page', wait_for(lambda: win.find() is not None, 30))
        if not win.dlg:
            return
        text = win.looked()
        print('    ' + text.replace('\r\n', ' | '))
        qualities, _ = combo(win.child(IDC_QUALITY))
        languages, chosen = combo(win.child(IDC_LANG))
        print('    qualities:', qualities, '| languages:', languages, '| name:', get_text(win.child(IDC_NAME)))
        check('the list the page names is found, with its two qualities, best first', len(qualities) == 2 and qualities[0].startswith('720p') and qualities[1].startswith('360p'), qualities)
        check('and its two languages, the one of the program chosen', len(languages) == 2 and languages[chosen].startswith('Español'), (languages, chosen))
        check('the name offered is the title of the page, as it reads', get_text(win.child(IDC_NAME)) == 'Curso de prueba lección 1', get_text(win.child(IDC_NAME)))
        check('it says that without ffmpeg the sound goes apart', 'ffmpeg' in text, text)
        win.choose(IDC_QUALITY, 1)
        win.choose(IDC_LANG, 1)
        text = win.download('Mi leccion elegida')
        print('    ' + text.replace('\r\n', ' | ').replace('\n', ' | '))
        check('the quality chosen is downloaded under the name given', same('Mi leccion elegida.ts', hls('v360', 6)), os.listdir(incoming))
        check('the language chosen is saved next to it', same('Mi leccion elegida.en.aac', hls('a_en', 6, 'aac')), os.listdir(incoming))
        check('nothing of the other quality or language was fetched', not any('v720_' in a or 'a_es_' in a for a in asked), [a for a in asked if 'seg/' in a][:6])
        # ---- one list, the sound inside
        text = win.look(BASE + '/hls/junto.m3u8')
        languages, _ = combo(win.child(IDC_LANG))
        check('a video with the sound inside offers that sound only', languages == ['(el del vídeo)'], languages)
        check('its name comes from its address', get_text(win.child(IDC_NAME)) == 'junto', get_text(win.child(IDC_NAME)))
        win.download('Todo junto')
        check('it is downloaded whole, in one file', same('Todo junto.ts', hls('junto', 5)), os.listdir(incoming))
        win.download('Todo junto')
        check('downloaded again under the same name, the first one is not written over', same('Todo junto (2).ts', hls('junto', 5)) and same('Todo junto.ts', hls('junto', 5)), os.listdir(incoming))
        # ---- what Pegaso does not take
        before = len(asked)
        text = win.look(BASE + '/hls/cifrado.m3u8')
        check('an encrypted video is refused, and said to be encrypted', 'cifrado' in text and not u.user32.IsWindowEnabled(win.child(IDC_GO)), text)
        check('neither its key nor its pieces are asked for', not any('clave' in a or 'seg/' in a for a in asked[before:]), asked[before:])
        text = win.look(BASE + '/hls/directo.m3u8')
        check('a live broadcast is refused', 'directo' in text and not u.user32.IsWindowEnabled(win.child(IDC_GO)), text)
        text = win.look(BASE + '/nada.html')
        check('a page without a video says that there is none, and why some sites are not taken', 'formato abierto' in text and 'servicio privado' in text, text)
        # ---- DASH
        text = win.look(BASE + '/dash/video.mpd')
        qualities, _ = combo(win.child(IDC_QUALITY))
        languages, chosen = combo(win.child(IDC_LANG))
        print('    qualities:', qualities, '| languages:', languages)
        check('DASH: two qualities, best first, and two languages', len(qualities) == 2 and qualities[0].startswith('1080p') and languages == ['es', 'en'] and chosen == 0, (qualities, languages))
        win.download('Curso en DASH')
        check('DASH: the best quality is downloaded, its first piece first', same('Curso en DASH.mp4', dash_video('v1080')), os.listdir(incoming))
        check('DASH: and the sound in Spanish next to it', same('Curso en DASH.es.m4a', dash_audio('a_es')), os.listdir(incoming))
        print('    screenshot:', u.screenshot(win.dlg, 'shot_video.png'))
        check('Pegaso can be used while the window is open', bool(u.user32.IsWindowEnabled(hwnd)))
        check('still running', p.poll() is None)
    finally:
        check('closes', stop(p))
    left = [n for n in os.listdir(os.path.join(w.RUN, 'Temp')) if n.startswith('pegaso-video')]
    check('no working file is left behind', not left, left)


def run_ffmpeg():
    print('---- with ffmpeg (a stand-in which joins its two inputs and writes down how it was called)')
    stub = os.path.join(w.RUN, 'ffmpeg-de-prueba.cmd')
    note = os.path.join(w.RUN, 'ffmpeg-args.txt')
    open(stub, 'w', encoding='ascii', newline='\r\n').write('\n'.join([
        '@echo off', 'setlocal', 'set "in1="', 'set "in2="', ':next', 'if "%~1"=="" goto done',
        'if "%~1"=="-i" (if not defined in1 (set "in1=%~2") else (set "in2=%~2"))', 'set "last=%~1"', 'shift /1', 'goto next', ':done',
        'echo %*> "%~dp0ffmpeg-args.txt"',
        'if defined in2 (copy /b "%in1%"+"%in2%" "%last%" >nul) else (copy /b "%in1%" "%last%" >nul)', '']))
    if os.path.exists(note):
        os.remove(note)
    instance('FfmpegPath=%s\n' % quote(stub))
    incoming = os.path.join(w.RUN, 'Incoming')
    p = r.start(w.RUN)
    try:
        hwnd = u.main_window(p.pid)
        win = Window(p, hwnd)
        u.user32.PostMessageW(hwnd, u.WM_COMMAND, MP_PEGASO_VIDEO, 0)
        check('the window opens from the Tools menu command', wait_for(lambda: win.find() is not None, 20))
        if not win.dlg:
            return
        text = win.look(BASE + '/hls/master.m3u8')
        check('with ffmpeg there, nothing is said about a file apart', 'ffmpeg' not in text and 'Encontrado' in text, text)
        text = win.download('Unido por ffmpeg')
        print('    ' + text)
        check('video and sound end up in one file', same('Unido por ffmpeg.mp4', hls('v720', 6) + hls('a_es', 6, 'aac')) and len(os.listdir(incoming)) == 1, os.listdir(incoming))
        called = open(note, encoding='latin-1').read() if os.path.isfile(note) else ''
        print('    ffmpeg was called with:', called.strip().replace(os.path.join(w.RUN, 'Temp'), '<Temp>'))
        check('ffmpeg is told to copy picture and sound as they are, not to convert them', '-c copy' in called and '-map 0:v:0' in called and '-map 1:a:0' in called, called)
    finally:
        check('closes', stop(p))
    left = [n for n in os.listdir(os.path.join(w.RUN, 'Temp')) if n.startswith('pegaso-video')]
    check('no working file is left behind', not left, left)
    for path in (stub, note):
        if os.path.exists(path):
            os.remove(path)


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    web = ThreadingHTTPServer(('127.0.0.1', PORT), Web)
    threading.Thread(target=web.serve_forever, daemon=True).start()
    which = sys.argv[1:] or ['plain', 'ffmpeg']
    if 'plain' in which:
        run_plain()
    if 'ffmpeg' in which:
        run_ffmpeg()
    print('RESULT', 'PASS' if ok else 'FAIL')


if __name__ == '__main__':
    main()
