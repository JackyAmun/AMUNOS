#!/usr/bin/env python3
"""validate_gui.py — GUI 窗口服务器回归 (v6.9)

QEMU 自动化: boot → 运行 GUI 控件演示 → 用 pmemsave 抓帧缓冲像素断言:
  T1  GUI 渲染: 桌面底色 + 窗口标题栏 + 按钮边框 + 列表白色底
  T2  弹窗覆盖/复原 (核心): 点"弹窗"→ 弹窗出现且覆盖下层; 点 OK → 关闭且
      下层像素精确复原 (0 diff) — 验证消灭"汉字/像素覆盖残留"bug
  T3  列表点选: 点击项 → 选中高亮 (蓝色) 出现
  T4  输入框键盘: 聚焦输入框 → 键入字符 → 输入框区域像素变化 (回显)
运行: wsl -e bash -c "python3 validate_gui.py"   (qemu 装于 WSL, localhost 共享)
"""
import socket, subprocess, time, os, sys, struct

MON = 44599
ROOT = '/mnt/c/Users/XU/Desktop/OSDev'
SERIAL = ROOT + '/vgui.serial'

def mon_cmd(s, cmd, wait=0.5):
    try:
        s.sendall((cmd + '\n').encode())
    except OSError:
        return b''
    time.sleep(wait); s.settimeout(0.6); out = b''
    try:
        while True:
            c = s.recv(65536)
            if not c: break
            out += c
    except socket.timeout:
        pass
    return out

def ch(s, txt, per=0.07):
    for c in txt: mon_cmd(s, 'sendkey ' + c, wait=per)

os.system("ps aux | grep qemu | grep -v grep | awk '{print $2}' | xargs -r kill -9 2>/dev/null; sleep 0.5")
qemu = subprocess.Popen(['qemu-system-i386', '-rtc', 'base=localtime',
    '-hda', ROOT + '/A.img', '-hdb', ROOT + '/B.img', '-hdc', ROOT + '/C.img',
    '-display', 'none', '-monitor', 'tcp:127.0.0.1:%d,server,nowait' % MON,
    '-serial', 'file:' + SERIAL, '-no-reboot'], cwd=ROOT)

def load_fb_params(s):
    mon_cmd(s, 'pmemsave 0x1500 16 "%s/fbinfo.bin"' % ROOT, 0.4)
    fb = open(ROOT + '/fbinfo.bin', 'rb').read()
    flag = fb[0]; base = struct.unpack('<I', fb[2:6])[0]
    w = struct.unpack('<H', fb[6:8])[0]; h = struct.unpack('<H', fb[8:10])[0]
    bpl = struct.unpack('<H', fb[11:13])[0]
    return flag, base, w, h, bpl

def dump_fb(s, p):
    mon_cmd(s, 'pmemsave 0xfd000000 0x96000 "%s"' % p, 0.7)
    return bytearray(open(p, 'rb').read())

def pix(d, bpl, x, y):
    o = y * bpl + x * 2
    return d[o] | (d[o + 1] << 8)

def cnt(d, bpl, c0, x0, y0, x1, y1):
    r = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            if pix(d, bpl, x, y) == c0: r += 1
    return r

def reg_pixels(d, bpl, x0, y0, x1, y1, step=3):
    return [pix(d, bpl, x, y) for y in range(y0, y1, step) for x in range(x0, x1, step)]

def get_serial():
    try:
        return open(SERIAL, 'rb').read().decode('utf-8', 'replace')
    except Exception:
        return ''

