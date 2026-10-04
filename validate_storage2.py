#!/usr/bin/env python3
# validate_storage2.py — v6.5.6 存储栈回归 (FDC/IDE, ATAPI+ISO9660, AHCI)
# 在 WSL 内运行: python3 validate_storage2.py
import subprocess, sys

CASES = [
    ("T1 IDE/FAT 多盘", ['--wait', '16', 'dir', 'd:', 'dir'],
     ['HZK16', 'HELLO.TXT', 'BIG.BIN']),
    ("T2 ATAPI CD-ROM + ISO9660", ['--cd', 'CD.iso', '--wait', '16',
                                   'd:', 'dir', 'type hello.txt'],
     ['HELLO.TXT', 'README.TXT', 'Hello from AMUNOS CD-ROM']),
    ("T3 AHCI SATA (q35)", ['--ahci', '--wait', '20', 'devs', 'dir'],
     ['SA0', 'AHCI SATA', 'class 0106', 'HZK16']),
    ("T4 FDC read/write", ['--floppy', '--wait', '20',
                           'devs', 'echo fdc-write-ok > fdc.txt', 'type fdc.txt'],
     ['FLOPPY 1.44M', 'RW REM', 'fdc-write-ok']),
    ("T5 MBR primary FAT16", ['--mbr', '--wait', '18', 'd:', 'dir',
                               'echo mbr-write-ok > mbr.txt', 'type mbr.txt'],
     ['HELLO.ELF', 'mbr-write-ok']),
]

def run(args):
    p = subprocess.run(['python3', 'qtest.py'] + args,
                       capture_output=True, text=True, timeout=300)
    return p.stdout + p.stderr

prep = subprocess.run(['make', 'storage-test-images'], capture_output=True, text=True)
if prep.returncode:
    print('image preparation failed')
    print(prep.stdout + prep.stderr)
    sys.exit(prep.returncode)

fails = 0
for name, args, keys in CASES:
    print('====', name)
    out = run(args)
    ok = True
    for k in keys:
        if k not in out:
            print('  MISSING:', k)
            ok = False
    if 'QEMU exited early' in out:
        print('  QEMU exited early!')
        ok = False
    print('  PASS' if ok else '  FAIL', '(%d keys)' % len(keys))
    if not ok:
        fails += 1
        print('  ---- last output ----')
        print(out[-1500:])
print('OVERALL', 'PASS' if fails == 0 else 'FAIL (%d)' % fails)
sys.exit(0 if fails == 0 else 1)
