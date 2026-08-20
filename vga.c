/* vga.c - VGA Text Mode Display Driver
 * Adapted from flash-4th-os/debug/dprintk.c */
#include "common.h"

#define VGA_BASE    0xB8000
#define VGA_COLS    80
#define VGA_ROWS    25
#define VGA_BYTES   (VGA_COLS * VGA_ROWS * 2)

/* vram: 逻辑文本缓冲 (v6.8)。文本模式默认指向硬件 0xB8000;
 * VBE 图形模式下 0xB8000 是显卡图形内存窗口 (非文本缓冲, 读写会进图形平面),
 * 故 fb_init 切到软件缓冲 vga_softbuf, 由 fb.c 渲染器画到帧缓冲。 */
static char *vram = (char *)VGA_BASE;
static char vga_softbuf[VGA_COLS * VGA_ROWS * 2];

/* 切到软件文本缓冲 (图形模式); fb_init 调用 */
void vga_enable_softbuf(void) { vram = vga_softbuf; }
/* 渲染器读取的文本缓冲 (图形模式=软件缓冲) */
const unsigned char *vga_textbuf(void) { return (const unsigned char *)vram; }
/* 当前文本缓冲基址 — SYS_VIDEO_BASE 返回给用户程序 (EDIT), 使其图形模式下
 * 也写 softbuf 而非物理 0xB8000 (VBE 图形模式下 0xB8000 是图形窗口)。 */
unsigned long vga_vram_base(void) { return (unsigned long)vram; }

/* ── 汉字格映射 (v6.8 中文): 与 softbuf 并行, 标记每格是 ASCII 还是汉字。
 *   单字节格 (80×25) 放不下 GB2312 双字节汉字 → 用这张表告诉渲染器:
 *     0         = ASCII 格 (fb_render 画 8×16 拉丁字形)
 *     GB码       = 汉字左格 (fb_render 画 16×16 HZK16 字形, 跨 (x,y)+(x+1,y))
 *     0xFFFF     = 汉字右格 (fb_render 跳过, 左格已覆盖两格宽)
 *   汉字经 put_cjk_str 写入: softbuf 两格放占位字符 (0xDB), 渲染器按此表画汉字。 */
static unsigned short cjk_cell[VGA_COLS * VGA_ROWS];

/* 在 (x,y) 放一个汉字 (占两格); gb = (gbH<<8)|gbL (0xA1A1..0xF7FE) */
void vga_cjk_set(int x, int y, unsigned gb) {
    if (x < 0 || x + 1 >= VGA_COLS || y < 0 || y >= VGA_ROWS) return;
    int o = y * VGA_COLS + x;
    cjk_cell[o] = (unsigned short)gb;
    cjk_cell[o + 1] = 0xFFFF;
}
/* 在 (x,y) 放一个替换框 □ (Unicode 不在 GB2312 字库时); 左格记 0xFFFE 哨兵 */
void vga_cjk_box(int x, int y) {
    if (x < 0 || x + 1 >= VGA_COLS || y < 0 || y >= VGA_ROWS) return;
    int o = y * VGA_COLS + x;
    cjk_cell[o] = 0xFFFE;
    cjk_cell[o + 1] = 0xFFFF;
}
/* 写 ASCII 到 (x,y) 前调用: 清掉该格及其关联的汉字标记。
 * 若 (x,y) 是汉字右格 (0xFFFF) → 其左格 (存 GB 码) 一并清;
 * 若 (x,y) 是汉字左格 (GB 码) → 其右格 (续) 一并清。 */
