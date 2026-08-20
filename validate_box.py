#!/usr/bin/env python3
# validate_box.py — v6.8.1:
#   验证 EDIT 窗框 (dflat.h 框线码改到 0x80-0x91 专用带后) 在帧缓冲按像素画成
#   干净的横线/竖线, 而不是被 GB2312 分组误判成中文/字母。
# 判据(针对实际渲染的形状):  A) 存在整行双像素水平线 (两相邻 y 行连续 ≥60/80 格
#   col3-4 同时亮起 == 横向框线); B) 左/右边为连续纵向 col3-4 线 (对应 SIDE).
from collections import Counter
import sys
from validate_editzh import boot,end,dump_fb,sio,sio_read,pix,ROOT

def main():
    q,m,ss,base,w,h,bpl=boot()
    sio(ss,b'edit UTF8_CN.TXT\r'); time.sleep(1.8)
    d=dump_fb(m,base,w*h*2,ROOT+'/fb.box.bin')

    # A) 横向框线: 顶部字符行 R 的 2 条框线像素行 (R*16+7, R*16+8) 都连续铺满
    #    ≥70/80 格为整 horizontal box line (EDIT 文本窗顶栏 y=39,40 实测 80/80).
    def fullcells(y):
        return sum(1 for x in range(0,w-7,8) if all(pix(d,bpl,x+c,y)!=0 for c in range(8)))
    hline_row=None
    for R in range(0,6):                       # 只查顶部区域(菜单/窗顶)
        if fullcells(R*16+7)>=70 and fullcells(R*16+8)>=70:
            hline_row=R; break

    # B) 左/右连续竖线: 字符列0 与列79, 竖向连续 ≥10 个 16px 行有 col3-4
    def vertrun(col):
        cnt=0; best=0
        for cy in range(25):
            px=col*8
            lit=pix(d,bpl,px+3,cy*16)!=0 and pix(d,bpl,px+4,cy*16)!=0 and \
               pix(d,bpl,px+3,cy*16+7)!=0
            cnt=cnt+1 if lit else 0
            best=max(best,cnt)
        return best
    vL=vertrun(0); vR=vertrun(79)

    print('top horizontal line at char row=%s'%hline_row)
    print('left vertical run rows=%d  right vertical run rows=%d'%(vL,vR))
    ok = hline_row is not None and vL>=8 and vR>=8
    print('BOXFIX OVERALL', 'PASS' if ok else 'FAIL')
    end(q,m)
    return 0 if ok else 1

if __name__=='__main__':
    import time
    sys.exit(main())