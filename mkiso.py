#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# mkiso.py — 最小 ISO9660 镜像 (v6.5.6 P3 测试盘)
#   system area 32KB | PVD@16 | 终止符@17 | 根目录@18 | 文件数据随后
import sys, struct

SECT = 2048

def dir_rec(lba, size, name, flags):
    # name: bytes, 不含 ';1' 之外的版本号处理; 记录长度补偶
    b = bytearray()
    b += bytes([0])              # 长度占位
    b += bytes([0])              # 扩展属性记录长
    b += struct.pack('<I', lba)
    b += struct.pack('>I', lba)
    b += struct.pack('<I', size)
    b += struct.pack('>I', size)
    b += bytes([120, 8, 30, 12, 0, 0, 0])   # 2020-08-30 12:00 GMT
    b += bytes([flags])          # bit1=目录
    b += bytes([0, 0])           # file unit / interleave
    b += struct.pack('<H', 1)    # 卷序号
    b += bytes([len(name)])
    b += name
    if len(b) % 2: b += b'\x00'
    b[0] = len(b)
    return bytes(b)

def build(out):
    nsec = 64
    img = bytearray(nsec * SECT)

    # PVD
    pvd = bytearray(SECT)
    pvd[0] = 1
    pvd[1:6] = b'CD001'
    pvd[6] = 1
    pvd[8:40] = b'AMUNOS CD'.ljust(32)
    pvd[40:71] = b'AMUNOS'.ljust(31)
    pvd[71:102] = b'AMUNOS'.ljust(31)
    pvd[120:124] = struct.pack('<I', nsec)
    pvd[124:128] = struct.pack('>I', nsec)
    # 根目录记录占位 (32 字节记录, 严格等长切片赋值, 防 bytearray 移位)
    img[16*SECT:17*SECT] = pvd

    # 卷描述符终止符
    term = bytearray(SECT)
    term[0] = 255
    term[1:6] = b'CD001'
    term[6] = 1
    img[17*SECT:18*SECT] = term

    # 文件数据: 从扇 20 起
    files = [
        (b'HELLO.TXT;1', b'Hello from AMUNOS CD-ROM!\r\nISO9660 read-only works.\r\n'),
        (b'README.TXT;1',  b'AMUNOS v6.5.6 ATAPI/ISO9660 test disc.\r\n'),
    ]
    flba = 20
    fmeta = []
    for name, data in files:
        fmeta.append((name, flba, len(data)))
        img[flba*SECT:(flba*SECT)+len(data)] = data
        flba += 1

    # 根目录内容: . .. 两个文件
    root_lba = 18
    ents = bytearray()
    root_size = 0
    # 先算根目录大小: . + .. + 2 文件
    approx = len(dir_rec(root_lba, 0, b'\x00', 0x02)) * 2 + sum(len(dir_rec(l, s, n, 0)) for n, l, s in fmeta)
    root_size = ((approx + SECT - 1) // SECT) * SECT
    ents += dir_rec(root_lba, root_size, b'\x00', 0x02)               # .
    ents += dir_rec(root_lba, root_size, b'\x01', 0x02)               # ..
    for name, lba, size in fmeta:
        ents += dir_rec(lba, size, name, 0)
    img[root_lba*SECT:root_lba*SECT+len(ents)] = ents

    # 回填 PVD 根目录记录 (32 字节等长)
    rec = dir_rec(root_lba, root_size, b'\x00', 0x02)
    assert len(rec) == 32
    img[16*SECT+156:16*SECT+188] = rec

    with open(out, 'wb') as f:
        f.write(img)
    print('%s: %d bytes, root@%d size=%d, files=%s' %
          (out, len(img), root_lba, root_size, [(n.decode(), s) for n, _, s in fmeta]))

if __name__ == '__main__':
    build(sys.argv[1] if len(sys.argv) > 1 else 'CD.iso')
