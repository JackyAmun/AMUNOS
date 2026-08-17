#!/usr/bin/env python3
r"""C.img for AMUNOS v6.5.1 — 第三数据盘 (次通道 -hdc, 内核盘符 C:)

Tree:
  C:\
  ├─ HELLO.ELF                    (根: 按名运行; 演示次通道可执行程序)
  ├─ USR\SRC\DEMO.C
  └─ CMDS.BIN                     (EDIT → \EDIT.ELF 相对映射, 全盘搜索命中)
"""
import struct, sys, os

path = sys.argv[1] if len(sys.argv) > 1 else 'C.img'
d = bytearray(2880 * 512)

# ── BPB (保留 1 扇区: 无内核; 与 B.img 几何一致) ──
d[0:3] = b'\xeb\x3c\x90'; d[3:11] = b'AMUNOS  '
struct.pack_into('<H', d, 11, 512); d[13] = 1; struct.pack_into('<H', d, 14, 1)
d[16] = 2; struct.pack_into('<H', d, 17, 224); struct.pack_into('<H', d, 19, 2880)
d[21] = 0xF0; struct.pack_into('<H', d, 22, 9)
struct.pack_into('<H', d, 24, 18); struct.pack_into('<H', d, 26, 2)
d[510] = 0x55; d[511] = 0xAA
# 保留簇 0/1 (media descriptor 0xF0 + reserved)
d[512:515] = b'\xf0\xff\xff'

ROOT = 19 * 512          # 根目录 LBA 19
DATA = 33 * 512          # 数据区 LBA 33

clu = 2                  # 下一个可分配簇
all_dirs = []            # 非根目录 (供最终落盘)

def set_fat(c, val):
    """FAT12 条目 c -> val (12 位), 与 fs.c 的 fat12_set_cluster 一致"""
    boff = 512 + (c + c // 2)
    cur = struct.unpack('<H', d[boff:boff + 2])[0]
    if c & 1:
        d[boff:boff + 2] = struct.pack('<H', (cur & 0x000F) | ((val & 0x0FFF) << 4))
    else:
        d[boff:boff + 2] = struct.pack('<H', (cur & 0xF000) | (val & 0x0FFF))

def alloc_clusters(n):
    global clu
    c = clu; clu += n
    if c + n - 1 > 0xFE0:
        raise SystemExit('disk full')
    for i in range(n):
        nxt = (c + i + 1) if i < n - 1 else 0xFFF
        set_fat(c + i, nxt)
    return c

def mk_entry(name8, ext3, attr, start, size):
    e = bytearray(32)
    e[0:8] = name8.ljust(8).encode()
    e[8:11] = ext3.encode()
    e[11] = attr
    struct.pack_into('<H', e, 26, start)
    struct.pack_into('<I', e, 28, size)
    return e

class Dir:
    def __init__(self, name8, cluster):
        self.name = name8
        self.cluster = cluster          # 0 = root
        self.entries = []

def mkdir(name8, parent, cap):
    global all_dirs
    ncl = max(1, (cap * 32 + 511) // 512)
    c = alloc_clusters(ncl)
    sub = Dir(name8, c)
    sub.entries.append(mk_entry('.', '   ', 0x10, c, 0))
    sub.entries.append(mk_entry('..', '   ', 0x10, parent.cluster, 0))
    parent.entries.append(mk_entry(name8, '   ', 0x10, c, 0))
    all_dirs.append(sub)
    return sub

def add_to(parent, name8, ext3, content, attr=0x20):
    C = content if isinstance(content, (bytes, bytearray)) else content.encode()
    nc = (len(C) + 511) // 512
    if nc == 0:
        nc = 1
    c = alloc_clusters(nc)
    for i in range(nc):
        doff = DATA + (c + i - 2) * 512
        d[doff:doff + 512] = C[i * 512:(i + 1) * 512].ljust(512, b'\x00')
    parent.entries.append(mk_entry(name8, ext3, attr, c, len(C)))
    return nc

def add_file(name8, ext3, path, attr=0x20):
    with open(path, 'rb') as f:
        return add_to(root, name8, ext3, f.read(), attr)

root = Dir('', 0)
USR_SRC = mkdir('USR', root, 16)
USR_SRC = mkdir('SRC', USR_SRC, 16)

# ── 用户 ELF (根: 按名运行; 演示次通道 C: 可执行) ──
if os.path.exists('hello.elf'):
    n = add_file('HELLO', 'ELF', 'hello.elf')
    print(f'hello.elf: {n} cluster(s)')
else:
    print('WARN: hello.elf not found (run: make hello.elf)')

# ── C 样例源码 (USR\SRC\) ──
add_to(USR_SRC, 'DEMO', 'C  ',
       'int main(){printf(700);printf(800);printf(900);return 0;}\n')

# ── 命令对照表 (v6.5.1): 相对映射, cmd_custom 自动补 C: 盘符 ──
CMDS_BIN = '''\
; AMUNOS 命令→ELF 对照表 (v6.5.1) C 盘
EDIT \\EDIT.ELF
HELLO \\HELLO.ELF
'''
add_to(root, 'CMDS', 'BIN', CMDS_BIN)

# ── 落盘 ──
for i, e in enumerate(root.entries):
    d[ROOT + i * 32:ROOT + i * 32 + 32] = e
for sub in all_dirs:
    base = DATA + (sub.cluster - 2) * 512
    for i, e in enumerate(sub.entries):
        d[base + i * 32:base + i * 32 + 32] = e
# FAT1 -> FAT2
d[5120:5120 + 9 * 512] = d[512:512 + 9 * 512]

with open(path, 'wb') as f:
    f.write(d)
nfiles = sum(1 for sub in all_dirs for e in sub.entries if e[11] != 0x10) + \
         sum(1 for e in root.entries if e[11] != 0x10)
print(f'{path} ready ({nfiles} files, {clu - 2} clusters, {len(all_dirs)} dirs)')
