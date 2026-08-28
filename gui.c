/* gui.c — 内核窗口服务器 (v6.9)
 *
 * 架构: 彻底告别"文本网格 + framebuffer 副作用"的旧渲染路径。GUI 模式下
 * 每个窗口持一块离屏 RGB565 缓冲, 任何变更 → 重画该窗缓冲 → 按 z 序自底
 * 向上把可见窗口 blit 到 VBE 帧缓冲。弹窗关闭 = 移除窗口 + 整屏重合成,
 * 下层内容精确恢复 → 从根本消灭"汉字盖不掉的旧像素/覆盖残留"bug
 * (旧 EDIT: CJK 16×16 直写 framebuffer 是 0xB8000 网格的副作用, 弹窗
 * 覆盖文本格却擦不掉已写的汉字像素)。
 *
 * 用户态只调本服务器的 syscall (28-43), 从不直接写帧缓冲。
 *
 * 模型: retained-mode 控件 (按钮/标签/输入框/列表), 每窗 ≤ GW_MAXWID 个,
 * 文本存固定槽 (无堆分配失败路径)。z 序用单调 gui_zmax, raise 置新 z。
 * GUI 模式下 fb_render / vga 叠加自愈 / 鼠标叠加 全部停用 (gate 在
 * fb.c / vga.c), 字体光标鼠标由本服务器在合成收尾自画。
 */
#include "common.h"
#include "latin_font.h"

int gui_active = 0; /* 见 common.h extern; fb.c/vga.c 据此停用 */

static void gcompose(void); /* 提前于菜单助手等引用, 原定义在下方 */
static void gui_mb_close(void); /* 活跃切换时关弹层用 */
static void gcompose_expose(int ex, int ey, int ew, int eh); /* 区域重合成 */
static int gadv(const unsigned char *s, int *clen, int *cjk); /* LFB 文本用 */
static unsigned ggb(const unsigned char *s); /* LFB 文本用 */

/* ── 常量 ── */
#define GW_MAXWIN 8
#define GW_MAXWID 24
#define GW_MAXITEMS 24
#define GW_TITLE 24

#define GW_BTN 0
#define GW_LBL 1
#define GW_EDIT 2
#define GW_LIST 3
#define GW_TEXTAREA 4
#define GW_CHECK 5 /* 复选框: chk=是否勾选 */
#define GW_RADIO 6 /* 单选钮: chk/grp=选中+互斥组 */
#define GW_MENU 7 /* 菜单栏 (gui_mb 全局) */
#define GW_STATUSBAR 8 /* 状态栏: 窗底横条显示状态文本 */

/* 多行文本区: 内容放独立固定槽池 (每槽 TX_SIZE 字节), 而非塞进 gui_wid_t 的
 * txt[64] — 避免结构体阵列被放大几百 KB。槽由 gw_new 分配, 关闭/替换时释放。 */
#define GW_TXPOOL 4
#define TX_SIZE 2048
static char gui_txpool[GW_TXPOOL][TX_SIZE];
static int gui_txused[GW_TXPOOL];

#define GEV_CLICK 1
#define GEV_KEY 2
#define GEV_ENTER 3
#define GEV_CLOSE 4 /* 标题栏 ✕ 关闭: 内核已关窗, 通知程序 (主窗→退) */

/* GUI (内核窗口服务器) 版本 — AMUNOS Classic GUI 0.2 (v6.5.3 携带)
 * 对应 docs/AMUNOS_Classic_GUI_设计与实现规划.md 的 GUI 0.2 里程碑:
 * Window/Button/Label/Edit/Textarea/List/中文 + 多窗口叠放 + 拖动chrome(最小/最大/关) + 文本选中。
 * 增: Checkbox / Radio / Menu(Alt+字母) / TAB 焦点循环 / List 读回。 */
#define GUI_VERSION "0.2"
#define GUI_VERSION_FULL "AMUNOS Classic GUI " GUI_VERSION

/* 窗口状态 () */
#define W_NORM 0 /* 正常 */
#define W_MIN 1 /* 最小化: 只剩 18px 标题条 */
#define W_MAX 2 /* 最大化: 铺满帧缓冲 */

/* 标题栏右侧 chrome: 3 个 18×18 控制钮 (右对齐) */
#define CHROME_W 18
#define CHROME_N 3

/* RGB565 主题色 */
#define C_DESKTOP 0xC618 /* 桌面底色 #C0C0C0 灰 (docs#21/#34: 灰色桌面) */
#define C_WINBG 0xF7BE
#define C_TITLEFX 0x0010 /* 活跃标题 #000080 深蓝 (docs#21 ACTIVE) */
#define C_TITLEF 0x8410 /* 非活跃标题 #808080 灰 (docs#21 INACTIVE) */
#define C_TITLEFG 0xFFFF
#define C_BTNBG 0xC618 /* 控件 FACE #C0C0C0 银 (docs#22) */
#define C_BTNBDR 0x4A49 /* SHADOW 暗边 */
#define C_EDBG 0xFFFF
#define C_EDBDR 0x4A49
#define C_SELBG 0x0010 /* 选中/高亮 #000080 (docs#21 HIGHLIGHT) */
#define C_SELFG 0xFFFF
#define C_TEXT 0x0000
#define C_PTR 0xFFFF

typedef struct {
 int type;
 int x, y, w, h;
 char txt[64]; /* 按钮/标签文本 | 输入框内容 */
 int caret; /* 输入框: 字节数 */
 char items[GW_MAXITEMS][32];
 int nitems, sel, scroll;
 int txid; /* 文本区: 槽序 (gui_txpool), -1 无 */
 int txlen; /* 文本区: 内容字节数 */
 int txc; /* 文本区: 光标字节偏移 */
 int txcol; /* 文本区: 记忆列 (像素) — ↑↓ 保持 */
 int txsc; /* 文本区: 顶部可见行 */
 /* 文本选择 (EDIT + TAREA 共享): 选区 = [min(anchor,active),
 * max(anchor,active)); 相等即空。锚点在按下/非 shift 移动时固定。 */
 int sel_anchor;
 int sel_active;
 /* 复选框/单选钮 */
 int chk; /* CHECK/RADIO: 是否选中 (0 未勾/1 勾选) */
 int grp; /* RADIO: 互斥组号 (同窗同组互斥) */
} gui_wid_t;

typedef struct {
 int used;
 int x, y, w, h;
 int z;
 int foc_wid; /* 该窗聚焦控件 (输入框), -1 无 */
 char title[GW_TITLE];
 unsigned short *buf; /* 离屏 w*h */
 gui_wid_t wd[GW_MAXWID];
 int nwid;
 /* 窗口状态: 最小化/最大化 + 还原矩形 (最小/最大前的 x,y,w,h) */
 int state;
 int rx, ry, rw, rh;
 int active; /* 文档: 活跃窗口 (只有一个 active=true, 点击激活) */
 int topmost; /* 1=置顶窗 (优先响应点击) */
} gui_win_t;

static gui_win_t GUW[GW_MAXWIN];
static int gui_zmax = 0;
static int active_win = -1; /* 活跃窗口 (只有一个 active=true, 文档规范) */
static int foc_win = -1; /* 聚焦窗口 (键盘路由; 默认同 active_win) */
static int prev_lbutton = 0;
static int gui_buf_h = 480; /* 帧缓冲高 */
static int gui_dirty = 1; /* 有待重合成 (状态变更或指针移动) */
static int dirty_win = -1; /* >=0: 仅需重blit该窗口 (widget 变更, 不全屏) */
static int last_mx = -1, last_my = -1; /* 上次合成时的指针位置 */
static int desktop_init = 0; /* 首次合成铺桌面底色, 之后免整屏清 → 抗闪 */

/* 拖动/活动选择状态 (按住跨 poll) */
static int drag_win = -1; /* 正在被拖的窗, -1=无 */
static int drag_offx = 0; /* 按下时 鼠标x - 窗x */
static int drag_offy = 0; /* 按下时 鼠标y - 窗y */
static int sel_drag_w = -1; /* 鼠标拖选中的窗, -1=无 */
static int sel_drag_k = -1; /* 鼠标拖选中的控件 */

/* ── 菜单栏: 单一全局 (同一时刻一个活跃窗口用菜单栏) ──
 * 每个菜单一个标题 (title) + 至多 GMW_ITEMS 项 (项 = items[i][.])。
 * 助记符: title 内第一个 "(X)" 的 ASCII 字母转小写 (>0), 无括号则取首 ASCII 字母。
 * "open" = 展开的菜单号 (0..nmenu-1, -1 无); "hilite" = 下拉面板高亮项。 */
#define GMW_MENUS 6
#define GMW_ITEMS 8
typedef struct {
 int used; /* 本栏占用 (建 menubar 置 1) */
 int win; /* 拥有窗口 */
 int nmenu; /* 菜单数 */
 char titles[GMW_MENUS][21]; /* 标题文本 e.g. "文件(F)" */
 char mnem[GMW_MENUS]; /* 小写 Alt 助记字母, 0=无 */
 int nitems[GMW_MENUS]; /* 各菜单项数 */
 char items[GMW_MENUS][GMW_ITEMS][32];
 int open, hilite; /* 展开的菜单 / 高亮项 (-1 无) */
 int pop_x, pop_y, pop_w, pop_h;/* 弹层绝对像素 (跟随点击菜单标题) */
} gui_menubar_t;
static gui_menubar_t gui_mb = {0};

/* ── 小工具 ── */
static int gstrlen(const char *s) { const char *p = s; while (*p) p++; return (int)(p - s); }
static void gcopy(char *d, const char *s, int cap) {
 int i = 0;
 while (s[i] && i < cap - 1) { d[i] = s[i]; i++; }
 d[i] = 0;
}

static inline void gpx(unsigned short *b, int bw, int bh, int x, int y,
 unsigned short c) {
 if ((unsigned)x < (unsigned)bw && (unsigned)y < (unsigned)bh)
 b[(unsigned)y * bw + (unsigned)x] = c;
}

/* ── LFB 直写版 ( 菜单弹层): 不走窗口 bitmap, 直接画到帧缓冲,
 * 弹层可覆盖其他窗口, 永远在 z 顶 ── */
static inline void gpx_lfb(unsigned fb, int bpl, int fbw, int fbh,
 int x, int y, unsigned short c) {
 if ((unsigned)x < (unsigned)fbw && (unsigned)y < (unsigned)fbh) {
 unsigned char *p = (unsigned char *)(fb + (unsigned)y * bpl + (unsigned)x * 2);
 p[0] = (unsigned char)(c & 0xFF); p[1] = (unsigned char)(c >> 8);
 }
}
static void gfill_lfb(unsigned fb, int bpl, int fbw, int fbh,
 int x, int y, int w, int h, unsigned short c) {
 int x1 = x + w; if (x < 0) x = 0; if (x1 > fbw) x1 = fbw;
 int y1 = y + h; if (y < 0) y = 0; if (y1 > fbh) y1 = fbh;
 for (int yy = y; yy < y1; yy++)
 for (int xx = x; xx < x1; xx++)
 gpx_lfb(fb, bpl, fbw, fbh, xx, yy, c);
}
static void gborder_lfb(unsigned fb, int bpl, int fbw, int fbh,
 int x, int y, int w, int h, unsigned short c) {
 gfill_lfb(fb, bpl, fbw, fbh, x, y, w, 1, c);
 gfill_lfb(fb, bpl, fbw, fbh, x, y + h - 1, w, 1, c);
 gfill_lfb(fb, bpl, fbw, fbh, x, y, 1, h, c);
 gfill_lfb(fb, bpl, fbw, fbh, x + w - 1, y, 1, h, c);
}
static void gtext_lfb(unsigned fb, int bpl, int fbw, int fbh,
 int px, int py, const unsigned char *s,
 unsigned short fg, unsigned short bg, int withbg) {
 while (s[0]) {
 int cl, cj, w = gadv(s, &cl, &cj);
 if (cj) {
 unsigned gb = ggb(s);
 unsigned char *hzk = fb_hzk16();
 if (gb && hzk && (gb >> 8) >= 0xA1 && (gb >> 8) <= 0xF7 && (gb & 0xFF) >= 0xA1) {
 const unsigned char *g = hzk +
 ((unsigned)((gb >> 8) - 0xA1) * 94 + ((gb & 0xFF) - 0xA1)) * 32;
 for (int r = 0; r < 16; r++) {
 unsigned char b0b = g[r * 2], b1 = g[r * 2 + 1];
 for (int c = 0; c < 8; c++)
 if ((b0b & (0x80 >> c)) || withbg)
 gpx_lfb(fb, bpl, fbw, fbh, px + c, py + r, (b0b & (0x80 >> c)) ? fg : bg);
 for (int c = 0; c < 8; c++)
 if ((b1 & (0x80 >> c)) || withbg)
 gpx_lfb(fb, bpl, fbw, fbh, px + 8 + c, py + r, (b1 & (0x80 >> c)) ? fg : bg);
 }
 } else { /* 替换框 □ */
 for (int r = 0; r < 16; r++)
 for (int c = 0; c < 16; c++) {
 int border = (r == 0 || r == 15 || c == 0 || c == 15);
 if (border || withbg)
 gpx_lfb(fb, bpl, fbw, fbh, px + c, py + r, border ? fg : bg);
 }
 }
 } else {
 const unsigned char *g = latin_font8x16 + s[0] * 16;
 for (int r = 0; r < 16; r++) {
 unsigned char bits = g[r];
 for (int c = 0; c < 8; c++)
 if ((bits & (0x80 >> c)) || withbg)
 gpx_lfb(fb, bpl, fbw, fbh, px + c, py + r, (bits & (0x80 >> c)) ? fg : bg);
 }
 }
 px += w; s += cl;
 }
}

/* 字符分类推进: 返回像素宽, 写 clen(字节数), cjk(1=汉字/UTF8码点)
 * ASCII=8 宽, CJK=16 宽。 */
static int gadv(const unsigned char *s, int *clen, int *cjk) {
 unsigned char b0 = s[0];
 if (b0 < 0x80) { *clen = 1; *cjk = 0; return 8; }
 /* 必须先判 UTF-8 再判原始 GB2312: UTF-8 续字节 0x80-0xBF 与 GB 低位
 * 0xA1-0xFE 有重叠 (如 '件'=E4 BB B6 的 0xBB 两种都算)。旧序先判 GB →
 * UTF-8 汉字的第 2 字节落在 [A1,BF] 时被误读成原始 GB, 整串错位乱码
 * (validate_gui 只查颜色抓不到字形; 用户实测 '退出' 正常, '件演示' 乱码)。 */
 if (b0 >= 0xE0 && b0 <= 0xEF && s[1] >= 0x80 && s[1] <= 0xBF
 && s[2] >= 0x80 && s[2] <= 0xBF) { *clen = 3; *cjk = 1; return 16; }
 if (b0 >= 0xC2 && b0 <= 0xDF && s[1] >= 0x80 && s[1] <= 0xA0) {
 *clen = 2; *cjk = 1; return 16; /* UTF-8 2 字节 */
 }
 if (b0 >= 0xA1 && b0 <= 0xF7 && s[1] >= 0xA1 && s[1] <= 0xFE) {
 *clen = 2; *cjk = 1; return 16; /* 原始 GB2312 */
 }
 *clen = 1; *cjk = 0; return 8; /* 其他单字节 */
}

static int gstr_px(const unsigned char *s) {
 int px = 0, cl, cj;
 while (s[0]) { px += gadv(s, &cl, &cj); s += cl; }
 return px;
}

/* 光标像素 x: 字节位置 caret 在字符串中的像素偏移 (caret 恒在字形边界) */
static int gcaret_px(const char *s, int caret) {
 int px = 0, i = 0;
 while (i < caret) {
 int cl, cj, w;
 gadv((const unsigned char*)s + i, &cl, &cj);
 if (cl < 1) cl = 1;
 w = (cj ? 16 : 8);
 if (i + cl > caret) break; /* 残缺字形: 停在其起点 (防御) */
 px += w; i += cl;
 }
 return px;
}

/* 光标前一个完整字形的字节起点 (←/退格用); -1 = 无 */
static int gglyph_start(const char *s, int pos) {
 int i = 0;
 while (i < pos) {
 int cl, cj;
 gadv((const unsigned char*)s + i, &cl, &cj);
 if (cl < 1) cl = 1;
 if (i + cl >= pos) return i;
 i += cl;
 }
 return -1;
}

