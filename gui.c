/* gui.c — 内核窗口服务器 (v6.9)
 *
 * 架构: 彻底告别"文本网格 + framebuffer 副作用"的旧渲染路径。GUI 模式下
 *   每个窗口持一块离屏 RGB565 缓冲, 任何变更 → 重画该窗缓冲 → 按 z 序自底
 *   向上把可见窗口 blit 到 VBE 帧缓冲。弹窗关闭 = 移除窗口 + 整屏重合成,
 *   下层内容精确恢复 → 从根本消灭"汉字盖不掉的旧像素/覆盖残留"bug
 *   (旧 EDIT: CJK 16×16 直写 framebuffer 是 0xB8000 网格的副作用, 弹窗
 *    覆盖文本格却擦不掉已写的汉字像素)。
 *
 * 用户态只调本服务器的 syscall (28-43), 从不直接写帧缓冲。
 *
 * 模型: retained-mode 控件 (按钮/标签/输入框/列表), 每窗 ≤ GW_MAXWID 个,
 *   文本存固定槽 (无堆分配失败路径)。z 序用单调 gui_zmax, raise 置新 z。
 * GUI 模式下 fb_render / vga 叠加自愈 / 鼠标叠加 全部停用 (gate 在
 *   fb.c / vga.c), 字体光标鼠标由本服务器在合成收尾自画。
 */
#include "common.h"
#include "latin_font.h"

int gui_active = 0;                 /* 见 common.h extern; fb.c/vga.c 据此停用 */

/* ── 常量 ── */
#define GW_MAXWIN   8
#define GW_MAXWID   16
#define GW_MAXITEMS 24
#define GW_TITLE    24

#define GW_BTN      0
#define GW_LBL      1
#define GW_EDIT     2
#define GW_LIST     3
#define GW_TEXTAREA 4

/* 多行文本区: 内容放独立固定槽池 (每槽 TX_SIZE 字节), 而非塞进 gui_wid_t 的
 * txt[64] — 避免结构体阵列被放大几百 KB。槽由 gw_new 分配, 关闭/替换时释放。 */
#define GW_TXPOOL   4
#define TX_SIZE     2048
static char  gui_txpool[GW_TXPOOL][TX_SIZE];
static int   gui_txused[GW_TXPOOL];

#define GEV_CLICK 1
#define GEV_KEY   2
#define GEV_ENTER 3
#define GEV_CLOSE 4              /* 标题栏 ✕ 关闭: 内核已关窗, 通知程序 (主窗→退) */

/* GUI (内核窗口服务器) 版本 — AMUNOS Classic GUI 0.1 (v6.5.3 携带)
 * 对应 docs/AMUNOS_Classic_GUI_设计与实现规划.md 的 GUI 0.1 里程碑:
 * Window/Button/Label/Edit/Textarea/List/中文 + 多窗口叠放 + 拖动chrome(最小/最大/关) + 文本选中。 */
#define GUI_VERSION      "0.1"
#define GUI_VERSION_FULL "AMUNOS Classic GUI " GUI_VERSION

/* 窗口状态 (v6.11) */
#define W_NORM   0               /* 正常 */
#define W_MIN    1               /* 最小化: 只剩 18px 标题条 */
#define W_MAX    2               /* 最大化: 铺满帧缓冲 */

/* 标题栏右侧 chrome: 3 个 18×18 控制钮 (右对齐) */
#define CHROME_W 18
#define CHROME_N 3

/* RGB565 主题色 */
#define C_DESKTOP  0x8410   /* 桌面底色 (中蓝, 非黑块; 侧边=窗外的桌面区) */
#define C_WINBG    0xF7BE
#define C_TITLEFX  0x0019
#define C_TITLEF   0x7BEF
#define C_TITLEFG  0xFFFF
#define C_BTNBG    0xD69A
#define C_BTNBDR   0x4A49
#define C_EDBG     0xFFFF
#define C_EDBDR    0x4A49
#define C_SELBG    0x0019
#define C_SELFG    0xFFFF
#define C_TEXT     0x0000
#define C_PTR      0xFFFF

