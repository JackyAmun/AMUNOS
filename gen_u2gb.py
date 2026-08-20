#!/usr/bin/env python3
# 生成 Unicode→GB2312 映射文件 U2GB.BIN (供内核把 UTF-8 码点查成 GB2312 字库偏移)
# 格式: 每条目 4 字节 LE = [gbcode u16][unicode u16], 按 unicode 升序 (内核 u32=uni<<16|gb, 二分查高16位)
import struct

entries = {}
for gh in range(0xA1, 0xF8):
    for gl in range(0xA1, 0xFF):
        try:
            uni = bytes([gh, gl]).decode('gb2312')
        except UnicodeDecodeError:
            continue
        if len(uni) != 1:
            continue
        u = ord(uni)
        gb = (gh << 8) | gl
        # 若有重复 unicode, 保留第一个 (GB2312 首位即简体常用)
        entries.setdefault(u, gb)

items = sorted(entries.items())
out = bytearray()
for u, gb in items:
    out += struct.pack('<HH', gb, u)   # gb 在前 (低16位), uni 在后 (高16位), 按 uni 升序

open('u2gb.bin', 'wb').write(bytes(out))
print('entries=%d bytes=%d  (%.1f KB)' % (len(items), len(out), len(out)/1024))
# 抽查几个常见字
for ch in '你好好世界界中文支持号':
    u = ord(ch); gb = entries.get(u)
    print('  %s U+%04X -> GB %04X' % (ch, u, gb if gb else 0))
