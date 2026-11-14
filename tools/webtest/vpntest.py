"""VPN protection scenario, through the real interface.

usage: python -I vpntest.py <file.torrent> <adapter that is down> <adapter that is up>

1. Pegaso starts tied to an adapter which is not connected, kill switch on:
   a web download and a torrent are added and nothing may be transferred.
2. The options page is used to switch to the connected adapter: both downloads run,
   and the torrent engine listens on that adapter only.
3. Back to the disconnected adapter: the transfers stop again.
"""
import ctypes
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import webtest as w  # noqa: E402
import uitest as u  # noqa: E402
import btuitest  # noqa: E402
from bttest import last_progress, read_log  # noqa: E402

MP_HM_PREFS = 10217
TVM_GETNEXTITEM, TVM_SELECTITEM = 0x110A, 0x110B
CB_GETCOUNT, CB_GETLBTEXT, CB_SETCURSEL = 0x0146, 0x0148, 0x014E
IDC_PG_VPN_ADAPTER, IDC_PG_VPN_KILL = 5024, 5025


def prefs_window(pid, main):
    for hwnd, title in u.top_windows(pid):
        if hwnd != main:
            return hwnd
    return None


def choose_adapter(pegaso, main, mem, name):
    """Opens the options, picks the adapter in the VPN combo box and accepts."""
    u.user32.PostMessageW(main, u.WM_COMMAND, MP_HM_PREFS, 0)
    time.sleep(3)
    prefs = prefs_window(pegaso.pid, main)
    trees = []

    def cb(h, _):
        buf = ctypes.create_unicode_buffer(64)
        u.user32.GetClassNameW(h, buf, 64)
        if buf.value == 'SysTreeView32':
            trees.append(h)
        return True

    u.user32.EnumChildWindows(prefs, u.EnumProc(cb), 0)
    item = u.user32.SendMessageW(trees[0], TVM_GETNEXTITEM, 0, 0)
    for _ in range(7):
        item = u.user32.SendMessageW(trees[0], TVM_GETNEXTITEM, 1, item)
    u.user32.SendMessageW(trees[0], TVM_SELECTITEM, 9, item)
    time.sleep(1.5)
    combo = u.find_child(prefs, IDC_PG_VPN_ADAPTER)
    count = u.user32.SendMessageW(combo, CB_GETCOUNT, 0, 0)
    entries = []
    for i in range(count):
        buf = ctypes.create_unicode_buffer(512)  # the system marshals this message for standard controls
        u.user32.SendMessageW(combo, CB_GETLBTEXT, i, ctypes.addressof(buf))
        entries.append(buf.value)
    index = [i for i, e in enumerate(entries) if e.startswith(name + '  -  ')]
    if not index:
        sys.exit('adapter %r not offered: %s' % (name, entries))
    u.user32.SendMessageW(combo, CB_SETCURSEL, index[0], 0)
    page = u.user32.GetParent(combo)
    u.user32.SendMessageW(page, u.WM_COMMAND, (1 << 16) | IDC_PG_VPN_ADAPTER, combo)  # CBN_SELCHANGE
    shot = u.screenshot(prefs, 'shot_vpn_%d.png' % index[0])
    u.user32.PostMessageW(prefs, u.WM_COMMAND, 1, 0)  # OK
    time.sleep(2)
    return entries, shot


def guard_lines():
    text = read_log('eMule.log') + '\n' + read_log('eMule_Verbose.log')
    return [l.strip()[21:] for l in sorted(text.splitlines()) if 'VPN protection' in l or 'listening on' in l]


def main():
    torrent, down, up = sys.argv[1], sys.argv[2], sys.argv[3]
    btuitest.deploy()
    w.reset_profile()
    ini = os.path.join(w.RUN, 'config', 'preferences.ini')
    from urllib.parse import quote
    text = open(ini).read() + 'PegasoBindAdapter=%s' % quote(down) + chr(10) + 'PegasoKillSwitch=1' + chr(10)
    open(ini, 'w', encoding='ascii', newline=chr(13) + chr(10)).write(text)

    server = w.start_server()
    pegaso = w.start_pegaso()
    main_wnd = u.main_window(pegaso.pid)
    u.user32.PostMessageW(main_wnd, u.WM_COMMAND, u.MP_HM_TRANSFER, 0)
    time.sleep(1)
    lv = u.find_child(main_wnd, u.IDC_DOWNLOADLIST)
    mem = u.Remote(pegaso.pid)
    ok = True
    try:
        # 1. blocked from the start
        w.add_url(w.BASE + '/slow.bin')
        w.add_url(torrent)
        time.sleep(25)
        req = u.requests_seen()
        done = last_progress()[0]
        rows = btuitest.rows(lv, mem)
        print('1. blocked: web requests %d, torrent %.1f MB, rows %s' % (req, done / 1048576, [r[:1] + r[-5:-3] for r in rows]))
        ok &= req == 0 and done == 0
        for l in guard_lines():
            print('   log: ' + l[:150])

        # 2. switch to the connected adapter
        entries, shot = choose_adapter(pegaso, main_wnd, mem, up)
        print('   adapters offered:', entries)
        print('   screenshot:', shot)
        time.sleep(30)
        req2 = u.requests_seen()
        done2 = last_progress()[0]
        print('2. on "%s": web requests %d, torrent %.1f MB' % (up, req2, done2 / 1048576))
        ok &= req2 > 0 and done2 > 0
        listening = [l for l in guard_lines() if 'listening on' in l]
        print('   torrent engine: ' + '; '.join(sorted(set(l.split('listening on ')[1] for l in listening))))

        # 3. back to the disconnected one
        choose_adapter(pegaso, main_wnd, mem, down)
        time.sleep(35)  # the engine restarts bound to the loopback address and reloads its state
        req3, sent3, done3 = u.requests_seen(), w.sent_bytes('/slow.bin'), last_progress()[0]
        time.sleep(20)
        req4, sent4, done4 = u.requests_seen(), w.sent_bytes('/slow.bin'), last_progress()[0]
        print('3. blocked again: web +%d requests, +%d bytes; torrent %.1f -> %.1f MB' % (req4 - req3, sent4 - sent3, done3 / 1048576, done4 / 1048576))
        ok &= req4 == req3 and sent4 == sent3 and done4 - done3 < 1048576
        for l in guard_lines()[-3:]:
            print('   log: ' + l[:150])
        print('RESULT', 'PASS' if ok else 'FAIL')
    finally:
        mem.close()
        if not w.close_gracefully(pegaso, 60):
            pegaso.kill()
        server.kill()


if __name__ == '__main__':
    main()
