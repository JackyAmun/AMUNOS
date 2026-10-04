#!/usr/bin/env python3
"""Wrap a FAT volume in a conventional MBR primary partition for P2 tests."""
import struct
import sys

source = sys.argv[1] if len(sys.argv) > 1 else 'C.vol'
target = sys.argv[2] if len(sys.argv) > 2 else 'C.img'
boot_path = sys.argv[3] if len(sys.argv) > 3 else None
start_lba = 2048

with open(source, 'rb') as f:
    volume = f.read()
if len(volume) % 512:
    raise SystemExit('source volume is not sector aligned')

sectors = len(volume) // 512
image = bytearray((start_lba + sectors) * 512)
entry = 446
image[entry] = 0x80                    # active primary partition
fat16_size = struct.unpack_from('<H', volume, 22)[0]
image[entry + 4] = 0x01 if sectors <= 8192 and fat16_size else 0x06
struct.pack_into('<I', image, entry + 8, start_lba)
struct.pack_into('<I', image, entry + 12, sectors)
image[510:512] = b'\x55\xaa'
if boot_path:
    with open(boot_path, 'rb') as f:
        boot = f.read(446)
    if len(boot) != 446:
        raise SystemExit('MBR boot code must contain at least 446 bytes')
    image[:446] = boot
image[start_lba * 512:] = volume

with open(target, 'wb') as f:
    f.write(image)
fat_type = 'FAT12' if image[entry + 4] == 0x01 else 'FAT16'
print(f'{target}: MBR {fat_type} partition LBA {start_lba}, {sectors} sectors')
