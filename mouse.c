/* mouse.c — PS/2 鼠标驱动 (IRQ12 → 中断向量 0x2C)
 *
 * 8042 初始化 + 三字节标准包解析, 维护像素坐标与按钮状态。
 * VGA 文本模式 (80x25) 以 8x16 字体 640x400 像素平面计,
 * 字符格坐标 = 像素 / 8 宽, / 16 高, 由 SYS_MOUSE 返回用户程序。
 * 鼠标光标由 QEMU GUI 渲染, 内核不画光标。
 */

#include "common.h"

#define KBD_CMD_PORT  0x64
#define KBD_DATA_PORT 0x60

/* VGA 文本模式 80x25 的像素平面尺寸 (8x16 字体) */
#define MOUSE_PX_W 640
#define MOUSE_PX_H 400

static int mouse_present = 0;
static int mx_px = MOUSE_PX_W / 2;    /* 初始屏幕中心 (与 QEMU GUI 光标起始一致) */
static int my_px = MOUSE_PX_H / 2;
static int mbuttons = 0;

/* 等待 8042 输入缓冲空 (写命令前) */
static void kbd_wait_out(void) {
    while (io_in8(KBD_CMD_PORT) & 0x02);
}
/* 等待 8042 输出缓冲有数据 (读数据前) */
static void kbd_wait_in(void) {
    while (!(io_in8(KBD_CMD_PORT) & 0x01));
}
/* 向鼠标 (aux 设备) 发命令: 0x64 写 0xD4, 再 0x60 写命令字节 */
static void aux_cmd(unsigned char cmd) {
    kbd_wait_out();
    io_out8(KBD_CMD_PORT, 0xD4);
    kbd_wait_out();
    io_out8(KBD_DATA_PORT, cmd);
}
/* 等待鼠标 ACK (0xFA) 并丢弃 */
static void aux_ack(void) {
    kbd_wait_in();
    (void)io_in8(KBD_DATA_PORT);
}

/* 清空 8042 输出缓冲。
 * keyboard_init() 的 update_leds() 只发命令不读 ACK, 0xFA 残留;
 * 若不先清掉, 后面读 config (0x20) 会误读到 0xFA 当配置字节写回,
 * 其 bit4=1 会禁用键盘 → 键盘链路死 (v6.7 鼠标驱动修复)。 */
static void flush_output(void) {
    while (io_in8(KBD_CMD_PORT) & 0x01)
        (void)io_in8(KBD_DATA_PORT);
}

/* 初始化 8042 控制器与鼠标 — 由 idt.c 的 init_idt() 调用 */
void mouse_init(void) {
    unsigned char cfg;

    flush_output();                    /* 清键盘 ACK 残留 */

    /* 1. 启用辅助设备 */
    kbd_wait_out();
    io_out8(KBD_CMD_PORT, 0xA8);

    /* 2. 读 8042 配置字节, 置 bit1 (aux IRQ 使能), 保留 bit0 (键盘 IRQ) */
    kbd_wait_out();
    io_out8(KBD_CMD_PORT, 0x20);
    kbd_wait_in();
    cfg = io_in8(KBD_DATA_PORT);
    cfg |= 0x02;                       /* enable aux IRQ */
    kbd_wait_out();
    io_out8(KBD_CMD_PORT, 0x60);
    kbd_wait_out();
    io_out8(KBD_DATA_PORT, cfg);

    /* 3. 鼠标默认设置 + 启用数据上报 */
    aux_cmd(0xF6);   aux_ack();        /* 默认设置 (禁用上报) */
    aux_cmd(0xF4);   aux_ack();        /* 启用数据上报 */

    mouse_present = 1;
}

/* ── 三字节标准包 ──
 * pkt[0]: bit0=左键 bit1=右键 bit2=中键 bit3=恒1 bit4=x符号 bit5=y符号 bit6/7=溢出
 * pkt[1]: dx (有符号, 用 bit4 扩展)   pkt[2]: dy (有符号, 用 bit5 扩展)
 * dy 正值 = 鼠标向上 → 屏幕 y (向下增大) 减 dy */
void mouse_handler(void) {
    static unsigned char pkt[3];
    static int idx = 0;
    unsigned char b;
    int n = 0;

    /* ── 排干 8042 输出缓冲里的全部 aux 字节 (v6.7 真实 GUI 修复) ──
     * QEMU 8.2.2 的 8042 对"输出缓冲源"做键盘优先仲裁 (ps2_read_data 按
     * obsrc 取字节, 键盘队列非空时 obsrc=键盘、bit5=0)。旧实现每次 IRQ12
     * 只读 1 字节, 快速移动+点击时键盘字节会插进三字节包之间, 把包截断
     * (状态机停在中间), 残包+新包混排 → 坐标/按钮全乱 → EDIT 行为混乱 →
     * 用户栈被破坏 → 中断 iret 弹出垃圾 EFLAGS(NT=1) → invalid tss type。
     * 这里只要 bit5(aux 数据)为 1 就读, 一次把整包排干, 不被键盘流量打断。
     * 上限 24 (≈1.5 包) 防病理状态死循环; 余量留给下一个 IRQ12 (QEMU 会
     * 对每个残留字节重新触发)。 */
    while ((io_in8(KBD_CMD_PORT) & 0x20) && n < 24) {
        b = io_in8(KBD_DATA_PORT);
        n++;
        if (!mouse_present) return;    /* 未初始化: 丢弃防阻塞 */

        /* 同步位 (bit3) 只在"等待包头"时有效 — dx/dy 字节的 bit3 也可能是 1,
         * 不能当作新包头, 否则包错位 (按钮/坐标全乱, v6.7 修复)。 */
        if (idx == 0) {
            if (b & 0x08) { pkt[0] = b; idx = 1; }
            continue;
        }
        pkt[idx++] = b;                /* 字节 2 → 3 (dx, dy) */
        if (idx == 3) {
            idx = 0;
            mbuttons = pkt[0] & 0x07;
            if (pkt[0] & (0x40 | 0x80)) continue;/* 溢出包: 移动量无效 */

            int dx = pkt[1], dy = pkt[2];
            if (pkt[0] & 0x10) dx -= 256;      /* x 符号位 → 负 */
            if (pkt[0] & 0x20) dy -= 256;      /* y 符号位 → 负 */
            mx_px += dx;
            my_px -= dy;                       /* dy 正值 = 鼠标向上 */
            if (mx_px < 0) mx_px = 0;
            if (mx_px >= MOUSE_PX_W) mx_px = MOUSE_PX_W - 1;
            if (my_px < 0) my_px = 0;
            if (my_px >= MOUSE_PX_H) my_px = MOUSE_PX_H - 1;
        }
    }
    /* 每包更新后重画鼠标指针 '█' (v6.7 软件叠加层) — 直接写 0xB8000,
     * 不开串口; 中断门内 IF=0, 与主循环的 put_char 无竞争 (自愈设计)。 */
    vga_mouse_redraw();
}

/* ── 供 SYS_MOUSE 查询 (common.h 声明) ── */
int mouse_installed_k(void)  { return mouse_present; }
int mouse_buttons_state(void){ return mbuttons; }
int mouse_char_x(void)       { return mx_px * 80 / MOUSE_PX_W; }
int mouse_char_y(void)       { return my_px * 25 / MOUSE_PX_H; }