void vga_cjk_ascii(int x, int y) {
    if (x < 0 || x >= VGA_COLS || y < 0 || y >= VGA_ROWS) return;
    int o = y * VGA_COLS + x;
    if (cjk_cell[o] == 0xFFFF) { cjk_cell[o] = 0; if (x > 0) cjk_cell[o - 1] = 0; }
    else if (cjk_cell[o] != 0)  { cjk_cell[o] = 0; if (x + 1 < VGA_COLS) cjk_cell[o + 1] = 0; }
}
/* 渲染器查询: (x,y) 格的汉字标记 (0=ASCII, 0xFFFF=汉字右格, 否则 GB 码) */
unsigned short vga_cjk_at(int x, int y) {
    if (x < 0 || x >= VGA_COLS || y < 0 || y >= VGA_ROWS) return 0;
    return cjk_cell[y * VGA_COLS + x];
}
/* 用户程序启动前清空 (其将重绘整屏; 不清则 shell 残留的汉字标记会
 * 在 EDIT 清屏后仍渲染出鬼影汉字)。 */
void vga_cjk_clear_all(void) {
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) cjk_cell[i] = 0;
}
/* v6.8.1 (SYS_CJKWCHAR): 在绝对格 (x,y) 放一个汉字 (占两格). EDIT 等用户程序
 * 绕开 put_cjk_str 的光标式写入, 于任意文本位置置汉字做本地化显示。
 * gb = GB2312 码 (含 0x05→0xE5 已还原); gb==0 → 替换框 □.
 * fg/bg 为 VGA 属性 (0-7); 越界静默忽略. 先清邻格避免残留半个汉字标记。 */
void vga_cjk_place_gb(int x, int y, unsigned gb, int fg, int bg) {
    if (x < 0 || x + 1 >= VGA_COLS || y < 0 || y >= VGA_ROWS) return;
    vga_cjk_ascii(x, y);
    vga_cjk_ascii(x + 1, y);
    int o = (y * VGA_COLS + x) * 2;
    char attr = (char)((bg << 4) | (fg & 0x0F));
    vram[o] = 0xDB; vram[o + 1] = attr;
    vram[o + 2] = 0xDB; vram[o + 3] = attr;
    if (gb) vga_cjk_set(x, y, gb); else vga_cjk_box(x, y);
}

// 光标坐标来自 kernel.c 的全局变量
extern int cur_x, cur_y;

/* ── 软件叠加层 (v6.7): 输入光标 | 与 鼠标指针 █ ──
 * 硬件文本光标只能横排块/下划线, 画不出 |, 也画不出跟随鼠标的指针。
 * 改为两个"画进 0xB8000 的字符叠加":
 *   - 输入光标: '|' (0x7C) — 替代硬件光标, 全局 (提示符/命令行/EDIT)。
 *   - 鼠标指针: '█' (0xDB) — 跟随 PS/2 鼠标, 画在字符格位置。
 * 每个叠加画下时保存该格原字符/属性, 移走时还原。还原前先核对该格仍
 * 显示我们画的叠加字符 (verify-before-restore) — 若已被新文本覆盖
 * (EDIT 直写 0xB8000、滚动、重绘), 说明新内容已是真文本, 直接丢弃旧
 * 影子, 避免陈旧还原盖掉新字符。这样叠加在任意直写者面前都自愈, 无需
 * 把所有写屏点都改成经 put_char。 */
#define IC_CH    '_'             /* 全局下划线输入光标 (用户要求) */
#define IC_ATTR  0x0F            /* 亮白 */
#define MC_CH    0xDB            /* █ 全块 */
#define MC_ATTR  0x0F            /* 黑底亮白 — 鼠标指针白色, 与全黑/亮白前景都对比 */

static int ic_x = -1, ic_y = -1;        /* 输入光标绘制位置; -1=未绘制 */
static unsigned char ic_sc, ic_sa;      /* 光标下原字符/属性 */
static int ic_px = 0, ic_py = 0;        /* 光标"应处"位置 (hide 后 show 用) */
static int ic_hidden = 0;               /* EDIT hidecursor 状态 */
static int mc_x = -1, mc_y = -1;        /* 鼠标指针绘制位置 */
static unsigned char mc_sc, mc_sa;
static int mc_hidden = 0;               /* 用户程序隐藏鼠标 (getvideo 捕获期间) */

