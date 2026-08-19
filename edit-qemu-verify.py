# 一次性验证: QEMU 自动启动 AMUNOS 三盘, 注入 EDIT, 截屏看 FreeDOS Edit UI。
# 用法: wsl -e bash -c "cd /mnt/c/Users/XU/Desktop/OSDev && python3 edit-qemu-verify.py"
import socket, subprocess, time, os

MON = 45457
ROOT = '/mnt/c/Users/XU/Desktop/OSDev'
PNG1 = ROOT + '/edit-boot.png'
PNG2 = ROOT + '/edit-editor.png'
PNG3 = ROOT + '/edit-filedlg.png'
SERLOG = ROOT + '/boot.serial'

def mon_cmd(s, cmd, wait=0.5):
    try:
        s.sendall(cmd.encode() + b'\n')
    except OSError:
        return b''
    time.sleep(wait)
    s.settimeout(0.8)
    out = b''
    try:
        while True:
            c = s.recv(65536)
            if not c:
                break
            out += c
    except socket.timeout:
        pass
    return out

qemu = subprocess.Popen([
    'qemu-system-i386',
    '-rtc', 'base=localtime',
    '-hda', ROOT + '/A.img',
    '-hdb', ROOT + '/B.img',
    '-hdc', ROOT + '/C.img',
    '-display', 'none',
    '-monitor', 'tcp:127.0.0.1:%d,server,nowait' % MON,
    '-serial', 'file:' + SERLOG,
    '-no-reboot',
], cwd=ROOT)

try:
    # 等 monitor 就绪 + 内核启动
    s = None
    for _ in range(30):
        try:
            s = socket.create_connection(('127.0.0.1', MON), timeout=2)
            break
        except OSError:
            time.sleep(0.5)
    if s is None:
        print('FAIL: monitor not reachable')
        qemu.kill()
        raise SystemExit(1)

    time.sleep(2)
    mon_cmd(s, 'info status')
    mon_cmd(s, 'screendump %s -f png' % PNG1, wait=0.6)
    print('boot screenshot saved')

    # 输入 EDIT 回车
    for c in list('edit'):
        mon_cmd(s, 'sendkey %s' % c, wait=0.12)
    mon_cmd(s, 'sendkey ret', wait=1.5)
    time.sleep(2)
    mon_cmd(s, 'screendump %s -f png' % PNG2, wait=0.6)
    print('editor screenshot saved')

    # Ctrl+O 打开文件对话框 (走 SYS_READDIR)
    mon_cmd(s, 'sendkey ctrl-o', wait=1.2)
    time.sleep(2)
    mon_cmd(s, 'screendump %s -f png' % PNG3, wait=0.6)
    print('filedialog screenshot saved')

    mon_cmd(s, 'quit', wait=0.3)
finally:
    try:
        qemu.wait(timeout=5)
    except subprocess.TimeoutExpired:
        qemu.kill()
    if os.path.exists(SERLOG):
        print('--- boot.serial ---')
        with open(SERLOG, 'rb') as f:
            print(f.read().decode('ascii', 'replace'))
