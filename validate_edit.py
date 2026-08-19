"""
验证: EDIT Open 对话框单击目录后,Files+Directories 都刷新;
光标是下划线;鼠标是白色。
"""
import socket, subprocess, time, os, sys

MON = 45463
ROOT = '/mnt/c/Users/XU/Desktop/OSDev'
SERIAL = ROOT + '/validate.serial'

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

# 杀残留 QEMU
os.system("wsl -e bash -c \"ps aux | grep qemu | grep -v grep | awk '{print \$2}' | xargs -r kill -9 2>/dev/null; sleep 0.5\"")

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
    # 输入 edit 启动编辑器
    for c in 'edit':
        mon_cmd(s, 'sendkey ' + c, wait=0.12)
    mon_cmd(s, 'sendkey ret', wait=1.5)
    time.sleep(1.5)
    p1 = dump_vga(s, '1_editor')
    print('--- 1) EDITOR screen ---')
    for r in vga_to_text(p1): print(r)

    # Ctrl+O 打开 Open
    mon_cmd(s, 'sendkey ctrl-o', wait=1.2)
    time.sleep(1.5)
    p2 = dump_vga(s, '2_opendlg')
    print('--- 2) OPEN DIALOG screen ---')
    for r in vga_to_text(p2): print(r)

    # 单击 Directories 列表里的 BIN 目录(行 8 col 19~)
    # 行 8 = Directories 列表第 1 行内容; col 19 是 Directories 列表左边
    # DFLAT 列表框的 LEFT_BUTTON 用 (x, y) 屏幕坐标
    # Directories 列表框: (col 19, row 6) 到 (col 32, row 16)
    # BIN 在第一行 -> 屏幕 (col 19+1, row 6+1) = (20, 7)
    mon_cmd(s, 'mouse_move 180 112', wait=0.3)  # 20*9=180, 7*16=112
    mon_cmd(s, 'mouse_button 1 1', wait=0.2)  # 左键按下
    mon_cmd(s, 'mouse_button 1 0', wait=0.8)  # 左键松开
    time.sleep(0.8)
    p3 = dump_vga(s, '3_after_click_bin')
    print('--- 3) AFTER CLICK BIN (Directories 单击) ---')
    for r in vga_to_text(p3): print(r)

    mon_cmd(s, 'quit', wait=0.3)
finally:
    try: qemu.wait(timeout=5)
    except subprocess.TimeoutExpired: qemu.kill()
