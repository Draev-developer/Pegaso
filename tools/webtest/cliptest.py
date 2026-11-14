"""Download links copied to the clipboard, and "add download links" from the menu.

usage: python -I cliptest.py

Writes to the clipboard of the computer for about half a minute (and puts back the text
which was there). It refuses to run if the clipboard holds something other than text.

  1. a web address of a file is copied: the "add links" window opens with it;
  2. a second address is copied while the window is open: it is added to the window;
  3. ordinary text is copied: nothing happens;
  4. the window is accepted: both files are downloaded;
  5. Tools > "add download links": the window opens empty, a link is typed, it downloads.
"""
import ctypes
import os
import sys
import time
from ctypes import wintypes

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import btuitest  # noqa: E402

MP_HM_DIRECT_DOWNLOAD = 10247
IDC_ELINK = 2010
WM_SETTEXT, WM_GETTEXT = 0x000C, 0x000D
CF_UNICODETEXT = 13
TEXT_FORMATS = {1, 7, 13, 16}  # CF_TEXT, CF_OEMTEXT, CF_UNICODETEXT, CF_LOCALE
TITLE = 'Añadir enlaces de descarga'

user32, kernel32 = ctypes.windll.user32, ctypes.windll.kernel32
kernel32.GlobalAlloc.restype = wintypes.HGLOBAL
kernel32.GlobalLock.restype = ctypes.c_void_p
kernel32.GlobalLock.argtypes = [wintypes.HGLOBAL]
kernel32.GlobalUnlock.argtypes = [wintypes.HGLOBAL]
user32.GetClipboardData.restype = wintypes.HANDLE
user32.SetClipboardData.argtypes = [wintypes.UINT, wintypes.HANDLE]


def open_clipboard():
    for _ in range(20):
        if user32.OpenClipboard(None):
            return True
        time.sleep(0.1)
    return False


def clipboard_state():
    """(formats, text or None)"""
    if not open_clipboard():
        sys.exit('clipboard is busy')
    try:
        formats, fmt = set(), 0
        while True:
            fmt = user32.EnumClipboardFormats(fmt)
            if not fmt:
                break
            formats.add(fmt)
        text = None
        if CF_UNICODETEXT in formats:
            handle = user32.GetClipboardData(CF_UNICODETEXT)
            ptr = kernel32.GlobalLock(handle)
            if ptr:
                text = ctypes.wstring_at(ptr)
                kernel32.GlobalUnlock(handle)
        return formats, text
    finally:
        user32.CloseClipboard()


def set_clipboard(text):
    if not open_clipboard():
        sys.exit('clipboard is busy')
    try:
        user32.EmptyClipboard()
        if text is not None:
            data = ctypes.create_unicode_buffer(text)
            handle = kernel32.GlobalAlloc(0x0002, ctypes.sizeof(data))
            ptr = kernel32.GlobalLock(handle)
            ctypes.memmove(ptr, data, ctypes.sizeof(data))
            kernel32.GlobalUnlock(handle)
            user32.SetClipboardData(CF_UNICODETEXT, handle)
    finally:
        user32.CloseClipboard()


def links_window(pid):
    for hwnd, title in u.top_windows(pid):
        if title == TITLE:
            return hwnd
    return None


def text_of(hwnd):
    buf = ctypes.create_unicode_buffer(4096)
    u.user32.SendMessageW(hwnd, WM_GETTEXT, 4096, ctypes.addressof(buf))
    return buf.value


def main():
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    formats, saved = clipboard_state()
    # formats registered by programs (0xC000 and above) come along with copied text, e.g. the
    # editor's own description of it; only the text itself is put back afterwards
    if {f for f in formats - TEXT_FORMATS if f < 0xC000 or saved is None}:
        sys.exit('the clipboard holds something other than text (formats %s): not touching it' % sorted(formats))
    btuitest.deploy()
    w.reset_profile()
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    text = open(ini).read().replace('Language=1033', 'Language=1034')
    open(ini, 'w', encoding='ascii', newline='\r\n').write(text)
    server = w.start_server()
    set_clipboard(w.BASE + '/named')  # there before Pegaso starts: must not be taken
    pegaso = w.start_pegaso()
    ok = True
    try:
        hwnd = u.main_window(pegaso.pid)
        time.sleep(2)
        print('0. link already in the clipboard at start: window opened =', links_window(pegaso.pid) is not None)
        ok &= links_window(pegaso.pid) is None

        set_clipboard(w.BASE + '/small.bin')
        time.sleep(3)
        dlg = links_window(pegaso.pid)
        edit = u.find_child(dlg, IDC_ELINK) if dlg else None
        print('1. file address copied: window opened = %s, contents = %r' % (dlg is not None, text_of(edit).split() if edit else None))
        ok &= dlg is not None and text_of(edit).split() == [w.BASE + '/small.bin']

        if dlg:
            print('   screenshot:', u.screenshot(dlg, 'shot_links.png'))
            set_clipboard('see ' + w.BASE + '/big.bin and also ' + w.BASE + '/small.bin')
            time.sleep(2)
            print('2. more copied while it is open: contents =', text_of(edit).split())
            ok &= text_of(edit).split() == [w.BASE + '/small.bin', w.BASE + '/big.bin']
            set_clipboard('nothing to download here, http://example.invalid/page.html')
            time.sleep(2)
            print('3. ordinary text copied: contents unchanged =', len(text_of(edit).split()) == 2)
            ok &= len(text_of(edit).split()) == 2
            u.user32.PostMessageW(dlg, u.WM_COMMAND, 1, 0)
            times = w.wait_files({'small.bin': '/small.bin', 'big.bin': '/big.bin'}, 90)
            print('4. accepted: downloaded =', times)
            ok &= all(v is not None for v in times.values())

        u.user32.PostMessageW(hwnd, u.WM_COMMAND, MP_HM_DIRECT_DOWNLOAD, 0)
        time.sleep(2)
        dlg = links_window(pegaso.pid)
        print('5. menu command: window opened =', dlg is not None)
        ok &= dlg is not None
        if dlg:
            edit = u.find_child(dlg, IDC_ELINK)
            ok &= text_of(edit) == ''
            buf = ctypes.create_unicode_buffer(w.BASE + '/norange.bin')
            u.user32.SendMessageW(edit, WM_SETTEXT, 0, ctypes.addressof(buf))
            time.sleep(1)
            u.user32.PostMessageW(dlg, u.WM_COMMAND, 1, 0)
            times = w.wait_files({'norange.bin': '/norange.bin'}, 60)
            print('   typed link downloaded =', times)
            ok &= times['norange.bin'] is not None
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        set_clipboard(saved)
        if not w.close_gracefully(pegaso, 60):
            pegaso.kill()
        server.kill()


if __name__ == '__main__':
    main()
