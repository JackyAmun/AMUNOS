import socket, subprocess, time, os, sys, struct
MON = 45474
ROOT = '/mnt/c/Users/XU/Desktop/OSDev'
SERIAL = ROOT + '/vzh.serial'
def mon_cmd(s, cmd, wait=0.5):
    try: s.sendall((cmd+'\n').encode())
    except OSError: return b''
    time.sleep(wait); s.settimeout(0.6); out=b''
    try:
        while True:
            c=s.recv(65536)
            if not c: break
            out+=c
    except socket.timeout: pass
    return out
def ch(s2, per=0.15):
    for c in s2: mon_cmd(s,'sendkey '+c,wait=per)

os.system("ps aux | grep qemu | grep -v grep | awk '{print $2}' | xargs -r kill -9 2>/dev/null; sleep 0.5")
qemu=subprocess.Popen(['qemu-system-i386','-rtc','base=localtime',
    '-hda',ROOT+'/A.img','-hdb',ROOT+'/B.img','-hdc',ROOT+'/C.img',
    '-display','none','-monitor','tcp:127.0.0.1:%d,server,nowait'%MON,
    '-serial','file:'+SERIAL,'-no-reboot'],cwd=ROOT)
def rgb565(v):
    r=(v>>11)&31; g=(v>>5)&63; b=v&31
    return (r<<3, g<<2, b<<3)
def dump_fb(s, base, n, p):
    mon_cmd(s,'pmemsave 0x%x 0x%x "%s"'%(base,n,p),wait=0.8)
    return open(p,'rb').read()
def load_fb_params(s):
    mon_cmd(s,'pmemsave 0x1500 16 "%s/fbinfo.bin"'%ROOT,wait=0.4)
    fb=open(ROOT+'/fbinfo.bin','rb').read()
    flag=fb[0]; base=struct.unpack('<I',fb[2:6])[0]
    w=struct.unpack('<H',fb[6:8])[0]; h=struct.unpack('<H',fb[8:10])[0]
    bpp=fb[10]; bpl=struct.unpack('<H',fb[11:13])[0]
    return flag,base,w,h,bpp,bpl
def pix(d,bpl,x,y):
    off=y*bpl+x*2; return d[off]|(d[off+1]<<8)
def count_nonblack(d,bpl,x0,y0,x1,y1):
    c=0
    for y in range(y0,y1):
        for x in range(x0,x1):
            if pix(d,bpl,x,y)!=0: c+=1
    return c
def hzk_glyph(gbH,gbL):
    hz=open(ROOT+'/HZK16','rb').read()
    o=((gbH-0xA1)*94+(gbL-0xA1))*32
    bm=hz[o:o+32]
    # 16x16 bits, bit7..0 per byte-pair per row
    return [[ (bm[r*2]>>(7-c))&1 for c in range(8)] + [ (bm[r*2+1]>>(7-c))&1 for c in range(8)] for r in range(16)]
def find_glyph(d,bpl,w,h,glyph,fg,bg):
    """search 16x16 grid-aligned region matching glyph (fg where bit, bg where clear) in visible band"""
    for gy in range(0,min(400,h)-16,16):
        for gx in range(0,w-16,8):
            ok=True
            for r in range(16):
                for c in range(16):
                    v=pix(d,bpl,gx+c,gy+r)
                    want = fg if glyph[r][c] else bg
                    if v!=want: ok=False; break
                if not ok: break
            if ok: return (gx,gy)
    return None
def get_serial():
    try:
        with open(SERIAL,'rb') as f: return f.read().decode('utf-8','replace')
    except Exception: return ''
def wait_serial_has(sub, tries=40):
    for _ in range(tries):
        if sub in get_serial(): return True
        time.sleep(0.25)
    return False

try:
    time.sleep(2)
    if qemu.poll() is not None:
        print('QEMU exited early rc=%s'%qemu.poll()); qemu.kill(); sys.exit(1)
    s=None
    for _ in range(60):
        if qemu.poll() is not None: break
        try: s=socket.create_connection(('127.0.0.1',MON),timeout=1); break
        except OSError: time.sleep(0.3)
    if not s: print('FAIL monitor (qemu alive=%s)'%(qemu.poll() is None)); qemu.kill(); sys.exit(1)
    time.sleep(3)
    flag,base,w,h,bpp,bpl=load_fb_params(s)
    print(f'fb: flag={flag} base=0x{base:x} w={w} h={h} bpp={bpp} bpl={bpl}')

    # ---- Test 1: 字库在 A: (串口确认, 不依赖 C:) ----
    t1 = wait_serial_has('HZK16 loaded')
    print('T1 HZK16 loaded from A:', 'PASS' if t1 else 'FAIL', '| serial:',
          [l for l in get_serial().splitlines() if 'HZK16' in l])

    # ---- Test 2: zh 在可见区渲染汉字 (搜索 HZK16 '你' 字形) ----
    ch('zh'); mon_cmd(s,'sendkey ret',wait=0.8)
    time.sleep(0.3)
    d=dump_fb(s,base,w*h*2,ROOT+'/fb.zh.bin')
    fg=0xFFFF; bg=0x0000
    pos=find_glyph(d,bpl,w,h,hzk_glyph(0xC4,0xE3),fg,bg)   # 你 = C4E3
    # also verify a second char to be robust
    pos2=find_glyph(d,bpl,w,h,hzk_glyph(0xBA,0xC3),fg,bg)  # 好 = BAC3
    t2 = pos is not None and pos2 is not None
    print('T2 zh visible CJK (你@%s 好@%s):'%(pos,pos2), 'PASS' if t2 else 'FAIL')

    # ---- Test 3: EDIT 在图形模式渲染 (UI 出现, 不再是空白/残留 shell) ----
    ch('edit'); mon_cmd(s,'sendkey ret',wait=1.0)
    time.sleep(0.8)
    d2=dump_fb(s,base,w*h*2,ROOT+'/fb.edit.bin')
    tb=count_nonblack(d2,bpl,0,0,w,min(400,h))
    row0=count_nonblack(d2,bpl,0,0,w,16)      # 菜单栏行
    t3 = tb>4000 and row0>100
    print('T3 EDIT UI text-band=%d row0(menu)=%d:'%(tb,row0), 'PASS' if t3 else 'FAIL')

    print('OVERALL', 'PASS' if (t1 and t2 and t3) else 'FAIL')
    mon_cmd(s,'quit',wait=0.3)
finally:
    try: qemu.wait(timeout=5)
    except subprocess.TimeoutExpired: qemu.kill()
