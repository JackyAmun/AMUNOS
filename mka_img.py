#!/usr/bin/env python3
r"""A.img for AMUNOS v6.5.1 — boot + kernel (sectors 0..104) + FAT12 system disk
with a two-level directory tree (BOOT\ BIN\ USR\LIB USR\INCLUDE USR\SRC).

Geometry (matches boot.asm BPB: reserved=105 sectors for boot+kernel):
  sector 0       boot.bin
  sector 1..104  kernel.bin (must stay < 104 sectors = 53248 bytes)
  sector 105..113  FAT1 (9 sectors)
  sector 114..122  FAT2 (9 sectors)
  sector 123..136  root dir (224 entries = 14 sectors)
  sector 137..     data area (root files + subdirectories)

Tree:
  A:\
  ├─ BOOT\BOOT.BIN, KERNEL.BIN
  ├─ BIN\TCC.ELF, EDIT.ELF
  ├─ USR\LIB\LIBC.A, LIBTCC1.A      (TCC 链接库; -L/-B 注入)
  ├─ USR\INCLUDE\*.H                (TCC 头; -I 注入)
  ├─ USR\SRC\HELLO.C, INP.C
  ├─ CRT1.O, CRTI.O, CRTN.O         (TCC crt_paths="A:\" 绝对前缀, 任意 cwd 可解析)
  └─ CMDS.BIN                       (EDIT → \\BIN\\EDIT.ELF)
"""
import struct, sys, os

path = sys.argv[1] if len(sys.argv) > 1 else 'A.img'
d = bytearray(2880 * 512)

RESV   = 105              # reserved sectors (boot + kernel)
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
all_dirs = []            # non-root dirs (for final layout pass)

