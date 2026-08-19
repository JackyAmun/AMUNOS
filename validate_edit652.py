import socket, subprocess, time, os, sys
MON = 45474
ROOT = '/mnt/c/Users/XU/Desktop/OSDev'
SERIAL = ROOT + '/v652e.serial'
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
    p=os.path.join(ROOT,'vga.v652e.%s.bin'%tag); mon_cmd(s,'pmemsave 0xb8000 0xfa0 "%s"'%p,wait=0.6); return p
def vga_text(p):
    d=open(p,'rb').read(); rows=[]
    for y in range(25):
        line=''
        for x in range(80):
            ch=d[(y*80+x)*2]; line += chr(ch) if 32<=ch<=126 else ' '
        rows.append(line)
    return rows
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
    for c in 'edit': mon_cmd(s,'sendkey '+c,wait=0.15)
    mon_cmd(s,'sendkey ret',wait=2.5)
    time.sleep(0.8)
    mon_cmd(s,'sendkey ctrl-o',wait=1.5)
    time.sleep(0.8)
    # 焦点: 最初在 ID_FILENAME editbox. Tab->Files, Tab->Directories
    mon_cmd(s,'sendkey tab',wait=0.4); time.sleep(0.2)
    mon_cmd(s,'sendkey tab',wait=0.4); time.sleep(0.2)
    # Directories 排序: BOOT,BIN,USR. 到 USR = down x2
    mon_cmd(s,'sendkey down',wait=0.4); time.sleep(0.3)
    mon_cmd(s,'sendkey down',wait=0.4); time.sleep(0.3)
    mon_cmd(s,'sendkey ret',wait=1.0); time.sleep(0.5)
    # 进 USR: 子目录 INCLUDE,LIB,SRC. 到 SRC = down x2
    mon_cmd(s,'sendkey down',wait=0.4); time.sleep(0.3)
    mon_cmd(s,'sendkey down',wait=0.4); time.sleep(0.3)
    mon_cmd(s,'sendkey ret',wait=1.0); time.sleep(0.5)
    p1=dump_vga(s,'in_usr_src')
    rows=vga_text(p1)
    print('--- in A:/USR/SRC (path field should show it) ---')
    for r in rows[:12]: print(r)
    # 切到 Files 列表: Directories 焦点 -> Shift-Tab x2 回 EDITBOX -> Tab 到 Files
    mon_cmd(s,'sendkey shift-tab',wait=0.4); time.sleep(0.2)
    mon_cmd(s,'sendkey shift-tab',wait=0.4); time.sleep(0.2)
    mon_cmd(s,'sendkey tab',wait=0.4); time.sleep(0.3)
    # Files 焦点, 直接回车打开第一项 (INP.C)
    mon_cmd(s,'sendkey ret',wait=1.5); time.sleep(1.0)
    p2=dump_vga(s,'after_open')
    rows2=vga_text(p2)
    print('--- after open (INP.C should be loaded) ---')
    for r in rows2[:20]: print(r)
    has_src_path=any('USR' in r and 'SRC' in r for r in rows)
    has_inp_content=any('inp.c' in r for r in rows2)
    print('---')
    print('Dialog path shows USR/SRC:', has_src_path)
    print('Editor loaded inp.c content:', has_inp_content)
    mon_cmd(s,'quit',wait=0.3)
finally:
    try: qemu.wait(timeout=5)
    except subprocess.TimeoutExpired: qemu.kill()