typedef struct {
    int  type;
    int  x, y, w, h;
    char txt[64];                    /* 按钮/标签文本 | 输入框内容 */
    int  caret;                      /* 输入框: 字节数 */
    char items[GW_MAXITEMS][32];
    int  nitems, sel, scroll;
    int  txid;                       /* 文本区: 槽序 (gui_txpool), -1 无 */
    int  txlen;                      /* 文本区: 内容字节数 */
    int  txc;                        /* 文本区: 光标字节偏移 */
    int  txcol;                      /* 文本区: 记忆列 (像素) — ↑↓ 保持 */
    int  txsc;                       /* 文本区: 顶部可见行 */
    /* v6.11 文本选择 (EDIT + TAREA 共享): 选区 = [min(anchor,active),
     * max(anchor,active)); 相等即空。锚点在按下/非 shift 移动时固定。 */
    int  sel_anchor;
    int  sel_active;
} gui_wid_t;

typedef struct {
    int  used;
    int  x, y, w, h;
    int  z;
    int  foc_wid;                    /* 该窗聚焦控件 (输入框), -1 无 */
    char title[GW_TITLE];
    unsigned short *buf;             /* 离屏 w*h */
    gui_wid_t wd[GW_MAXWID];
    int  nwid;
    /* v6.11 窗口状态: 最小化/最大化 + 还原矩形 (最小/最大前的 x,y,w,h) */
    int  state;
    int  rx, ry, rw, rh;
} gui_win_t;

static gui_win_t GUW[GW_MAXWIN];
static int gui_zmax = 0;
static int foc_win = -1;             /* 聚焦窗口 (键盘路由) */
static int prev_lbutton = 0;
static int gui_buf_h = 480;          /* 帧缓冲高 */
static int gui_dirty = 1;            /* 有待重合成 (状态变更或指针移动) */
static int dirty_win = -1;           /* >=0: 仅需重blit该窗口 (widget 变更, 不全屏) */
static int last_mx = -1, last_my = -1;  /* 上次合成时的指针位置 */

/* v6.11 拖动/活动选择状态 (按住跨 poll) */
static int drag_win    = -1;         /* 正在被拖的窗, -1=无 */
static int drag_offx   = 0;          /* 按下时 鼠标x - 窗x */
static int drag_offy   = 0;          /* 按下时 鼠标y - 窗y */
static int sel_drag_w  = -1;         /* 鼠标拖选中的窗, -1=无 */
static int sel_drag_k  = -1;         /* 鼠标拖选中的控件 */

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
        *clen = 2; *cjk = 1; return 16;          /* UTF-8 2 字节 */
    }
    if (b0 >= 0xA1 && b0 <= 0xF7 && s[1] >= 0xA1 && s[1] <= 0xFE) {
        *clen = 2; *cjk = 1; return 16;         /* 原始 GB2312 */
    }
    *clen = 1; *cjk = 0; return 8;              /* 其他单字节 */
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
        if (i + cl > caret) break;      /* 残缺字形: 停在其起点 (防御) */
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
static unsigned ggb(const unsigned char *s);   /* 见后: 译 UTF8/GB → GB2312 码 */
static int gtx_line_start(const char *buf, int len, int p) {
    (void)len; int i = p; while (i > 0 && buf[i-1] != '\n') i--; return i;
}
static int gtx_line_end(const char *buf, int len, int p) {
    int i = p; while (i < len && buf[i] != '\n') i++; return i;   /* '\n' 或 len */
}
static int gtx_row(const char *buf, int len, int p) {
    int r = 0, i = 0; while (i < p) { if (buf[i] == '\n') r++; i++; } return r;
}
static int gtx_row_start(const char *buf, int len, int row) {
    int r = 0, i = 0;
    while (r < row && i < len) { if (buf[i] == '\n') r++; i++; }
    return i;                                                   /* 该行首字节 */
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

/* ── v6.11 文本选择 + 窗口 chrome 助手 ── */
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
            } else {                              /* 替换框 □ */
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
            } else {                              /* 替换框 □ */
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
    for (int i = 0; i < CHROME_N; i++)          /* 按钮间竖分隔线 */
        gfill(b, bw, bh, bx + i * CHROME_W, 0, 1, CHROME_W, C_BTNBDR);
    gfill(b, bw, bh, bx + 2, 13, 14, 2, glyph);                 /* ▁ 最小化 */
    gborder(b, bw, bh, bx + 18 + 2, 2, 14, 14, glyph);          /* ▢ 最大化 */
    gx_x(b, bw, bh, bx + 36 + 2, 2, glyph);                     /* ✕ 关闭 */
}

