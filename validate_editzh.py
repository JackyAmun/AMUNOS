#!/usr/bin/env python3
# validate_editzh.py — v6.8.1:
#   A3  shell DIR 列 GB2312 中文文件名  (put_cjk_str)
#   B2  EDIT 打开并显示中文文件  (wputs-CJK:  UTF-8 与 GB2312 内容)
#   B2c EDIT 退格整字删(视觉效果, 不落盘)
# 输入经 COM1 双向串口 tcp 驱动 (kbd 单槽缓冲对 sendkey 丢字不可靠)。
import socket, subprocess, time, os, struct, sys
MON  = 45475    # QMP/monitor
SPORT= 4565     # 串口 tcp (QEMU -serial, server=listen)
ROOT = '/mnt/c/Users/XU/Desktop/OSDev'
SERLOG = ROOT + '/vedzh.ser'
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
def sio(ss, data):
    """向串口写字节(驱动 shell/EDIT 输入), 稍加间隔避免 FIFO 满。"""
    for b in data:
        try: ss.sendall(bytes([b])); time.sleep(0.05)
        except OSError: break
def sio_read(ss, wait=0.3):
    time.sleep(wait); ss.settimeout(0.3); out=b''
    try:
        while True:
            c=ss.recv(65536)
            if not c: break
            out+=c
    except socket.timeout: pass
    return out
def pix(d,bpl,x,y):
    off=y*bpl+x*2; return d[off]|(d[off+1]<<8)
def hzk_glyph(gbH,gbL):
    hz=open(ROOT+'/HZK16','rb').read()
    o=((gbH-0xA1)*94+(gbL-0xA1))*32
    bm=hz[o:o+32]
    return [[ (bm[r*2]>>(7-c))&1 for c in range(8)]+[ (bm[r*2+1]>>(7-c))&1 for c in range(8)] for r in range(16)]
def find_glyph(d,bpl,w,h,glyph):
    for cy in range(0,min(400,h)-16,16):
        for cx in range(0,w-16,8):
            cnt={}
            for r in range(16):
                for c in range(16):
                    vv=pix(d,bpl,cx+c,cy+r); cnt[vv]=cnt.get(vv,0)+1
            if len(cnt)<2: continue
            pairs=sorted(cnt.items(),key=lambda kv:-kv[1])
            bg=pairs[0][0]; fg=pairs[1][0]
            if pairs[1][1]<8: continue
            miss=0
            for r in range(16):
                for c in range(16):
                    want=fg if glyph[r][c] else bg
                    if pix(d,bpl,cx+c,cy+r)!=want: miss+=1
            if miss<=16: return (cx,cy)
    return None
def find_any(d,bpl,w,h,glyphs):
    for g in glyphs:
        p=find_glyph(d,bpl,w,h,hzk_glyph(*g))
        if p is not None: return (g,p)
    return None
def load_fb_params(m):
    mon_cmd(m,'pmemsave 0x1500 16 "%s/fbinfo.bin"'%ROOT,wait=0.4)
    fb=open(ROOT+'/fbinfo.bin','rb').read()
    return struct.unpack('<I',fb[2:6])[0],struct.unpack('<H',fb[6:8])[0],\
           struct.unpack('<H',fb[8:10])[0],struct.unpack('<H',fb[11:13])[0],fb[10]
def dump_fb(m,base,n,p):
    mon_cmd(m,'pmemsave 0x%x 0x%x "%s"'%(base,n,p),wait=0.8)
    return open(p,'rb').read()
def boot():
    os.system("ps aux | grep qemu | grep -v grep | awk '{print $2}' | xargs -r kill -9; sleep 0.5")
    q=subprocess.Popen(['qemu-system-i386','-rtc','base=localtime',
        '-hda',ROOT+'/A.img','-hdb',ROOT+'/B.img','-hdc',ROOT+'/C.img',
        '-display','none','-monitor','tcp:127.0.0.1:%d,server,nowait'%MON,
        '-serial','tcp:127.0.0.1:%d,server,nowait'%SPORT,'-no-reboot'],cwd=ROOT)
    time.sleep(2)
    m=ss=None
    for _ in range(80):
        if q.poll() is not None: break
        try:
            m=m or socket.create_connection(('127.0.0.1',MON),timeout=1)
            ss=ss or socket.create_connection(('127.0.0.1',SPORT),timeout=1)
            if m and ss: break
        except OSError: time.sleep(0.3)
    base,w,h,bpl,bpp=load_fb_params(m)
    time.sleep(2); sio_read(ss,0.2)   # 排空启动串口输出
    return q,m,ss,base,w,h,bpl
