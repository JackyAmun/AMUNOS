with open('kernel.bin', 'rb') as f:
    d = f.read()
total = len(d)
print('total size:', total)
zero_runs = []
i = 0
while i < total:
    if d[i] == 0:
        j = i
        while j < total and d[j] == 0:
            j += 1
        if j - i >= 4096:
            zero_runs.append((i, j - i))
        i = j
    else:
        i += 1
print('zero runs >= 4KB:')
for off, ln in zero_runs[:20]:
    print('  off=' + str(off) + ' len=' + str(ln) + ' (' + str(ln / 1024) + 'KB)')
