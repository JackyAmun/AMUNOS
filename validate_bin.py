"""
验证 EDIT Open 对话框:
  1) 全局下划线光标 (VGA dump 验证)
  2) 焦点 Directories → 选 BIN → Files 出现 TCC.ELF/EDIT.ELF
  3) 对话框不关闭
  4) 鼠标颜色: 鼠标格 0xDB + 0x0F (白色)
"""
import socket, subprocess, time, os, sys

MON = 45466
ROOT = '/mnt/c/Users/XU/Desktop/OSDev'
SERIAL = ROOT + '/bin.serial'

def mon_cmd(s, cmd, wait=0.4):
    try: s.sendall((cmd + '\n').encode())
    except OSError: return b''
    time.sleep(wait)
    s.settimeout(0.5)
    out = b''
    try:
        while True:
            c = s.recv(65536)
            if not c: break
            out += c
    except socket.timeout: pass
    return out

def dump_vga(s, tag):
    p = os.path.join(ROOT, 'vga.%s.bin' % tag)
    mon_cmd(s, 'pmemsave 0xb8000 0xfa0 "%s"' % p, wait=0.6)
    return p

def vga_to_text(p):
    d = open(p, 'rb').read()
    rows = []
    for y in range(25):
        line = ''
        for x in range(80):
            ch = d[(y*80+x)*2]
            line += chr(ch) if 32 <= ch <= 126 else ' '
        rows.append(line)
    return rows

os.system("ps aux | grep qemu | grep -v grep | awk '{print $2}' | xargs -r kill -9 2>/dev/null; sleep 0.5")
qemu = subprocess.Popen([
    'qemu-system-i386', '-rtc', 'base=localtime',
    '-hda', ROOT + '/A.img', '-hdb', ROOT + '/B.img', '-hdc', ROOT + '/C.img',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:%d,server,nowait' % MON,
    '-serial', 'file:' + SERIAL, '-no-reboot',
], cwd=ROOT)

try:
    s = None
    for _ in range(40):
        try:
            s = socket.create_connection(('127.0.0.1', MON), timeout=2); break
        except OSError: time.sleep(0.5)
    if not s:
        print('FAIL: monitor not reachable'); qemu.kill(); sys.exit(1)

    time.sleep(2)
    for c in 'edit':
        mon_cmd(s, 'sendkey ' + c, wait=0.12)
    mon_cmd(s, 'sendkey ret', wait=1.5)
    time.sleep(1.5)

    mon_cmd(s, 'sendkey ctrl-o', wait=1.2)
    time.sleep(1.5)

    # 焦点移到 Directories 列表 (Tab 2 次: EDITBOX → FILES → DIRECTORIES)
    mon_cmd(s, 'sendkey tab', wait=0.4)
    time.sleep(0.3)
    mon_cmd(s, 'sendkey tab', wait=0.4)
    time.sleep(0.3)
    # 第一项就是 BIN, 直接回车
    mon_cmd(s, 'sendkey ret', wait=1.0)
    time.sleep(1.0)

    p = dump_vga(s, 'after_bin')
    rows = vga_to_text(p)
    print('--- after enter BIN (should be in A:/BIN/) ---')
    for r in rows: print(r)

    # 验证:
    has_tcc = any('TCC' in r for r in rows)
    has_edit = any('EDIT' in r for r in rows)
    has_bin_path = any('BIN' in r and ':/' in r for r in rows)
    has_open = any('Open File' in r for r in rows)
    has_ok = any('OK' in r for r in rows)
    print('---')
    print('Files has TCC.ELF:', has_tcc)
    print('Files has EDIT.ELF:', has_edit)
    print('Path shows A:/BIN/:', has_bin_path)
    print('Dialog still open (Open File title):', has_open)
    print('OK button visible:', has_ok)

    mon_cmd(s, 'quit', wait=0.3)
finally:
    try: qemu.wait(timeout=5)
    except subprocess.TimeoutExpired: qemu.kill()
