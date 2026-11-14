"""Does the antivirus of this computer answer through AMSI, the way Pegaso asks it?

usage: python -I amsicheck.py

Hands two buffers to AMSI, in memory only (nothing is written to disk): harmless text,
and the standard EICAR antivirus test string, which every antivirus reports on purpose.
Pegaso uses the same calls for finished downloads (PegasoScan.cpp).
"""
import ctypes
import sys

amsi = ctypes.windll.amsi
amsi.AmsiInitialize.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_void_p)]
amsi.AmsiScanBuffer.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_ulong, ctypes.c_wchar_p, ctypes.c_void_p,
                                ctypes.POINTER(ctypes.c_int)]
amsi.AmsiUninitialize.argtypes = [ctypes.c_void_p]


def scan(ctx, data, name):
    result = ctypes.c_int(0)
    hr = amsi.AmsiScanBuffer(ctx, data, len(data), name, None, ctypes.byref(result))
    return hr & 0xffffffff, result.value


def main():
    ctx = ctypes.c_void_p()
    hr = amsi.AmsiInitialize('Pegaso', ctypes.byref(ctx))
    if hr:
        sys.exit('AmsiInitialize failed: 0x%08X' % (hr & 0xffffffff))
    # the test string is put together here so that this script is not itself reported
    eicar = ('X5O!P%@AP[4' + chr(92) + 'PZX54(P^)7CC)7}$' + 'EICAR-STANDARD-' + 'ANTIVIRUS-TEST-FILE!$H+H*').encode('ascii')
    clean = scan(ctx, b'hello, this is harmless text\r\n' * 20, 'clean.txt')
    bad = scan(ctx, eicar, 'eicar.com')
    amsi.AmsiUninitialize(ctx)
    print('clean text : hr=0x%08X result=%d' % clean)
    print('EICAR test : hr=0x%08X result=%d  (32768 or more = malware)' % bad)
    ok = clean[0] == 0 and clean[1] < 32768 and bad[0] == 0 and bad[1] >= 32768
    print('RESULT', 'PASS' if ok else 'FAIL (no antivirus answering through AMSI, or it does not report the test string)')


if __name__ == '__main__':
    main()