static void gw_draw(gui_win_t *w, gui_wid_t *wd) {
    unsigned short *b = w->buf; int bw = w->w, bh = w->h;
    switch (wd->type) {
    case GW_BTN: {
        gfill(b, bw, bh, wd->x, wd->y, wd->w, wd->h, C_BTNBG);
        gborder(b, bw, bh, wd->x, wd->y, wd->w, wd->h, C_BTNBDR);
        gfill(b, bw, bh, wd->x, wd->y + wd->h - 3, wd->w, 2, C_BTNBDR);
        int tw = gstr_px((const unsigned char*)wd->txt);
        int tx = wd->x + (wd->w - tw) / 2, ty = wd->y + (wd->h - 16) / 2;
        gtext(b, bw, bh, tx, ty, (const unsigned char*)wd->txt, C_TEXT, C_BTNBG, 1);
        (void)bh;
        break;
    }
    case GW_LBL:
        gtext(b, bw, bh, wd->x, wd->y, (const unsigned char*)wd->txt, C_TEXT, 0, 0);
        break;
    case GW_EDIT: {
        gfill(b, bw, bh, wd->x, wd->y, wd->w, wd->h, C_EDBG);
        gborder(b, bw, bh, wd->x, wd->y, wd->w, wd->h, C_EDBDR);
        gtext(b, bw, bh, wd->x + 3, wd->y + 1, (const unsigned char*)wd->txt, C_TEXT, C_EDBG, 1);
        if (wd->sel_anchor != wd->sel_active) {   /* v6.11 选区高亮 (先于光标) */
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
        if (isfoc) {                              /* 块状光标 (在光标字节处, 非恒在串尾) */
            int cx = wd->x + 3 + gcaret_px(wd->txt, wd->caret);
            for (int r = 0; r < 14; r++) gpx(b, bw, bh, cx, wd->y + 1 + r, 0xFC30);
        }
        break;
    }
    case GW_LIST: {
        gfill(b, bw, bh, wd->x, wd->y, wd->w, wd->h, C_EDBG);
        gborder(b, bw, bh, wd->x, wd->y, wd->w, wd->h, C_EDBDR);
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
        gborder(b, bw, bh, wd->x, wd->y, wd->w, wd->h, C_EDBDR);
        int visible = (wd->h - 2) / 16; if (visible < 1) visible = 1;
        int crow = gtx_row(buf, len, wd->txc);
        if (wd->txsc < 0) wd->txsc = 0;
        if (crow < wd->txsc) wd->txsc = crow;               /* 光标上卷 */
        if (crow >= wd->txsc + visible) wd->txsc = crow - visible + 1;
        int ls = gtx_row_start(buf, len, wd->txsc);
        int has_sel = (wd->sel_anchor != wd->sel_active);
        int slo = 0, shi = 0; if (has_sel) sel_range(wd, &slo, &shi);
        for (int r = 0; r < visible; r++) {
            int le = ls;
            while (le < len && buf[le] != '\n') le++;
            int iy = wd->y + 1 + r * 16;
            gtx_line(b, bw, bh, wd->x + 2, iy, buf, ls, le, C_TEXT, C_EDBG, 1);
            if (has_sel) {                          /* v6.11 选区高亮 (逐行交叠) */
                int s0 = slo > ls ? slo : ls;
                int s1 = shi < le ? shi : le;
                if (s0 < s1) {
                    int x0 = wd->x + 2 + gtx_px(buf, ls, s0, le);
                    int x1 = wd->x + 2 + gtx_px(buf, ls, s1, le);
                    gfill(b, bw, bh, x0, iy, x1 - x0, 16, C_SELBG);
                    gtx_line(b, bw, bh, x0, iy, buf, s0, s1, C_SELFG, C_SELBG, 1);
                }
            }
            if (le >= len) break;                           /* 内容最后一行 */
            ls = le + 1;
        }
        int isfoc = (w == &GUW[foc_win >= 0 ? foc_win : 0] && w->foc_wid >= 0
                     && wd == &w->wd[w->foc_wid]);
        if (isfoc && crow >= wd->txsc && crow < wd->txsc + visible) {
            int cls = gtx_line_start(buf, len, wd->txc);
            int clp = gtx_px(buf, cls, wd->txc, gtx_line_end(buf, len, wd->txc));
            int cx = wd->x + 2 + clp, cy = wd->y + 1 + (crow - wd->txsc) * 16;
            for (int r = 0; r < 14; r++) gpx(b, bw, bh, cx, cy + r, 0xFC30);
        }
        break;
    }
    }
}

static void gw_redraw(gui_win_t *w) {
    gui_dirty = 1;
    dirty_win = (int)(w - GUW);   /* 只重blit本窗, 不全屏清桌面 → 交互不闪 */
    gfill(w->buf, w->w, w->h, 0, 0, w->w, w->h, C_WINBG);
    int isfoc = (foc_win == (w - GUW));
    unsigned short band = isfoc ? C_TITLEFX : C_TITLEF;
    gfill(w->buf, w->w, w->h, 0, 0, w->w, 18, band);
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
    if (w->state != W_MIN) {            /* 最小化条只留标题带 (无控制钮/下划线) */
        gdraw_chrome(w->buf, w->w, w->h, w->w, isfoc);
        gfill(w->buf, w->w, w->h, 0, 19, w->w, 1, C_BTNBDR);
    }
    for (int i = 0; i < w->nwid; i++) gw_draw(w, &w->wd[i]);
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
    for (int j = 0; j < 9; j++) {               /* 黑描边 = 原形膨胀 1px */
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
    for (int j = 0; j < 8; j++)                 /* 白填充 (原形) */
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

static inline void ptr_clamp(int *x, int *y) {       /* 与 gui_draw_pointer 同钳制 */
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
    int px = ptr_bg_x, py = ptr_bg_y;                /* 已是钳制后坐标 */
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
static int gfull_force = 0;              /* 拖动结束兜底: 忽略 30Hz 限流整屏一次 */
static int gcompose_full(void) {
    if (!fb_active()) return 1;
    unsigned now = task_ticks();
    if (!gfull_force && now - last_full_tick < 3) return 0;
    gfull_force = 0; last_full_tick = now;
    unsigned fb = fb_vbe_base(); int fbw = fb_vbe_w(), fbh = fb_vbe_h();
    int bpl = fb_vbe_bpl();
    for (int y = 0; y < fbh; y++)
        for (int x = 0; x < fbw; x++) {
            unsigned char *p = (unsigned char *)(fb + (unsigned)y * bpl + (unsigned)x * 2);
            p[0] = (unsigned char)(C_DESKTOP & 0xFF); p[1] = (unsigned char)(C_DESKTOP >> 8);
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
    /* 指针落在本窗上: 重存背景 + 重画 */
    if (mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + w_draw_h(w)) {
        ptr_bg_valid = 0;
        ptr_save_region(mx, my);
        gui_draw_pointer();
    }
    last_mx = mx; last_my = my;
    gui_dirty = 0;
}

/* 把窗口 m 的离屏 buffer 拷到 LFB, 裁剪到 [cx0,cx1)×[cy0,cy1) 及 m 本体矩形
 * (v6.12, 暴露区补窗用)。 */
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

/* 拖动快路径 (v6.12): 只把被拖窗 bitmap 移到新位置 + 重画暴露区(下层窗/桌面), 绝
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
            { ux0,      uy0,       nx - 1,     uy1 - 1 },  /* 左 */
            { nx + ow,  uy0,       ux1 - 1,    uy1 - 1 },  /* 右 */
            { nx,       uy0,       nx + ow - 1, ny - 1 },  /* 上 */
            { nx,       ny + oh,   nx + ow - 1, uy1 - 1 }, /* 下 */
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
        if (gcompose_full()) return;                        /* 整屏 (新建/关闭/raise) */
    }
    if (mx == last_mx && my == last_my) return;             /* 真无变化 */
    if (!mouse_installed_k()) { last_mx = mx; last_my = my; return; }
    ptr_restore_region();                                   /* 擦旧指针 */
    ptr_save_region(mx, my);                                /* 存新背景 */
    gui_draw_pointer();                                     /* 画新指针 */
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
    if (type == GW_TEXTAREA) {                /* 分配多行内容槽 */
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
    serial_puts(GUI_VERSION_FULL " ready\n");   /* v6.5.3: 启动时公告 GUI 版本 */
    gui_active = 1;
    gui_buf_h = fb_vbe_h(); if (gui_buf_h <= 0) gui_buf_h = 480;
    gui_zmax = 0; foc_win = -1; gui_dirty = 1; dirty_win = -1;
    drag_win = -1; sel_drag_w = -1;
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
        gcopy(wd->title, title ? title : "", GW_TITLE);
        wd->buf = (unsigned short *)mem_alloc((unsigned)w * (unsigned)h * 2);
        if (!wd->buf) { wd->used = 0; gui_zmax--; return -1; }
        gw_redraw(wd);
        gcompose();
        return k;
    }
    return -1;
}

int gui_win_close(int id) {
    if (!gui_active || id < 0 || id >= GW_MAXWIN || !GUW[id].used) return -1;
    if (GUW[id].buf) mem_free(GUW[id].buf);
    GUW[id].used = 0; gui_dirty = 1; dirty_win = -1;   /* 关窗须整屏恢复下层 */
    if (foc_win == id) foc_win = -1;
    gcompose();
    return 0;
}

int gui_win_raise(int id) {
    if (!gui_active || id < 0 || id >= GW_MAXWIN || !GUW[id].used) return -1;
    int oldfoc = foc_win;
    GUW[id].z = ++gui_zmax;
    if (GUW[id].foc_wid >= 0) foc_win = id;
    gw_redraw(&GUW[id]);
    if (oldfoc >= 0 && oldfoc != id && GUW[oldfoc].used) {
        gw_redraw(&GUW[oldfoc]);        /* 旧焦点窗标题恢复未聚焦色 */
        dirty_win = -1;                 /* 两窗都变 → 整屏 */
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

/* 多行文本区 (v6.10): 内容存独立槽池 (TX_SIZE), 光标字节偏移 + ↑↓←→ 全编辑 */
int gui_tarea(int win, int x, int y, int w, int h) {
    if (!gui_active || win < 0 || win >= GW_MAXWIN || !GUW[win].used) return -1;
    if (w < 40) w = 40; if (w > 620) w = 620;
    if (h < 34) h = 34; if (h > 460) h = 460;
    int id = gw_new(&GUW[win], GW_TEXTAREA, x, y, w, h);
    if (id >= 0) GUW[win].foc_wid = id;     /* 文本区即聚焦控件 */
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

/* 内容读回 (v6.10): 把文本区内容拷入 user buf (含 NUL), 返回字节数 */
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

int gui_wnd_text(int win, int ctl, const char *str) {
    gui_wid_t *wd = gw_get(win, ctl);
    if (!wd || wd->type == GW_LIST) return -1;
    gcopy(wd->txt, str ? str : "", 64);
    if (wd->type == GW_EDIT) {
        wd->caret = gstrlen(wd->txt);
        wd->sel_anchor = wd->sel_active = wd->caret;   /* 设文本 → 清选区 */
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
        wd->caret = sel_delete(wd, 0);      /* EDIT: len_io=NULL */

    int nc = -1;                            /* 导航键的新光标; -1=非导航 */
    if (ch == 128) {                        /* ← */
        int s = gglyph_start(wd->txt, wd->caret);
        nc = (s >= 0) ? s : wd->caret;
    } else if (ch == 129) {                 /* → */
        int len = gstrlen(wd->txt);
        if (wd->caret < len) {
            int cl, cj;
            gadv((const unsigned char*)wd->txt + wd->caret, &cl, &cj);
            if (cl < 1) cl = 1;
            nc = wd->caret + cl;
        }
    } else if (ch == 132) {                 /* HOME */
        nc = 0;
    } else if (ch == 133) {                 /* END */
        nc = gstrlen(wd->txt);
    }
    if (nc >= 0) {                          /* v6.11 选区: shift 扩展 / 否则塌缩 */
        int old = wd->caret;
        if (is_shift && wd->sel_anchor == wd->sel_active) wd->sel_anchor = old;
        wd->caret = nc; wd->sel_active = nc;
        if (!is_shift) wd->sel_anchor = wd->sel_active;
    } else if (ch == 127) {                 /* DEL: 删光标后的字形 */
        int len = gstrlen(wd->txt);
        if (wd->caret < len) {
            int cl, cj;
            gadv((const unsigned char*)wd->txt + wd->caret, &cl, &cj);
            if (cl < 1) cl = 1;
            for (int i = wd->caret; i < len - cl; i++) wd->txt[i] = wd->txt[i + cl];
            wd->txt[len - cl] = 0;
        }
    } else if (ch == '\b') {                /* 退格: 删光标前的整字形 */
        if (wd->caret > 0) {
            int s = gglyph_start(wd->txt, wd->caret);
            if (s < 0) s = 0;
            int len = gstrlen(wd->txt), shift = wd->caret - s;
            for (int i = s; i < len - shift; i++) wd->txt[i] = wd->txt[i + shift];
            wd->txt[len - shift] = 0;
            wd->caret = s;
        }
    } else if (ch >= 0x20 && ch <= 0x7E) {  /* 可打印: 光标处插入 */
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

/* 多行文本区编辑 (v6.10): ch 码值同 SYS_GETKEY — '\n'/'\r'(回车插行) '\b'
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

    if (ch == 128) {                                /* ← */
        if (c > 0) {
            int ls = gtx_line_start(buf, len, c);
            int s = gtx_prev(buf, ls, c);
            c = (s >= 0) ? s : (c - 1);             /* 行首 → 上一行行尾 */
        }
    } else if (ch == 129) {                         /* → */
        if (c < len) {
            if (buf[c] == '\n') c++; else { int cl, cj;
                gadv((const unsigned char*)buf + c, &cl, &cj); if (cl < 1) cl = 1; c += cl; }
        }
    } else if (ch == 130) {                         /* ↑ */
        int ls = gtx_line_start(buf, len, c);
        if (ls > 0) {                               /* 非首行 */
            int pls = gtx_line_start(buf, len, ls - 1), ple = ls - 1;
            c = gtx_byte_px(buf, pls, ple, wd->txcol);
        }
    } else if (ch == 131) {                         /* ↓ */
        int le = gtx_line_end(buf, len, c);
        if (le < len) {                             /* 有下一行 */
            int nls = le + 1, nle = gtx_line_end(buf, len, nls);
            c = gtx_byte_px(buf, nls, nle, wd->txcol);
        }
    } else if (ch == 132) {                         /* HOME 行首 */
        c = gtx_line_start(buf, len, c);
    } else if (ch == 133) {                         /* END 行尾 */
        c = gtx_line_end(buf, len, c);
    } else if (ch == 139) {                         /* ↑页 */
        int vis = (wd->h - 2) / 16; if (vis < 1) vis = 1;
        int r = gtx_row(buf, len, c) - vis; if (r < 0) r = 0;
        int ls = gtx_row_start(buf, len, r);
        c = gtx_byte_px(buf, ls, gtx_line_end(buf, len, ls), wd->txcol);
    } else if (ch == 140) {                         /* ↓页 */
        int vis = (wd->h - 2) / 16; if (vis < 1) vis = 1;
        int r = gtx_row(buf, len, c) + vis;
        int ls = gtx_row_start(buf, len, r);
        c = gtx_byte_px(buf, ls, gtx_line_end(buf, len, ls), wd->txcol);
    } else if (ch == 127) {                         /* DEL: 删光标本字形 */
        if (c < len) {
            int d;
            if (buf[c] == '\n') d = 1; else { int cl, cj;
                gadv((const unsigned char*)buf + c, &cl, &cj); if (cl < 1) cl = 1; d = cl; }
            gtx_lshift(buf, c, d, len); len -= d;
        }
    } else if (ch == '\b') {                        /* 退格: 删光标前整字形 */
        if (c > 0) {
            int d;
            if (buf[c - 1] == '\n') d = 1;          /* 行首 → 并入上一行 */
            else { int ls = gtx_line_start(buf, len, c); int s = gtx_prev(buf, ls, c);
                   d = (s >= 0) ? (c - s) : 1; }
            gtx_lshift(buf, c - d, d, len); len -= d; c -= d;
        }
    } else if (ch == '\n' || ch == '\r') {          /* 回车: 光标处换行 */
        if (len < TX_SIZE) {
            gtx_rshift(buf, c, 1, len, TX_SIZE);
            buf[c] = '\n'; c++; len++; wd->txcol = 0;
        }
    } else if (ch >= 0x20 && ch <= 0x7E) {          /* 可打印: 光标处插入 */
        if (len < TX_SIZE) {
            gtx_rshift(buf, c, 1, len, TX_SIZE);
            buf[c] = (char)ch; c++; len++;
        }
    }

    /* v6.11 选区: 导航键 shift 扩展选区 / 否则塌缩 (编辑键已由 sel_delete 塌缩) */
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
    gui_dirty = 1; dirty_win = win;      /* 直接改缓冲, 须标记本窗待重blit */
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
            gdx_move(drag_win, nx, ny);   /* v6.12 拖动快路径: 拷窗口+补暴露区+指针, 无整屏清 */
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
        gw_redraw(w);                   /* 重画选区高亮+光标 进离屏buf (单选窗blit) */
        gcompose();
        goto kbd;
    }
    /* ── 松开边沿 → 结束拖窗/拖选 (保留选区) ── */
    if (was && !lb) {
        if (drag_win >= 0) { gui_dirty = 1; dirty_win = -1; gfull_force = 1; } /* 拖完收尾兜底 */
        drag_win = -1; sel_drag_w = sel_drag_k = -1;
    }

    /* ── 新按 → 命中窗口/chrome/控件 ── */
    if (lb && !was) {
        int top = -1, topz = -1, need_full = 0;
        for (int k = 0; k < GW_MAXWIN; k++) {
            gui_win_t *w = &GUW[k];
            if (!w->used) continue;
            /* 可见高度用 w_draw_h: 最小化条只占 18px, 不可见主体不得命中 */
            if (mx >= w->x && mx < w->x + w->w && my >= w->y && my < w->y + w_draw_h(w))
                if (w->z > topz) { topz = w->z; top = k; }
        }
        if (top >= 0) {
            gui_win_t *w = &GUW[top];
            w->z = ++gui_zmax;              /* raise */
            if (foc_win != top && foc_win >= 0 && GUW[foc_win].used) {
                gui_win_t *ow = &GUW[foc_win];      /* 焦点变更 → 塌缩旧窗选区 */
                for (int i = 0; i < ow->nwid; i++) {
                    gui_wid_t *od = &ow->wd[i];
                    if (od->type == GW_EDIT || od->type == GW_TEXTAREA)
                        od->sel_anchor = od->sel_active = 0;
                }
                gw_redraw(ow); need_full = 1;   /* 旧窗重画(去选区) + 强制整屏 */
            }
            foc_win = top;

            int in_title = (my >= w->y && my < w->y + 18);
            int chrome_x = w->x + w->w - CHROME_N * CHROME_W;

            /* 点最小化条任意处 → 还原 */
            if (in_title && w->state == W_MIN) {
                w->x = w->rx; w->y = w->ry; w->w = w->rw; w->h = w->rh;
                w->state = W_NORM;
                gw_redraw(w); gui_dirty = 1; dirty_win = -1; gcompose();
                goto kbd;
            }
            /* chrome 三钮 (仅正常窗口显示) */
            if (in_title && w->state != W_MIN && mx >= chrome_x) {
                int ci = (mx - chrome_x) / CHROME_W;
                if (ci == 0) {              /* ▁ 最小化 */
                    w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
                    w->state = W_MIN;
                    gw_redraw(w); gui_dirty = 1; dirty_win = -1; gcompose();
                    goto kbd;
                } else if (ci == 1) {       /* ▢ 最大化/还原 */
                    if (w->state == W_MAX) {
                        w->x = w->rx; w->y = w->ry; w->w = w->rw; w->h = w->rh;
                        w->state = W_NORM;
                    } else {
                        w->rx = w->x; w->ry = w->y; w->rw = w->w; w->rh = w->h;
                        w->x = 0; w->y = 0; w->w = fbw; w->h = fbh; w->state = W_MAX;
                        unsigned short *nb = (unsigned short*)mem_alloc(
                            (unsigned)w->w * (unsigned)w->h * 2);
                        if (nb) { mem_free(w->buf); w->buf = nb; }
                    }
                    gw_redraw(w); gui_dirty = 1; dirty_win = -1; gcompose();
                    goto kbd;
                } else if (ci == 2) {       /* ✕ 关闭: 立即关窗 + 通知程序 */
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
            /* body → 控件命中 */
            int ctl = -1, ch = 0;
            for (int i = 0; i < w->nwid; i++) {
                gui_wid_t *g = &w->wd[i];
                if (mx >= w->x + g->x && mx < w->x + g->x + g->w &&
                    my >= w->y + g->y && my < w->y + g->y + g->h) { ctl = i; break; }
            }
            if (ctl >= 0) {
                gui_wid_t *g = &w->wd[ctl];
                if (g->type == GW_EDIT) {
                    w->foc_wid = ctl;
                    g->caret = gcaret_from_px(g->txt,
                               mx - (w->x + g->x) - 3);
                    g->sel_anchor = g->sel_active = g->caret;   /* 点选清选区 */
                    sel_drag_w = top; sel_drag_k = ctl;         /* 拖选起点 */
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
                } else {
                    w->foc_wid = -1;
                }
                gw_redraw(w);
            } else {
                gw_redraw(w);               /* 点空白: 仅 raise */
            }
            if (need_full) dirty_win = -1;  /* 焦点变更塌扩了旧窗 → 整屏重合成 */
            gcompose();
            if (ctl >= 0 && n < max) {
                ev[0] = GEV_CLICK; ev[1] = top; ev[2] = ctl; ev[3] = ch;
                n += 4; ev += 4;
            }
        }
    }

kbd:
    /* ── 键盘 → 聚焦输入框 / 多行文本区 ── */
    int fk = key_pressed;
    if (fk && foc_win >= 0 && foc_win < GW_MAXWIN && GUW[foc_win].used) {
        gui_win_t *w = &GUW[foc_win];
        int fw = w->foc_wid;
        if (fw >= 0 && fw < w->nwid) {
            int ft = w->wd[fw].type;
            if (ft == GW_EDIT || ft == GW_TEXTAREA) {
                key_pressed = 0;
                int ch = 0;
                if (fk == 1) ch = current_char;
                else if (fk == 2) ch = '\n';  /* 回车 → 多行换行 / 单行忽略 */
                else if (fk == 3) ch = '\b';
                else if (fk == 4) ch = 128;  /* ← */
                else if (fk == 5) ch = 129;  /* → */
                else if (fk == 6) ch = 130;  /* ↑ */
                else if (fk == 7) ch = 131;  /* ↓ */
                else if (fk == 9) ch = 127;  /* DEL */
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
    /* 光标跟随: 每次轮询重合成, 让指针在移动时看见新位置 (整屏重绘自动擦旧光标) */
    gcompose();
    return n / 4;
}