/* 从点击像素 x (相对文本起点) 求光标字节位置:
 * 前半格 → 光标在字形前, 后半格 → 在字形后, 越界 → 串首/串尾 */
static int gcaret_from_px(const char *s, int px) {
 int x = 0, i = 0;
 while (s[i]) {
 int cl, cj, w;
 gadv((const unsigned char*)s + i, &cl, &cj);
 if (cl < 1) cl = 1; w = (cj ? 16 : 8);
 if (px <= x + w / 2) return i;
 x += w; i += cl;
 if (px <= x) return i;
 }
 return i;
}

/* ── 多行文本区: 有界行解析 (内容不是 NUL 结尾, 按 b1 限, 行以 '\n' 分界) ── */
static unsigned ggb(const unsigned char *s); /* 见后: 译 UTF8/GB → GB2312 码 */
static int gtx_line_start(const char *buf, int len, int p) {
 (void)len; int i = p; while (i > 0 && buf[i-1] != '\n') i--; return i;
}
static int gtx_line_end(const char *buf, int len, int p) {
 int i = p; while (i < len && buf[i] != '\n') i++; return i; /* '\n' 或 len */
}
static int gtx_row(const char *buf, int len, int p) {
 int r = 0, i = 0; while (i < p) { if (buf[i] == '\n') r++; i++; } return r;
}
static int gtx_row_start(const char *buf, int len, int row) {
 int r = 0, i = 0;
 while (r < row && i < len) { if (buf[i] == '\n') r++; i++; }
 return i; /* 该行首字节 */
}
/* 行 buf[b0..idx) 的像素宽 (idx 恒在字形边界) */
static int gtx_px(const char *buf, int b0, int idx, int b1) {
 int px = 0, i = b0;
 while (i < idx) {
 int cl, cj; gadv((const unsigned char*)buf + i, &cl, &cj);
 if (cl < 1) cl = 1;
 if (i + cl > idx) break;
 px += (cj ? 16 : 8); i += cl;
 }
 return px;
}
/* 行内光标前一完整字形的字节起点 (←/退格用); -1 = 在行首 */
static int gtx_prev(const char *buf, int b0, int p) {
 int i = b0;
 while (i < p) {
 int cl, cj; gadv((const unsigned char*)buf + i, &cl, &cj);
 if (cl < 1) cl = 1;
 if (i + cl >= p) return i;
 i += cl;
 }
 return -1;
}
/* 行内最接近像素 px 的光标字节 (前半字形→前, 后半→后) */
static int gtx_byte_px(const char *buf, int b0, int b1, int px) {
 int x = 0, i = b0;
 while (i < b1) {
 int cl, cj, w; gadv((const unsigned char*)buf + i, &cl, &cj);
 if (cl < 1) cl = 1; w = (cj ? 16 : 8);
 if (px <= x + w / 2) return i;
 x += w; i += cl; if (i >= b1 || px <= x) return i;
 }
 return b0;
}
static void gtx_rshift(char *b, int at, int n, int len, int cap) {
 if (n <= 0 || len + n > cap) return;
 for (int i = len; i > at; i--) b[i + n - 1] = b[i - 1];
}
static void gtx_lshift(char *b, int at, int n, int len) {
 for (int i = at; i < len - n; i++) b[i] = b[i + n];
}
/* 截掉尾部残缺字形 (cap 截断可能把 UTF-8/GB 掐半) */
static void gtx_trim(char *b, int len) {
 int i = 0;
 while (i < len) {
 int cl, cj; gadv((const unsigned char*)b + i, &cl, &cj);
 if (cl < 1) cl = 1;
 if (i + cl > len) break;
 i += cl;
 }
 b[i] = 0;
}

/* ── 文本选择 + 窗口 chrome 助手 ── */
/* 选区 = [lo, hi) 字节; anchor==active → 空 */
static inline void sel_range(const gui_wid_t *wd, int *lo, int *hi) {
 int a = wd->sel_anchor, b = wd->sel_active;
 if (a > b) { int t = a; a = b; b = t; }
 *lo = a; *hi = b;
}
/* 删除选中区间, 光标塌缩到 lo。TAREA 传 &wd->txlen, EDIT 传 NULL。返回新光标。 */
static int sel_delete(gui_wid_t *wd, int *len_io) {
 int lo, hi; sel_range(wd, &lo, &hi);
 int n = hi - lo;
 if (n > 0) {
 if (wd->type == GW_TEXTAREA) {
 char *buf = gui_txpool[wd->txid];
 gtx_lshift(buf, lo, n, *len_io);
 *len_io -= n;
 } else {
 int L = gstrlen(wd->txt);
 for (int i = lo; i < L - n; i++) wd->txt[i] = wd->txt[i + n];
 wd->txt[L - n] = 0;
 }
 }
 wd->sel_anchor = wd->sel_active = lo;
 return lo;
}
/* 窗口可见高度: 最小化只剩 18px 标题条 (离屏缓冲仍保持原大小, 只 blit 前 18 行) */
static inline int w_draw_h(const gui_win_t *w) {
 return (w->state == W_MIN) ? 18 : w->h;
}

/* 渲染有界行 buf[b0..b1) 到缓冲, 逐字形推进 x (不读 b1 之后) */
static void gtx_line(unsigned short *b, int bw, int bh,
 int px, int py, const char *buf, int b0, int b1,
 unsigned short fg, unsigned short bg, int withbg) {
 int i = b0;
 while (i < b1) {
 int cl, cj, w = gadv((const unsigned char*)buf + i, &cl, &cj);
 if (cj) {
 unsigned gb = ggb((const unsigned char*)buf + i);
 unsigned char *hzk = fb_hzk16();
 if (gb && hzk && (gb >> 8) >= 0xA1 && (gb >> 8) <= 0xF7 && (gb & 0xFF) >= 0xA1) {
 const unsigned char *g = hzk +
 ((unsigned)((gb >> 8) - 0xA1) * 94 + ((gb & 0xFF) - 0xA1)) * 32;
 for (int r = 0; r < 16; r++) {
 unsigned char b0b = g[r * 2], b1b = g[r * 2 + 1];
 for (int c = 0; c < 8; c++)
 if ((b0b & (0x80 >> c)) || withbg)
 gpx(b, bw, bh, px + c, py + r, (b0b & (0x80 >> c)) ? fg : bg);
 for (int c = 0; c < 8; c++)
 if ((b1b & (0x80 >> c)) || withbg)
 gpx(b, bw, bh, px + 8 + c, py + r, (b1b & (0x80 >> c)) ? fg : bg);
 }
 } else { /* 替换框 □ */
 for (int r = 0; r < 16; r++)
 for (int c = 0; c < 16; c++) {
 int border = (r == 0 || r == 15 || c == 0 || c == 15);
 if (border || withbg) gpx(b, bw, bh, px + c, py + r, border ? fg : bg);
 }
 }
 } else {
 const unsigned char *g = latin_font8x16 + buf[i] * 16;
 for (int r = 0; r < 16; r++) {
 unsigned char bits = g[r];
 for (int c = 0; c < 8; c++)
 if ((bits & (0x80 >> c)) || withbg)
 gpx(b, bw, bh, px + c, py + r, (bits & (0x80 >> c)) ? fg : bg);
 }
 }
 px += w; i += cl;
 }
}

