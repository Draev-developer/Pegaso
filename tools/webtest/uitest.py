"""UI level scenarios for Pegaso web downloads: drives the real download list.

usage: python -I uitest.py ui|limit

The download is selected in the list control and the commands of its context menu
(pause, resume, cancel) are sent to it, i.e. the same code path as a user click.
"""
import ctypes
import os
import subprocess
import sys
import time
from ctypes import wintypes

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402

user32 = ctypes.WinDLL('user32', use_last_error=True)
kernel32 = ctypes.WinDLL('kernel32', use_last_error=True)
user32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, ctypes.c_size_t, ctypes.c_ssize_t]
user32.SendMessageW.restype = ctypes.c_ssize_t
user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, ctypes.c_size_t, ctypes.c_ssize_t]
kernel32.OpenProcess.restype = wintypes.HANDLE
kernel32.VirtualAllocEx.restype = ctypes.c_void_p
kernel32.VirtualAllocEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
kernel32.WriteProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
kernel32.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
kernel32.VirtualFreeEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD]

WM_COMMAND = 0x0111
LVM_GETITEMCOUNT = 0x1004
LVM_SETITEMSTATE = 0x102B
LVM_GETITEMTEXTW = 0x1073
LVM_GETHEADER = 0x101F
HDM_GETITEMCOUNT = 0x1200
LVIS_SELECTED, LVIS_FOCUSED = 2, 1
IDC_DOWNLOADLIST = 2048
MP_CANCEL, MP_PAUSE, MP_RESUME, MP_HM_TRANSFER = 10201, 10203, 10204, 10211
EnumProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


class LVITEM(ctypes.Structure):
    _fields_ = [('mask', wintypes.UINT), ('iItem', ctypes.c_int), ('iSubItem', ctypes.c_int), ('state', wintypes.UINT),
                ('stateMask', wintypes.UINT), ('pszText', ctypes.c_void_p), ('cchTextMax', ctypes.c_int),
                ('iImage', ctypes.c_int), ('lParam', ctypes.c_ssize_t), ('iIndent', ctypes.c_int),
                ('iGroupId', ctypes.c_int), ('cColumns', wintypes.UINT), ('puColumns', ctypes.c_void_p),
                ('piColFmt', ctypes.c_void_p), ('iGroup', ctypes.c_int)]


def top_windows(pid):
    found = []

    def cb(hwnd, _):
        p = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid and user32.IsWindowVisible(hwnd):
            buf = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(hwnd, buf, 256)
            found.append((hwnd, buf.value))
        return True

    user32.EnumWindows(EnumProc(cb), 0)
    return found


def main_window(pid):
    for hwnd, title in top_windows(pid):
        if title.startswith('Pegaso v'):
            return hwnd
    sys.exit('main window not found')


def find_child(parent, ctrl_id):
    found = []

    def cb(hwnd, _):
        if user32.GetDlgCtrlID(hwnd) == ctrl_id:
            found.append(hwnd)
        return True

    user32.EnumChildWindows(parent, EnumProc(cb), 0)
    return found[0] if found else None


class Remote:
    """A block of memory inside the Pegaso process, for list view messages."""

    def __init__(self, pid, size=4096):
        self.h = kernel32.OpenProcess(0x0038, False, pid)  # VM_OPERATION | VM_READ | VM_WRITE
        self.addr = kernel32.VirtualAllocEx(self.h, None, size, 0x3000, 0x04)
        if not self.addr:
            sys.exit('VirtualAllocEx failed')

    def write(self, obj, offset=0):
        kernel32.WriteProcessMemory(self.h, self.addr + offset, ctypes.byref(obj), ctypes.sizeof(obj), None)

    def read(self, size, offset=0):
        buf = ctypes.create_string_buffer(size)
        kernel32.ReadProcessMemory(self.h, self.addr + offset, buf, size, None)
        return buf.raw

    def close(self):
        kernel32.VirtualFreeEx(self.h, self.addr, 0, 0x8000)
        kernel32.CloseHandle(self.h)


def select_item(lv, mem, index):
    item = LVITEM(stateMask=LVIS_SELECTED | LVIS_FOCUSED, state=LVIS_SELECTED | LVIS_FOCUSED)
    mem.write(item)
    return user32.SendMessageW(lv, LVM_SETITEMSTATE, index, mem.addr)


def row_text(lv, mem, index):
    """The list is owner drawn; columns which keep text in the control are returned, the rest is empty."""
    header = user32.SendMessageW(lv, LVM_GETHEADER, 0, 0)
    cols = user32.SendMessageW(header, HDM_GETITEMCOUNT, 0, 0)
    out = []
    for col in range(cols):
        item = LVITEM(iSubItem=col, pszText=mem.addr + 512, cchTextMax=256)
        mem.write(item)
        n = user32.SendMessageW(lv, LVM_GETITEMTEXTW, index, mem.addr)
        out.append(mem.read(n * 2, 512).decode('utf-16-le', 'replace'))
    return out


def screenshot(hwnd, name):
    path = os.path.join(HERE, name)
    script = r'''
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class Shot { [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
public struct RECT { public int L, T, R, B; } }
"@
$h = [IntPtr]%d
$r = New-Object Shot+RECT
[void][Shot]::GetWindowRect($h, [ref]$r)
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[void][Shot]::PrintWindow($h, $dc, 2)
$g.ReleaseHdc($dc)
$bmp.Save('%s', [System.Drawing.Imaging.ImageFormat]::Png)
''' % (hwnd, path)
    subprocess.run(['powershell', '-NoProfile', '-Command', script], capture_output=True, timeout=60)
    return path


def served(path='/slow.bin'):
    """Bytes the server has finished sending plus nothing for running connections: use growth of the part file instead."""
    return w.sent_bytes(path)