def set_fat(c, val):
    """FAT12 entry c -> val (12-bit), consistent with fs.c fat12_set_cluster."""
    boff = FAT1 + (c + c // 2)
    cur = struct.unpack('<H', d[boff:boff + 2])[0]
    if c & 1:
        d[boff:boff + 2] = struct.pack('<H', (cur & 0x000F) | ((val & 0x0FFF) << 4))
    else:
        d[boff:boff + 2] = struct.pack('<H', (cur & 0xF000) | (val & 0x0FFF))

def alloc_clusters(n):
    """Allocate n consecutive clusters, chain them to EOF, return first."""
    global clu
    c = clu; clu += n
    if c + n - 1 > 0xFE0:
        raise SystemExit('disk full')
    for i in range(n):
        nxt = (c + i + 1) if i < n - 1 else 0xFFF
        set_fat(c + i, nxt)
    return c

def _b8(s, n):
    """name8/ext3 -> raw n bytes, accepting str (UTF-8) or bytes (e.g. GB2312 8.3)."""
    b = s if isinstance(s, bytes) else s.encode()
    return b.ljust(n, b' ')

def mk_entry(name8, ext3, attr, start, size):
    e = bytearray(32)
    e[0:8] = _b8(name8, 8)
    e[8:11] = _b8(ext3, 3)
    e[11] = attr
    struct.pack_into('<H', e, 26, start)
    struct.pack_into('<I', e, 28, size)
    return e

def mkdir(name8, parent, cap):
    """Create a subdir `name8` inside `parent` (a Dir), capacity `cap` entries.
    Allocates the cluster chain, writes . / .. entries, returns the Dir."""
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
    """Write a file into `parent`, multi-cluster. Returns cluster count."""
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

def add_file_to(parent, name8, ext3, path):
    with open(path, 'rb') as f:
        return add_to(parent, name8, ext3, f.read())

def add_opt_to(parent, name8, ext3, path):
    if os.path.exists(path):
        return add_file_to(parent, name8, ext3, path)
    print(f'WARN: {path} not found (skipping)')
    return 0

class Dir:
    def __init__(self, name8, cluster):
        self.name = name8
        self.cluster = cluster          # 0 = root
        self.entries = []               # 32-byte entry blobs

root = Dir('', 0)
BOOT = mkdir('BOOT', root, 16)
BIN  = mkdir('BIN',  root, 16)
USR  = mkdir('USR',  root, 16)
USR_LIB = mkdir('LIB',     USR, 16)
USR_INC = mkdir('INCLUDE', USR, 40)
USR_SRC = mkdir('SRC',     USR, 16)

# ── BOOT\ : 引导/内核副本 ──
add_opt_to(BOOT, 'BOOT',   'BIN', 'boot.bin')
add_opt_to(BOOT, 'KERNEL', 'BIN', 'kernel.bin')

# ── BIN\ : 系统可执行程序 (CMDS.TXT 表指向这里; TCC 走命令前缀) ──
add_opt_to(BIN, 'TCC',  'ELF', 'tcc.elf')
add_opt_to(BIN, 'EDIT', 'ELF', 'edit.elf')

# ── USR\LIB\ : TCC 链接库 (cmd_tcc 注入 -L/-B) ──
add_opt_to(USR_LIB, 'LIBC',    'A  ', 'libc/libc.a')
add_opt_to(USR_LIB, 'LIBTCC1', 'A  ', 'makar/vendor/tinycc/lib/libtcc1.a')

# ── USR\INCLUDE\ : TCC 内置头 + libc 头 (cmd_tcc 注入 -I) ──
for h in ['stdarg', 'stddef', 'stdbool', 'float', 'varargs']:
    add_opt_to(USR_INC, h.upper(), 'H  ', f'makar/vendor/tinycc/include/{h}.h')
for h in ['stdio', 'stdlib', 'string', 'strings', 'ctype', 'malloc', 'syscall',
          'errno', 'fcntl', 'unistd', 'limits', 'stdint', 'inttypes', 'setjmp',
          'math', 'time', 'assert', 'dirent']:
    add_opt_to(USR_INC, h.upper(), 'H  ', f'libc/{h}.h')

# ── USR\SRC\ : 示例源码 ──
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
add_to(USR_SRC, 'HELLO', 'C  ', HELLO_C)
add_opt_to(USR_SRC, 'INP', 'C  ', 'inp.c')     # 输入测试源码 (可在 OS 内 TCC 编译)

# ── HZK16 汉字点阵字库 (v6.8 中文渲染): 内核 fb_font_init 从 A:HZK16 加载 ──
add_opt_to(root, 'HZK16', '   ', 'HZK16')
# ── U2GB  Unicode→GB2312 映射 (v6.8 UTF-8 支持): 把 UTF-8 码点查成 GB2312 字库偏移 ──
add_opt_to(root, 'U2GB', 'BIN', 'u2gb.bin')

# ── TCC crt 文件留根 (TCC crt_paths="A:\" 绝对前缀, 任何盘/目录都能解析) ──
add_opt_to(root, 'CRT1', 'O  ', 'libc/crt1.o')
add_opt_to(root, 'CRTI', 'O  ', 'libc/crti.o')
add_opt_to(root, 'CRTN', 'O  ', 'libc/crtn.o')

# ── 命令→ELF 对照表 (v6.5.1): CMDS.BIN, 内核保护 (DEL/REN/覆盖拒绝, EDIT/INSTALL 可写) ──
CMDS_BIN = '''\
; AMUNOS 命令→ELF 对照表 (v6.5.1)
; 格式: 命令名 目标ELF   (相对路径自动补来源盘盘符; 全盘 A:-D: 搜索)
; 注释以 ; 或 # 开头; 用 EDIT CMDS.BIN 编辑或 INSTALL 命令追加
EDIT /BIN/EDIT.ELF
'''
add_to(root, 'CMDS', 'BIN', CMDS_BIN)

# ── 中文演示/验证样本 (v6.8.1): GB2312 中文文件名 + GB/UTF-8 内容 ──
#   中文.TXT  = GB2312 文件名 (8.3 塞双字节); 内容为 GB2312 编码
#   UTF8_CN   = UTF-8 编码内容 (EDIT 的 UTF-8 渲染验证目标)
#   GB_CN     = GB2312 编码内容 (EDIT 的 GB2312 渲染/整字删验证目标)
add_to(root, '中文'.encode('gb2312'), 'TXT',
       '这是中文文件名, GB2312 编码内容\n第二行 123 abc\n'.encode('gb2312'))
add_to(root, 'UTF8_CN', 'TXT',
       '这是 UTF-8 编码的中文内容\n第二行 456 def\n')
add_to(root, 'GB_CN', 'TXT',
       '这是 GB2312 编码的中文内容\n第二行 789 ghi\n'.encode('gb2312'))

# ── 布局落盘: 根目录 + 各子目录 + FAT2 ──
for i, e in enumerate(root.entries):
    d[ROOT + i * 32:ROOT + i * 32 + 32] = e
for sub in all_dirs:
    base = DATA + (sub.cluster - 2) * 512
    for i, e in enumerate(sub.entries):
        d[base + i * 32:base + i * 32 + 32] = e
d[FAT2:FAT2 + FATSEC * 512] = d[FAT1:FAT1 + FATSEC * 512]

with open(path, 'wb') as f:
    f.write(d)
nfiles = sum(1 for sub in all_dirs for e in sub.entries if e[11] != 0x10) + \
         sum(1 for e in root.entries if e[11] != 0x10)
print(f'{path} ready ({nfiles} files, {clu - 2} clusters, {len(all_dirs)} dirs)')