static void cell_get(int x, int y, unsigned char *c, unsigned char *a) {
    int o = (y * VGA_COLS + x) * 2;
    *c = vram[o]; *a = vram[o + 1];
}
static void cell_put(int x, int y, unsigned char c, unsigned char a) {
    int o = (y * VGA_COLS + x) * 2;
    vram[o] = c; vram[o + 1] = a;
}
static int cell_shows(int x, int y, unsigned char c, unsigned char a) {
    unsigned char c2, a2;
    cell_get(x, y, &c2, &a2);
    return c2 == c && a2 == a;
}

/* 还原输入光标格 (仅当格上仍是我们的 | 才写回; 否则已被新文本覆盖, 跳过) */
static void ic_clear(void) {
    if (ic_x >= 0) {
        if (cell_shows(ic_x, ic_y, IC_CH, IC_ATTR))
            cell_put(ic_x, ic_y, ic_sc, ic_sa);
        ic_x = -1;
    }
}
/* 在 (x,y) 画输入光标 (鼠标在上则不盖; 已在此处且完好则不动) */
static void ic_draw_at(int x, int y) {
    if (x < 0 || x >= VGA_COLS || y < 0 || y >= VGA_ROWS) { ic_clear(); return; }
    if (mc_x == x && mc_y == y) return;                       /* 鼠标优先 */
    if (ic_x == x && ic_y == y && cell_shows(x, y, IC_CH, IC_ATTR)) return;
    ic_clear();
    cell_get(x, y, &ic_sc, &ic_sa); cell_put(x, y, IC_CH, IC_ATTR);
    ic_x = x; ic_y = y;
}
/* 还原鼠标指针格 */
static void mc_clear(void) {
    if (mc_x >= 0) {
        if (cell_shows(mc_x, mc_y, MC_CH, MC_ATTR))
            cell_put(mc_x, mc_y, mc_sc, mc_sa);
        mc_x = -1;
    }
}
/* 在 (x,y) 画鼠标指针 (输入光标在上则先清掉它 — 鼠标优先, 指针不丢) */
static void mc_draw_at(int x, int y) {
    if (x < 0 || x >= VGA_COLS || y < 0 || y >= VGA_ROWS) { mc_clear(); return; }
    if (mc_x == x && mc_y == y && cell_shows(x, y, MC_CH, MC_ATTR)) return;
    if (ic_x == x && ic_y == y) ic_clear();
    mc_clear();
    cell_get(x, y, &mc_sc, &mc_sa); cell_put(x, y, MC_CH, MC_ATTR);
    mc_x = x; mc_y = y;
}

/* 重新铺两个叠加: 先还原旧的, 再画鼠标 █ 与输入光标 |。
 * shell 每次 update_cursor 都调用; 鼠标优先 (重叠时 | 让位, 指针不丢)。
 * 归属切换: shell 拥有光标 → ic_px/ic_py 追到 cur_x/cur_y, 并强制显示。 */
void vga_overlay_refresh(void) {
    ic_hidden = 0;
    ic_px = cur_x; ic_py = cur_y;
    vga_overlay_selfheal();
}

/* 定时器自愈 (v6.7): 每 tick 重铺两个叠加 (尊重 ic_hidden, 不强制显示)。
 * 任何直写者 (EDIT 的 DFLAT 重绘、滚动) 抹掉 █/| 后 10ms 内恢复, 不依赖
 * 程序是否轮询鼠标。输入光标画在 ic_px/ic_py (shell 经 update_cursor 设,
 * EDIT 经 sys_cur 设)。 */
void vga_overlay_selfheal(void) {
    if (gui_active) return;          /* GUI 窗口服务器自绘图层, 文本叠加停用 */
    ic_clear();
    mc_clear();
    if (!mc_hidden && mouse_installed_k()) mc_draw_at(mouse_char_x(), mouse_char_y());
    if (!ic_hidden) ic_draw_at(ic_px, ic_py);
}

/* 只刷新鼠标叠加 (鼠标 IRQ 与 SYS_MOUSE 轮询共用)。
 * 必须原子 (cli): 位置读取 + 清除旧格 + 画新格 一气呵成, 否则 SYS_MOUSE
 * 轮询与鼠标 IRQ 会交错 — 轮询读到旧位置、IRQ 已画新位置, 轮询再把旧位置
 * 画回去, 而旧格的影子已被新画覆盖 → 出现无人清除的残留 █ (v6.7 修复)。 */
