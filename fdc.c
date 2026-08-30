/* fdc.c — 软盘控制器 (82077AA) 运行时驱动 (v6.5.6 阶段 P1)
 *
 * 设计: **非 DMA (ND) 全轮询** — SPECIFY 置 ND=1, 数据相 MSR.RQM+DIO
 * 每字节就绪后从 FIFO (0x3F5) 读写, 不需要 8237 DMA 控制器代码, 也
 * 不接 IRQ6 (内核 PIC 目前只开 IRQ0/1/2/12, 保持不动)。
 * 电机: fdc_init 时开 A 电机并常开 (QEMU 无代价; 真机后续加电机超时),
 * 从而任何上下文 (含 task_init 之前的 kmain 早期) 都可安全调用。
 * 几何: 1.44MB = 80 柱 × 2 头 × 18 扇, 512B/扇, 仅驱动器 0。
 */
#include "common.h"
#include "fdc.h"

#define FDC_DOR   0x3F2   /* 数字输出: 电机/复位/DMA使能/驱动选择 */
#define FDC_MSR   0x3F4   /* 主状态: RQM(0x80) DIO(0x40) NDMA(0x20) BUSY(0x10) */
#define FDC_FIFO  0x3F5
#define FDC_CCR   0x3F7   /* 数据速率 500kbps */

#define FDC_SPT   18
#define FDC_HPC   2

#define RQM 0x80
#define DIO 0x40

static int fdc_ok = 0;

/* 本地串口十进制 (调试用) */
static void fdc_dbgdec(unsigned v)
{
    char b[12]; int i = 0;
    if (!v) { serial_putc('0'); return; }
    while (v) { b[i++] = '0' + (char)(v % 10); v /= 10; }
    while (--i >= 0) serial_putc(b[i]);
}

/* 轮询 MSR 直到 (st & mask)==val; iter 用尽返回 -1。
 * 每 4096 次 inb 让一步 (无 task_sleep 依赖, task_init 前也可用)。 */
static int msr_wait(unsigned mask, unsigned val, unsigned iter)
{
    unsigned i;
    for (i = 0; i < iter; i++) {
        unsigned st = io_in8(FDC_MSR);
        if ((st & mask) == val) return 0;
        if ((i & 0xFFF) == 0xFFF) io_in8(0x80);   /* 微延时 */
    }
    return -1;
}

/* 命令相写一字节: 等 RQM 且 DIO=0 */
static int fdc_out(unsigned char b)
{
    if (msr_wait(0xC0, RQM, 200000)) return -1;
    io_out8(FDC_FIFO, b);
    return 0;
}

/* 结果相读一字节: 等 RQM 且 DIO=1 */
static int fdc_in(unsigned char *b)
{
    if (msr_wait(0xC0, RQM | DIO, 200000)) return -1;
    *b = io_in8(FDC_FIFO);
    return 0;
}

/* Sense Interrupt Status: 返回 ST0 (PCN 存 *pcn) */
static int fdc_sense(unsigned char *st0, unsigned char *pcn)
{
    unsigned char s;
    if (fdc_out(0x08)) return -1;
    if (fdc_in(&s)) return -1;
    if (fdc_in(pcn)) return -1;
    *st0 = s;
    return 0;
}

static int fdc_seek(unsigned cyl, unsigned head)
{
    unsigned char st0, pcn;
    int tries;
    if (fdc_out(0x0F)) return -1;               /* SEEK */
    if (fdc_out((unsigned char)((head << 2) | 0))) return -1;
    if (fdc_out((unsigned char)cyl)) return -1;
    for (tries = 0; tries < 8; tries++) {       /* SEEK 需 Sense Interrupt 收尾 */
        if (fdc_sense(&st0, &pcn)) return -1;
        if ((st0 & 0x20) && pcn == cyl) return 0;   /* 寻道完成位 */
    }
    return -1;
}

int fdc_init(void)
{
    unsigned char st0, pcn;
    int i;

    io_out8(FDC_CCR, 0x00);                     /* 500kbps */
    io_out8(FDC_DOR, 0x00);                     /* 进复位 */
    for (i = 0; i < 1000; i++) io_in8(0x80);    /* ≥ 复位脉宽 */
    io_out8(FDC_DOR, 0x1C);                     /* 出复位 + 电机A + IRQ/DMA 使能 */
    if (msr_wait(RQM, RQM, 400000)) return -1;  /* 复位完成 → RQM */

    for (i = 0; i < 4; i++) {                   /* 复位产生 4 个中断, 全部吃掉 */
        if (fdc_sense(&st0, &pcn)) return -1;
    }
    /* SPECIFY: SRT=0xC(步进) HUT=0xF(保持), HLT=1(2ms) ND=1 → 非DMA */
    if (fdc_out(0x03) || fdc_out(0xCF) || fdc_out(0x03)) return -1;
    /* RECALIBRATE 到 0 柱 */
    if (fdc_out(0x07) || fdc_out(0x00)) return -1;
    for (i = 0; i < 8; i++) {
        if (fdc_sense(&st0, &pcn)) return -1;
        if ((st0 & 0x20) && pcn == 0) { fdc_ok = 1; return 0; }
    }
    return -1;
}

int fdc_ready(void) { return fdc_ok; }

int fdc_read_sectors(unsigned lba, unsigned count, void *buf)
{
    unsigned char st[7], r0;
    unsigned char *p = buf;
    unsigned cyl, head, sect, i, n;

    if (!fdc_ok || count == 0 || count > 18) return -1;
    if (lba >= 2880 || lba + count > 2880) return -1;

    for (n = 0; n < count; n++) {
        unsigned l = lba + n;
        cyl = l / (FDC_SPT * FDC_HPC);
        head = (l / FDC_SPT) % FDC_HPC;
        sect = l % FDC_SPT + 1;

        if (fdc_seek(cyl, head)) return -1;
        /* READ (MFM|MT): 头, C, H, R, N=2(512B), EOT=18, GPL, DTL */
        if (fdc_out(0x46) || fdc_out((unsigned char)(head << 2)) ||
            fdc_out((unsigned char)cyl) || fdc_out(head) ||
            fdc_out((unsigned char)sect) || fdc_out(2) ||
            fdc_out((unsigned char)sect) || fdc_out(0x1B) || fdc_out(0xFF)) return -1;
        /* EOT=当前扇号: 无 DMA 无 TC 引脚, 命令读完本扇即自然结束 (0x46 无 MT) */

        /* 数据相: 512B 全轮询读入 (QEMU ND 模式每字节 RQM|DIO) */
        for (i = 0; i < 512; i++) {
            unsigned char b;
            if (msr_wait(0xC0, RQM | DIO, 200000)) return -1;
            b = io_in8(FDC_FIFO);
            if (n * 512 + i < count * 512) p[n * 512 + i] = b;
        }
        /* 结果相 7 字节 */
        for (i = 0; i < 7; i++) {
            if (fdc_in(&st[i])) {
                serial_puts("[FDC] result byte fail i=");
                fdc_dbgdec(i);
                serial_puts(" msr=");
                fdc_dbgdec(io_in8(FDC_MSR));
                serial_puts("\n");
                return -1;
            }
        }
        r0 = st[0];
        if ((r0 & 0xC0) != 0x00 && (r0 & 0xC0) != 0x40) {
            serial_puts("[FDC] st0=");
            fdc_dbgdec(r0);
            serial_puts(" st1=");
            fdc_dbgdec(st[1]);
            serial_puts("\n");
            return -1;                          /* ST0 bit7-6: 00=正常结束 */
        }
    }
    return 0;
}
