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

/* 设置硬件光标位置 */
void update_cursor() {
    unsigned short pos = cur_y * VGA_COLS + cur_x;
    io_out8(0x3D4, 14);                    // 光标高位寄存器
    io_out8(0x3D5, (pos >> 8) & 0xFF);
    io_out8(0x3D4, 15);                    // 光标低位寄存器
    io_out8(0x3D5, pos & 0xFF);
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
        scroll_up();
        cur_y = VGA_ROWS - 1;
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
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++) {
        int offset = i * 2;
        vram[offset]     = ' ';
        vram[offset + 1] = 0x07;
    }
    cur_x = 0;
    cur_y = 0;
    update_cursor();
}
