/* serial.c — AMUNOS v6.5 串口(COM1)/并口(LPT1) 轮询驱动
 *
 * 16550 UART (COM1 = 0x3F8) 全轮询收发, 不碰 IRQ (ARCHITECTURE.md §4)。
 * LPT1 = 0x378, 写并口需要 STROBE 升沿 (QEMU -parallel file: 也靠它触发)。
 */

#include "common.h"

#define COM1       0x3F8
#define COM1_LSR   (COM1 + 5)     /* 线路状态寄存器: bit0=RX ready, bit5=THR empty */
#define LPT1_DATA  0x378
#define LPT1_STAT  0x379          /* bit7 = BUSY (1=不忙) */
#define LPT1_CTRL  0x37A          /* bit0=STROBE, bit2=INIT, bit3=SELECT */

/* 初始化 COM1: 115200, 8N1, FIFO, DTR|RTS */
void serial_init(void) {
    io_out8(COM1 + 1, 0x00);   /* IER: 关中断 */
    io_out8(COM1 + 3, 0x80);   /* LCR: DLAB=1 */
    io_out8(COM1 + 0, 0x01);   /* DLL=1  → 115200 */
    io_out8(COM1 + 1, 0x00);   /* DLM=0 */
    io_out8(COM1 + 3, 0x03);   /* LCR: 8N1, DLAB=0 */
    io_out8(COM1 + 2, 0xC7);   /* FCR: 开 FIFO + 清缓冲 */
    io_out8(COM1 + 4, 0x03);   /* MCR: DTR|RTS */
}

/* 输出一个字符到 COM1 (等 THR 空)。无 UART 时 LSR 读 0xFF, 不会死锁 */
void serial_putc(char c) {
    while ((io_in8(COM1_LSR) & 0x20) == 0);
    io_out8(COM1, c);
}

/* 输出字符串, 把 '\n' 翻译成 CRLF (串口终端惯用) */
void serial_puts(char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

/* 非阻塞读一个字符: 有数据返回字符, 无返回 -1 (远程控制台/SLIP 原语) */
int serial_getc(void) {
    if (io_in8(COM1_LSR) & 0x01) return io_in8(COM1);
    return -1;
}

/* 输出一个字符到 LPT1: 有界忙等不忙 → 写数据 → STROBE 升沿 → 回落 */
void lpt_putc(char c) {
    int i;
    for (i = 0; i < 100000 && (io_in8(LPT1_STAT) & 0x80) == 0; i++);  /* 有界: 不挂死 */
    io_out8(LPT1_DATA, c);
    io_out8(LPT1_CTRL, 0x0D);     /* STROBE 升沿 */
    for (i = 0; i < 1000; i++);   /* 脉宽 (真实硬件需要) */
    io_out8(LPT1_CTRL, 0x0C);     /* STROBE 回落 */
}

/* 输出字符串到 LPT1, 同样做 CRLF 翻译 */
void lpt_puts(char *s) {
    while (*s) {
        if (*s == '\n') lpt_putc('\r');
        lpt_putc(*s++);
    }
}
