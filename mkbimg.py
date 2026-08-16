#!/usr/bin/env python3
"""B.img for AMUNOS v6.4 — C test files + ELF executable (multi-cluster)"""
import struct, sys, os

path = sys.argv[1] if len(sys.argv) > 1 else 'B.img'
d = bytearray(2880 * 512)

# ── BPB ──
d[0:3] = b'\xeb\x3c\x90'; d[3:11] = b'AMUNOS  '
struct.pack_into('<H', d, 11, 512); d[13] = 1; struct.pack_into('<H', d, 14, 1)
d[16] = 2; struct.pack_into('<H', d, 17, 224); struct.pack_into('<H', d, 19, 2880)
d[21] = 0xF0; struct.pack_into('<H', d, 22, 9)
struct.pack_into('<H', d, 24, 18); struct.pack_into('<H', d, 26, 2)
d[510] = 0x55; d[511] = 0xAA
# 保留簇 0/1 (media descriptor 0xF0 + reserved)
d[512:518] = b'\xf0\xff\xff\x0f\xf0\xff'
d[5120:5126] = b'\xf0\xff\xff\x0f\xf0\xff'

ROOT = 19 * 512          # 根目录 LBA 19
ENT = 32                 # 目录条目大小
DATA = 33 * 512          # 数据区 LBA 33
clu = 2                  # 下一个可分配簇
nfiles = 0               # 已添加的文件数

def set_fat(c, val):
    """设置 FAT12 条目 c -> val (12 位), 与 fs.c 的 fat12_set_cluster 一致"""
    boff = 512 + (c + c // 2)
    cur = struct.unpack('<H', d[boff:boff + 2])[0]
    if c & 1:
        d[boff:boff + 2] = struct.pack('<H', (cur & 0x000F) | ((val & 0x0FFF) << 4))
    else:
        d[boff:boff + 2] = struct.pack('<H', (cur & 0xF000) | (val & 0x0FFF))

def add(name8, ext3, attr, content):
    """添加文件, 支持多簇 (跨簇链)"""
    global clu, nfiles
    C = content if isinstance(content, (bytes, bytearray)) else content.encode()
    nc = (len(C) + 511) // 512
    if nc == 0:
        nc = 1
    if clu + nc - 1 > 0xFE0:
        raise SystemExit('disk full')
    # 目录条目 (每个文件占一个目录项, 用文件计数 nfiles 而非簇计数 clu --
    # 否则多簇文件之后的文件会被放到错误的条目位置, 留出空隙导致目录提前结束)
    off = ROOT + nfiles * ENT
    d[off:off + 8] = name8.ljust(8).encode()
    d[off + 8:off + 11] = ext3.encode()
    d[off + 11] = attr
    struct.pack_into('<H', d, off + 26, clu)      # start_cluster
    struct.pack_into('<I', d, off + 28, len(C))   # size
    # 数据 (跨簇, 末簇补零)
    for i in range(nc):
        doff = DATA + (clu + i - 2) * 512
        d[doff:doff + 512] = C[i * 512:(i + 1) * 512].ljust(512, b'\x00')
    # FAT 链: 簇[i] -> 簇[i+1], 末簇 -> 0xFFF (EOF)
    for i in range(nc):
        nxt = (clu + i + 1) if i < nc - 1 else 0xFFF
        set_fat(clu + i, nxt)
    clu += nc
    nfiles += 1
    return nc

def add_file(name8, ext3, path, attr=0x20):
    with open(path, 'rb') as f:
        return add(name8, ext3, attr, f.read())

# ── C 测试文件 (CC 编译器用) ──
add('HW', 'C  ', 0x20, 'int main(){printf(42);printf(123);return 0;}\n')
add('RETVAL', 'C  ', 0x20, 'int main(){return 1;}\n')
add('COUNT5', 'C  ', 0x20, 'int main(){printf(1);printf(2);printf(3);printf(4);printf(5);return 0;}\n')
add('LOOP99', 'C  ', 0x20, 'int main(){while(1){printf(99);}return 0;}\n')
add('CALC', 'C  ', 0x20, 'int main(){\n  a = input();\n  b = input();\n  printf(a + b);\n  printf(a * b);\n  return a - b;\n}\n')
add('IFDEMO', 'C  ', 0x20, 'int main(){\n  a = input();\n  if (a > 10) { printf(1); }\n  if (a <= 10) { printf(0); }\n  return a;\n}\n')
add('WHILE', 'C  ', 0x20, 'int main(){\n  a = 0;\n  while (a < 3) { printf(a); a = a + 1; }\n  return a;\n}\n')

# ── ELF 可执行文件 (多簇) ──
if os.path.exists('hello.elf'):
    n = add_file('HELLO', 'ELF', 'hello.elf')
    print(f'hello.elf: {n} cluster(s)')
else:
    print('WARN: hello.elf not found (run: make hello.elf)')

if os.path.exists('inp.elf'):
    n = add_file('INP', 'ELF', 'inp.elf')
    print(f'inp.elf: {n} cluster(s)')
else:
    print('WARN: inp.elf not found (run: make inp.elf)')

# 复制 FAT1 -> FAT2
d[5120:5120 + 9 * 512] = d[512:512 + 9 * 512]

with open(path, 'wb') as f:
    f.write(d)
print(f'B.img ready ({nfiles} files, {clu - 2} clusters)')
