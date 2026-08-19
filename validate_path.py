"""
v6.5.2 验证:
  1) EDIT Open 对话框: 选 A:/USR/SRC/INP.C 后, 真正打开 (不丢目录变 A:/INP.C)
  2) 退出 EDIT 后, 跑一个 C 程序 + 输入, 验证文本流光标跟随 (无残留)
"""
import socket, subprocess, time, os, sys

MON = 45472
ROOT = '/mnt/c/Users/XU/Desktop/OSDev'
SERIAL = ROOT + '/v652b.serial'

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

def send_chars(s, s_str, per=0.15):
    for c in s_str:
        if c == ' ':
            mon_cmd(s, 'sendkey sp', wait=per)
        else:
            mon_cmd(s, 'sendkey ' + c, wait=per)

def dump_vga(s, tag):
    p = os.path.join(ROOT, 'vga.v652b.%s.bin' % tag)
    mon_cmd(s, 'pmemsave 0xb8000 0xfa0 "%s"' % p, wait=0.6)
    return p

def vga_text(p):
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
    # 准备: cd 到 USR/SRC, 用 tcc 编译 inp.c → 生成 INP.ELF, 再用 echo 写一个 INP.C 源
    send_chars(s, 'cd usr/src', per=0.18)
    mon_cmd(s, 'sendkey ret', wait=0.6)
    # type "hello world" file via echo
    send_chars(s, 'echo hello world', per=0.18)
    mon_cmd(s, 'sendkey ret', wait=0.6)

    # 回根目录, 启动 EDIT
    send_chars(s, 'a:', per=0.18)
    mon_cmd(s, 'sendkey ret', wait=0.6)
    send_chars(s, 'edit', per=0.18)
    mon_cmd(s, 'sendkey ret', wait=2.5)
    time.sleep(0.8)

    # Ctrl+O
    mon_cmd(s, 'sendkey ctrl-o', wait=1.5)
    time.sleep(0.8)

    # 切到 Directories (Tab 2 次), 下箭头选 USR
    mon_cmd(s, 'sendkey tab', wait=0.4); time.sleep(0.2)
    mon_cmd(s, 'sendkey tab', wait=0.4); time.sleep(0.2)
    mon_cmd(s, 'sendkey down', wait=0.4); time.sleep(0.3)
    mon_cmd(s, 'sendkey ret', wait=1.0)
    # 选 SRC
    mon_cmd(s, 'sendkey down', wait=0.4); time.sleep(0.3)
    mon_cmd(s, 'sendkey ret', wait=1.0)

    # Tab 到 Files 列表 (Directories 焦点, Tab 到 Drives, 再 Tab 到 Files 不行;
    # 准确: 现在焦点在 Directories. Shift+Tab 2 次回到 EDITBOX, Tab 到 Files)
    mon_cmd(s, 'sendkey shift-tab', wait=0.4); time.sleep(0.2)
    mon_cmd(s, 'sendkey shift-tab', wait=0.4); time.sleep(0.2)
    mon_cmd(s, 'sendkey tab', wait=0.4); time.sleep(0.2)

    p_pre = dump_vga(s, 'before_choose')
    print('--- before choose (Files list) ---')
    for r in vga_text(p_pre)[:8]: print(r)

    # Files 焦点后, 直接回车 (LB_CHOOSE → ID_OK)
    mon_cmd(s, 'sendkey ret', wait=1.5)
    time.sleep(1.0)

    p = dump_vga(s, 'after_open')
    rows = vga_text(p)
    print('--- after open file (should be loaded, title contains filename) ---')
    for r in rows[:6]: print(r)
    print('...')
    for r in rows[6:18]: print(r)

    has_hello = any('hello' in r for r in rows)
    has_path = any('USR' in r and 'SRC' in r for r in rows)
    print('---')
    print('File content "hello" visible:', has_hello)
    print('A:/USR/SRC path present:', has_path)

    # Esc 关 EDIT
    mon_cmd(s, 'sendkey esc', wait=0.4)
    time.sleep(0.3)
    mon_cmd(s, 'sendkey esc', wait=0.4)
    time.sleep(0.5)
    p2 = dump_vga(s, 'back_prompt')
    print('--- back to prompt ---')
    for r in vga_text(p2): print(r)

    # 测试 2: 跑一个 C 程序 + 输入, 验证文本光标跟随
    send_chars(s, 'inp', per=0.18)
    mon_cmd(s, 'sendkey ret', wait=1.5)
    time.sleep(0.3)
    # 给个 5 个字符的输入
    for c in 'abcde':
        mon_cmd(s, 'sendkey ' + c, wait=0.2)
    time.sleep(0.3)
    mon_cmd(s, 'sendkey ret', wait=0.4)  # inp 应该回显
    time.sleep(0.3)
    mon_cmd(s, 'sendkey ret', wait=0.4)  # 退出 inp
    time.sleep(0.5)

    p3 = dump_vga(s, 'after_inp')
    print('--- after C program inp (no residue expected) ---')
    for r in vga_text(p3): print(r)

    mon_cmd(s, 'quit', wait=0.3)
finally:
    try: qemu.wait(timeout=5)
    except subprocess.TimeoutExpired: qemu.kill()