void vga_mouse_redraw(void) {
    if (gui_active) return;          /* GUI 模式: 自绘鼠标指针, 不进 softbuf */
    unsigned int flags;
    __asm__ volatile("pushfl; popl %0" : "=r"(flags));
    __asm__ volatile("cli");
    int x = -1, y = -1;
    if (!mc_hidden && mouse_installed_k()) { x = mouse_char_x(); y = mouse_char_y(); }
    mc_clear();
    if (x >= 0) mc_draw_at(x, y);
    __asm__ volatile("pushl %0; popfl" :: "r"(flags));
}

/* 定位输入光标 (用户程序/EDIT 用; 不动 cur_x/cur_y — 那是 shell 的) */
void soft_cursor_at(int x, int y) {
    ic_px = x; ic_py = y;
    if (ic_hidden) return;
    ic_draw_at(x, y);
}
void soft_cursor_hide(void) { ic_hidden = 1; ic_clear(); }
void soft_cursor_show(void) { ic_hidden = 0; ic_draw_at(ic_px, ic_py); }

/* 隐藏/恢复鼠标指针叠加 (DFLAT getvideo/storevideo 捕获背景期间用,
 * 否则定时器自愈把 █ 画进正在捕获的区域, 烤进背景缓冲 -> 残留) */
void soft_mouse_hide(void) { mc_hidden = 1; mc_clear(); }
void soft_mouse_show(void) { mc_hidden = 0; mc_draw_at(mouse_char_x(), mouse_char_y()); }

/* 直写一个 VGA 单元 (绕过 put_char 流式推进; redraw() 尾部清格、
 * demo_clock 等用), 自动清掉该格上的叠加, 避免陈旧还原。 */
void vga_poke(int x, int y, unsigned char ch, unsigned char attr) {
    if (x < 0 || x >= VGA_COLS || y < 0 || y >= VGA_ROWS) return;
    if (ic_x == x && ic_y == y) ic_clear();
    if (mc_x == x && mc_y == y) mc_clear();
    vga_cjk_ascii(x, y);       /* 直写 ASCII 前清汉字标记 (demo_clock/redraw 清格) */
    cell_put(x, y, ch, attr);
}

/* 更新光标 — 软件 | 替代硬件文本光标后, 只剩"隐藏硬件光标 + 重绘叠加"。
 * 程序 (如 EDIT) 用 0x3D4/0x0A bit5 藏过光标后, 回 shell 必须恢复可见。 */
void update_cursor() {
    io_out8(0x3D4, 0x0A);            /* 隐藏硬件文本光标 (bit5=1) */
    io_out8(0x3D5, 0x20 | 0x0E);
    io_out8(0x3D4, 0x0B);
    io_out8(0x3D5, 0x0F);
    vga_overlay_refresh();
}

/* 滚屏：整屏上移一行 (softbuf 与 cjk_cell 同步滚动) */
static void scroll_up() {
    // 将第 1 行到第 24 行复制到第 0 行到第 23 行
    for (int row = 0; row < VGA_ROWS - 1; row++) {
        char *dst = vram + row * VGA_COLS * 2;
        char *src = vram + (row + 1) * VGA_COLS * 2;
        for (int col = 0; col < VGA_COLS * 2; col++) {
            dst[col] = src[col];
        }
        for (int col = 0; col < VGA_COLS; col++) {
            cjk_cell[row * VGA_COLS + col] = cjk_cell[(row + 1) * VGA_COLS + col];
        }
    }
    // 最后一行填空格
    char *last = vram + (VGA_ROWS - 1) * VGA_COLS * 2;
    for (int col = 0; col < VGA_COLS; col++) {
        last[col * 2]     = ' ';
        last[col * 2 + 1] = 0x07;
        cjk_cell[(VGA_ROWS - 1) * VGA_COLS + col] = 0;
    }
}

