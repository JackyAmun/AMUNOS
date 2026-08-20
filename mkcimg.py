#!/usr/bin/env python3
r"""C.img for AMUNOS v6.5.1 — 第三数据盘 (次通道 -hdc, 内核盘符 C:)

FAT16 (32MB, 8 扇区/簇): 验证内核 FAT12/16 自动识别 + 每簇扇区数寻址。

Tree:
  C:/
  ├─ HELLO.ELF                    (根: 按名运行; 演示次通道可执行程序)
  ├─ USR/SRC/DEMO.C
  └─ CMDS.BIN                     (EDIT → /EDIT.ELF 相对映射, 全盘搜索命中)
"""
import struct, sys, os

path = sys.argv[1] if len(sys.argv) > 1 else 'C.img'

BPS = 512; SPC = 8; RES = 1; NFAT = 2; ROOT_ENTRIES = 512
TOTAL_SEC = 65536                      # 32MB
FAT_SEC = ((65536 - RES - 512 // BPS) // SPC + 2) * 2  # 粗估后精算
ROOT_SEC = ROOT_ENTRIES * 32 // BPS    # 32
DATA_SEC = TOTAL_SEC - RES - NFAT * FAT_SEC - ROOT_SEC
NCLU = DATA_SEC // SPC
FAT_SEC = ((NCLU + 2) * 2 + BPS - 1) // BPS
DATA_SEC = TOTAL_SEC - RES - NFAT * FAT_SEC - ROOT_SEC
NCLU = DATA_SEC // SPC
FAT_SEC = ((NCLU + 2) * 2 + BPS - 1) // BPS
DATA_SEC = TOTAL_SEC - RES - NFAT * FAT_SEC - ROOT_SEC
NCLU = DATA_SEC // SPC
FAT_SEC = ((NCLU + 2) * 2 + BPS - 1) // BPS

FAT_LBA = RES
ROOT_LBA = RES + NFAT * FAT_SEC
DATA_LBA = ROOT_LBA + ROOT_SEC
print(f'FAT16 C.img: total={TOTAL_SEC} spc={SPC} fat={FAT_SEC}sec '
      f'root@{ROOT_LBA} data@{DATA_LBA} clusters={NCLU}')

d = bytearray(TOTAL_SEC * BPS)

# ── BPB ──
d[0:3] = b'\xeb\x3c\x90'; d[3:11] = b'AMUNOS  '
struct.pack_into('<H', d, 11, BPS); d[13] = SPC; struct.pack_into('<H', d, 14, RES)
d[16] = NFAT; struct.pack_into('<H', d, 17, ROOT_ENTRIES)
struct.pack_into('<H', d, 19, 0)                    # >65535 扇区: 16 位置 0, 用 32 位字段
d[21] = 0xF8                                       # media 固定盘
struct.pack_into('<H', d, 22, FAT_SEC)
struct.pack_into('<H', d, 24, 63); struct.pack_into('<H', d, 26, 255)
struct.pack_into('<I', d, 28, 32)                  # hidden sectors
struct.pack_into('<I', d, 32, TOTAL_SEC)           # 32-bit total sectors (65536)
d[510] = 0x55; d[511] = 0xAA
# 保留簇 0/1
struct.pack_into('<H', d, FAT_LBA*BPS + 0, 0xFFF8)
struct.pack_into('<H', d, FAT_LBA*BPS + 2, 0xFFFF)

def set_fat(c, val):
    o = FAT_LBA*BPS + c*2
    struct.pack_into('<H', d, o, val & 0xFFFF)

clu = 2
def alloc(n):
    global clu
    c = clu; clu += n
    if c + n - 1 > NCLU + 1: raise SystemExit('disk full')
    for i in range(n):
        set_fat(c+i, (c+i+1) if i < n-1 else 0xFFFF)
    return c

def mk_entry(n8, e3, attr, start, size):
    e = bytearray(32)
    e[0:8] = n8.ljust(8).encode(); e[8:11] = e3.encode()
    e[11] = attr
    struct.pack_into('<H', e, 26, start)
    struct.pack_into('<I', e, 28, size)
    return e

class Dir:
    def __init__(self, name8, cluster):
        self.name = name8
        self.cluster = cluster          # 0 = root
        self.entries = []

def mkdir(name8, parent):
    c = alloc(1)
    sub = Dir(name8, c)
    sub.entries.append(mk_entry('.', '   ', 0x10, c, 0))
    sub.entries.append(mk_entry('..', '   ', 0x10, parent.cluster, 0))
    parent.entries.append(mk_entry(name8, '   ', 0x10, c, 0))
    return sub

def add_to(parent, name8, ext3, content, attr=0x20):
    C = content if isinstance(content, (bytes, bytearray)) else content.encode()
    nc = (len(C) + SPC*BPS - 1) // (SPC*BPS)
    if nc == 0: nc = 1
    c = alloc(nc)
    for i in range(nc):
        off = (DATA_LBA + (c + i - 2) * SPC) * BPS
        d[off:off + SPC*BPS] = C[i*SPC*BPS:(i+1)*SPC*BPS].ljust(SPC*BPS, b'\x00')
    parent.entries.append(mk_entry(name8, ext3, attr, c, len(C)))
    return c

def add_file(name8, ext3, path, attr=0x20):
    with open(path, 'rb') as f:
        return add_to(root, name8, ext3, f.read(), attr)

root = Dir('', 0)
USR = mkdir('USR', root)
USR_SRC = mkdir('SRC', USR)

# ── 用户 ELF (根: 按名运行; 演示 FAT16 次通道 C: 可执行) ──
if os.path.exists('hello.elf'):
    add_file('HELLO', 'ELF', 'hello.elf')
else:
    print('WARN: hello.elf not found (run: make hello.elf)')

# ── HZK16 汉字点阵字库: 已移至 A: (mka_img.py), C: 不放 (v6.8) ──

# ── C 样例源码 (USR/SRC/) ──
add_to(USR_SRC, 'DEMO', 'C  ',
       'int main(){printf("DEMO OK from TCC\\n");return 0;}\n')

# ── 命令对照表 (v6.5.1): 相对映射, cmd_custom 自动补 C: 盘符 ──
CMDS_BIN = '''\
; AMUNOS 命令→ELF 对照表 (v6.5.1) C 盘
EDIT /EDIT.ELF
HELLO /HELLO.ELF
'''
add_to(root, 'CMDS', 'BIN', CMDS_BIN)

# ── 落盘: 根目录 + 各子目录 ──
for i, e in enumerate(root.entries):
    d[ROOT_LBA*BPS + i*32: ROOT_LBA*BPS + i*32 + 32] = e
for sub in [USR, USR_SRC]:
    off = (DATA_LBA + (sub.cluster - 2) * SPC) * BPS
    for i, e in enumerate(sub.entries):
        d[off + i*32: off + i*32 + 32] = e
# FAT2 = FAT1
d[FAT_LBA*BPS + FAT_SEC*BPS : FAT_LBA*BPS + 2*FAT_SEC*BPS] = \
    d[FAT_LBA*BPS : FAT_LBA*BPS + FAT_SEC*BPS]

with open(path, 'wb') as f:
    f.write(d)
nfiles = sum(1 for sub in [root, USR, USR_SRC] for e in sub.entries if e[11] != 0x10)
print(f'{path} ready ({nfiles} files, {clu - 2} clusters, 2 dirs)')
