#!/usr/bin/env python3
"""A.img for AMUNOS v6.4 — boot + kernel (sectors 0..99) + FAT12 system disk.

Geometry (matches boot.asm BPB: reserved=100 sectors for boot+kernel):
  sector 0       boot.bin
  sector 1..99   kernel.bin (must stay < 99 sectors = 50688 bytes)
  sector 100..108  FAT1 (9 sectors)
  sector 109..117  FAT2 (9 sectors)
  sector 118..131  root dir (224 entries = 14 sectors)
  sector 132..     data area
"""
import struct, sys, os

path = sys.argv[1] if len(sys.argv) > 1 else 'A.img'
d = bytearray(2880 * 512)

RESV   = 100              # reserved sectors (boot + kernel)
FATSEC = 9                # sectors per FAT
ROOTENT= 224              # root directory entries
FAT1   = RESV * 512       # offset of FAT1
FAT2   = FAT1 + FATSEC * 512
ROOT   = FAT2 + FATSEC * 512              # = sector 118
DATA   = ROOT + (ROOTENT * 32)            # = sector 132

# ── boot + kernel preamble ──
with open('boot.bin', 'rb') as f:
    d[0:512] = f.read(512)
with open('kernel.bin', 'rb') as f:
    k = f.read()
if len(k) > (RESV - 1) * 512:
    raise SystemExit(f'kernel.bin too big: {len(k)} > {(RESV-1)*512}')
d[512:512 + len(k)] = k

# ── FAT reserved clusters 0/1 ──
d[FAT1:FAT1 + 3] = b'\xf0\xff\xff'

clu = 2                  # next allocatable cluster
nfiles = 0               # files added so far

def set_fat(c, val):
    """FAT12 entry c -> val (12-bit), consistent with fs.c fat12_set_cluster."""
    boff = FAT1 + (c + c // 2)
    cur = struct.unpack('<H', d[boff:boff + 2])[0]
    if c & 1:
        d[boff:boff + 2] = struct.pack('<H', (cur & 0x000F) | ((val & 0x0FFF) << 4))
    else:
        d[boff:boff + 2] = struct.pack('<H', (cur & 0xF000) | (val & 0x0FFF))

def add(name8, ext3, content, attr=0x20):
    """Add a file (multi-cluster).  Directory entry index uses the file count,
    not the cluster count, so multi-cluster files don't leave entry gaps."""
    global clu, nfiles
    C = content if isinstance(content, (bytes, bytearray)) else content.encode()
    nc = (len(C) + 511) // 512
    if nc == 0:
        nc = 1
    if clu + nc - 1 > 0xFE0:
        raise SystemExit('disk full')

    off = ROOT + nfiles * 32
    d[off:off + 8] = name8.ljust(8).encode()
    d[off + 8:off + 11] = ext3.encode()
    d[off + 11] = attr
    struct.pack_into('<H', d, off + 26, clu)
    struct.pack_into('<I', d, off + 28, len(C))

    for i in range(nc):
        doff = DATA + (clu + i - 2) * 512
        d[doff:doff + 512] = C[i * 512:(i + 1) * 512].ljust(512, b'\x00')

    for i in range(nc):
        nxt = (clu + i + 1) if i < nc - 1 else 0xFFF
        set_fat(clu + i, nxt)
    clu += nc
    nfiles += 1
    return nc

def add_file(name8, ext3, path):
    with open(path, 'rb') as f:
        return add(name8, ext3, f.read())

def add_opt_file(name8, ext3, path):
    if os.path.exists(path):
        return add_file(name8, ext3, path)
    print(f'WARN: {path} not found (skipping)')
    return 0

# ── binaries ──
add_opt_file('TCC',     'ELF', 'tcc.elf')
add_opt_file('LIBC',    'A  ', 'libc/libc.a')
add_opt_file('LIBTCC1', 'A  ', 'makar/vendor/tinycc/lib/libtcc1.a')
add_opt_file('CRT1',    'O  ', 'libc/crt1.o')
add_opt_file('CRTI',    'O  ', 'libc/crti.o')
add_opt_file('CRTN',    'O  ', 'libc/crtn.o')

# ── TCC built-in headers ──
for h in ['stdarg', 'stddef', 'stdbool', 'float', 'varargs']:
    add_opt_file(h.upper(), 'H  ', f'makar/vendor/tinycc/include/{h}.h')

# ── libc headers (flat root dir; sys/ subdir headers are host-compile only) ──
for h in ['stdio', 'stdlib', 'string', 'strings', 'ctype', 'malloc', 'syscall',
          'errno', 'fcntl', 'unistd', 'limits', 'stdint', 'inttypes', 'setjmp',
          'math', 'time', 'assert', 'dirent']:
    add_opt_file(h.upper(), 'H  ', f'libc/{h}.h')

# ── example source ──
HELLO_C = '''\
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    printf("Hello from TCC on AMUNOS!\\n");
    char *s = (char *)malloc(64);
    if (s) {
        strcpy(s, "libc: malloc+strcpy OK");
        printf("%s\\n", s);
        free(s);
    }
    printf("argc=%d\\n", argc);
    return 0;
}
'''
add('HELLO', 'C  ', HELLO_C)
add_opt_file('INP', 'C  ', 'inp.c')     # 输入测试源码 (可在 OS 内 TCC 编译)

# ── copy FAT1 -> FAT2 ──
d[FAT2:FAT2 + FATSEC * 512] = d[FAT1:FAT1 + FATSEC * 512]

with open(path, 'wb') as f:
    f.write(d)
print(f'{path} ready ({nfiles} files, {clu - 2} clusters)')
