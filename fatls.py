import struct, sys

data_blk = 0

def read_fat_chain(d, bps, spc, fat_start, first_cluster):
    global data_blk
    clus = first_cluster
    secs = []
    seen = set()
    while clus >= 2:
        if clus in seen:
            break
        seen.add(clus)
        off = fat_start * bps + (clus + (clus >> 1))  # FAT12 (3-byte packed)
        w = struct.unpack('<H', d[off:off + 2])[0]
        nxt = w >> 4 if (clus & 1) else (w & 0xFFF)
        secs.append(data_blk + (clus - 2) * spc)
        if nxt >= 0xFF8:
            break
        clus = nxt
    return secs

def list_dir(d, bps, spc, nfat, fatsz, fat_start, root_start, rde, first_cluster, depth):
    if first_cluster < 2:
        blob = d[root_start * bps: root_start * bps + rde * 32]
    else:
        secs = read_fat_chain(d, bps, spc, fat_start, first_cluster)
        blob = b''.join(d[s * bps:(s + 1) * bps] for s in secs)
    for i in range(len(blob) // 32):
        e = blob[i * 32:i * 32 + 32]
        if e[0] in (0, 0xE5):
            continue
        name = e[:11].decode('ascii', 'replace').strip()
        attr = e[11]
        cl = struct.unpack('<H', e[26:28])[0]
        if attr & 0x10:
            print('  ' * depth + '[%s]' % name)
            if depth < 3:
                list_dir(d, bps, spc, nfat, fatsz, fat_start, root_start, rde, cl, depth + 1)
        else:
            sz = struct.unpack('<I', e[28:32])[0]
            print('  ' * depth + '%s (%d)' % (name, sz))

def fat_tree(img):
    global data_blk
    d = open(img, 'rb').read()
    bps = struct.unpack('<H', d[11:13])[0]
    spc = d[13]
    res = struct.unpack('<H', d[14:16])[0]
    nfat = d[16]
    rde = struct.unpack('<H', d[17:19])[0]
    tot = struct.unpack('<H', d[19:21])[0] or struct.unpack('<I', d[32:36])[0]
    fatsz = struct.unpack('<H', d[22:24])[0] or struct.unpack('<I', d[36:40])[0]
    fat_start = res
    root_start = res + nfat * fatsz
    data_blk = root_start + (rde * 32 + bps - 1) // bps
    ftype = 'FAT12' if tot < 4085 else ('FAT16' if tot < 65525 else 'FAT32')
    print('%s: %s spc=%d' % (img, ftype, spc))
    list_dir(d, bps, spc, nfat, fatsz, fat_start, root_start, rde, 0, 0)

for img in sys.argv[1:]:
    fat_tree(img)