def end(q,m):
    try: mon_cmd(m,'quit',wait=0.3)
    except Exception: pass
    try: q.wait(timeout=6)
    except subprocess.TimeoutExpired: q.kill()

G={'中':(0xD6,0xD0),'文':(0xCE,0xC4),'内':(0xCA,0xD4),'容':(0xC8,0xDD),'编':(0xB1,0xE0),'码':(0xC2,0xEB)}
# "这是(GB/UTF8 内容行) 编码的中文内容..." — 任一命中即证明该行中文已渲染
CONTENT_CHARS=[(0xD5,0xE2),(0xCA,0xC7),(0xB1,0xE0),(0xC2,0xEB),(0xB5,0xC4),
               (0xD6,0xD0),(0xCE,0xC4),(0xCA,0xD4),(0xC8,0xDD)]

def main():
    R=[]
    # ---- 场景1: shell DIR 列中文文件名 ----
    q,m,ss,base,w,h,bpl=boot()
    sio(ss,b'dir\r'); sio_read(ss,0.2); time.sleep(0.3)
    d=dump_fb(m,base,w*h*2,ROOT+'/fb.dir.bin')
    a3 = find_glyph(d,bpl,w,h,hzk_glyph(*G['中'])) is not None and find_glyph(d,bpl,w,h,hzk_glyph(*G['文'])) is not None
    print('A3 DIR 中文文件名:', 'PASS' if a3 else 'FAIL'); R.append(a3); end(q,m)

    # ---- 场景2: EDIT 打开 UTF-8 文件 (串口完整命令) ----
    q,m,ss,base,w,h,bpl=boot()
    sio(ss,b'edit UTF8_CN.TXT\r'); time.sleep(1.8)
    d=dump_fb(m,base,w*h*2,ROOT+'/fb.utf8.bin')
    hit=find_any(d,bpl,w,h,CONTENT_CHARS)
    b2a = hit is not None
    print('B2a EDIT 显示 UTF-8(命中%s):'%(str(hit and hit[0] or None)), 'PASS' if b2a else 'FAIL'); R.append(b2a)
    end(q,m)

    # ---- 场景3: EDIT 打开 GB 文件 + 退格整字删视觉 ----
    q,m,ss,base,w,h,bpl=boot()
    sio(ss,b'edit GB_CN.TXT\r'); time.sleep(1.8)
    d=dump_fb(m,base,w*h*2,ROOT+'/fb.gb.bin')
    hit=find_any(d,bpl,w,h,CONTENT_CHARS)
    b2b = hit is not None
    print('B2b EDIT 显示 GB2312(命中%s):'%(str(hit and hit[0] or None)), 'PASS' if b2b else 'FAIL'); R.append(b2b)
    # B2c 退格整字删: 首字"这"占 2 格, 右箭头(Forward mb 跳 2 格)后 BackSpace 应整字删,
    #   使"是"成为行首 (若只删半个字节, 残剩高字节 0xD5 会渲染成乱码而非整字消失)
    before_zhe = find_glyph(d,bpl,w,h,hzk_glyph(0xD5,0xE2))     # 这 删前
    sio(ss,b'\x1b[C'); time.sleep(0.4)                          # RightArrow → 跳到"这"之后
    sio(ss,b'\x08');  time.sleep(0.8)                           # BackSpace → 整字删"这"
    d2=dump_fb(m,base,w*h*2,ROOT+'/fb.gb.bs.bin')
    zhe_after = find_glyph(d2,bpl,w,h,hzk_glyph(0xD5,0xE2))     # 这 删后
    shi_after = find_glyph(d2,bpl,w,h,hzk_glyph(0xCA,0xC7))     # 是 应成为行首
    b2c = before_zhe is not None and zhe_after is None and shi_after is not None
    print('B2c 退格整字删(视觉): 删前这@%s 删后这@%s 是@%s ->'%(before_zhe,zhe_after,shi_after), 'PASS' if b2c else 'FAIL')
    R.append(b2c); end(q,m)
    print('EDITZH OVERALL', 'PASS' if all(R) else 'FAIL')

if __name__=='__main__':
    main()