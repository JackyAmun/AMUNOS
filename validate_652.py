import socket, subprocess, time, os, sys
MON = 45473
ROOT = '/mnt/c/Users/XU/Desktop/OSDev'
SERIAL = ROOT + '/v652c.serial'
def mon_cmd(s, cmd, wait=0.4):
    try: s.sendall((cmd+'\n').encode())
    except OSError: return b''
    time.sleep(wait); s.settimeout(0.5); out=b''
    try:
        while True:
            c=s.recv(65536)
            if not c: break
            out+=c
    except socket.timeout: pass
    return out
def dump_vga(s, tag):
    p=os.path.join(ROOT,'vga.v652c.%s.bin'%tag); mon_cmd(s,'pmemsave 0xb8000 0xfa0 "%s"'%p,wait=0.6); return p
def vga_text(p):
    d=open(p,'rb').read(); rows=[]
    for y in range(25):
        line=''
        for x in range(80):
            ch=d[(y*80+x)*2]; line += chr(ch) if 32<=ch<=126 else ' '
        rows.append(line)
    return rows
def ch(s2, per=0.15):
    for c in s2: mon_cmd(s,'sendkey '+('sp' if c==' ' else c),wait=per)

os.system("ps aux | grep qemu | grep -v grep | awk '{print $2}' | xargs -r kill -9 2>/dev/null; sleep 0.5")
qemu=subprocess.Popen(['qemu-system-i386','-rtc','base=localtime',
    '-hda',ROOT+'/A.img','-hdb',ROOT+'/B.img','-hdc',ROOT+'/C.img',
    '-display','none','-monitor','tcp:127.0.0.1:%d,server,nowait'%MON,
    '-serial','file:'+SERIAL,'-no-reboot'],cwd=ROOT)
try:
    s=None
    for _ in range(40):
        try: s=socket.create_connection(('127.0.0.1',MON),timeout=2); break
        except OSError: time.sleep(0.5)
    if not s: print('FAIL monitor'); qemu.kill(); sys.exit(1)
    time.sleep(2)
    ch('tsrc')
    mon_cmd(s,'sendkey ret',wait=2.0)
    time.sleep(1.5)
    # 输入 abc 然后回车
    for c in 'abc':
        mon_cmd(s,'sendkey '+c,wait=0.25)
    mon_cmd(s,'sendkey ret',wait=0.5)
    time.sleep(0.3)
    mon_cmd(s,'sendkey ctrl-c',wait=0.5)
    time.sleep(0.5)
    p=dump_vga(s,'final')
    print('--- VGA dump ---')
    for r in vga_text(p): print(r)
    mon_cmd(s,'quit',wait=0.3)
finally:
    try: qemu.wait(timeout=5)
    except subprocess.TimeoutExpired: qemu.kill()
