#!/usr/bin/env python3
# qtest.py — 快速 QEMU 冒烟测试驱动 (v6.5.6 调试提速)
# 单进程连 monitor, 整串命令一次发送; 用法:
#   python3 qtest.py "dir" "type hello.txt" "d:"     # 逐条 sendkey 输入并回车
#   python3 qtest.py --wait 14 --keydelay 0.05 ...   # 自定义开机等待/键间隔
#   串口输出打到最后 (来自 /tmp/ser.log)
import socket, subprocess, sys, time, os

SOCK = '/tmp/mon.sock'
LOG = '/tmp/ser.log'

KEYMAP = {' ': 'spc', '.': 'dot', '>': 'shift-dot', ':': 'shift-semicolon',
          '/': 'slash', '\\': 'backslash', '-': 'minus', '=': 'equal',
          '\n': 'ret'}

def mon_cmds(cmds, wait=0.12):
    s = socket.socket(socket.AF_UNIX)
    s.connect(SOCK)
    s.settimeout(0.3)
    try: s.recv(65536)
    except OSError: pass
    for c in cmds:
        s.sendall((c + '\n').encode())
        time.sleep(wait)
    s.close()

def type_line(line):
    cmds = []
    for ch in line:
        if ch.isupper():
            cmds.append('sendkey shift-' + ch.lower())
        else:
            cmds.append('sendkey ' + KEYMAP.get(ch, ch))
    cmds.append('sendkey ret')
    mon_cmds(cmds)

def main():
    args = sys.argv[1:]
    wait = 14.0
    kd = 0.06
    cd = None
    ahci = False
    if args and args[0] == '--cd':
        cd = args[1]; args = args[2:]
    if args and args[0] == '--ahci':
        ahci = True; args = args[1:]
    if args and args[0] == '--wait':
        wait = float(args[1]); args = args[2:]
    subprocess.run(['rm', '-f', LOG])
    qargs = ['qemu-system-i386', '-nographic', '-rtc', 'base=localtime',
             '-hda', 'A.img', '-hdb', 'B.img', '-hdc', 'C.img']
    if ahci:
        # q35 内置 ICH9 AHCI (bus ide.0..ide.5): D32 挂 bus=ide.1 避开 -hdb
        qargs += ['-M', 'q35',
                  '-drive', 'if=none,id=s0,file=D32.img,format=raw',
                  '-device', 'ide-hd,drive=s0,bus=ide.4,unit=0']
    elif not cd:
        qargs += ['-hdd', 'D32.img']
    if cd:
        qargs += ['-drive', 'if=none,id=cd0,file=' + cd + ',media=cdrom',
                  '-device', 'ide-cd,drive=cd0,bus=ide.1,unit=1']
    qargs += ['-serial', 'file:' + LOG,
              '-monitor', 'unix:' + SOCK + ',server,nowait',
              '-display', 'none']
    p = subprocess.Popen(qargs,
                         stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    time.sleep(wait)
    if p.poll() is not None:
        print('QEMU exited early:', p.stderr.read().decode(errors='replace'))
        return
    for line in args:
        type_line(line)
        time.sleep(1.2)
    time.sleep(2)
    mon_cmds(['quit'])
    time.sleep(1)
    p.terminate()
    try:
        print(open(LOG, encoding='utf-8', errors='replace').read()[-4000:])
    except FileNotFoundError:
        print('no serial log')

if __name__ == '__main__':
    main()
