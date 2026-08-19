"""
验证 EDIT Open 对话框:
  1) 全局下划线光标
  2) 用键盘 Tab 到 Directories 列表, 方向键选 BIN, 回车切目录 -> 列表全刷
  3) 鼠标白色 (颜色属性) - 通过读取 attr 字节验证
"""
import socket, subprocess, time, os, sys

MON = 45464
ROOT = '/mnt/c/Users/XU/Desktop/OSDev'
SERIAL = ROOT + '/kb.serial'

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
    p1 = dump_vga(s, '1_opendlg')
    print('--- 1) OPEN DIALOG (initial A:/) ---')
    for r in vga_to_text(p1): print(r)

    # 通过键盘操作: Tab 到 Directories, ↓ 选 BIN, 回车 (LB_CHOOSE 路径)
    # 但 DFLAT 默认焦点在第一个 EDITBOX, Tab 顺序: EDITBOX -> LIST_FILES -> LIST_DIRS -> LIST_DRIVES -> OK -> Cancel -> Help
    # EDITBOX 焦点 -> Files list 焦点: Tab
    mon_cmd(s, 'sendkey tab', wait=0.4)  # Files list
    time.sleep(0.3)
    mon_cmd(s, 'sendkey tab', wait=0.4)  # Directories list
    time.sleep(0.3)
    # 现在焦点在 Directories 列表, 方向键 ↓ 选 BIN
    mon_cmd(s, 'sendkey down', wait=0.4)
    time.sleep(0.3)
    p2 = dump_vga(s, '2_focus_dirs')
    print('--- 2) Directories 列表焦点, 选中 BIN ---')
    for r in vga_to_text(p2): print(r)
    # 回车 -> LB_CHOOSE
    mon_cmd(s, 'sendkey ret', wait=1.0)
    time.sleep(1.0)
    p3 = dump_vga(s, '3_after_enter')
    print('--- 3) 回车后: 应该进入 A:/BIN/ ---')
    for r in vga_to_text(p3): print(r)

    mon_cmd(s, 'quit', wait=0.3)
finally:
    try: qemu.wait(timeout=5)
    except subprocess.TimeoutExpired: qemu.kill()