def completed_bytes():
    """Size on disk is not progress (sparse file); count what the server delivered through live requests instead."""
    return os.path.getsize(os.path.join(w.RUN, 'Temp', '001.part')) if os.path.isfile(os.path.join(w.RUN, 'Temp', '001.part')) else 0


def requests_seen(path='/slow.bin'):
    return sum(1 for l in open(w.SERVER_LOG) if l.startswith('GET ' + path + ' '))


def setup(extra_prefs=''):
    w.reset_profile()
    if extra_prefs:
        ini = os.path.join(w.RUN, 'config', 'preferences.ini')
        text = open(ini).read()
        for line in extra_prefs.strip().splitlines():
            key = line.split('=')[0] + '='
            text = '\n'.join(l for l in text.splitlines() if not l.startswith(key)) + '\n' + line + '\n'
        open(ini, 'w', newline='\r\n').write(text)
    server = w.start_server()
    pegaso = w.start_pegaso()
    hwnd = main_window(pegaso.pid)
    user32.PostMessageW(hwnd, WM_COMMAND, MP_HM_TRANSFER, 0)  # show the transfers page
    time.sleep(1)
    lv = find_child(hwnd, IDC_DOWNLOADLIST)
    if not lv:
        sys.exit('download list not found')
    return server, pegaso, hwnd, lv


def scenario_ui():
    server, pegaso, hwnd, lv = setup()
    mem = Remote(pegaso.pid)
    ok = True
    try:
        w.add_url(w.BASE + '/slow.bin')
        time.sleep(5)
        count = user32.SendMessageW(lv, LVM_GETITEMCOUNT, 0, 0)
        print('items in download list:', count)
        ok &= count == 1
        print('row:', [t for t in row_text(lv, mem, 0) if t])
        print('screenshot:', screenshot(hwnd, 'shot_downloading.png'))

        # pause: connections must end and no data may flow afterwards
        select_item(lv, mem, 0)
        user32.PostMessageW(lv, WM_COMMAND, MP_PAUSE, 0)
        time.sleep(3)
        sent_a, req_a = w.sent_bytes('/slow.bin'), requests_seen()
        time.sleep(4)
        sent_b, req_b = w.sent_bytes('/slow.bin'), requests_seen()
        print('paused: served %.1f MB, during 4 s more: +%d bytes, +%d requests' % (sent_a / 1048576, sent_b - sent_a, req_b - req_a))
        ok &= sent_a > 0 and sent_b == sent_a and req_b == req_a
        print('screenshot:', screenshot(hwnd, 'shot_paused.png'))

        # resume: new requests, starting where the data ends
        user32.PostMessageW(lv, WM_COMMAND, MP_RESUME, 0)
        time.sleep(4)
        req_c = requests_seen()
        new_ranges = [l.split()[2] for l in open(w.SERVER_LOG) if l.startswith('GET /slow.bin ')][req_b:]
        print('resumed: +%d requests, e.g. %s' % (req_c - req_b, new_ranges[:3]))
        ok &= req_c > req_b and all(not r.startswith('bytes=0-') for r in new_ranges)

        # cancel: confirmation box, then the part file must be gone
        user32.PostMessageW(lv, WM_COMMAND, MP_CANCEL, 0)
        time.sleep(2)
        boxes = [h for h, t in top_windows(pegaso.pid) if h != hwnd]
        print('confirmation windows:', len(boxes))
        for box in boxes:
            user32.PostMessageW(box, WM_COMMAND, 6, 0)  # IDYES
        time.sleep(3)
        left = sorted(os.listdir(os.path.join(w.RUN, 'Temp')))
        count = user32.SendMessageW(lv, LVM_GETITEMCOUNT, 0, 0)
        print('after cancel: list items %d, Temp: %s' % (count, left))
        ok &= count == 0 and not any(n.startswith('001.part') for n in left)
        sent_d = w.sent_bytes('/slow.bin')
        time.sleep(3)
        ok &= w.sent_bytes('/slow.bin') == sent_d

        # the same address can be added again and completes
        w.add_url(w.BASE + '/slow.bin')
        times = w.wait_files({'slow.bin': '/slow.bin'}, 90)
        ok &= w.check({'slow.bin': '/slow.bin'}, times)
        w.tail_log(10)
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        mem.close()
        if not w.close_gracefully(pegaso):
            pegaso.kill()
        server.kill()


def scenario_limit():
    """Download limit of 500 KB/s against a server offering 8 x 300 KB/s."""
    server, pegaso, hwnd, lv = setup('MaxDownload=500\nDownloadCapacity=10000\nMaxUpload=100\nUploadCapacityNew=1000')
    try:
        w.add_url(w.BASE + '/slow.bin')
        time.sleep(4)
        w.close_gracefully  # noqa: B018 (kept for symmetry)
        t0 = time.time()
        a = w.sent_bytes('/slow.bin')
        time.sleep(12)
        print('screenshot:', screenshot(hwnd, 'shot_limit.png'))
        pegaso_closed = w.close_gracefully(pegaso)
        time.sleep(1)
        total = w.sent_bytes('/slow.bin')
        elapsed = time.time() - t0 + 4
        rate = total / elapsed / 1024
        print('graceful close:', pegaso_closed)
        print('served %.1f MB in about %.0f s = %.0f KB/s (limit 500, unlimited would be about 2400)' % (total / 1048576, elapsed, rate))
        print('RESULT', 'PASS' if 250 < rate < 650 else 'FAIL')
    finally:
        if pegaso.poll() is None:
            pegaso.kill()
        server.kill()


if __name__ == '__main__':
    globals()['scenario_' + sys.argv[1]]()
