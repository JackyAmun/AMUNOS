#!/usr/bin/env python3
# mkd32.py — 生成 FAT32 测试数据盘 D32.img (v6.5.6 P2)
# 几何: 36MB / 512B 簇 (spc=1) → ~72K 簇 > 65524 → 必须是 FAT32.
# 内容: HELLO.TXT / GB 中文.TXT (GB2312 8.3 名) / BIG.BIN (100KB, >64KB 能力验证)
import struct, sys

TOTAL = 73728        # 36MB / 512
SPC   = 1
RSVD  = 32           # FAT32 惯例 (含 FSInfo@1 / 备份引导@6)
NFAT  = 2

# 迭代收敛 FAT 大小
spf = 512
for _ in range(10):
    data_start = RSVD + NFAT * spf
    clusters = (TOTAL - data_start) // SPC
    spf = (clusters * 4 + 511) // 512
DATA_START = RSVD + NFAT * spf
CLUSTERS = (TOTAL - DATA_START) // SPC
assert CLUSTERS > 65524, 'not FAT32 territory'

img = bytearray(TOTAL * 512)

# ── 引导扇区 (BPB) ──
bs = bytearray(512)
bs[0:3] = b'\xEB\x3C\x90'
bs[3:11] = b'AMUNOS32'
bs[11:13] = struct.pack('<H', 512)      # bytes/sector
bs[13] = SPC
bs[14:16] = struct.pack('<H', RSVD)
bs[16] = NFAT
bs[17:19] = struct.pack('<H', 0)        # root entries (FAT32=0)
bs[19:21] = struct.pack('<H', 0)        # total16 (FAT32=0)
bs[21] = 0xF8
bs[22:24] = struct.pack('<H', 0)        # spf16 (FAT32=0)
bs[24:26] = struct.pack('<H', 63)       # sectors/track
bs[26:28] = struct.pack('<H', 16)       # heads
bs[28:32] = struct.pack('<I', 0)        # hidden
bs[32:36] = struct.pack('<I', TOTAL)    # total32
bs[36:40] = struct.pack('<I', spf)      # sectors per FAT (32bit)
bs[40:42] = struct.pack('<H', 0)        # ext flags
bs[42:44] = struct.pack('<H', 0)        # fs version
bs[44:48] = struct.pack('<I', 2)        # root cluster
bs[48:50] = struct.pack('<H', 1)        # fsinfo sector
bs[50:52] = struct.pack('<H', 6)        # backup boot sector
bs[64] = 0x28                           # ext boot sig
bs[71:82] = b'AMUNOS FAT'
bs[82:90] = b'FAT32   '
bs[510:512] = b'\x55\xAA'
img[0:512] = bs

# ── FSInfo (扇 1) ──
fsi = bytearray(512)
fsi[0:4] = b'RRaA'
fsi[484:488] = b'rrAa'
fsi[488:492] = struct.pack('<I', 0xFFFFFFFF)   # free count unknown
fsi[492:496] = struct.pack('<I', 0xFFFFFFFF)
img[1*512:2*512] = fsi

# ── 备份引导 (扇 6) ──
img[6*512:7*512] = bs

# ── FAT (两份) ──
fat = bytearray(spf * 512)
struct.pack_into('<I', fat, 0, 0x0FFFFFF8)
struct.pack_into('<I', fat, 4, 0x0FFFFFFF)

def fat_set(c, v):
    struct.pack_into('<I', fat, c * 4, v)

fat_set(2, 0x0FFFFFFF)   # 根目录簇 2 = EOC (漏写会被分配器当作空闲簇回收!)

# ── 根目录 (簇 2 起) ──
root = bytearray(512 * SPC)

def add_entry(buf, off, name11, attr, cluster, size):
    e = bytearray(32)
    e[0:11] = name11
    e[11] = attr
    e[26:28] = struct.pack('<H', cluster & 0xFFFF)
    e[20:22] = struct.pack('<H', (cluster >> 16) & 0xFFFF)
    e[28:32] = struct.pack('<I', size)
    buf[off*32:(off+1)*32] = e

# 卷标 + 3 文件
add_entry(root, 0, b'AMUNOSFAT32', 0x08, 0, 0)

files = []
def add_file(name11, data):
    n = max(1, (len(data) + 511) // 512)
    # 从簇 3 起顺序分配 (根目录只占簇 2)
    start = 3 + sum(f[2] for f in files)
    for i in range(n):
        fat_set(start + i, 0x0FFFFFFF if i == n - 1 else start + i + 1)
    off = DATA_START + (start - 2) * SPC
    img[off*512:off*512+len(data)] = data
    assert len(name11) == 11, name11
    add_entry(root, 1 + len(files), name11, 0x20, start, len(data))
    files.append((name11, data, n))

add_file(b'HELLO   TXT', b'AMUNOS FAT32 read/write OK\r\n')
cn = '中文测试 FAT32 数据盘 内容正常'.encode('gb2312')
add_file(b'\xD6\xD0\xCE\xC4       ', cn)   # 中文.TXT (GB 4字节+4空格名, 3空格ext = 11)
add_file(b'BIG     BIN', bytes((i * 7 + 0x20) & 0xFF for i in range(100 * 1024)))

img[DATA_START*512:(DATA_START+SPC)*512] = root
img[(RSVD)*512:(RSVD)*512+spf*512] = fat
img[(RSVD+spf)*512:(RSVD+2*spf)*512] = fat

out = sys.argv[1] if len(sys.argv) > 1 else 'D32.img'
open(out, 'wb').write(img)
print('%s: %.1fMB FAT32, spf=%d clusters=%d, files: %s' %
      (out, TOTAL*512/1e6, spf, CLUSTERS,
       ', '.join(f[0].decode('latin-1').strip() for f in files)))