results = {}
try:
    time.sleep(2)
    if qemu.poll() is not None:
        print('QEMU exited early rc=%s' % qemu.poll()); sys.exit(1)
    s = None
    for _ in range(60):
        if qemu.poll() is not None: break
        try:
            s = socket.create_connection(('127.0.0.1', MON), timeout=1); break
        except OSError:
            time.sleep(0.3)
    if not s:
        print('FAIL monitor'); qemu.kill(); sys.exit(1)
    time.sleep(3)
    flag, base, w, h, bpl = load_fb_params(s)
    print('fb: flag=%d base=0x%x w=%d h=%d bpl=%d' % (flag, base, w, h, bpl))

    # 启动 GUI 演示
    ch(s, 'gui'); mon_cmd(s, 'sendkey ret', 0.8); time.sleep(1.2)

    cur = [320, 200]                       # 内核鼠标初始像素位置 (屏幕中心)
    PARK = (500, 20)                       # 停靠点: 桌面右上, 避开全部检查区
    def mm(tx, ty, wait=0.4):              # 移动鼠标到绝对像素 (小步分帧, 防丢包)
        while abs(tx - cur[0]) > 80 or abs(ty - cur[1]) > 80:
            sx = 80 if tx > cur[0] else -80
            sy = 80 if ty > cur[1] else -80
            dx = tx - cur[0]; dy = ty - cur[1]
            nx = cur[0] + (sx if abs(dx) > 80 else dx)
            ny = cur[1] + (sy if abs(dy) > 80 else dy)
            mon_cmd(s, 'mouse_move %d %d' % (nx - cur[0], ny - cur[1]), wait)
            cur[0], cur[1] = nx, ny
        mon_cmd(s, 'mouse_move %d %d' % (tx - cur[0], ty - cur[1]), wait)
        cur[0], cur[1] = tx, ty
    def click(tx, ty, hold=0.9):           # 移动+按下+长按+释放
        mm(tx, ty); time.sleep(0.25)
        mon_cmd(s, 'mouse_button 1', 0.05)
        time.sleep(hold)                   # 长按让 demo 事件循环必捕到上升沿
        mon_cmd(s, 'mouse_button 0', 0.05)
        time.sleep(0.5)                    # 释放后复位 prev_lbutton
    def click_until(tx, ty, pred, tries=6, settle=0.4):
        """点击直到屏幕状态满足 pred() (QEMU 注入偶发丢点击, 重试直到生效)。"""
        for _ in range(tries):
            click(tx, ty)
            time.sleep(settle)
            if pred():
                return True
        return False
    def popup_open_state():
        return cnt(dump_fb(s, ROOT + '/vgpop.bin'), bpl, 0xF7BE,
                   PX0, PY0, PX1, PY1) > 20000   # 弹窗体填满区域 (>2 万 = 弹窗开)
    def popup_closed_state():
        return cnt(dump_fb(s, ROOT + '/vgpop.bin'), bpl, 0xF7BE,
                   PX0, PY0, PX1, PY1) < 15000   # 弹窗关: 只剩主窗右侧窗底

    mm(*PARK); time.sleep(0.3)              # 先把光标停到桌面右上 (避开全部检查区)

    # ── T1 渲染 ──
    d = dump_fb(s, ROOT + '/vg1.bin')
    desk = cnt(d, bpl, 0x8410, 500, 430, 640, 480)
    btn  = cnt(d, bpl, 0xD69A, 40, 80, 90, 107)
    list = cnt(d, bpl, 0xFFFF, 101, 171, 379, 329)
    t1 = desk > 4000 and btn > 100 and list > 3000
    results['T1 render'] = (t1, 'desk=%d btn=%d list=%d' % (desk, btn, list))

    # ── T1b 标题字形数 (防 UTF-8 被误读成 GB 的乱码回归) ──
    # 标题 "控件演示" = 4 个 CJK 字形, 各占 16px 宽, 白 fg 于标题栏。
    # 乱码 bug: 3 字节 UTF-8 第 2 字节落在 [A1,BF] 时被误读成原始 GB → 字形错位
    # 变多 (旧 bug 渲染出 ~6 个字形含替换框)。只查颜色抓不到, 这里数字形列。
    def title_glyph_cols():
        d = dump_fb(s, ROOT + '/vgt.bin')
        cols = 0
        for gx in range(24, 24 + 6 * 16, 16):      # 6 槽宽扫描
            has = any(pix(d, bpl, x, y) == 0xFFFF
                      for y in range(21, 37) for x in range(gx, gx + 16))
            if has: cols += 1
        return cols
    tg = title_glyph_cols()
    t1b = tg == 4
    results['T1b title glyphs'] = (t1b, 'glyph_cols=%d (want 4)' % tg)

    # ── T2 弹窗覆盖 + 复原 (核心) ──
    PX0, PY0, PX1, PY1 = 170, 180, 470, 300     # 弹窗区域 (居中 300x120)
    d0 = dump_fb(s, ROOT + '/vg2a.bin')         # 光标已停 PARK, 不在区域
    r0 = reg_pixels(d0, bpl, PX0, PY0, PX1, PY1)
    ok_open  = click_until(64, 93, popup_open_state)   # 点"弹窗"直到弹窗出现
    mm(*PARK); time.sleep(0.3)

    # ── T2b 弹窗标签非黑块 (标签 bg 透明; 旧 bug: 空像素画 bg=黑 → 整格黑块) ──
    # 弹窗(170,180) 标签 "中文消息..." at buf(16,40) → LFB(186,220), ~176x16。
    dl = dump_fb(s, ROOT + '/vg2l.bin')
    L0, L1, L2, L3 = 186, 220, 362, 236
    lab_black = sum(1 for y in range(L1, L3) for x in range(L0, L2, 2)
                    if pix(dl, bpl, x, y) == 0x0000)
    lab_winbg = sum(1 for y in range(L1, L3) for x in range(L0, L2, 2)
                    if pix(dl, bpl, x, y) == 0xF7BE)
    lab_total = ((L2 - L0) // 2) * (L3 - L1)
    t2b = 0 < lab_black < lab_total * 0.40 and lab_winbg > 0
    results['T2b dialog label non-block'] = (t2b, 'black=%d winbg=%d/%d' % (lab_black, lab_winbg, lab_total))

    d1 = dump_fb(s, ROOT + '/vg2b.bin')
    r1 = reg_pixels(d1, bpl, PX0, PY0, PX1, PY1)
    ok_close = click_until(304, 271, popup_closed_state)  # 点 OK 直到弹窗关/复原
    mm(*PARK); time.sleep(0.5)
    d2 = dump_fb(s, ROOT + '/vg2c.bin')
    r2 = reg_pixels(d2, bpl, PX0, PY0, PX1, PY1)
    d_open = sum(1 for a, b in zip(r0, r1) if a != b)
    d_rest = sum(1 for a, b in zip(r0, r2) if a != b)
    if d_rest > 0:                                # 诊断: 未复原像素的颜色分布
        from collections import Counter
        diff_px = Counter()
        y = PY0; x = PX0
        for a, b in zip(r0, r2):
            if a != b: diff_px[(a, b)] += 1
            x += 3
            if x >= PX1: x = PX0; y += 3
        print('  RESTORE DIFF colors:', dict(list(diff_px.items())[:6]))
    t2 = ok_open and ok_close and d_open > 50 and d_rest == 0
    results['T2 popup cover+restore'] = (t2, 'open_chg=%d restore_diff=%d' % (d_open, d_rest))

    # ── T3 列表点选 (重试直到高亮出现) ──
    def list_sel():
        d = dump_fb(s, ROOT + '/vg3.bin')
        return cnt(d, bpl, 0x0019, 101, 171, 200, 187) > 300
    ok_list = click_until(120, 178, list_sel)
    d3 = dump_fb(s, ROOT + '/vg3.bin')
    blue = cnt(d3, bpl, 0x0019, 101, 171, 200, 187)
    t3 = ok_list and blue > 300
    results['T3 list select'] = (t3, 'blue=%d' % blue)

    # ── T4 输入框键盘回显 (聚焦 + 键入; 重试直到回显出现) ──
    def edit_caret():                          # 聚焦输入框后出现闪烁块光标 (0xFC30)
        d = dump_fb(s, ROOT + '/vg4c.bin')
        return cnt(d, bpl, 0xFC30, 100, 128, 340, 150) > 2
    def t4_try():
        foc = click_until(220, 138, edit_caret)   # 点输入框中段, 直到聚焦 (光标出现)
        d4 = dump_fb(s, ROOT + '/vg4a.bin')
        r4a = reg_pixels(d4, bpl, 100, 131, 340, 147, 2)
        ch(s, 'abc'); time.sleep(0.3)             # 连打三字符, diff 留足余量
        d5 = dump_fb(s, ROOT + '/vg4b.bin')
        r5 = reg_pixels(d5, bpl, 100, 131, 340, 147, 2)
        return foc and sum(1 for a, b in zip(r4a, r5) if a != b) > 10
    t4 = False
    for _ in range(6):
        if t4_try():
            t4 = True
            break
    results['T4 edit typing'] = (t4, '')

    # ── T5 方向键光标 + 光标处插入 (核心: HOME/←→ 动 caret, 插入在光标处非串尾) ──
    # 编辑框 LFB(100,130,340,148), 文本起点 x=103, 每 ASCII 字形 8px。
    # 光标块(0xFC30)所在 x 列 = 光标位置 → 用像素直接证 caret 动了。
    def caret_x():
        d = dump_fb(s, ROOT + '/vg5c.bin')
        for x in range(101, 220):
            c = sum(1 for y in range(131, 146) if pix(d, bpl, x, y) == 0xFC30)
            if c > 8: return x
        return -1
    click_until(200, 80, lambda: True); time.sleep(0.3)   # 点"清空"按钮 → 内容确定为空
    click_until(220, 138, edit_caret); time.sleep(0.3)    # 点输入框重新聚焦 (光标 x=103)
    ch(s, 'abc'); time.sleep(0.3)                          # "abc" 光标在串尾 x=103+24=127
    cx_end = caret_x()
    mon_cmd(s, 'sendkey home', 0.5); time.sleep(0.3)      # HOME → 光标到串首 x≈103
    cx_home = caret_x()
    sb = dump_fb(s, ROOT + '/vg5b.bin')
    ch(s, 'd'); time.sleep(0.3)                            # 光标处插入 → "dabc" 光标 x≈111
    sa = dump_fb(s, ROOT + '/vg5a.bin')
    cx_ins = caret_x()
    diff_first = sum(1 for yy in range(131, 147, 2) for xx in range(103, 111, 2)
                     if pix(sb, bpl, xx, yy) != pix(sa, bpl, xx, yy))
    # 若为"串尾追加": HOME 后插入会变成 "abcX" 光标 x=135 → cx_ins 断言失败
    t5 = (125 <= cx_end <= 130 and 101 <= cx_home <= 105
          and 109 <= cx_ins <= 113 and diff_first > 0)
    results['T5 arrow caret+insert'] = (t5,
        'end=%d home=%d ins=%d diff=%d' % (cx_end, cx_home, cx_ins, diff_first))

    # ── T6 多行文本区 GW_TEXTAREA + 内容读回 (v6.10) ──
    # 编辑器窗 abs(140,60,420,360); 文本区 rel(8,30,404,260)→abs(148,90..552,350),
    # 正文起点 (150,91), 每行 16px。初始 3 行 + 结尾空行 → 行0-3 于 y91/107/123/139。
    def ta_dark(y0, y1):
        return cnt(dump_fb(s, ROOT + '/vg6a.bin'), bpl, 0x0000, 150, y0, 540, y1)
    def ta_caret():
        return cnt(dump_fb(s, ROOT + '/vg6c.bin'), bpl, 0xFC30, 148, 91, 552, 350)
    ok_open_ed = click_until(350, 90, lambda: ta_dark(91, 140) > 300)   # 点"编辑器"
    d0 = ta_dark(91, 140)
    ok_foc = click_until(200, 110, ta_caret)      # 点文本区→聚焦+块状光标
    mon_cmd(s, 'sendkey end', 0.4); time.sleep(0.2)    # 行尾(文本该行末)
    mon_cmd(s, 'sendkey ret', 0.4); time.sleep(0.2)    # 换行 → 后文下移一行
    ch(s, 'z'); time.sleep(0.3)                        # 新行打 z
    # 换行使原 L2 下移到 y139 (原为空行区 y136-152); 该区应由空变有字
    new_row = ta_dark(136, 152)
    # ── 内容读回: 点"读回" rel(8,300)→abs(148,360..196,386); 结果到状态标签(236,370) ──
    lb0 = reg_pixels(dump_fb(s, ROOT + '/vg6d.bin'), bpl, 236, 370, 520, 386, 2)
    click_until(172, 372, lambda: True); time.sleep(0.5)
    lb1 = reg_pixels(dump_fb(s, ROOT + '/vg6e.bin'), bpl, 236, 370, 520, 386, 2)
    lb_chg = sum(1 for a, b in zip(lb0, lb1) if a != b)
    t6 = ok_open_ed and ok_foc and d0 > 300 and new_row > 30 and lb_chg > 2
    results['T6 textarea+readback'] = (t6,
        'open=%s foc=%s dark=%d newrow=%d lbl_change=%d'
        % (ok_open_ed, ok_foc, d0, new_row, lb_chg))

    # ── 鼠标按住拖动 (选中/移动共用): 下→移动(按住)→松 ──
    def drag_select(x0, y0, x1, y1, wait=0.4):
        mm(x0, y0); time.sleep(0.3)
        mon_cmd(s, 'mouse_button 1', 0.05); time.sleep(0.35)   # 按下 (落锚点/置 drag_win)
        mm(x1, y1, wait); time.sleep(0.35)                     # 按住移动 (扩展 active/拖动)
        mon_cmd(s, 'mouse_button 0', 0.05); time.sleep(0.5)    # 松开 (清 drag_win/sel_drag)

    # ── T8 文本选中 (鼠标拖选, v6.11) ──
    # 编辑器窗 abs(140,60,420,360), 文本区 abs(148,90..552,350), 行1于 y=107..122。
    # 选区高亮 C_SELBG=0x0019; 只统计文本区内 y 99..140 (避开标题带 y<60)。
    def ta_selcnt(y0, y1):
        return cnt(dump_fb(s, ROOT + '/vg8s.bin'), bpl, 0x0019, 146, y0, 532, y1)
    def ta_foc():
        return cnt(dump_fb(s, ROOT + '/vg8c.bin'), bpl, 0xFC30, 148, 91, 552, 350) > 2
    mm(*PARK); time.sleep(0.3)
    click_until(200, 110, ta_foc); time.sleep(0.3)     # 聚焦文本区, 光标落行1
    base8 = ta_selcnt(99, 140)
    drag_select(160, 110, 286, 110)                    # 行1上拖选一段
    sel8 = ta_selcnt(99, 140)
    click(200, 29); time.sleep(0.4)                    # 点主窗标题 → 改焦 → 选区塌缩
    clr8 = ta_selcnt(99, 140)
    t8 = base8 < 5 and sel8 > 250 and clr8 < 10
    results['T8 textarea selection'] = (t8, 'base=%d sel=%d clr=%d' % (base8, sel8, clr8))

    # ── T7 窗口 chrome (v6.11): 关闭 / 拖动移动 / 最大化还原 ──
    # 编辑器窗 chrome 区 abs[506,560)x[60,78): ✕关=(551,69) ▢最=(533,69) ▁最=(507,69)
    # 关闭后编辑器原区 (470,70,550,410) 变桌面 0x8410 (main 只到 x<460)。
    pre_close = cnt(dump_fb(s, ROOT + '/vg7a.bin'), bpl, 0x8410, 470, 70, 550, 410)
    ok_close = click_until(551, 69, lambda: cnt(dump_fb(s, ROOT + '/vg7b.bin'),
                                                bpl, 0x8410, 470, 70, 550, 410) > 9000)
    main_ok = cnt(dump_fb(s, ROOT + '/vg7b.bin'), bpl, 0x0019, 40, 21, 440, 38) > 500
    mm(*PARK); time.sleep(0.3)
    results['T7a chrome close'] = (ok_close and main_ok,
                                   'pre_desk=%d close_ok=%s main_title=%s' % (pre_close, ok_close, main_ok))

    # 拖动主窗标题 (300,29)->(360,79): 主窗(20,20,440,340)→(80,70,440,340)。
    # 造标题带 (80,21..39) 消失、新带 (80,71..89) 出现 → 证移动且无残影。
    db = dump_fb(s, ROOT + '/vg7d1.bin')
    old_title_pre = cnt(db, bpl, 0x0019, 80, 21, 300, 39)
    new_title_pre = cnt(db, bpl, 0x0019, 80, 70, 300, 89)
    drag_select(300, 29, 360, 79)
    da = dump_fb(s, ROOT + '/vg7d2.bin')
    old_title_post = cnt(da, bpl, 0x0019, 80, 21, 300, 39)
    new_title_post = cnt(da, bpl, 0x0019, 80, 70, 300, 89)
    t7b = (old_title_pre > 400 and new_title_pre < 10
           and old_title_post < 10 and new_title_post > 400)
    results['T7b drag move'] = (t7b,
        'old_pre=%d new_pre=%d old_post=%d new_post=%d'
        % (old_title_pre, new_title_pre, old_title_post, new_title_post))

    # 主窗现(80,70); chrome ▢ abs(80+413,79)=(493,79)。最大化 → 标题蓝铺满 (0,0,640,18)。
    def maximized():
        return cnt(dump_fb(s, ROOT + '/vg7m.bin'), bpl, 0x0019, 0, 0, 640, 18) > 9000
    ok_max = click_until(493, 79, maximized)
    def de_maximized():
        d = dump_fb(s, ROOT + '/vg7r.bin')
        return cnt(d, bpl, 0x0019, 0, 0, 640, 18) < 500
    ok_rest = click_until(613, 9, de_maximized)     # 满屏后 ▢ 在 (640-54+27, 9)=(613,9)
    drest = dump_fb(s, ROOT + '/vg7r2.bin')
    title_back = cnt(drest, bpl, 0x0019, 180, 71, 300, 88) > 400
    t7c = ok_max and ok_rest and title_back
    results['T7c max+restore'] = (t7c,
        'max=%s rest=%s title_back=%s' % (ok_max, ok_rest, title_back))

    # ── T9 拖动快路径: 中途不松手即见窗口跟随 + 暴露区正确 (v6.12 无桌面闪清) ──
    # 主窗现 (80,70,440,340), 标题带 y[70,88)。按 (300,79) 拖把手(off 220,9),
    # 移到 (300,109) 不松 → 新原点 (80,100), 标题带 y[100,118]。
    # 上缘暴露条 y[70,99] x[80,520) 应补成桌面 0x8410 (无旧窗残影); 若走旧整屏清会
    # 闪, 但静态帧此刻窗口已在中位 → 证"随持拖动 + 暴露补正确", 即抗闪机制。
    mm(300, 79); time.sleep(0.3)
    mon_cmd(s, 'mouse_button 1', 0.05); time.sleep(0.3)   # 按下标题开始拖动
    mm(300, 109); time.sleep(0.6)                          # 移到中位, 按住不停
    d9 = dump_fb(s, ROOT + '/vg9m.bin')
    mid_band   = cnt(d9, bpl, 0x0019, 120, 101, 400, 118)  # 新标题带已到 y100-118
    mid_expose = cnt(d9, bpl, 0x8410, 90, 70, 400, 99)     # 上缘暴露条变桌面
    mon_cmd(s, 'mouse_button 0', 0.05); time.sleep(0.6)    # 松开 → 收尾整屏兜底
    d9f = dump_fb(s, ROOT + '/vg9f.bin')
    fin_band   = cnt(d9f, bpl, 0x0019, 120, 101, 400, 118)
    fin_expose = cnt(d9f, bpl, 0x8410, 90, 70, 400, 99)
    t9 = (mid_band > 400 and mid_expose > 1200
          and fin_band > 400 and fin_expose > 1200)
    results['T9 drag mid follow+expose'] = (t9,
        'mid_band=%d mid_expose=%d fin_band=%d fin_expose=%d'
        % (mid_band, mid_expose, fin_band, fin_expose))

    print('OVERALL', 'PASS' if all(v[0] for v in results.values()) else 'FAIL')
    for k, (ok, info) in results.items():
        print(('PASS' if ok else 'FAIL'), k, info)
    mon_cmd(s, 'quit', 0.3)
finally:
    try:
        qemu.wait(timeout=5)
    except subprocess.TimeoutExpired:
        qemu.kill()
