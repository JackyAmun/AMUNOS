/* vga.c - VGA Text Mode Display Driver
 * Adapted from flash-4th-os/debug/dprintk.c */
#include "common.h"

#define VGA_BASE    0xB8000
#define VGA_COLS    80
#define VGA_ROWS    25
#define VGA_BYTES   (VGA_COLS * VGA_ROWS * 2)

static char *const vram = (char *)VGA_BASE;

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

/* 滚屏：整屏上移一行 */
static void scroll_up() {
    // 将第 1 行到第 24 行复制到第 0 行到第 23 行
    for (int row = 0; row < VGA_ROWS - 1; row++) {
        char *dst = vram + row * VGA_COLS * 2;
        char *src = vram + (row + 1) * VGA_COLS * 2;
        for (int col = 0; col < VGA_COLS * 2; col++) {
            dst[col] = src[col];
        }
    }
    // 最后一行填空格
    char *last = vram + (VGA_ROWS - 1) * VGA_COLS * 2;
    for (int col = 0; col < VGA_COLS; col++) {
        last[col * 2]     = ' ';
        last[col * 2 + 1] = 0x07;
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
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) {
        int offset = i * 2;
        vram[offset]     = ' ';
        vram[offset + 1] = 0x07;
    }
    cur_x = 0;
    cur_y = 0;
    update_cursor();
}