/* 输出一个字符到当前光标位置 (VGA + 串口双输出)
 * serial_putc 在 serial.c 定义, 经 common.h 声明调用 (v6.5 起正式驱动) */
void put_char(char c, char color) {
    /* 串口镜像: 文本原样; 控制字符翻译, 避免远程控制台错乱
     * (LF→CRLF 防换行不回车; TAB→4 空格) */
    if (c == '\n') { serial_putc('\r'); serial_putc('\n'); }
    else if (c == '\t') { serial_puts("    "); }
    else serial_putc(c);
    if (c == '\n') {
        cur_x = 0;
        cur_y++;
    } else if (c == '\r') {
        cur_x = 0;
    } else if (c == '\b') {
        if (cur_x > 0) cur_x--;
    } else if (c == '\t') {
        cur_x = (cur_x + 4) & ~3;  // tab = 4 spaces
        if (cur_x >= VGA_COLS) {
            cur_x = 0;
            cur_y++;
        }
    } else {
        /* 写格前清掉该格上的叠加 (输入光标 | 或鼠标 █), 其影子随之作废 */
        if (ic_x == cur_x && ic_y == cur_y) ic_clear();
        if (mc_x == cur_x && mc_y == cur_y) mc_clear();
        vga_cjk_ascii(cur_x, cur_y);       /* 该格若是汉字占位 → 清掉汉字标记 */
        int offset = (cur_y * VGA_COLS + cur_x) * 2;
        vram[offset]     = c;
        vram[offset + 1] = color;
        cur_x++;
        if (cur_x >= VGA_COLS) {
            cur_x = 0;
            cur_y++;
        }
    }

    if (cur_y >= VGA_ROWS) {
        /* 滚动前先把叠加还原进缓冲区, 让它随内容上移, 滚动后再重铺 */
        ic_clear();
        mc_clear();
        scroll_up();
        cur_y = VGA_ROWS - 1;
        if (!mc_hidden && mouse_installed_k()) mc_draw_at(mouse_char_x(), mouse_char_y());
        ic_draw_at(cur_x, cur_y);
    }

    /* 文本流推进 -> 软件输入光标跟随 cur_x/cur_y。
     * 不跟随的话, C 程序 (不经 shell 的 redraw/update_cursor) 回显输入时,
     * 定时器自愈一直把 '_' 画在过时位置 (提示符末尾) -> 残留 (v6.5.2)。
     * EDIT 直写 0xB8000 不走 put_char, 其光标由 sys_cur 独立定位, 不受影响。 */
    if (!ic_hidden) {
        ic_px = cur_x;
        ic_py = cur_y;
        ic_draw_at(cur_x, cur_y);
    }
}

/* 输出字符串 */
void put_str(char *s) {
    while (*s) {
        put_char(*s, 0x07);
        s++;
    }
    update_cursor();
}

/* 清屏 */
void cls() {
    ic_clear(); mc_clear();          /* 复位叠加影子状态 (整屏随即清空) */
    vga_cjk_clear_all();             /* 清汉字格标记, 防清屏后旧汉字鬼影 (v6.8 修复) */
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) {
        int offset = i * 2;
        vram[offset]     = ' ';
        vram[offset + 1] = 0x07;
    }
    cur_x = 0;
    cur_y = 0;
    update_cursor();
}

/* 输出一段可能含 GB2312 汉字的字符串到当前光标处 (v6.8 中文)。
 * 双字节 (>=0xA1 且后字节存在) → 汉字: softbuf 两格放占位 0xDB + cjk_cell 记 GB 码,
 * 由 fb_render 经 HZK16 画 16×16; ASCII → 走 put_char。
 * 非图形模式 (fb 未启用) 退化为 put_str 原样吐字节。 */
