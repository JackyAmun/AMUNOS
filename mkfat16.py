#!/usr/bin/env python3
r"""mkfat16.py — 生成 FAT16 数据盘镜像 (测试 AMUNOS 的 FAT16 兼容性)

几何: 32MB, 512B/扇区, 8 扇区/簇 (4KB), 2 份 FAT, 根目录 512 项。
与真实 mkfs.fat -F 16 生成的布局一致 (仅根目录/文件内容不同)。

用法: python3 mkfat16.py out.img
  默认在根放 USR\SRC\DEMO.C 与 CMDS.BIN, 供 AMUNOS 读取测试。
"""
import struct, sys, os

path = sys.argv[1] if len(sys.argv) > 1 else 'fat16.img'

BPS = 512; SPC = 8; RES = 1; NFAT = 2; ROOT_ENTRIES = 512
TOTAL_SEC = 65536                      # 32MB
ROOT_SEC = ROOT_ENTRIES * 32 // BPS    # 32
DATA_SEC = TOTAL_SEC - RES - NFAT * 9 - ROOT_SEC   # 先估 FAT=9 扇再校正
NCLU = (TOTAL_SEC - RES - ROOT_SEC) // (SPC + 0)   # 初估
# 计算 FAT 扇区数: 每个条目 2 字节
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
print(f'FAT16: total={TOTAL_SEC} spc={SPC} fat={FAT_SEC}sec '
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

def add_to(parent, name8, ext3, content, attr=0x20):
    C = content if isinstance(content, bytes) else content.encode()
    nc = (len(C) + SPC*BPS - 1) // (SPC*BPS)
    if nc == 0: nc = 1
    c = alloc(nc)
    for i in range(nc):
        off = (DATA_LBA + (c + i - 2) * SPC) * BPS
        d[off:off + SPC*BPS] = C[i*SPC*BPS:(i+1)*SPC*BPS].ljust(SPC*BPS, b'\x00')
    parent.entries.append(mk_entry(name8, ext3, attr, c, len(C)))
    return c

class P:
    def __init__(self, clus):
        self.clus = clus
        self.entries = []

root = P(0)
usr = P(alloc(1)); root.entries.append(mk_entry('USR', '   ', 0x10, usr.clus, 0))
usr.entries.append(mk_entry('.', '   ', 0x10, usr.clus, 0))
usr.entries.append(mk_entry('..', '   ', 0x10, 0, 0))
src = P(alloc(1)); usr.entries.append(mk_entry('SRC', '   ', 0x10, src.clus, 0))
src.entries.append(mk_entry('.', '   ', 0x10, src.clus, 0))
src.entries.append(mk_entry('..', '   ', 0x10, usr.clus, 0))

if os.path.exists('hello.elf'):
    add_to(root, 'HELLO', 'ELF', open('hello.elf','rb').read())
else:
    print('WARN: hello.elf not found')

add_to(src, 'DEMO', 'C  ',
       'int main(){printf("DEMO OK from TCC\\n");return 0;}\n')
CMDS = '; FAT16 C 盘命令表\nEDIT /EDIT.ELF\nHELLO /HELLO.ELF\n'
add_to(root, 'CMDS', 'BIN', CMDS)

# 落盘: 根目录 + 各子目录
for i, e in enumerate(root.entries):
    d[ROOT_LBA*BPS + i*32: ROOT_LBA*BPS + i*32 + 32] = e
for c, entries in [(usr.clus, usr.entries), (src.clus, src.entries)]:
    off = (DATA_LBA + (c - 2) * SPC) * BPS
    for i, e in enumerate(entries):
        d[off + i*32: off + i*32 + 32] = e
# FAT2 = FAT1
d[FAT_LBA*BPS + FAT_SEC*BPS : FAT_LBA*BPS + 2*FAT_SEC*BPS] = \
    d[FAT_LBA*BPS : FAT_LBA*BPS + FAT_SEC*BPS]

with open(path, 'wb') as f:
    f.write(d)
print(f'{path} ready ({len(root.entries)}+{len(usr.entries)}+{len(src.entries)} entries, {clu-2} clusters)')