/* 把 UTF8/GB 码点翻译成 GB2312 码 (0=不在字库) */
static unsigned ggb(const unsigned char *s) {
 unsigned char b0 = s[0];
 /* 顺序须与 gadv 一致: UTF-8 优先, 再原始 GB2312 (见 gadv 注释) */
 if (b0 >= 0xE0 && b0 <= 0xEF && s[1] >= 0x80 && s[1] <= 0xBF
 && s[2] >= 0x80 && s[2] <= 0xBF)
 return fb_uni_to_gb(((b0 & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F));
 if (b0 >= 0xC2 && b0 <= 0xDF && s[1] >= 0x80 && s[1] <= 0xA0)
 return fb_uni_to_gb(((b0 & 0x1F) << 6) | (s[1] & 0x3F));
 if (b0 >= 0xA1 && b0 <= 0xF7 && s[1] >= 0xA1 && s[1] <= 0xFE)
 return ((unsigned)b0 << 8) | s[1];
 return 0;
}

/* 渲染字符串到缓冲 (逐字形推进 x) */
static void gtext(unsigned short *b, int bw, int bh,
 int px, int py, const unsigned char *s,
 unsigned short fg, unsigned short bg, int withbg) {
 while (s[0]) {
 int cl, cj, w = gadv(s, &cl, &cj);
 if (cj) {
 unsigned gb = ggb(s);
 unsigned char *hzk = fb_hzk16();
 if (gb && hzk && (gb >> 8) >= 0xA1 && (gb >> 8) <= 0xF7 && (gb & 0xFF) >= 0xA1) {
 const unsigned char *g = hzk +
 ((unsigned)((gb >> 8) - 0xA1) * 94 + ((gb & 0xFF) - 0xA1)) * 32;
 for (int r = 0; r < 16; r++) {
 unsigned char b0b = g[r * 2], b1 = g[r * 2 + 1];
 for (int c = 0; c < 8; c++)
 if ((b0b & (0x80 >> c)) || withbg)
 gpx(b, bw, bh, px + c, py + r, (b0b & (0x80 >> c)) ? fg : bg);
 for (int c = 0; c < 8; c++)
 if ((b1 & (0x80 >> c)) || withbg)
 gpx(b, bw, bh, px + 8 + c, py + r, (b1 & (0x80 >> c)) ? fg : bg);
 }
 } else { /* 替换框 □ */
 for (int r = 0; r < 16; r++)
 for (int c = 0; c < 16; c++) {
 int border = (r == 0 || r == 15 || c == 0 || c == 15);
 if (border || withbg)
 gpx(b, bw, bh, px + c, py + r, border ? fg : bg);
 }
 }
 } else {
 const unsigned char *g = latin_font8x16 + s[0] * 16;
 for (int r = 0; r < 16; r++) {
 unsigned char bits = g[r];
 for (int c = 0; c < 8; c++)
 if ((bits & (0x80 >> c)) || withbg)
 gpx(b, bw, bh, px + c, py + r, (bits & (0x80 >> c)) ? fg : bg);
 }
 }
 px += w; s += cl;
 }
}

static void gfill(unsigned short *b, int bw, int bh,
 int x, int y, int w, int h, unsigned short c) {
 for (int j = 0; j < h; j++)
 for (int i = 0; i < w; i++)
 gpx(b, bw, bh, x + i, y + j, c);
}

static void gborder(unsigned short *b, int bw, int bh,
 int x, int y, int w, int h, unsigned short c) {
 gfill(b, bw, bh, x, y, w, 1, c);
 gfill(b, bw, bh, x, y + h - 1, w, 1, c);
 gfill(b, bw, bh, x, y, 1, h, c);
 gfill(b, bw, bh, x + w - 1, y, 1, h, c);
}

/* ✕ 关闭钮: 14×14 对角虚线 (2px 内嵌, 落进 18×18 按钮) */
static void gx_x(unsigned short *b, int bw, int bh, int x0, int y0, int fg) {
 for (int k = 0; k < 14; k++) {
 gpx(b, bw, bh, x0 + k, y0 + k, fg);
 gpx(b, bw, bh, x0 + 13 - k, y0 + k, fg);
 }
}
/* 标题栏右侧 chrome 三钮: ▁ 最小化 / ▢ 最大化 / ✕ 关闭 (右对齐 winw 宽) */
static void gdraw_chrome(unsigned short *b, int bw, int bh, int winw, int isfoc) {
 unsigned short glyph = isfoc ? C_TITLEFG : C_TITLEF;
 int bx = winw - CHROME_N * CHROME_W;
 for (int i = 0; i < CHROME_N; i++) /* 按钮间竖分隔线 */
 gfill(b, bw, bh, bx + i * CHROME_W, 0, 1, CHROME_W, C_BTNBDR);
 gfill(b, bw, bh, bx + 2, 13, 14, 2, glyph); /* ▁ 最小化 */
 gborder(b, bw, bh, bx + 18 + 2, 2, 14, 14, glyph); /* ▢ 最大化 */
 gx_x(b, bw, bh, bx + 36 + 2, 2, glyph); /* ✕ 关闭 */
}

/* 该控件是否聚焦 (TAB/点击后的键盘路由目标; LBL/MENU 恒假) */
static inline int gw_isfoc(const gui_win_t *w, const gui_wid_t *wd) {
 return (foc_win >= 0 && w == &GUW[foc_win] && wd == &w->wd[w->foc_wid]);
}

/* 复选框盒: 14×14 边框盒; 勾选画 2px 宽的 √ (下折→上挑), 顶点≈(5,10),
 * 起点≈(2,7), 终点≈(12,3), 整笔画居中清晰 */
static void gchk_box(unsigned short *b, int bw, int bh,
 int x, int y, int checked, int isfoc) {
 gfill(b, bw, bh, x, y, 14, 14, C_EDBG);
 gborder(b, bw, bh, x, y, 14, 14, isfoc ? C_TITLEFX : C_BTNBDR);
 if (checked) {
 /* 下折笔 2px: (2,7)(2,8)(3,8)(3,9)(4,9)(4,10)(5,10)(5,11) */
 int dl[8][2] = {{2,7},{2,8},{3,8},{3,9},{4,9},{4,10},{5,10},{5,11}};
 /* 上挑笔 2px: (5,10)(5,11)(6,9)(6,10)(7,8)(7,9)(8,7)(8,8)(9,6)(9,7)
 * (10,5)(10,6)(11,4)(11,5)(12,3)(12,4) */
 int ul[16][2] = {{5,10},{5,11},{6,9},{6,10},{7,8},{7,9},{8,7},{8,8},
 {9,6},{9,7},{10,5},{10,6},{11,4},{11,5},{12,3},{12,4}};
 for (int k = 0; k < 8; k++)
 gpx(b, bw, bh, x + dl[k][0], y + dl[k][1], C_TEXT);
 for (int k = 0; k < 16; k++)
 gpx(b, bw, bh, x + ul[k][0], y + ul[k][1], C_TEXT);
 }
}

/* 单选钮 (): 14×14 圆; 选中填实心点; 聚焦画亮环 */
static void grad_box(unsigned short *b, int bw, int bh,
 int x, int y, int checked, int isfoc) {
 for (int j = 0; j < 14; j++)
 for (int i = 0; i < 14; i++) {
 int dx = i - 6, dy = j - 6; /* 圆心 7,7 */
 int d2 = dx * dx + dy * dy;
 if (d2 <= 49) /* 外圆 r=7 */
 gpx(b, bw, bh, x + i, y + j, C_EDBG);
 }
 unsigned short ring = isfoc ? C_TITLEFX : C_BTNBDR;
 for (int j = 0; j < 14; j++) for (int i = 0; i < 14; i++) {
 int dx = i - 6, dy = j - 6, d2 = dx * dx + dy * dy;
 if (d2 > 36 && d2 <= 49) gpx(b, bw, bh, x + i, y + j, ring); /* r∈(6,7] */
 if (checked && d2 <= 16) gpx(b, bw, bh, x + i, y + j, C_TEXT); /* 实心 r≤4 */
 }
}

/* docs#22 凹陷边缘: 左/上=SHADOW, 右/下=LIGHT (输入框/列表/文本区) */
static void gsunken(unsigned short *b, int bw, int bh,
 int x, int y, int w, int h) {
 gfill(b, bw, bh, x, y, w, 1, C_BTNBDR);
 gfill(b, bw, bh, x, y, 1, h, C_BTNBDR);
 gfill(b, bw, bh, x, y + h - 1, w, 1, 0xFFFF);
 gfill(b, bw, bh, x + w - 1, y, 1, h, 0xFFFF);
}

static void gfocus_ring(gui_win_t *w, gui_wid_t *wd) { /* 非文字控件聚焦描边 */
 unsigned short *b = w->buf; int bw = w->w, bh = w->h;
 gborder(b, bw, bh, wd->x - 1, wd->y - 1, wd->w + 2, wd->h + 2, C_TITLEFX);
}

static void gw_draw(gui_win_t *w, gui_wid_t *wd) {
 unsigned short *b = w->buf; int bw = w->w, bh = w->h;
 int gfoc = gw_isfoc(w, wd);
 switch (wd->type) {
 case GW_BTN: {
 gfill(b, bw, bh, wd->x, wd->y, wd->w, wd->h, C_BTNBG);
 /* docs#22 凸起: 左/上 2px 亮, 右/下 2px 暗 */
 gfill(b, bw, bh, wd->x, wd->y, wd->w, 2, 0xFFFF);
 gfill(b, bw, bh, wd->x, wd->y, 2, wd->h, 0xFFFF);
 gfill(b, bw, bh, wd->x, wd->y + wd->h - 2, wd->w, 2, C_BTNBDR);
 gfill(b, bw, bh, wd->x + wd->w - 2, wd->y, 2, wd->h, C_BTNBDR);
 int tw = gstr_px((const unsigned char*)wd->txt);
 int tx = wd->x + (wd->w - tw) / 2, ty = wd->y + (wd->h - 16) / 2;
 gtext(b, bw, bh, tx, ty, (const unsigned char*)wd->txt, C_TEXT, C_BTNBG, 1);
 (void)bh;
 if (gfoc) gfocus_ring(w, wd); /* 聚焦亮框 */
 break;
 }
 case GW_LBL: {
 /* 透明背景: 显式清掉文字所占矩形, 防止"新短文覆盖在旧长文后面"残影。
 * gw_redraw 虽已整窗清 C_WINBG, 但保险: 按文字实际像素宽清一次。 */
 int tw = gstr_px((const unsigned char*)wd->txt);
 if (tw > 0) gfill(b, bw, bh, wd->x, wd->y, tw, 16, C_WINBG);
 gtext(b, bw, bh, wd->x, wd->y, (const unsigned char*)wd->txt, C_TEXT, 0, 0);
 break;
 }
 case GW_STATUSBAR: {
 /* 状态栏: 窗底凹陷横条 — 灰底 + 顶部暗分隔线 + 黑字 (工业计算机质感) */
 gfill(b, bw, bh, wd->x, wd->y, wd->w, wd->h, C_BTNBG);
 gfill(b, bw, bh, wd->x, wd->y, wd->w, 1, C_BTNBDR); /* 顶部暗分隔线 */
 int tw = gstr_px((const unsigned char*)wd->txt);
 if (tw > 0) gtext(b, bw, bh, wd->x + 4, wd->y + (wd->h - 16) / 2,
  (const unsigned char*)wd->txt, C_TEXT, C_BTNBG, 1);
 break;
 }
 case GW_EDIT: {
 gfill(b, bw, bh, wd->x, wd->y, wd->w, wd->h, C_EDBG);
 gsunken(b, bw, bh, wd->x, wd->y, wd->w, wd->h); /* docs#22 凹陷 */
 gtext(b, bw, bh, wd->x + 3, wd->y + 1, (const unsigned char*)wd->txt, C_TEXT, C_EDBG, 1);
 if (wd->sel_anchor != wd->sel_active) { /* 选区高亮 (先于光标) */
 int slo, shi; sel_range(wd, &slo, &shi);
 int x0 = wd->x + 3 + gcaret_px(wd->txt, slo);
 int x1 = wd->x + 3 + gcaret_px(wd->txt, shi);
 if (x1 > x0) {
 gfill(b, bw, bh, x0, wd->y + 1, x1 - x0, 14, C_SELBG);
 gtx_line(b, bw, bh, x0, wd->y + 1, wd->txt, slo, shi,
 C_SELFG, C_SELBG, 1);
 }
 }
 int isfoc = (w == &GUW[foc_win >= 0 ? foc_win : 0] && wd == &w->wd[w->foc_wid]
 && w->foc_wid >= 0);
 if (isfoc) { /* 块状光标 (在光标字节处, 非恒在串尾), 2px 宽更醒目 */
 int cx = wd->x + 3 + gcaret_px(wd->txt, wd->caret);
 for (int r = 0; r < 14; r++) {
 gpx(b, bw, bh, cx, wd->y + 1 + r, 0xFC30);
 gpx(b, bw, bh, cx + 1, wd->y + 1 + r, 0xFC30);
 }
 }
 break;
 }
 case GW_LIST: {
 gfill(b, bw, bh, wd->x, wd->y, wd->w, wd->h, C_EDBG);
 gsunken(b, bw, bh, wd->x, wd->y, wd->w, wd->h); /* docs#22 凹陷 */
 int visible = (wd->h - 2) / 16; if (visible < 1) visible = 1;
 if (wd->scroll > wd->nitems - visible) wd->scroll = wd->nitems - visible;
 if (wd->scroll < 0) wd->scroll = 0;
 for (int i = 0; i < visible; i++) {
 int item = wd->scroll + i; if (item >= wd->nitems) break;
 int sel = (wd->sel == item);
 int iy = wd->y + 1 + i * 16;
 gfill(b, bw, bh, wd->x + 1, iy, wd->w - 2, 16, sel ? C_SELBG : C_EDBG);
 gtext(b, bw, bh, wd->x + 3, iy, (const unsigned char*)wd->items[item],
 sel ? C_SELFG : C_TEXT, sel ? C_SELBG : C_EDBG, 1);
 }
 break;
 }
 case GW_TEXTAREA: {
 if (wd->txid < 0) break;
 const char *buf = gui_txpool[wd->txid];
 int len = wd->txlen;
 gfill(b, bw, bh, wd->x, wd->y, wd->w, wd->h, C_EDBG);
 gsunken(b, bw, bh, wd->x, wd->y, wd->w, wd->h); /* docs#22 凹陷 */
 int visible = (wd->h - 2) / 16; if (visible < 1) visible = 1;
 int crow = gtx_row(buf, len, wd->txc);
 if (wd->txsc < 0) wd->txsc = 0;
 if (crow < wd->txsc) wd->txsc = crow; /* 光标上卷 */
 if (crow >= wd->txsc + visible) wd->txsc = crow - visible + 1;
 int ls = gtx_row_start(buf, len, wd->txsc);
 int has_sel = (wd->sel_anchor != wd->sel_active);
 int slo = 0, shi = 0; if (has_sel) sel_range(wd, &slo, &shi);
 for (int r = 0; r < visible; r++) {
 int le = ls;
 while (le < len && buf[le] != '\n') le++;
 int iy = wd->y + 1 + r * 16;
 gtx_line(b, bw, bh, wd->x + 2, iy, buf, ls, le, C_TEXT, C_EDBG, 1);
 if (has_sel) { /* 选区高亮 (逐行交叠) */
 int s0 = slo > ls ? slo : ls;
 int s1 = shi < le ? shi : le;
 if (s0 < s1) {
 int x0 = wd->x + 2 + gtx_px(buf, ls, s0, le);
 int x1 = wd->x + 2 + gtx_px(buf, ls, s1, le);
 gfill(b, bw, bh, x0, iy, x1 - x0, 16, C_SELBG);
 gtx_line(b, bw, bh, x0, iy, buf, s0, s1, C_SELFG, C_SELBG, 1);
 }
 }
 if (le >= len) break; /* 内容最后一行 */
 ls = le + 1;
 }
 int isfoc = (w == &GUW[foc_win >= 0 ? foc_win : 0] && w->foc_wid >= 0
 && wd == &w->wd[w->foc_wid]);
 if (isfoc && crow >= wd->txsc && crow < wd->txsc + visible) {
 int cls = gtx_line_start(buf, len, wd->txc);
 int clp = gtx_px(buf, cls, wd->txc, gtx_line_end(buf, len, wd->txc));
 int cx = wd->x + 2 + clp, cy = wd->y + 1 + (crow - wd->txsc) * 16;
 /* 2px 宽块光标 */
 for (int r = 0; r < 14; r++) {
 gpx(b, bw, bh, cx, cy + r, 0xFC30);
 gpx(b, bw, bh, cx + 1, cy + r, 0xFC30);
 }
 }
 break;
 }
 case GW_CHECK: { /* 复选框: [x] 文本 () */
 gchk_box(b, bw, bh, wd->x, wd->y + 1, wd->chk, gfoc);
 gtext(b, bw, bh, wd->x + 18, wd->y + 1,
 (const unsigned char*)wd->txt, C_TEXT, 0, 0);
 break;
 }
 case GW_RADIO: { /* 单选钮: (o) 文本 () */
 grad_box(b, bw, bh, wd->x, wd->y + 1, wd->chk, gfoc);
 gtext(b, bw, bh, wd->x + 18, wd->y + 1,
 (const unsigned char*)wd->txt, C_TEXT, 0, 0);
 break;
 }
 case GW_MENU: /* 菜单栏条 (Win9x 风: 两端+底部外框 + 右下阴影) */
 if (gui_mb.win != (int)(w - GUW) || !gui_mb.used) break;
 gfill(b, bw, bh, 0, wd->y, w->w, 18, C_BTNBG);
 /* Win9x 外框: 左/右/底各 1px 黑色 + 内侧上 1px 亮线 (弹层不画框/阴影仍干净) */
 gfill(b, bw, bh, 0, wd->y, 1, 18, C_TEXT);
 gfill(b, bw, bh, w->w - 1, wd->y, 1, 18, C_TEXT);
 gfill(b, bw, bh, 0, wd->y + 18 - 1, w->w, 1, C_TEXT);
 gfill(b, bw, bh, 1, wd->y, w->w - 2, 1, 0xFFFF);
 /* Win9x 阴影: 右下 2px 黑色硬边 (与弹层阴影风格一致) */
 gfill(b, bw, bh, w->w, wd->y + 1, 2, 18 + 2, 0x0000);
 gfill(b, bw, bh, 1, wd->y + 18, w->w, 2, 0x0000);
 {
 int tx = 4;
 for (int i = 0; i < gui_mb.nmenu; i++) {
 int on = (gui_mb.open == i);
 const char *t = gui_mb.titles[i];
 int tw = gstr_px((const unsigned char*)t);
 gfill(b, bw, bh, tx, wd->y, tw + 16, 18,
 on ? C_SELBG : C_BTNBG); /* 打开项反蓝 */
 /* 活跃层级: 非活跃窗的菜单标题文字变灰 */
 unsigned short mfg = on ? C_SELFG : (w->active ? C_TEXT : C_TITLEF);
 gtext(b, bw, bh, tx + 8, wd->y + 1, (const unsigned char*)t,
 mfg, on ? C_SELBG : C_BTNBG, 1);
 tx += tw + 16;
 }
 }
 /* 弹层 (open>=0) 由 gcompose_full/gblit_win 末尾统一画到 LFB, 永远在 z 顶 */
 break;
 }
}


static void gw_redraw(gui_win_t *w) {
 gui_dirty = 1;
 dirty_win = (int)(w - GUW); /* 只重blit本窗, 不全屏清桌面 → 交互不闪 */
 gfill(w->buf, w->w, w->h, 0, 0, w->w, w->h, C_WINBG);
 int isfoc = (active_win == (w - GUW)); /* 活跃窗口蓝色标题, 非活跃灰色 */
 unsigned short band = isfoc ? C_TITLEFX : C_TITLEF;
 /* Win9x window frame: 1px 黑细外框 (CTLCOLOR_WINDOWFRAME) + 1px 内侧亮线
 * 模拟早期 Windows 立体感; 不画现代模糊阴影 (docs#30 禁止) */
 gborder(w->buf, w->w, w->h, 0, 0, w->w, w->h, C_TEXT); /* 1px 黑外框 */
 gfill(w->buf, w->w, w->h, 1, 1, w->w - 2, 1, 0xFFFF); /* 内侧上 1px 亮 */
 gfill(w->buf, w->w, w->h, 1, w->h - 2, w->w - 2, 1, C_BTNBDR); /* 下暗 */
 gfill(w->buf, w->w, w->h, 1, 1, 1, w->h - 2, 0xFFFF); /* 左亮 */
 gfill(w->buf, w->w, w->h, w->w - 2, 1, 1, w->h - 2, C_BTNBDR); /* 右暗 */
 /* 标题带: 跨宽度, 但避开 2px 边线 */
 gfill(w->buf, w->w, w->h, 2, 2, w->w - 4, 16, band);
 /* 标题文字: 右让出 chrome 区, 过长按字形截断 + 补 "..." */
 int maxw = w->w - (CHROME_N * CHROME_W) - 8;
 int tlen = gstrlen(w->title), n = 0, used = 0, over = 0;
 while (n < tlen) {
 int cl, cj, cw = gadv((const unsigned char*)w->title + n, &cl, &cj);
 if (cl < 1) cl = 1;
 if (used + cw > maxw) { over = 1; break; }
 used += cw; n += cl;
 }
 gtx_line(w->buf, w->w, w->h, 4, 1, w->title, 0, n, C_TITLEFG, band, 1);
 if (over) gtext(w->buf, w->w, w->h, 4 + used, 1,
 (const unsigned char*)"...", C_TITLEFG, band, 1);
 if (w->state != W_MIN) { /* 最小化条只留标题带 (无控制钮/下划线) */
 gdraw_chrome(w->buf, w->w, w->h, w->w, isfoc);
 gfill(w->buf, w->w, w->h, 0, 19, w->w, 1, C_BTNBDR);
 } else { /* 最小化条: 右侧画 ▢ 图标 (12x10) 提示"可点击最大化" */
 int mx = w->w - 16, my = 3, mw = 12, mh = 10;
 gborder(w->buf, w->w, w->h, mx, my, mw, mh, C_TITLEFG);
 gfill(w->buf, w->w, w->h, mx + 2, my + 2, mw - 4, mh - 4, band);
 }
 for (int i = 0; i < w->nwid; i++) gw_draw(w, &w->wd[i]);
}

/* ── 交互助手 ── */

/* 可聚焦控件? (LBL/MENU 不可; EDIT/TAREA/BTN/CHECK/RADIO/LIST 可) */
static int gw_focusable(int type) {
 return type == GW_EDIT || type == GW_TEXTAREA || type == GW_BTN ||
 type == GW_CHECK || type == GW_RADIO || type == GW_LIST;
}

/* TAB/方向键在窗内循环聚焦 (dir=+1 下一, -1 上一)。聚焦到控件并重画。 */
static int gw_focus_step(gui_win_t *w, int dir) {
 if (w->nwid == 0) return -1;
 int cur = w->foc_wid;
 for (int s = 1; s <= w->nwid; s++) {
 int k = (cur + dir * s + w->nwid) % w->nwid;
 if (gw_focusable(w->wd[k].type)) {
 w->foc_wid = k; gw_redraw(w); return k;
 }
 }
 return -1;
}

/* 同窗同组的单选互斥: 清同组所有 chk, 再把 g 置勾。返回 1=已勾。 */
static int gw_radio_check(gui_win_t *w, gui_wid_t *g) {
 for (int i = 0; i < w->nwid; i++)
 if (w->wd[i].type == GW_RADIO && w->wd[i].grp == g->grp)
 w->wd[i].chk = 0;
 g->chk = 1;
 return 1;
}

/* ── 菜单栏助手 (gui_mb) ── */

/* 计算菜单 m 的弹层绝对坐标 (pop_x/y/w/h): 横向从标题条中心起, 纵向在标题条下沿.
 * 不修改 mb 状态, 仅计算. 返回 1 成功, 0 mb.win 失效. */
static int gui_mb_compute_popup(int m, int *ox, int *oy, int *ow, int *oh) {
 if (m < 0 || m >= gui_mb.nmenu) return 0;
 if (gui_mb.win < 0 || gui_mb.win >= GW_MAXWIN || !GUW[gui_mb.win].used) return 0;
 gui_win_t *w = &GUW[gui_mb.win];
 /* 算标题条内 m 的局部 x: 累加 0..m-1 标题宽 + 间距 */
 int tx = 4;
 for (int i = 0; i < m; i++) {
 int tw = gstr_px((const unsigned char*)gui_mb.titles[i]);
 tx += tw + 16;
 }
 /* 项宽 = 最长项 + 24, 最小 96 */
 int ni = gui_mb.nitems[m];
 int pw = 0;
 for (int i = 0; i < ni; i++) {
 int tw = gstr_px((const unsigned char*)gui_mb.items[m][i]);
 if (tw > pw) pw = tw;
 }
 pw += 24; if (pw < 96) pw = 96;
 int ph = ni * 16 + 2;
 int fbw = fb_vbe_w(), fbh = fb_vbe_h();
 int px = w->x + tx;
 if (px + pw > fbw) px = fbw - pw; /* 超出右屏 → 左移贴右 */
 if (px < 0) px = 0;
 int py = w->y + 18 + 18; /* 标题 18 + 菜单条 18 */
 if (py + ph > fbh) py = fbh - ph;
 if (py < 0) py = 0;
 *ox = px; *oy = py; *ow = pw; *oh = ph;
 return 1;
}

/* 把弹层 (gui_mb.pop_x/y/w/h) 直写到 LFB — 永远在所有窗口之上.
 * 必须先在 gcompose_full/gblit_win 末尾调, 否则被后续 blit 覆盖. */
static void gdraw_menu_popup(void) {
 if (!fb_active()) return;
 if (gui_mb.open < 0) return;
 if (gui_mb.win < 0 || gui_mb.win >= GW_MAXWIN || !GUW[gui_mb.win].used) return;
 int m = gui_mb.open;
 int ni = gui_mb.nitems[m];
 if (ni <= 0) return;
 int px = gui_mb.pop_x, py = gui_mb.pop_y;
 int pw = gui_mb.pop_w, ph = gui_mb.pop_h;
 if (pw <= 0 || ph <= 0) return;
 unsigned fb = fb_vbe_base();
 int fbw = fb_vbe_w(), fbh = fb_vbe_h();
 int bpl = fb_vbe_bpl();
 /* Win9x 弹层阴影: 2px 黑色右下偏移 (与窗口 frame 风格一致的硬边阴影,
 * 非现代模糊阴影; docs#30 禁模糊但允许 Win9x 硬边阴影) */
 int sh = 2;
 gfill_lfb(fb, bpl, fbw, fbh, px + sh, py + ph, pw, sh, 0x0000); /* 底部 */
 gfill_lfb(fb, bpl, fbw, fbh, px + pw, py + sh, sh, ph, 0x0000); /* 右侧 */
 /* 背景 + 边框 */
 gfill_lfb(fb, bpl, fbw, fbh, px + 1, py + 1, pw - 2, ph - 2, C_EDBG);
 gborder_lfb(fb, bpl, fbw, fbh, px, py, pw, ph, C_BTNBDR);
 /* 各项 */
 int iy = py + 1;
 for (int i = 0; i < ni; i++) {
 const char *itxt = gui_mb.items[m][i];
 int sep = (itxt[0] == '-');
 if (gui_mb.hilite == i && !sep) {
 gfill_lfb(fb, bpl, fbw, fbh, px + 1, iy, pw - 2, 16, C_SELBG);
 gtext_lfb(fb, bpl, fbw, fbh, px + 8, iy,
 (const unsigned char*)itxt, C_SELFG, C_SELBG, 1);
 } else if (sep) {
 gfill_lfb(fb, bpl, fbw, fbh, px + 4, iy + 8, pw - 8, 1, C_BTNBDR);
 } else {
 gtext_lfb(fb, bpl, fbw, fbh, px + 8, iy,
 (const unsigned char*)itxt, C_TEXT, C_EDBG, 1);
 }
 iy += 16;
 }
}

static void gui_mb_redraw(void) {
 if (gui_mb.win >= 0 && gui_mb.win < GW_MAXWIN && GUW[gui_mb.win].used) {
 gw_redraw(&GUW[gui_mb.win]); gcompose();
 }
}
static void gui_mb_close(void) {
 if (gui_mb.open != -1) {
 int px = gui_mb.pop_x, py = gui_mb.pop_y;
 int pw = gui_mb.pop_w, ph = gui_mb.pop_h;
 gui_mb.open = -1; gui_mb.hilite = -1;
 if (gui_mb.win >= 0 && gui_mb.win < GW_MAXWIN && GUW[gui_mb.win].used)
 gw_redraw(&GUW[gui_mb.win]); /* 标题条去高亮 */
 /* 弹层矩形区域暴露: 弹层可能伸出窗沿压到桌面, 全量 blit 不清桌面
 * (desktop_init 优化) → 须显式擦弹层矩形再补相交窗 */
 gcompose_expose(px, py, pw, ph);
 }
}
static void gui_mb_open_menu(int m) {
 if (m < 0 || m >= gui_mb.nmenu) return;
 int px, py, pw, ph;
 if (!gui_mb_compute_popup(m, &px, &py, &pw, &ph)) return;
 gui_mb.pop_x = px; gui_mb.pop_y = py;
 gui_mb.pop_w = pw; gui_mb.pop_h = ph;
 gui_mb.open = m; gui_mb.hilite = 0;
 /* 整屏: 标题条高亮 + 弹层覆在 LFB 顶层 */
 gui_dirty = 1; dirty_win = -1;
 gui_mb_redraw();
}
static void gui_mb_switch(int d) { /* ←/→ 切换菜单 */
 int n = gui_mb.nmenu; if (n < 1) return;
 int m = gui_mb.open < 0 ? 0 : (gui_mb.open + d + n) % n;
 gui_mb_open_menu(m);
}
static void gui_mb_move(int d) { /* ↑/↓ 移动高亮项 (跳过分隔) */
 int m = gui_mb.open; if (m < 0) return;
 int ni = gui_mb.nitems[m]; if (ni < 1) return;
 int h = gui_mb.hilite, step = 0;
 if (h < 0 || h >= ni) h = (d > 0) ? -1 : ni; /* 首次进入 */
 do { h += d; if (h < 0) h = ni - 1; if (h >= ni) h = 0; step++; }
 while (gui_mb.items[m][h][0] == '-' && step < ni * 2);
 gui_mb.hilite = h; gui_mb_redraw();
}

static void gdraw_icon(void) __attribute__((unused,noinline));
static void gui_draw_pointer(void) {
 unsigned fb = fb_vbe_base(); int fbw = fb_vbe_w(), fbh = fb_vbe_h();
 int bpl = fb_vbe_bpl(), x = mouse_px_x(), y = mouse_px_y();
 if (x >= fbw - 9) x = fbw - 9; if (y >= fbh - 9) y = fbh - 9;
 if (x < 0) x = 0; if (y < 0) y = 0;
 /* 7×8 箭头 (尖端朝左上): 黑描边 + 白填充, 亮/暗背景都可见。
 * 原形 7×8 + 1px 描边膨胀 = 包围盒 9×10, 与 ptr_save_region 一致。 */
 static const unsigned char S[8] = { 0x40,0x60,0x70,0x78,0x7C,0x7E,0x76,0x66 };
 #define S_BIT(j,i) (((S[j] >> (6-(i))) & 1))
 for (int j = 0; j < 9; j++) { /* 黑描边 = 原形膨胀 1px */
 for (int i = 0; i < 8; i++) {
 int in = (j < 8 && i < 7) ? S_BIT(j,i) : 0;
 int up = (j > 0 && i < 7) ? S_BIT(j-1,i) : 0;
 int dn = (j < 7 && i < 7) ? S_BIT(j+1,i) : 0;
 int lf = (j < 8 && i > 0) ? S_BIT(j,i-1) : 0;
 int rt = (j < 8 && i < 6) ? S_BIT(j,i+1) : 0;
 if (in || up || dn || lf || rt) {
 unsigned char *p = (unsigned char *)(fb + (unsigned)(y+j)*bpl + (unsigned)(x+i)*2);
 p[0] = 0; p[1] = 0;
 }
 }
 }
 for (int j = 0; j < 8; j++) /* 白填充 (原形) */
 for (int i = 0; i < 7; i++)
 if (S_BIT(j,i)) {
 unsigned char *p = (unsigned char *)(fb + (unsigned)(y+j)*bpl + (unsigned)(x+i)*2);
 p[0] = 0xFF; p[1] = 0xFF;
 }
 #undef S_BIT
}

/* ── 指针 sprite: 保存背景, 移动时擦旧画新, 绝不全屏重写 ──
 * gui_draw_pointer 体 (6×7) + 尾 (3×3) = 包围盒 9×10。钳制必须与
 * gui_draw_pointer 完全一致, 否则边缘处擦/画错位。 */
static unsigned short ptr_bg[10 * 10];
static int ptr_bg_x = 0, ptr_bg_y = 0, ptr_bg_valid = 0;

static inline void ptr_clamp(int *x, int *y) { /* 与 gui_draw_pointer 同钳制 */
 int fbw = fb_vbe_w(), fbh = fb_vbe_h();
 if (*x >= fbw - 9) *x = fbw - 9; if (*x < 0) *x = 0;
 if (*y >= fbh - 9) *y = fbh - 9; if (*y < 0) *y = 0;
}
static void ptr_save_region(int px, int py) {
 ptr_clamp(&px, &py);
 unsigned fb = fb_vbe_base(); int bpl = fb_vbe_bpl();
 int fbw = fb_vbe_w(), fbh = fb_vbe_h();
 int x0 = px, y0 = py, x1 = px + 10, y1 = py + 10;
 if (x1 > fbw) x1 = fbw; if (y1 > fbh) y1 = fbh;
 int w = x1 - x0, h = y1 - y0;
 if (w <= 0 || h <= 0) { ptr_bg_valid = 0; return; }
 int si = 0;
 for (int j = 0; j < h; j++) {
 unsigned char *p = (unsigned char *)(fb + (unsigned)(y0 + j) * bpl + (unsigned)x0 * 2);
 for (int i = 0; i < w; i++) { ptr_bg[si++] = p[0] | (p[1] << 8); p += 2; }
 }
 ptr_bg_x = px; ptr_bg_y = py; ptr_bg_valid = 1;
}
static void ptr_restore_region(void) {
 if (!ptr_bg_valid) return;
 int px = ptr_bg_x, py = ptr_bg_y; /* 已是钳制后坐标 */
 unsigned fb = fb_vbe_base(); int bpl = fb_vbe_bpl();
 int fbw = fb_vbe_w(), fbh = fb_vbe_h();
 int x0 = px, y0 = py, x1 = px + 10, y1 = py + 10;
 if (x1 > fbw) x1 = fbw; if (y1 > fbh) y1 = fbh;
 int w = x1 - x0, h = y1 - y0;
 if (w <= 0 || h <= 0) { ptr_bg_valid = 0; return; }
 int si = 0;
 for (int j = 0; j < h; j++) {
 unsigned char *p = (unsigned char *)(fb + (unsigned)(y0 + j) * bpl + (unsigned)x0 * 2);
 for (int i = 0; i < w; i++) { p[0] = ptr_bg[si] & 0xFF; p[1] = (ptr_bg[si] >> 8) & 0xFF; si++; p += 2; }
 }
}

/* 整屏重合成: 桌面底 + 自底向上 blit 窗口 + 保存指针背景 + 画指针
 * 限流 ≤~33ms(30Hz): 若距上次 <3 tick(约 33ms)则跳过, 让 QEMU 有时间把
 * 上一帧完整显示出来, 否则以轮询速度狂写 LFB → 主机持续重绘 → 交互闪烁
 * (v6.9.3)。返回 1=已合成, 0=被限流(留待下轮)。 */
static unsigned last_full_tick = 0;
static int gfull_force = 0; /* 拖动结束兜底: 忽略 30Hz 限流整屏一次 */
static int gcompose_full(void) {
 if (!fb_active()) return 1;
 unsigned now = task_ticks();
 if (!gfull_force && now - last_full_tick < 3) return 0;
 gfull_force = 0; last_full_tick = now;
 unsigned fb = fb_vbe_base(); int fbw = fb_vbe_w(), fbh = fb_vbe_h();
 int bpl = fb_vbe_bpl();
 /* 抗闪: 只在首帧铺桌面底色; 之后按 z 全量 blit 本身幂等, 无需整屏清
 * (整屏清与重 blit 分帧可见 → 点击/切活跃整屏闪烁) */
 if (!desktop_init) {
 for (int y = 0; y < fbh; y++)
 for (int x = 0; x < fbw; x++) {
 unsigned char *p = (unsigned char *)(fb + (unsigned)y * bpl + (unsigned)x * 2);
 p[0] = (unsigned char)(C_DESKTOP & 0xFF); p[1] = (unsigned char)(C_DESKTOP >> 8);
 }
 desktop_init = 1;
 }
 for (int z = 1; z <= gui_zmax; z++) {
 for (int k = 0; k < GW_MAXWIN; k++) {
 gui_win_t *w = &GUW[k];
 if (!w->used || w->z != z) continue;
 for (int y = 0; y < w_draw_h(w); y++) {
 int yy = w->y + y; if (yy < 0 || yy >= fbh) continue;
 for (int x = 0; x < w->w; x++) {
 int xx = w->x + x; if (xx < 0 || xx >= fbw) continue;
 unsigned char *p = (unsigned char *)(fb + (unsigned)yy * bpl + (unsigned)xx * 2);
 unsigned short c = w->buf[(unsigned)y * w->w + (unsigned)x];
 p[0] = (unsigned char)(c & 0xFF); p[1] = (unsigned char)(c >> 8);
 }
 }
 }
 }
 /* : 菜单弹层永远在 z 顶, 画在所有窗口之上 (指针之下) */
 gdraw_menu_popup();
 int mx = mouse_px_x(), my = mouse_px_y();
 if (mouse_installed_k()) {
 ptr_save_region(mx, my);
 gui_draw_pointer();
 } else {
 ptr_bg_valid = 0;
 }
 last_mx = mx; last_my = my;
 gui_dirty = 0;
 return 1;
}

/* 区域重合成: 只擦暴露矩形 (关窗/最小化收起的区) + 重 blit 相交窗/活跃窗。
 * 取代"整屏清桌面再全部重 blit" — 消除关窗/最小化时的整屏闪烁。 */
static void gcompose_expose(int ex, int ey, int ew, int eh) {
 if (!fb_active()) return;
 unsigned fb = fb_vbe_base(); int fbw = fb_vbe_w(), fbh = fb_vbe_h();
 int bpl = fb_vbe_bpl();
 int x1 = ex + ew, y1 = ey + eh;
 if (ex < 0) ex = 0; if (ey < 0) ey = 0;
 if (x1 > fbw) x1 = fbw; if (y1 > fbh) y1 = fbh;
 for (int y = ey; y < y1; y++)
 for (int x = ex; x < x1; x++) {
 unsigned char *p = (unsigned char *)(fb + (unsigned)y * bpl + (unsigned)x * 2);
 p[0] = (unsigned char)(C_DESKTOP & 0xFF); p[1] = (unsigned char)(C_DESKTOP >> 8);
 }
 int mx = mouse_px_x(), my = mouse_px_y();
 int ptr_hit = (mx >= ex && mx < x1 && my >= ey && my < y1);
 for (int z = 1; z <= gui_zmax; z++)
 for (int k = 0; k < GW_MAXWIN; k++) {
 gui_win_t *w = &GUW[k];
 if (!w->used || w->z != z) continue;
 /* 暴露矩形相交窗必 blit; 活跃窗标题色可能已变, 也 blit */
 if (k != active_win &&
 !(w->x < x1 && w->x + w->w > ex && w->y < y1 && w->y + w_draw_h(w) > ey))
 continue;
 for (int y = 0; y < w_draw_h(w); y++) {
 int yy = w->y + y; if (yy < 0 || yy >= fbh) continue;
 for (int x = 0; x < w->w; x++) {
 int xx = w->x + x; if (xx < 0 || xx >= fbw) continue;
 unsigned char *p = (unsigned char *)(fb + (unsigned)yy * bpl + (unsigned)xx * 2);
 unsigned short c = w->buf[(unsigned)y * w->w + (unsigned)x];
 p[0] = (unsigned char)(c & 0xFF); p[1] = (unsigned char)(c >> 8);
 }
 }
 if (mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + w_draw_h(w))
 ptr_hit = 1;
 }
 gdraw_menu_popup();
 if (mouse_installed_k()) {
 if (ptr_hit) {
 ptr_bg_valid = 0;
 ptr_save_region(mx, my);
 gui_draw_pointer();
 }
 } else {
 ptr_bg_valid = 0;
 }
 last_mx = mx; last_my = my;
 gui_dirty = 0; dirty_win = -1;
}

/* 只重blit单个窗口到 LFB (widget 变更): 不整屏清桌面/不重blit其他窗 →
 * 点按钮/打字不再出现"先全屏变暗再重绘"的闪烁 (v6.9.5)。窗口在顶层不透明,
 * 直接覆盖即可; 指针若落在该区, 先擦旧背景再画新, 不残留鬼影。 */
static void gblit_win(int k) {
 if (!fb_active()) return;
 gui_win_t *w = &GUW[k];
 if (!w->used) return;
 unsigned fb = fb_vbe_base(); int fbw = fb_vbe_w(), fbh = fb_vbe_h();
 int bpl = fb_vbe_bpl();
 int mx = mouse_px_x(), my = mouse_px_y();
 /* 已保存的指针背景若覆盖本窗矩形, 先擦掉, 否则 blit 会盖掉指针下的
 * 已保存像素 → 指针移动后留残影 */
 if (ptr_bg_valid && ptr_bg_x < w->x + w->w && ptr_bg_x + 10 > w->x &&
 ptr_bg_y < w->y + w_draw_h(w) && ptr_bg_y + 10 > w->y)
 ptr_restore_region();
 for (int y = 0; y < w_draw_h(w); y++) {
 int yy = w->y + y; if (yy < 0 || yy >= fbh) continue;
 for (int x = 0; x < w->w; x++) {
 int xx = w->x + x; if (xx < 0 || xx >= fbw) continue;
 unsigned char *p = (unsigned char *)(fb + (unsigned)yy * bpl + (unsigned)xx * 2);
 unsigned short c = w->buf[(unsigned)y * w->w + (unsigned)x];
 p[0] = (unsigned char)(c & 0xFF); p[1] = (unsigned char)(c >> 8);
 }
 }
 /* z 序修复: 本窗可能是被埋在下层的 (demo 改它任一控件都会 gw_redraw →
 * 单窗 blit)。若直接画上 LFB, 它会盖住 z 更高的活跃窗 — "灰窗浮在蓝窗上"。
 * 补: 按 z 序重 blit 所有与本窗相交的更高窗。 */
 int ptr_hit = (mx >= w->x && mx < w->x + w->w
 && my >= w->y && my < w->y + w_draw_h(w));
 for (int q = 0; q < GW_MAXWIN; q++) {
 gui_win_t *o = &GUW[q];
 if (!o->used || q == k || o->z <= w->z) continue;
 if (!(o->x < w->x + w->w && o->x + o->w > w->x
 && o->y < w->y + w_draw_h(w) && o->y + w_draw_h(o) > w->y)) continue;
 for (int y = 0; y < w_draw_h(o); y++) {
 int yy = o->y + y; if (yy < 0 || yy >= fbh) continue;
 for (int x = 0; x < o->w; x++) {
 int xx = o->x + x; if (xx < 0 || xx >= fbw) continue;
 unsigned char *p = (unsigned char *)(fb + (unsigned)yy * bpl + (unsigned)xx * 2);
 unsigned short c = o->buf[(unsigned)y * o->w + (unsigned)x];
 p[0] = (unsigned char)(c & 0xFF); p[1] = (unsigned char)(c >> 8);
 }
 }
 if (mx >= o->x && mx < o->x + o->w && my >= o->y && my < o->y + w_draw_h(o))
 ptr_hit = 1;
 }
 /* : 弹层在 z 顶 — 若弹层与被 blit 区域相交, blit 已盖掉弹层像素, 补画 */
 if (gui_mb.used && gui_mb.open >= 0
 && gui_mb.pop_x < w->x + w->w && gui_mb.pop_x + gui_mb.pop_w > w->x
 && gui_mb.pop_y < w->y + w_draw_h(w) && gui_mb.pop_y + gui_mb.pop_h > w->y)
 gdraw_menu_popup();
 /* 指针落在被 blit 区域: 重存背景 + 重画 (弹层已在指针下) */
 if (ptr_hit) {
 ptr_bg_valid = 0;
 ptr_save_region(mx, my);
 gui_draw_pointer();
 }
 last_mx = mx; last_my = my;
 gui_dirty = 0;
}

/* 把窗口 m 的离屏 buffer 拷到 LFB, 裁剪到 [cx0,cx1)×[cy0,cy1) 及 m 本体矩形
 * (, 暴露区补窗用)。 */
static void blit_win_into(const gui_win_t *m, int cx0, int cy0, int cx1, int cy1) {
 unsigned fb = fb_vbe_base(); int fbw = fb_vbe_w(), fbh = fb_vbe_h();
 int bpl = fb_vbe_bpl();
 int ix0 = cx0, iy0 = cy0, ix1 = cx1, iy1 = cy1;
 if (ix0 < m->x) ix0 = m->x;
 if (iy0 < m->y) iy0 = m->y;
 if (ix1 > m->x + m->w) ix1 = m->x + m->w;
 if (iy1 > m->y + w_draw_h(m)) iy1 = m->y + w_draw_h(m);
 if (ix0 < 0) ix0 = 0; if (iy0 < 0) iy0 = 0;
 if (ix1 > fbw) ix1 = fbw; if (iy1 > fbh) iy1 = fbh;
 if (ix0 >= ix1 || iy0 >= iy1) return;
 for (int y = iy0; y < iy1; y++) {
 const unsigned short *src = m->buf + (unsigned)(y - m->y) * m->w + (ix0 - m->x);
 unsigned char *p = (unsigned char *)(fb + (unsigned)y * bpl + (unsigned)ix0 * 2);
 for (int x = ix0; x < ix1; x++) {
 unsigned short c = *src++;
 p[0] = (unsigned char)(c & 0xFF); p[1] = (unsigned char)(c >> 8);
 p += 2;
 }
 }
}

/* 拖动快路径 (): 只把被拖窗 bitmap 移到新位置 + 重画暴露区(下层窗/桌面), 绝
 * 不整屏清桌面 → 拖动不再"整屏闪成桌面再重画"(旧 gcompose_full)。窗口内容拖动期间
 * 不变, 无需 gw_redraw。被拖窗按住时已 raise 到顶层, 其下只有更低窗口+桌面, 补暴露
 * 区即像素精确; 结束由调用方(gui_events 松开)触发一次强制整屏兜底。 */
static void gdx_move(int k, int nx, int ny) {
 gui_win_t *w = &GUW[k];
 int ox = w->x, oy = w->y, ow = w->w, oh = w_draw_h(w);
 if (nx != ox || ny != oy) {
 unsigned fb = fb_vbe_base(); int fbw = fb_vbe_w(), fbh = fb_vbe_h();
 int bpl = fb_vbe_bpl();
 /* ① 先停指针: 擦旧背景, 免得 blit 盖到指针下已存背景 */
 ptr_restore_region(); ptr_bg_valid = 0;
 /* ② 被拖窗 bitmap → 新位置 (w->x/y 仍旧值, 显式按 nx,ny 拷) */
 for (int y = 0; y < oh; y++) {
 int yy = ny + y; if (yy < 0 || yy >= fbh) continue;
 int x0 = nx < 0 ? -nx : 0;
 int xe = nx + ow; if (xe > fbw) xe = fbw;
 const unsigned short *src = w->buf + (unsigned)y * ow + x0;
 unsigned char *p = (unsigned char *)(fb + (unsigned)yy * bpl + (unsigned)(nx + x0) * 2);
 for (int x = x0; x + nx < xe; x++) {
 unsigned short c = src[x - x0];
 p[0] = (unsigned char)(c & 0xFF); p[1] = (unsigned char)(c >> 8);
 p += 2;
 }
 }
 w->x = nx; w->y = ny;
 /* ③ 暴露区 = 旧∪新 包围盒 − 新rect, 4 矩形分解; 每个填桌面 + 自底向上 blit 更低窗 */
 int ux0 = ox < nx ? ox : nx, uy0 = oy < ny ? oy : ny;
 int ux1 = (ox > nx ? ox : nx) + ow, uy1 = (oy > ny ? oy : ny) + oh;
 if (ux0 < 0) ux0 = 0; if (uy0 < 0) uy0 = 0;
 if (ux1 > fbw) ux1 = fbw; if (uy1 > fbh) uy1 = fbh;
 int EX[4][4] = {
 { ux0, uy0, nx - 1, uy1 - 1 }, /* 左 */
 { nx + ow, uy0, ux1 - 1, uy1 - 1 }, /* 右 */
 { nx, uy0, nx + ow - 1, ny - 1 }, /* 上 */
 { nx, ny + oh, nx + ow - 1, uy1 - 1 }, /* 下 */
 };
 for (int i = 0; i < 4; i++) {
 int cx0 = EX[i][0], cy0 = EX[i][1], cx1 = EX[i][2], cy1 = EX[i][3];
 if (cx0 > cx1 || cy0 > cy1) continue;
 for (int y = cy0; y <= cy1; y++) {
 unsigned char *p = (unsigned char *)(fb + (unsigned)y * bpl + (unsigned)cx0 * 2);
 for (int x = cx0; x <= cx1; x++) {
 p[0] = (unsigned char)(C_DESKTOP & 0xFF); p[1] = (unsigned char)(C_DESKTOP >> 8);
 p += 2;
 }
 }
 for (int z = 1; z < w->z; z++)
 for (int mm = 0; mm < GW_MAXWIN; mm++) {
 gui_win_t *m = &GUW[mm];
 if (m->used && m->z == z) blit_win_into(m, cx0, cy0, cx1 + 1, cy1 + 1);
 }
 }
 }
 /* ④ 指针重画 */
 int mx = mouse_px_x(), my = mouse_px_y();
 if (mouse_installed_k()) {
 ptr_save_region(mx, my);
 gui_draw_pointer();
 } else {
 ptr_bg_valid = 0;
 }
 last_mx = mx; last_my = my;
 gui_dirty = 0;
}

/* 增量指针更新: 擦旧背景 + 存新背景 + 画新指针。绝不写窗口像素 → 鼠标移动
 * 不再触发整屏重写 → 无撕裂、无闪烁 (v6.9.2 修复)。 */
static void gcompose(void) {
 if (!fb_active()) return;
 int mx = mouse_px_x(), my = mouse_px_y();
 if (gui_dirty) {
 if (dirty_win >= 0) { gblit_win(dirty_win); dirty_win = -1; return; }
 if (gcompose_full()) return; /* 整屏 (新建/关闭/raise) */
 }
 if (mx == last_mx && my == last_my) return; /* 真无变化 */
 if (!mouse_installed_k()) { last_mx = mx; last_my = my; return; }
 ptr_restore_region(); /* 擦旧指针 */
 ptr_save_region(mx, my); /* 存新背景 */
 gui_draw_pointer(); /* 画新指针 */
 last_mx = mx; last_my = my;
}

static gui_wid_t *gw_get(int win, int ctl) {
 if (win < 0 || win >= GW_MAXWIN) return 0;
 gui_win_t *w = &GUW[win];
 if (!w->used) return 0;
 if (ctl < 0 || ctl >= w->nwid) return 0;
 return &w->wd[ctl];
}

/* 新控件: w 准备布局; ww/hh 为 0 表示标签 (无框) */
static int gw_new(gui_win_t *w, int type, int x, int y, int ww, int hh) {
 if (w->nwid >= GW_MAXWID) return -1;
 gui_wid_t *wd = &w->wd[w->nwid];
 wd->type = type; wd->x = x; wd->y = y; wd->w = ww; wd->h = hh;
 wd->txt[0] = 0; wd->caret = 0; wd->nitems = 0; wd->sel = -1; wd->scroll = 0;
 wd->txid = -1; wd->txlen = 0; wd->txc = 0; wd->txcol = 0; wd->txsc = 0;
 wd->sel_anchor = 0; wd->sel_active = 0;
 wd->chk = 0; wd->grp = 0;
 if (type == GW_TEXTAREA) { /* 分配多行内容槽 */
 for (int s = 0; s < GW_TXPOOL; s++)
 if (!gui_txused[s]) { wd->txid = s; gui_txused[s] = 1; gui_txpool[s][0] = 0; break; }
 }
 int id = w->nwid++;
 if (type == GW_EDIT) w->foc_wid = id;
 gw_redraw(w); gcompose();
 return id;
}

/* ── 公共 API (syscall 28-43) ── */
int gui_enter(void) {
 if (!fb_active()) return -1;
 serial_puts(GUI_VERSION_FULL " ready\n"); /* v6.5.3: 启动时公告 GUI 版本 */
 gui_active = 1;
 gui_buf_h = fb_vbe_h(); if (gui_buf_h <= 0) gui_buf_h = 480;
 gui_zmax = 0; foc_win = -1; gui_dirty = 1; dirty_win = -1;
 drag_win = -1; sel_drag_w = -1;
 gfull_force = 1; /* 启动强制铺一次桌面, 30Hz 限流会跳过首轮 */
 gcompose();
 return 0;
}

void gui_leave(void) {
 if (!gui_active) return;
 gui_active = 0;
 for (int k = 0; k < GW_MAXWIN; k++)
 if (GUW[k].used) { if (GUW[k].buf) mem_free(GUW[k].buf); GUW[k].used = 0; }
 foc_win = -1; gui_zmax = 0;
 drag_win = -1; sel_drag_w = -1;
 cls();
}

int gui_win(int x, int y, int w, int h, const char *title) {
 if (!gui_active) return -1;
 if (w < 1 || h < 1 || w > 640 || h > 480) return -1;
 int fbw = fb_vbe_w(), fbh = gui_buf_h;
 if (x < 0) x = 0; if (y < 0) y = 0;
 if (x + w > fbw) x = fbw - w; if (y + h > fbh) y = fbh - h;
 for (int k = 0; k < GW_MAXWIN; k++) {
 gui_win_t *wd = &GUW[k];
 if (wd->used) continue;
 wd->used = 1; wd->x = x; wd->y = y; wd->w = w; wd->h = h;
 wd->z = ++gui_zmax; wd->foc_wid = -1; wd->nwid = 0;
 wd->state = W_NORM; wd->rx = x; wd->ry = y; wd->rw = w; wd->rh = h;
 wd->topmost = 0;
 gcopy(wd->title, title ? title : "", GW_TITLE);
 wd->buf = (unsigned short *)mem_alloc((unsigned)w * (unsigned)h * 2);
 if (!wd->buf) { wd->used = 0; gui_zmax--; return -1; }
 /* 新窗口(含弹窗)自动成为活跃窗口: 活跃窗始终在最顶层 */
 int olda = active_win;
 if (olda >= 0 && olda < GW_MAXWIN && GUW[olda].used) {
 GUW[olda].active = 0;
 for (int i = 0; i < GUW[olda].nwid; i++) { /* 旧活跃窗选区塌缩 */
 gui_wid_t *od = &GUW[olda].wd[i];
 if (od->type == GW_EDIT || od->type == GW_TEXTAREA)
 od->sel_anchor = od->sel_active = 0;
 }
 gw_redraw(&GUW[olda]); /* 旧活跃窗标题转灰 */
 }
 wd->active = 1;
 active_win = k;
 if (gui_mb.used && gui_mb.open >= 0 && gui_mb.win != k)
 gui_mb_close(); /* 活跃层级: 弹层属主非新活跃窗 → 收起 */
 foc_win = k; /* 键盘焦点跟随活跃 */
 gw_redraw(wd);
 dirty_win = -1;
 last_full_tick = 0;
 gfull_force = 1; /* 切活跃立即刷 (消"卡一帧") */
 gcompose();
 return k;
 }
 return -1;
}

int gui_win_close(int id) {
 if (!gui_active || id < 0 || id >= GW_MAXWIN || !GUW[id].used) return -1;
 int ex = GUW[id].x, ey = GUW[id].y, ew = GUW[id].w, eh = w_draw_h(&GUW[id]);
 if (GUW[id].buf) mem_free(GUW[id].buf);
 GUW[id].used = 0; gui_dirty = 1; dirty_win = -1;
 if (foc_win == id) foc_win = -1;
 if (gui_mb.used && gui_mb.win == id) gui_mb_close(); /* 菜单栏属主关 → 收弹层 */
 if (active_win == id) {
 /* 关的是活跃窗 → 把活跃转给 z 最高的剩余窗口 (无窗则 -1) */
 active_win = -1;
 int bestz = -1;
 for (int k = 0; k < GW_MAXWIN; k++)
 if (GUW[k].used && GUW[k].z > bestz) { bestz = GUW[k].z; active_win = k; }
 if (active_win >= 0) {
 GUW[active_win].active = 1;
 foc_win = active_win;
 gw_redraw(&GUW[active_win]); /* 新活跃窗标题转蓝 */
 last_full_tick = 0;
 gfull_force = 1; /* 切活跃立即刷 (消"卡一帧") */
 }
 }
 gcompose_expose(ex, ey, ew, eh); /* 只擦被关窗矩形 + 重blit相交/活跃窗 */
 return 0;
}

int gui_win_raise(int id) {
 if (!gui_active || id < 0 || id >= GW_MAXWIN || !GUW[id].used) return -1;
 int olda = active_win;
 GUW[id].z = ++gui_zmax;
 /* raise = 置顶 = 成为活跃窗口 (规范: 活跃窗始终在最顶层) */
 if (olda != id) {
 if (olda >= 0 && olda < GW_MAXWIN && GUW[olda].used) {
 GUW[olda].active = 0;
 for (int i = 0; i < GUW[olda].nwid; i++) { /* 旧活跃窗选区塌缩 */
 gui_wid_t *od = &GUW[olda].wd[i];
 if (od->type == GW_EDIT || od->type == GW_TEXTAREA)
 od->sel_anchor = od->sel_active = 0;
 }
 }
 GUW[id].active = 1;
 active_win = id;
 if (gui_mb.used && gui_mb.open >= 0 && gui_mb.win != id)
 gui_mb_close(); /* 活跃层级: 弹层属主非新活跃窗 → 收起 */
 }
 foc_win = id; /* 键盘焦点恒跟随活跃窗口 */
 gw_redraw(&GUW[id]);
 if (olda >= 0 && olda != id && olda < GW_MAXWIN && GUW[olda].used) {
 gw_redraw(&GUW[olda]); /* 旧活跃窗标题转灰 */
 dirty_win = -1;
 last_full_tick = 0;
 gfull_force = 1; /* 切活跃立即刷 (消"卡一帧") */
 }
 gcompose();
 return 0;
}

int gui_btn(int win, int cx, int cy, const char *label) {
 if (!gui_active || win < 0 || win >= GW_MAXWIN || !GUW[win].used) return -1;
 int tw = label ? gstr_px((const unsigned char*)label) : 0;
 int ww = 16 + tw; if (ww < 48) ww = 48;
 int id = gw_new(&GUW[win], GW_BTN, cx, cy, ww, 26);
 if (id >= 0) gcopy(GUW[win].wd[id].txt, label ? label : "", 64);
 return id;
}

int gui_lbl(int win, int x, int y, const char *text) {
 if (!gui_active || win < 0 || win >= GW_MAXWIN || !GUW[win].used) return -1;
 int id = gw_new(&GUW[win], GW_LBL, x, y, 0, 0);
 if (id >= 0) gcopy(GUW[win].wd[id].txt, text ? text : "", 64);
 return id;
}

int gui_statusbar(int win, int x, int y, int w, const char *text) {
 if (!gui_active || win < 0 || win >= GW_MAXWIN || !GUW[win].used) return -1;
 if (w < 20) w = 20;
 int id = gw_new(&GUW[win], GW_STATUSBAR, x, y, w, 18);
 if (id >= 0) gcopy(GUW[win].wd[id].txt, text ? text : "", 64);
 return id;
}

int gui_edit(int win, int cx, int cy, int w) {
 if (!gui_active || win < 0 || win >= GW_MAXWIN || !GUW[win].used) return -1;
 if (w > 56 * 8) w = 56 * 8; if (w < 20) w = 20;
 return gw_new(&GUW[win], GW_EDIT, cx, cy, w, 18);
}

int gui_list(int win, int x, int y, int w, int h) {
 if (!gui_active || win < 0 || win >= GW_MAXWIN || !GUW[win].used) return -1;
 if (w > 300) w = 300; if (h > 240) h = 240;
 return gw_new(&GUW[win], GW_LIST, x, y, w, h);
}

/* 多行文本区 (): 内容存独立槽池 (TX_SIZE), 光标字节偏移 + ↑↓←→ 全编辑 */
int gui_tarea(int win, int x, int y, int w, int h) {
 if (!gui_active || win < 0 || win >= GW_MAXWIN || !GUW[win].used) return -1;
 if (w < 40) w = 40; if (w > 620) w = 620;
 if (h < 34) h = 34; if (h > 460) h = 460;
 int id = gw_new(&GUW[win], GW_TEXTAREA, x, y, w, h);
 if (id >= 0) GUW[win].foc_wid = id; /* 文本区即聚焦控件 */
 return id;
}

/* 设文本区内容 (从 user buf 拷 len 字节, 截到 TX_SIZE, 去尾部残缺字形) */
int gui_tarea_set(int win, int ctl, const char *str, int len) {
 gui_wid_t *wd = gw_get(win, ctl);
 if (!wd || wd->type != GW_TEXTAREA) return -1;
 if (len < 0) len = 0;
 if (len > TX_SIZE) len = TX_SIZE;
 char *b = gui_txpool[wd->txid];
 for (int i = 0; i < len; i++) b[i] = str[i];
 gtx_trim(b, len);
 wd->txlen = gstrlen(b);
 wd->txc = 0; wd->txcol = 0; wd->txsc = 0;
 wd->sel_anchor = wd->sel_active = 0;
 gw_redraw(&GUW[win]); gcompose();
 return 0;
}

/* 内容读回 (): 把文本区内容拷入 user buf (含 NUL), 返回字节数 */
int gui_tarea_get(int win, int ctl, char *buf, int max) {
 gui_wid_t *wd = gw_get(win, ctl);
 if (!wd || wd->type != GW_TEXTAREA) return -1;
 if (!buf || max < 1) return 0;
 int n = wd->txlen; if (n > max - 1) n = max - 1;
 const char *b = gui_txpool[wd->txid];
 for (int i = 0; i < n; i++) buf[i] = b[i];
 buf[n] = 0;
 return n;
}

int gui_list_set(int win, int ctl, const char *str) {
 gui_wid_t *wd = gw_get(win, ctl);
 if (!wd || wd->type != GW_LIST) return -1;
 if (!str || !str[0]) wd->nitems = 0, wd->sel = -1, wd->scroll = 0;
 else if (wd->nitems < GW_MAXITEMS) gcopy(wd->items[wd->nitems++], str, 32);
 gw_redraw(&GUW[win]); gcompose();
 return wd->nitems;
}

/* List 读回: 把当前选中项文本拷入 buf, 返回选中索引 (-1 无选中) */
int gui_list_get(int win, int ctl, char *buf, int max) {
 gui_wid_t *wd = gw_get(win, ctl);
 if (!wd || wd->type != GW_LIST) return -1;
 if (buf && max > 0) buf[0] = 0;
 int i = wd->sel;
 if (i >= 0 && i < wd->nitems && buf && max > 1)
 gcopy(buf, wd->items[i], max);
 return i;
}

int gui_list_n(int win, int ctl) {
 gui_wid_t *wd = gw_get(win, ctl);
 if (!wd || wd->type != GW_LIST) return -1;
 return wd->nitems;
}

/* 复选框 / 单选钮 / 菜单栏 */
int gui_check(int win, int cx, int cy, const char *label) {
 if (!gui_active || win < 0 || win >= GW_MAXWIN || !GUW[win].used) return -1;
 int id = gw_new(&GUW[win], GW_CHECK, cx, cy, 14 + 18 + gstr_px((const unsigned char*)label), 18);
 if (id >= 0) gcopy(GUW[win].wd[id].txt, label ? label : "", 64);
 return id;
}
int gui_check_set(int win, int ctl, int state) {
 gui_wid_t *wd = gw_get(win, ctl);
 if (!wd || wd->type != GW_CHECK && wd->type != GW_RADIO) return -1;
 wd->chk = state ? 1 : 0;
 gw_redraw(&GUW[win]); gcompose();
 return 0;
}
int gui_radio(int win, int cx, int cy, const char *label) {
 if (!gui_active || win < 0 || win >= GW_MAXWIN || !GUW[win].used) return -1;
 int id = gw_new(&GUW[win], GW_RADIO, cx, cy, 14 + 18 + gstr_px((const unsigned char*)label), 18);
 if (id >= 0) { gcopy(GUW[win].wd[id].txt, label ? label : "", 64);
 GUW[win].wd[id].grp = 0; } /* 同窗同位组 0 互斥 */
 return id;
}

int gui_menubar(int win) {
 if (!gui_active || win < 0 || win >= GW_MAXWIN || !GUW[win].used) return -1;
 /* 重绑: 清掉旧栏数据, 绑到新窗 */
 if (gui_mb.used && gui_mb.win != win)
 { int k = gui_mb.win; if (k >= 0 && k < GW_MAXWIN && GUW[k].used)
 { gw_redraw(&GUW[k]); gcompose(); } }
 gui_mb.used = 1; gui_mb.win = win; gui_mb.nmenu = 0;
 gui_mb.open = -1; gui_mb.hilite = -1;
 int id = gw_new(&GUW[win], GW_MENU, 0, 18, GUW[win].w, 18);
 return id;
}
/* 加一个菜单到栏: title 如 "文件(F)" → 助记符取括号内 ASCII 字母小写 */
int gui_menu_add(int win, int ctl, const char *title) {
 (void)ctl;
 if (gui_mb.win != win || gui_mb.nmenu >= GMW_MENUS) return -1;
 int i = gui_mb.nmenu;
 gcopy(gui_mb.titles[i], title ? title : "", 21);
 gui_mb.nitems[i] = 0;
 gui_mb.mnem[i] = 0; /* 解析助记符 */
 const char *t = gui_mb.titles[i];
 char lc = 0; int k;
 for (k = 0; t[k]; k++) if (t[k] == '(') break; /* 括号助记 "(F)" */
 if (t[k] == '(' && t[k+1]) { lc = t[k+1]; if (lc >= 'A' && lc <= 'Z') lc += 32; }
 if (!lc) for (k = 0; t[k]; k++) if (t[k] >= 'A' && t[k] <= 'Z') { lc = t[k] + 32; break; }
 gui_mb.mnem[i] = lc;
 gui_mb.nmenu++;
 gui_mb_redraw();
 return i;
}
/* 给菜单 m 加一项 (id 内联): "-" 或空 = 分隔项; 返回项索引或 -1 */
int gui_menu_item(int win, int ctl, int menu, const char *item) {
 (void)ctl;
 if (gui_mb.win != win || menu < 0 || menu >= gui_mb.nmenu) return -1;
 if (gui_mb.nitems[menu] >= GMW_ITEMS) return -1;
 int i = gui_mb.nitems[menu]++;
 gcopy(gui_mb.items[menu][i], item ? item : "-", 32);
 return i;
}

int gui_wnd_text(int win, int ctl, const char *str) {
 gui_wid_t *wd = gw_get(win, ctl);
 if (!wd || wd->type == GW_LIST) return -1;
 gcopy(wd->txt, str ? str : "", 64);
 if (wd->type == GW_EDIT) {
 wd->caret = gstrlen(wd->txt);
 wd->sel_anchor = wd->sel_active = wd->caret; /* 设文本 → 清选区 */
 }
 gw_redraw(&GUW[win]); gcompose();
 return 0;
}

/* 输入框编辑: ch 可为可打印字符(光标处插入) / '\b'(整字形退格) / '\r'(忽略) /
 * 128(←) 129(→) 132(HOME) 133(END) 127(DEL)。码值与 SYS_GETKEY 一致。 */
int gui_edit_char(int win, int ctl, int ch) {
 gui_wid_t *wd = gw_get(win, ctl);
 if (!wd || wd->type != GW_EDIT) return -1;

 /* 改动键 (删除/替换) 且已有选区 → 先删选中, 塌缩后再常规编辑 */
 if ((ch == 127 || ch == '\b' || (ch >= 0x20 && ch <= 0x7E))
 && wd->sel_anchor != wd->sel_active)
 wd->caret = sel_delete(wd, 0); /* EDIT: len_io=NULL */

 int nc = -1; /* 导航键的新光标; -1=非导航 */
 if (ch == 128) { /* ← */
 int s = gglyph_start(wd->txt, wd->caret);
 nc = (s >= 0) ? s : wd->caret;
 } else if (ch == 129) { /* → */
 int len = gstrlen(wd->txt);
 if (wd->caret < len) {
 int cl, cj;
 gadv((const unsigned char*)wd->txt + wd->caret, &cl, &cj);
 if (cl < 1) cl = 1;
 nc = wd->caret + cl;
 }
 } else if (ch == 132) { /* HOME */
 nc = 0;
 } else if (ch == 133) { /* END */
 nc = gstrlen(wd->txt);
 }
 if (nc >= 0) { /* 选区: shift 扩展 / 否则塌缩 */
 int old = wd->caret;
 if (is_shift && wd->sel_anchor == wd->sel_active) wd->sel_anchor = old;
 wd->caret = nc; wd->sel_active = nc;
 if (!is_shift) wd->sel_anchor = wd->sel_active;
 } else if (ch == 127) { /* DEL: 删光标后的字形 */
 int len = gstrlen(wd->txt);
 if (wd->caret < len) {
 int cl, cj;
 gadv((const unsigned char*)wd->txt + wd->caret, &cl, &cj);
 if (cl < 1) cl = 1;
 for (int i = wd->caret; i < len - cl; i++) wd->txt[i] = wd->txt[i + cl];
 wd->txt[len - cl] = 0;
 }
 } else if (ch == '\b') { /* 退格: 删光标前的整字形 */
 if (wd->caret > 0) {
 int s = gglyph_start(wd->txt, wd->caret);
 if (s < 0) s = 0;
 int len = gstrlen(wd->txt), shift = wd->caret - s;
 for (int i = s; i < len - shift; i++) wd->txt[i] = wd->txt[i + shift];
 wd->txt[len - shift] = 0;
 wd->caret = s;
 }
 } else if (ch >= 0x20 && ch <= 0x7E) { /* 可打印: 光标处插入 */
 int len = gstrlen(wd->txt);
 if (len < 62) {
 for (int i = len; i > wd->caret; i--) wd->txt[i] = wd->txt[i - 1];
 wd->txt[wd->caret] = (char)ch;
 wd->caret++;
 wd->txt[len + 1] = 0;
 }
 }
 gw_redraw(&GUW[win]); gcompose();
 return wd->caret;
}

/* 多行文本区编辑 (): ch 码值同 SYS_GETKEY — '\n'/'\r'(回车插行) '\b'
 * (整字形退格) 127(删光标后字形) 128← 129→ 130↑ 131↓ 132行首 133行尾
 * 139↑页 140↓页 141(INS, v1 忽略) 可打印(光标处插入)。↑↓按记忆列 txcol。 */
int gui_tarea_char(int win, int ctl, int ch) {
 gui_wid_t *wd = gw_get(win, ctl);
 if (!wd || wd->type != GW_TEXTAREA) return -1;
 if (wd->txid < 0) return -1;
 char *buf = gui_txpool[wd->txid];
 int len = wd->txlen, c = wd->txc;

 /* 改动键 (删/换) 且已有选区 → 先删选中, 塌缩 (光标落选区低端) */
 int editing = (ch == 127 || ch == '\b' || ch == '\n' || ch == '\r'
 || (ch >= 0x20 && ch <= 0x7E));
 if (editing && wd->sel_anchor != wd->sel_active)
 c = sel_delete(wd, &len);
 int oldc = c;

 if (ch == 128) { /* ← */
 if (c > 0) {
 int ls = gtx_line_start(buf, len, c);
 int s = gtx_prev(buf, ls, c);
 c = (s >= 0) ? s : (c - 1); /* 行首 → 上一行行尾 */
 }
 } else if (ch == 129) { /* → */
 if (c < len) {
 if (buf[c] == '\n') c++; else { int cl, cj;
 gadv((const unsigned char*)buf + c, &cl, &cj); if (cl < 1) cl = 1; c += cl; }
 }
 } else if (ch == 130) { /* ↑ */
 int ls = gtx_line_start(buf, len, c);
 if (ls > 0) { /* 非首行 */
 int pls = gtx_line_start(buf, len, ls - 1), ple = ls - 1;
 c = gtx_byte_px(buf, pls, ple, wd->txcol);
 }
 } else if (ch == 131) { /* ↓ */
 int le = gtx_line_end(buf, len, c);
 if (le < len) { /* 有下一行 */
 int nls = le + 1, nle = gtx_line_end(buf, len, nls);
 c = gtx_byte_px(buf, nls, nle, wd->txcol);
 }
 } else if (ch == 132) { /* HOME 行首 */
 c = gtx_line_start(buf, len, c);
 } else if (ch == 133) { /* END 行尾 */
 c = gtx_line_end(buf, len, c);
 } else if (ch == 139) { /* ↑页 */
 int vis = (wd->h - 2) / 16; if (vis < 1) vis = 1;
 int r = gtx_row(buf, len, c) - vis; if (r < 0) r = 0;
 int ls = gtx_row_start(buf, len, r);
 c = gtx_byte_px(buf, ls, gtx_line_end(buf, len, ls), wd->txcol);
 } else if (ch == 140) { /* ↓页 */
 int vis = (wd->h - 2) / 16; if (vis < 1) vis = 1;
 int r = gtx_row(buf, len, c) + vis;
 int ls = gtx_row_start(buf, len, r);
 c = gtx_byte_px(buf, ls, gtx_line_end(buf, len, ls), wd->txcol);
 } else if (ch == 127) { /* DEL: 删光标本字形 */
 if (c < len) {
 int d;
 if (buf[c] == '\n') d = 1; else { int cl, cj;
 gadv((const unsigned char*)buf + c, &cl, &cj); if (cl < 1) cl = 1; d = cl; }
 gtx_lshift(buf, c, d, len); len -= d;
 }
 } else if (ch == '\b') { /* 退格: 删光标前整字形 */
 if (c > 0) {
 int d;
 if (buf[c - 1] == '\n') d = 1; /* 行首 → 并入上一行 */
 else { int ls = gtx_line_start(buf, len, c); int s = gtx_prev(buf, ls, c);
 d = (s >= 0) ? (c - s) : 1; }
 gtx_lshift(buf, c - d, d, len); len -= d; c -= d;
 }
 } else if (ch == '\n' || ch == '\r') { /* 回车: 光标处换行 */
 if (len < TX_SIZE) {
 gtx_rshift(buf, c, 1, len, TX_SIZE);
 buf[c] = '\n'; c++; len++; wd->txcol = 0;
 }
 } else if (ch >= 0x20 && ch <= 0x7E) { /* 可打印: 光标处插入 */
 if (len < TX_SIZE) {
 gtx_rshift(buf, c, 1, len, TX_SIZE);
 buf[c] = (char)ch; c++; len++;
 }
 }

 /* 选区: 导航键 shift 扩展选区 / 否则塌缩 (编辑键已由 sel_delete 塌缩) */
 if (ch == 128 || ch == 129 || ch == 130 || ch == 131 || ch == 132 ||
 ch == 133 || ch == 139 || ch == 140) {
 if (is_shift && wd->sel_anchor == wd->sel_active) wd->sel_anchor = oldc;
 wd->sel_active = c;
 if (!is_shift) wd->sel_anchor = c;
 }

 /* 水平移动/键入后更新记忆列 (↑↓ 用 wd->txcol 不动) */
 if (ch == 128 || ch == 129 || ch == 132 || ch == 133 || (ch >= 0x20 && ch <= 0x7E)) {
 int ls = gtx_line_start(buf, len, c);
 wd->txcol = gtx_px(buf, ls, c, gtx_line_end(buf, len, c));
 }

 wd->txc = c; wd->txlen = len;
 gw_redraw(&GUW[win]); gcompose();
 return c;
}

int gui_fill(int win, int x, int y, int w, int h, unsigned short color) {
 if (!gui_active || win < 0 || win >= GW_MAXWIN || !GUW[win].used) return -1;
 gfill(GUW[win].buf, GW_MAXWIN && win >= 0 ? GUW[win].w : 0, GUW[win].h,
 x, y, w, h, color);
 gui_dirty = 1; dirty_win = win; /* 直接改缓冲, 须标记本窗待重blit */
 gcompose();
 return 0;
}

int gui_text(int win, int x, int y, const char *str) {
 if (!gui_active || win < 0 || win >= GW_MAXWIN || !GUW[win].used) return -1;
 gtext(GUW[win].buf, GUW[win].w, GUW[win].h, x, y,
 (const unsigned char*)str, C_TEXT, 0, 0);
 gui_dirty = 1; dirty_win = win;
 gcompose();
 return 0;
}

int gui_dialog(int parent, int w, int h, const char *title) {
 (void)parent;
 if (!gui_active) return -1;
 int sw = fb_vbe_w(), sh = gui_buf_h;
 return gui_win((sw - w) / 2, (sh - h) / 2, w, h, title ? title : "消息");
}

/* 事件轮询: 每批最多 max 个 gui_ev_t {type,win,ctl,ch}。 */
int gui_events(void *buf, int max) {
 if (!buf || max < 1) return 0;
 int *ev = (int*)buf; int n = 0;
 int lb = mouse_lbutton();
 int mx = mouse_px_x(), my = mouse_px_y();
 int fbw = fb_vbe_w(), fbh = gui_buf_h;
 int was = prev_lbutton;
 prev_lbutton = lb;

 /* ── 按住跨 poll 1: 撞标题栏拖窗 → 整屏重合成 (擦移走旧区) ── */
 if (lb && drag_win >= 0 && drag_win < GW_MAXWIN && GUW[drag_win].used) {
 gui_win_t *w = &GUW[drag_win];
 int nx = mx - drag_offx, ny = my - drag_offy;
 if (nx < 0) nx = 0; if (ny < 0) ny = 0;
 if (nx + w->w > fbw) nx = fbw - w->w;
 if (ny + w_draw_h(w) > fbh) ny = fbh - w_draw_h(w);
 if (nx != w->x || ny != w->y) {
 gdx_move(drag_win, nx, ny); /* 拖动快路径: 拷窗口+补暴露区+指针, 无整屏清 */
 }
 goto kbd;
 }
 /* ── 按住跨 poll 2: 鼠标拖选 (选区高亮用单窗 blit, 尺寸不变) ── */
 if (lb && sel_drag_w >= 0 && sel_drag_w < GW_MAXWIN && GUW[sel_drag_w].used) {
 gui_win_t *w = &GUW[sel_drag_w];
 gui_wid_t *g = &w->wd[sel_drag_k];
 if (g->type == GW_EDIT) {
 int relx = mx - (w->x + g->x) - 3; if (relx < 0) relx = 0;
 g->caret = gcaret_from_px(g->txt, relx);
 g->sel_active = g->caret;
 } else if (g->type == GW_TEXTAREA && g->txid >= 0) {
 const char *tb = gui_txpool[g->txid];
 int tlen = g->txlen;
 int linepy = my - (w->y + g->y) - 1; if (linepy < 0) linepy = 0;
 int r = linepy / 16;
 int ls = gtx_row_start(tb, tlen, g->txsc + r);
 int le = gtx_line_end(tb, tlen, ls);
 int pxx = mx - (w->x + g->x) - 2; if (pxx < 0) pxx = 0;
 g->txc = gtx_byte_px(tb, ls, le, pxx);
 g->sel_active = g->txc;
 }
 gw_redraw(w); /* 重画选区高亮+光标 进离屏buf (单选窗blit) */
 gcompose();
 goto kbd;
 }
 /* ── 松开边沿 → 结束拖窗/拖选 (保留选区) ── */
 if (was && !lb) {
 if (drag_win >= 0 && drag_win < GW_MAXWIN && GUW[drag_win].used) {
 gui_win_t *dw = &GUW[drag_win];
 int drag_moved = 0;
 if (dw->state == W_MIN) { /* 最小化窗被拖: 未移 → 还原; 已移 → 保持最小化 */
 if (dw->x == dw->rx && dw->y == dw->ry) {
 dw->x = dw->rx; dw->y = dw->ry;
 dw->w = dw->rw; dw->h = dw->rh;
 /* 还原到之前的状态: 若还原矩形=全屏 → 还原到 W_MAX (修"最小化后无法
 * 最大化"bug: 之前总是 W_NORM, 即便之前是 MAX 也会被强降) */
 int fbw2 = fb_vbe_w(), fbh2 = fb_vbe_h();
 if (dw->rx == 0 && dw->ry == 0 && dw->rw == fbw2 && dw->rh == fbh2)
  dw->state = W_MAX;
 else
  dw->state = W_NORM;
 gw_redraw(dw);
 drag_moved = 1;
 }
 /* 最小化条被移: 不变状态, 整屏合成补暴露区 */
 }
 /* 拖完收尾: 不整屏清 (会闪), 走 gblit_win 按 z 序补画
 * (暴露区 gdx_move 已补; 但弹层/相交窗可能因 gdx_move 拷贝污染, 须再走一次) */
 last_full_tick = 0; /* 强制下次 gcompose 走全量 (避免 30Hz 限流造成 1 帧延迟) */
 gui_dirty = 1; dirty_win = -1;
 }
 drag_win = -1; sel_drag_w = sel_drag_k = -1;
 }

 /* ── 菜单栏 (, ): 弹层跟点击位置, 在所有窗之上 ── */
 if (lb && !was && gui_mb.used && gui_mb.win == active_win
 && gui_mb.win >= 0 && gui_mb.win < GW_MAXWIN
 && GUW[gui_mb.win].used) {
 gui_win_t *mw = &GUW[gui_mb.win];
 if (gui_mb.open >= 0) {
 /* 弹层打开中: 命中弹层 → 激活; 命中菜单条标题 → toggle;
 * 其他 → 关菜单, 让 click 继续走 chrome/控件命中 (raise/激活按钮) */
 int m = gui_mb.open, ni = gui_mb.nitems[m];
 int px = gui_mb.pop_x, py = gui_mb.pop_y;
 int pw = gui_mb.pop_w, ph = gui_mb.pop_h;
 if (mx >= px && mx < px + pw && my >= py && my < py + ph) {
 int it = (my - py - 1) / 16;
 if (it >= 0 && it < ni && gui_mb.items[m][it][0] != '-') {
 int ctl = -1;
 for (int kk = 0; kk < mw->nwid; kk++)
 if (mw->wd[kk].type == GW_MENU) { ctl = kk; break; }
 if (n < max) {
 ev[0] = GEV_CLICK; ev[1] = gui_mb.win; ev[2] = ctl;
 ev[3] = (m << 8) | it; n += 4; ev += 4;
 }
 }
 gui_mb_close(); goto kbd; /* 弹层内点 → 消费 */
 }
 /* 检查是否点菜单条标题 (切换菜单) */
 if (my >= mw->y + 18 && my < mw->y + 18 + 18) {
 int tx = mw->x + 4;
 for (int i = 0; i < gui_mb.nmenu; i++) {
 int tw = gstr_px((const unsigned char*)gui_mb.titles[i]);
 if (mx >= tx && mx < tx + tw + 16) {
 if (gui_mb.open == i) gui_mb_close();
 else gui_mb_open_menu(i);
 goto kbd; /* 标题条点 → 消费 */
 }
 tx += tw + 16;
 }
 }
 /* 弹层外 (其他窗/控件) → 关菜单 + 不消费 (让 chrome/控件逻辑处理) */
 gui_mb_close();
 } else {
 /* 弹层未开: 击菜单条标题 → open/toggle */
 if (my >= mw->y + 18 && my < mw->y + 18 + 18) {
 int tx = mw->x + 4;
 for (int i = 0; i < gui_mb.nmenu; i++) {
 int tw = gstr_px((const unsigned char*)gui_mb.titles[i]);
 if (mx >= tx && mx < tx + tw + 16) {
 if (gui_mb.open == i) gui_mb_close();
 else gui_mb_open_menu(i);
 goto kbd;
 }
 tx += tw + 16;
 }
 }
 }
 }

 /* ── 新按 → 命中窗口/chrome/控件 ── */
 if (lb && !was) {
 int top = -1, topz = -1, need_full = 0;
 /* 弹层在所有窗之上 (直写 LFB): 点击落在打开弹层内 → 强制路由到菜单窗。
 * 修"在当前窗选中后点击会跳到下层窗": 弹层盖住下层窗时, 下层窗几何上
 * 反而是该点最高窗口, 原循环会 raise 它并点它的控件而非激活菜单项。 */
 int pop_hit = (gui_mb.used && gui_mb.open >= 0 && gui_mb.win == active_win
  && mx >= gui_mb.pop_x && mx < gui_mb.pop_x + gui_mb.pop_w
  && my >= gui_mb.pop_y && my < gui_mb.pop_y + gui_mb.pop_h);
 if (pop_hit && gui_mb.win >= 0 && gui_mb.win < GW_MAXWIN && GUW[gui_mb.win].used) {
  top = gui_mb.win; /* 弹层可能伸出窗沿, 不能靠几何命中判定 */
 } else {
  for (int k = 0; k < GW_MAXWIN; k++) {
  gui_win_t *w = &GUW[k];
  if (!w->used) continue;
  /* 可见高度用 w_draw_h: 最小化条只占 18px, 不可见主体不得命中 */
  if (mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + w_draw_h(w))
  if (w->z > topz) { topz = w->z; top = k; }
  }
 }
 if (top >= 0) {
 gui_win_t *w = &GUW[top];
 int in_title = (my >= w->y && my < w->y + 18);
 /* active 管理: 任何位置点击都只作用到活跃窗口 (哪怕下面有其他窗);
 * 例外: 点击到未被遮挡的标题栏 → 切换活跃窗口 */
 if (active_win != top && in_title) {
 /* 点到非活跃窗未遮挡的标题栏 → 切换活跃窗口 */
 int olda = active_win;
 if (olda >= 0 && olda < GW_MAXWIN && GUW[olda].used)
 GUW[olda].active = 0;
 w->active = 1;
 active_win = top;
 if (gui_mb.used && gui_mb.open >= 0 && gui_mb.win != top)
 gui_mb_close(); /* 活跃层级: 弹层属主非新活跃窗 → 收起 */
 w->z = ++gui_zmax; /* raise */
 if (olda >= 0 && olda < GW_MAXWIN && GUW[olda].used && olda != top) {
 gw_redraw(&GUW[olda]); /* 旧活跃窗标题转灰 */
 need_full = 1; /* 两窗标题色都变 → 整屏 */
 last_full_tick = 0;
 gfull_force = 1; /* 切活跃立即刷 (消"卡一帧") */
 }
 } else if (active_win == top) {
 /* 点在活跃窗口 → 正常处理 (raise), 绝不下落到下层窗 */
 w->z = ++gui_zmax;
 } else {
 /* 点在非活跃窗的本体 → 忽略, 不切换不透传 */
 goto kbd;
 }
 if (foc_win != active_win && active_win >= 0 && active_win < GW_MAXWIN
 && GUW[active_win].used) {
 gui_win_t *ow = &GUW[foc_win]; /* 焦点变更 → 塌缩旧窗选区 */
 if (foc_win >= 0 && foc_win < GW_MAXWIN && ow->used) {
 for (int i = 0; i < ow->nwid; i++) {
 gui_wid_t *od = &ow->wd[i];
 if (od->type == GW_EDIT || od->type == GW_TEXTAREA)
 od->sel_anchor = od->sel_active = 0;
 }
 gw_redraw(ow); need_full = 1; /* 旧窗重画(去选区) + 标题转灰 */
 }
 }
 foc_win = active_win; /* 键盘焦点始终跟随活跃窗口 */

 int chrome_x = w->x + w->w - CHROME_N * CHROME_W;

 /* 点最小化条 → 拖动模式 (按住移动 = 移动最小化条; 短按未移 = 还原)
 * 例外: 点 ▢ 图标区 (右侧 16px 内) → 直接最大化 (修"最小化后无法最大化"bug) */
 if (in_title && w->state == W_MIN) {
 if (mx >= w->x + w->w - 16 && mx < w->x + w->w && my >= w->y + 2 && my < w->y + 14) {
  /* 展开按钮 → 恢复原大小, 不是直接最大化 */
  w->x = w->rx; w->y = w->ry; w->w = w->rw; w->h = w->rh;
  /* 检查还原矩形是否为全屏: 若是则恢复为 W_MAX, 否则 W_NORM */
  int fbw = fb_vbe_w(), fbh = fb_vbe_h();
  if (w->rx == 0 && w->ry == 0 && w->rw == fbw && w->rh == fbh)
  w->state = W_MAX;
  else
  w->state = W_NORM;
  /* 大小变化需重新分配缓冲 */
  unsigned short *nb = (unsigned short*)mem_alloc((unsigned)w->w * (unsigned)w->h * 2);
  if (nb) { mem_free(w->buf); w->buf = nb; }
  gw_redraw(w); gui_dirty = 1; dirty_win = -1; gcompose();
  goto kbd;
 }
 drag_win = top; drag_offx = mx - w->x; drag_offy = my - w->y;
 /* 不立即还原: 松开时若未移 → 还原, 已移 → 保持最小化在新位 */
 goto kbd;
 }
 /* chrome 三钮 (仅正常窗口显示) */
 if (in_title && w->state != W_MIN && mx >= chrome_x) {
 int ci = (mx - chrome_x) / CHROME_W;
 int wmax_restore = 0; /* ▢ 还原 (最大化→正常) 置位: 须暴露旧最大化区 */
 if (ci == 0) { /* ▁ 最小化 */
 w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
 w->state = W_MIN;
 gw_redraw(w);
 gcompose_expose(w->x, w->y + 18, w->w, w->h - 18); /* 只擦收起的窗体 */
 goto kbd;
 } else if (ci == 1) { /* ▢ 最大化/还原 */
 if (w->state == W_MAX) {
 w->x = w->rx; w->y = w->ry; w->w = w->rw; w->h = w->rh;
 w->state = W_NORM;
 wmax_restore = 1; /* 几何缩小 → 旧最大化区须暴露重画 */
 } else {
 w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
 w->x = 0; w->y = 0; w->w = fbw; w->h = fbh; w->state = W_MAX;
 unsigned short *nb = (unsigned short*)mem_alloc(
 (unsigned)w->w * (unsigned)w->h * 2);
 if (nb) { mem_free(w->buf); w->buf = nb; }
 }
 gw_redraw(w);
 if (wmax_restore)
 gcompose_expose(0, 0, fbw, fbh); /* 缩小: 旧最大化区暴露 (偶发操作, 可整屏) */
 else
 gcompose();
 goto kbd;
 } else if (ci == 2) { /* ✕ 关闭: 立即关窗 + 通知程序 */
 gui_win_close(top);
 if (n < max) { ev[0] = GEV_CLOSE; ev[1] = top; ev[2] = 0; ev[3] = 0;
 n += 4; ev += 4; }
 gcompose();
 goto kbd;
 }
 }
 /* 标题栏拖 (非 chrome 区) */
 if (in_title && w->state != W_MIN && mx < chrome_x) {
 drag_win = top; drag_offx = mx - w->x; drag_offy = my - w->y;
 gw_redraw(w); if (need_full) dirty_win = -1; gcompose();
 goto kbd;
 }
 /* 菜单栏条 / 打开的下拉面板: 鼠标点击 (窗有 menubar 才进) */
 if (gui_mb.used && gui_mb.win == top) {
 int winw = w->w, wwww = w->h;
 int in_mb = (my >= w->y + 18 && my < w->y + 36);
 /* 打开的下拉面板: y∈[36, 36+ph) 且 x∈[px, px+pw) */
 if (gui_mb.open >= 0) {
 int m = gui_mb.open, ni = gui_mb.nitems[m], pw = 0;
 for (int i = 0; i < ni; i++) {
 int tw = gstr_px((const unsigned char*)gui_mb.items[m][i]);
 if (tw > pw) pw = tw;
 }
 pw += 24; if (pw < 96) pw = 96;
 int ph = ni * 16 + 2, px = 0, py = 18 + 18;
 if (px + pw > winw) px = winw - pw;
 if (my >= w->y + py && my < w->y + py + ph
 && mx >= w->x + px && mx < w->x + px + pw) {
 int it = (my - (w->y + py) - 1) / 16;
 if (it >= 0 && it < ni
 && gui_mb.items[m][it][0] != '-') {
 int ctl = -1;
 for (int kk = 0; kk < w->nwid; kk++)
 if (w->wd[kk].type == GW_MENU) { ctl = kk; break; }
 if (n < max) {
 ev[0] = GEV_CLICK; ev[1] = top; ev[2] = ctl;
 ev[3] = (m << 8) | it; n += 4; ev += 4;
 }
 }
 gui_mb_close();
 gcompose();
 goto kbd;
 }
 /* 点下拉外: 关菜单, 让点击继续处理其他控件 */
 if (!in_mb) gui_mb_close();
 }
 if (in_mb) { /* 标题列 → 打开/切换 */
 int tx = 4, mi = -1;
 for (int i = 0; i < gui_mb.nmenu; i++) {
 int tw = gstr_px((const unsigned char*)gui_mb.titles[i]);
 if (mx - (w->x + tx) >= 0 && mx - (w->x + tx) < tw + 16) {
 mi = i; break;
 }
 tx += tw + 16;
 }
 if (mi >= 0) {
 if (gui_mb.open == mi) gui_mb_close();
 else gui_mb_open_menu(mi);
 gcompose();
 goto kbd;
 }
 /* 点在条内但未命中标题: 关菜单 (避免下拉消失时仍点中) */
 if (gui_mb.open >= 0) gui_mb_close();
 }
 (void)wwww;
 }
 /* body → 控件命中 */
 int ctl = -1, ch = 0;
 for (int i = 0; i < w->nwid; i++) {
 gui_wid_t *g = &w->wd[i];
 if (g->type == GW_MENU) continue; /* 菜单栏在前面已处理 */
 if (mx >= w->x + g->x && mx < w->x + g->x + g->w &&
 my >= w->y + g->y && my < w->y + g->y + g->h) { ctl = i; break; }
 }
 if (ctl >= 0) {
 gui_wid_t *g = &w->wd[ctl];
 if (g->type == GW_EDIT) {
 w->foc_wid = ctl;
 g->caret = gcaret_from_px(g->txt,
 mx - (w->x + g->x) - 3);
 g->sel_anchor = g->sel_active = g->caret; /* 点选清选区 */
 sel_drag_w = top; sel_drag_k = ctl; /* 拖选起点 */
 } else if (g->type == GW_TEXTAREA) {
 w->foc_wid = ctl;
 if (g->txid >= 0) {
 const char *tb = gui_txpool[g->txid];
 int tlen = g->txlen;
 int linepy = my - (w->y + g->y) - 1; if (linepy < 0) linepy = 0;
 int r = linepy / 16;
 int ls = gtx_row_start(tb, tlen, g->txsc + r);
 int le = gtx_line_end(tb, tlen, ls);
 int pxx = mx - (w->x + g->x) - 2; if (pxx < 0) pxx = 0;
 g->txc = gtx_byte_px(tb, ls, le, pxx);
 g->txcol = gtx_px(tb, ls, g->txc, le);
 g->sel_anchor = g->sel_active = g->txc;
 sel_drag_w = top; sel_drag_k = ctl;
 }
 } else if (g->type == GW_LIST) {
 w->foc_wid = -1;
 int visible = (g->h - 2) / 16; if (visible < 1) visible = 1;
 int item = g->scroll + ((my - (w->y + g->y)) - 1) / 16;
 if (item >= g->nitems) item = -1;
 if (item >= 0) { g->sel = item; ch = item; }
 } else if (g->type == GW_CHECK) { /* 复选框: 点击切换 */
 w->foc_wid = ctl; g->chk = !g->chk; ch = g->chk;
 } else if (g->type == GW_RADIO) { /* 单选: 同组互斥 */
 w->foc_wid = ctl; gw_radio_check(w, g); ch = 1;
 } else if (g->type == GW_BTN) { /* 按钮: 点击也设焦点, 便于 TAB 导航 */
 w->foc_wid = ctl;
 } else {
 w->foc_wid = -1;
 }
 gw_redraw(w);
 } else {
 gw_redraw(w); /* 点空白: 仅 raise */
 }
 if (need_full) dirty_win = -1; /* 焦点变更塌扩了旧窗 → 整屏重合成 */
 gcompose();
 if (ctl >= 0 && n < max) {
 ev[0] = GEV_CLICK; ev[1] = top; ev[2] = ctl; ev[3] = ch;
 n += 4; ev += 4;
 }
 }
 }

kbd:
 /* ── 键盘 (): 菜单 → TAB 循环 → 列表方向 → 文本控件 () ── */
 int fk = key_pressed;
 if (fk) {
 int fhk = 0; /* 本轮已处理则跳过下一段 */
 /* 1) 菜单打开中: 全部键给菜单 (ESC/方向/Enter/Alt+字母) */
 if (gui_mb.used && gui_mb.open >= 0) {
 key_pressed = 0; fhk = 1;
 if (fk == 8) gui_mb_close(); /* ESC */
 else if (fk == 4) gui_mb_switch(-1); /* ← */
 else if (fk == 5) gui_mb_switch(1); /* → */
 else if (fk == 6) gui_mb_move(-1); /* ↑ */
 else if (fk == 7) gui_mb_move(1); /* ↓ */
 else if (fk == 2) { /* ENTER: 激活高亮项 */
 int m = gui_mb.open, it = gui_mb.hilite;
 if (m >= 0 && it >= 0 && it < gui_mb.nitems[m]
 && gui_mb.items[m][it][0] != '-') {
 int ctl = -1;
 for (int kk = 0; kk < GUW[gui_mb.win].nwid; kk++)
 if (GUW[gui_mb.win].wd[kk].type == GW_MENU) { ctl = kk; break; }
 if (n < max) {
 ev[0] = GEV_CLICK; ev[1] = gui_mb.win; ev[2] = ctl;
 ev[3] = (m << 8) | it; n += 4; ev += 4;
 }
 }
 gui_mb_close();
 } else if (fk == 1 && is_alt) {
 int lc = current_char | 0x20;
 if (lc >= 'a' && lc <= 'z') {
 for (int i = 0; i < gui_mb.nmenu; i++)
 if (gui_mb.mnem[i] == lc) { gui_mb_open_menu(i); break; }
 }
 }
 }
 /* 2) Alt+字母 无菜单开 → 开对应菜单 */
 else if (fk == 1 && is_alt && gui_mb.used && gui_mb.open < 0) {
 int lc = current_char | 0x20;
 if (lc >= 'a' && lc <= 'z') {
 for (int i = 0; i < gui_mb.nmenu; i++) {
 if (gui_mb.mnem[i] == lc) {
 key_pressed = 0; fhk = 1;
 gui_mb_open_menu(i);
 break;
 }
 }
 }
 }
 /* 3) TAB/Shift-TAB 焦点循环 */
 if (!fhk && fk == 1 && current_char == '\t' && foc_win >= 0
 && foc_win < GW_MAXWIN && GUW[foc_win].used) {
 if (gw_focus_step(&GUW[foc_win], is_shift ? -1 : 1) >= 0) {
 key_pressed = 0; fhk = 1;
 }
 }
 /* 4) 列表焦点 + 方向/Enter */
 if (!fhk && foc_win >= 0 && foc_win < GW_MAXWIN && GUW[foc_win].used) {
 gui_win_t *w = &GUW[foc_win];
 int fw = w->foc_wid;
 if (fw >= 0 && fw < w->nwid && w->wd[fw].type == GW_LIST) {
 gui_wid_t *g = &w->wd[fw];
 if (fk == 6) { /* ↑ */
 if (g->sel > 0) g->sel--;
 if (g->sel < g->scroll) g->scroll = g->sel;
 key_pressed = 0; fhk = 1; gw_redraw(w); gcompose();
 } else if (fk == 7) { /* ↓ */
 if (g->sel + 1 < g->nitems) g->sel++;
 int vis = (g->h - 2) / 16; if (vis < 1) vis = 1;
 if (g->sel >= g->scroll + vis) g->scroll = g->sel - vis + 1;
 key_pressed = 0; fhk = 1; gw_redraw(w); gcompose();
 } else if (fk == 2) { /* ENTER: 确认 */
 if (g->sel >= 0 && g->sel < g->nitems && n < max) {
 ev[0] = GEV_CLICK; ev[1] = foc_win; ev[2] = fw; ev[3] = g->sel;
 n += 4; ev += 4;
 }
 key_pressed = 0; fhk = 1;
 }
 }
 }
 /* 4b) 按钮 / 复选 / 单选: 空格激活+方向键焦点步 () */
 if (!fhk && fk && foc_win >= 0 && foc_win < GW_MAXWIN && GUW[foc_win].used) {
 gui_win_t *w = &GUW[foc_win];
 int fw = w->foc_wid;
 if (fw >= 0 && fw < w->nwid) {
 gui_wid_t *g = &w->wd[fw];
 if (g->type == GW_BTN) {
 if (fk == 1 && current_char == ' ') { /* 空格 = 按下按钮 */
 key_pressed = 0; fhk = 1;
 gw_redraw(w); gcompose();
 if (n < max) {
 ev[0] = GEV_CLICK; ev[1] = foc_win; ev[2] = fw; ev[3] = 0;
 n += 4; ev += 4;
 }
 } else if (fk == 4 || fk == 5 || fk == 6 || fk == 7) {
 key_pressed = 0; fhk = 1;
 gw_focus_step(w, (fk == 4 || fk == 6) ? -1 : 1);
 }
 } else if (g->type == GW_CHECK) {
 if (fk == 1 && current_char == ' ') { /* 空格 = 切换 */
 key_pressed = 0; fhk = 1;
 g->chk = !g->chk;
 gw_redraw(w); gcompose();
 if (n < max) {
 ev[0] = GEV_CLICK; ev[1] = foc_win; ev[2] = fw; ev[3] = g->chk;
 n += 4; ev += 4;
 }
 } else if (fk == 4 || fk == 5 || fk == 6 || fk == 7) {
 key_pressed = 0; fhk = 1;
 gw_focus_step(w, (fk == 4 || fk == 6) ? -1 : 1);
 }
 } else if (g->type == GW_RADIO) {
 if (fk == 1 && current_char == ' ') { /* 空格 = 互斥勾选 */
 key_pressed = 0; fhk = 1;
 gw_radio_check(w, g);
 gw_redraw(w); gcompose();
 if (n < max) {
 ev[0] = GEV_CLICK; ev[1] = foc_win; ev[2] = fw; ev[3] = 1;
 n += 4; ev += 4;
 }
 } else if (fk == 4 || fk == 5 || fk == 6 || fk == 7) {
 key_pressed = 0; fhk = 1;
 gw_focus_step(w, (fk == 4 || fk == 6) ? -1 : 1);
 }
 }
 }
 }
 /* 5) 文本控件字符/方向键 ( 不变) */
 if (!fhk && fk && foc_win >= 0 && foc_win < GW_MAXWIN && GUW[foc_win].used) {
 gui_win_t *w = &GUW[foc_win];
 int fw = w->foc_wid;
 if (fw >= 0 && fw < w->nwid) {
 int ft = w->wd[fw].type;
 if (ft == GW_EDIT || ft == GW_TEXTAREA) {
 key_pressed = 0;
 int ch = 0;
 if (fk == 1) ch = current_char;
 else if (fk == 2) ch = '\n'; /* 回车 → 多行换行 / 单行忽略 */
 else if (fk == 3) ch = '\b';
 else if (fk == 4) ch = 128; /* ← */
 else if (fk == 5) ch = 129; /* → */
 else if (fk == 6) ch = 130; /* ↑ */
 else if (fk == 7) ch = 131; /* ↓ */
 else if (fk == 9) ch = 127; /* DEL */
 else if (fk == 10) ch = 132; /* HOME */
 else if (fk == 11) ch = 133; /* END */
 else if (fk == 18) ch = 139; /* ↑页 */
 else if (fk == 19) ch = 140; /* ↓页 */
 else if (fk == 20) ch = 141; /* INS (v1 忽略) */
 if (ch) {
 if (ft == GW_TEXTAREA) gui_tarea_char(foc_win, fw, ch);
 else gui_edit_char(foc_win, fw, ch);
 if (n < max) {
 ev[0] = (ch == '\n' || ch == '\r') ? GEV_ENTER : GEV_KEY;
 ev[1] = foc_win; ev[2] = fw; ev[3] = ch;
 n += 4; ev += 4;
 }
 }
 }
 }
 }
 }
 /* 光标跟随: 每次轮询重合成, 让指针在移动时看见新位置 (整屏重绘自动擦旧光标) */
 gcompose();
 return n / 4;
}