static void cjk_advance(void) {
    if (cur_x + 1 >= VGA_COLS) {        /* 放不下两格 → 换行滚动 */
        cur_x = 0; cur_y++;
        if (cur_y >= VGA_ROWS) { scroll_up(); cur_y = VGA_ROWS - 1; }
    }
}
/* 在光标处放一个 GB2312 汉字 (占两格) */
static void cjk_place(unsigned gb, char color) {
    cjk_advance();
    int o = (cur_y * VGA_COLS + cur_x) * 2;
    vram[o] = 0xDB; vram[o + 1] = color;
    vram[o + 2] = 0xDB; vram[o + 3] = color;
    vga_cjk_set(cur_x, cur_y, gb);
    cur_x += 2;
}
/* 在光标处放一个替换框 □ (Unicode 不在 GB2312 字库) */
static void cjk_place_box(char color) {
    cjk_advance();
    int o = (cur_y * VGA_COLS + cur_x) * 2;
    vram[o] = 0xDB; vram[o + 1] = color;
    vram[o + 2] = 0xDB; vram[o + 3] = color;
    vga_cjk_box(cur_x, cur_y);
    cur_x += 2;
}
/* 输出一个 Unicode 码点: 查 GB2312 → 画汉字; 不在字库 → 替换框 */
static void cjk_render_uni(unsigned cp, char color) {
    if (cp < 0x80) { put_char((char)cp, color); return; }
    unsigned gb = fb_uni_to_gb(cp);
    if (gb) cjk_place(gb, color); else cjk_place_box(color);
}

/* 输出一段可能含 GB2312 / UTF-8 汉字的字符串到当前光标处 (v6.8 中文)。
 * 逐字节自动识别: UTF-8 2/3/4 字节 (含 BOM) 或 GB2312 双字节 →
 * softbuf 两格放占位 0xDB + cjk_cell 记 GB 码 (或 0xFFFE 替换框), 由 fb_render
 * 经 HZK16 画 16×16; ASCII → 走 put_char。
 * 非图形模式 (fb 未启用) 退化为 put_str 原样吐字节。 */
void put_cjk_str(const unsigned char *s, char color) {
    if (!fb_active()) { put_str((char *)s); return; }
    while (*s) {
        unsigned char b = *s;
        unsigned cp = 0;
        if (b == 0xEF && s[1] == 0xBB && s[2] == 0xBF) { s += 3; continue; }  /* UTF-8 BOM */
        if (b >= 0xE0 && b <= 0xEF && s[1] >= 0x80 && s[1] <= 0xBF
            && s[2] >= 0x80 && s[2] <= 0xBF) {            /* UTF-8 3 字节 (CJK) */
            cp = ((unsigned)(b & 0x0F) << 12) | ((unsigned)(s[1] & 0x3F) << 6)
                 | (unsigned)(s[2] & 0x3F);
            s += 3;
            cjk_render_uni(cp, color);
            continue;
        }
        if (b >= 0xF0 && b <= 0xF4 && s[1] >= 0x80 && s[1] <= 0xBF
            && s[2] >= 0x80 && s[2] <= 0xBF && s[3] >= 0x80 && s[3] <= 0xBF) {
            cp = ((unsigned)(b & 0x07) << 18) | ((unsigned)(s[1] & 0x3F) << 12)
                 | ((unsigned)(s[2] & 0x3F) << 6) | (unsigned)(s[3] & 0x3F);
            s += 4;
            cjk_render_uni(cp, color);
            continue;
        }
        if (b >= 0xC2 && b <= 0xDF && s[1] >= 0x80 && s[1] <= 0xA0) {
            /* UTF-8 2 字节: 续字节 0x80-0xA0 必非 GB2312 低位 → 定为 UTF-8 */
            cp = ((unsigned)(b & 0x1F) << 6) | (unsigned)(s[1] & 0x3F);
            s += 2;
            cjk_render_uni(cp, color);
            continue;
        }
        if (b >= 0xA1 && b <= 0xF7 && s[1] >= 0xA1) {     /* GB2312 双字节 */
            cjk_place(((unsigned)b << 8) | (unsigned char)s[1], color);
            s += 2;
            continue;
        }
        put_char((char)b, color);                          /* ASCII */
        s++;
    }
    update_cursor();
}
