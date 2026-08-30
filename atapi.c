/* atapi.c — ATAPI 光驱驱动 (v6.5.6 P3, 只读)
 *
 * 探测: IDE1 从属 (0x170, dev=1) 发 IDENTIFY PACKET DEVICE (0xA1);
 * 读:   PACKET (0xA0) + SCSI READ(10), PIO 一次 2048 字节 (= 4 个 512 扇)。
 * 全轮询, 无中断/DMA。仅支持 QEMU 标准位置 (-cdrom = IDE1 slave)。
 */
#include "common.h"
#include "atapi.h"

#define AT_BASE   0x170
#define AT_DATA   (AT_BASE + 0)   /* 数据 (16bit) */
#define AT_ERR    (AT_BASE + 1)   /* = Feature */
#define AT_SCNT   (AT_BASE + 2)
#define AT_LBAM   (AT_BASE + 3)
#define AT_LBAH   (AT_BASE + 4)
#define AT_DH     (AT_BASE + 6)   /* 驱动/头 */
#define AT_STAT   (AT_BASE + 7)
#define AT_CMD    (AT_BASE + 7)
#define AT_CTL    (AT_BASE + 0x206)

#define SRST 0x04
#define nIEN 0x02

static int atapi_ok = 0;
static int in_sense = 0;   /* REQUEST SENSE 递归防护 */
static unsigned int atapi_sectors = 0;   /* 2048B 扇区数 (READ CAPACITY) */

static int wait_not_busy(int iter)
{
    int i;
    for (i = 0; i < iter; i++) {
        unsigned char st = io_in8(AT_STAT);
        if (!(st & 0x80)) return 0;      /* BSY 清零 */
        if ((st & 0x01) || (st & 0x20)) return -1;   /* ERR / DF */
        if ((i & 0xFFF) == 0xFFF) io_in8(0x80);
    }
    return -1;
}

static int wait_drq(void)
{
    int i;
    for (i = 0; i < 400000; i++) {
        unsigned char st = io_in8(AT_STAT);
        if (st & 0x08) return 0;         /* DRQ */
        if (st & 0x01) return -1;        /* ERR */
        if ((i & 0xFFF) == 0xFFF) io_in8(0x80);
    }
    return -1;
}

/* 12 字节 packet 命令 (PIO); 失败打印位置码 f1-f4 + status/error */
static int atapi_packet(const unsigned char *pkt, unsigned short *buf, int words)
{
    io_out8(AT_DH, 0xB0);                /* LBA 从属 */
    if (wait_not_busy(400000)) return -1;
    io_out8(AT_SCNT, 0);                 /* 特性: 无重叠/无 DMA */
    io_out8(AT_LBAM, 0xFE);   /* byte count limit = 0xFFFE (PIO-in 最大) */
    io_out8(AT_LBAH, 0xFF);
    io_out8(AT_CTL, nIEN);               /* 关中断 (轮询) */
    io_out8(AT_CMD, 0xA0);               /* PACKET */
    if (wait_drq()) return -1;
    for (int i = 0; i < 6; i++)
        io_out16(AT_DATA, ((unsigned short *)pkt)[i]);
    for (int i = 0; i < words; i++) {
        if (wait_drq()) return -1;
        buf[i] = io_in16(AT_DATA);
    }
    if (wait_not_busy(400000)) {
        serial_puts("[ATAPI] f3"); serial_putc(10);
        return -1;
    }
    if (io_in8(AT_STAT) & 0x01) return -1;
    return 0;
}

int atapi_probe(void)
{
    unsigned short id[256];

    /* 软复位通道 */
    io_out8(AT_CTL, SRST | nIEN);
    { volatile int d; for (d = 0; d < 100000; d++) io_in8(0x80); }
    io_out8(AT_CTL, nIEN);
    if (wait_not_busy(1000000)) { serial_puts("[ATAPI] busy after reset\n"); return -1; }

    /* IDENTIFY PACKET DEVICE (0xA1) 是普通 ATA 命令: 直接写命令寄存器,
     * 数据相 256 字 DRQ PIO; 不能当作 SCSI 包塞进 PACKET (0xA0) — 会 ABRT */
    io_out8(AT_DH, 0xB0);
    if (wait_not_busy(400000)) { serial_puts("[ATAPI] sel busy\n"); return -1; }
    io_out8(AT_CMD, 0xA1);
    if (wait_drq()) {
        { char hb[] = "0123456789ABCDEF";
          serial_puts("[ATAPI] id fail st=");
          serial_putc(hb[(io_in8(AT_STAT)>>4)&0xF]); serial_putc(hb[io_in8(AT_STAT)&0xF]);
          serial_puts(" er="); serial_putc(hb[(io_in8(AT_ERR)>>4)&0xF]); serial_putc(hb[io_in8(AT_ERR)&0xF]);
          serial_putc('\n'); }
        return -1;
    }
    for (int i = 0; i < 256; i++) id[i] = io_in16(AT_DATA);
    if (wait_not_busy(400000)) return -1;
    if (0) {
        { char hb[] = "0123456789ABCDEF";
          serial_puts("[ATAPI] id fail st=");
          serial_putc(hb[(io_in8(AT_STAT)>>4)&0xF]); serial_putc(hb[io_in8(AT_STAT)&0xF]);
          serial_puts(" er="); serial_putc(hb[(io_in8(AT_ERR)>>4)&0xF]); serial_putc(hb[io_in8(AT_ERR)&0xF]);
          serial_putc('\n'); }
        return -1;
    }
    serial_puts("[ATAPI] id0=");
    { unsigned v = id[0]; char hb[] = "0123456789ABCDEF";
      for (int k = 12; k >= 0; k -= 4) serial_putc(hb[(v>>k)&0xF]); }
    serial_putc('\n');
    if (!(id[0] & 0x8000)) return -1;    /* bit15=1 → packet 设备 */
    atapi_ok = 1;

    /* READ CAPACITY (0x25): 返回 8 字节 (最后 LBA + 块长) */
    {
        unsigned char cp[12] = { 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        unsigned short cap[4];
        if (atapi_packet(cp, cap, 4) == 0) {
            /* READ CAPACITY 返回大端: [最后LBA][块长], 扇数 = 最后LBA+1 */
            unsigned char *c = (unsigned char *)cap;
            unsigned int last = ((unsigned int)c[0] << 24) | (c[1] << 16)
                              | (c[2] << 8) | c[3];
            atapi_sectors = last + 1;
        }
    }
    return 0;
}

int atapi_ready(void) { return atapi_ok; }
unsigned int atapi_capacity(void) { return atapi_sectors; }

/* 读 count 个 2048B 扇 (ISO LBA), 写入 buf (2048*count 字节) */
int atapi_read_sectors_2048(unsigned lba, unsigned count, void *buf)
{
    unsigned short *p = buf;
    for (unsigned n = 0; n < count; n++) {
        unsigned int b = lba + n;   /* READ(10) LBA 单位即 2048B 块 */
        unsigned char pkt[12] = { 0x28, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        pkt[2] = (unsigned char)(b >> 24);       /* READ(10) LBA (MSB) */
        pkt[3] = (unsigned char)(b >> 16);
        pkt[4] = (unsigned char)(b >> 8);
        pkt[5] = (unsigned char)b;
        pkt[7] = 0;                              /* 传输 1×2048B 块 */
        pkt[8] = 1;
        if (atapi_packet(pkt, p, 1024)) {
            { char hb[] = "0123456789ABCDEF";
              serial_puts("[ATAPI] read fail lba=");
              for (int k = 28; k >= 0; k -= 4) serial_putc(hb[((lba+n)>>k)&0xF]);
              serial_puts(" st="); serial_putc(hb[(io_in8(AT_STAT)>>4)&0xF]); serial_putc(hb[io_in8(AT_STAT)&0xF]);
              serial_puts(" er="); serial_putc(hb[(io_in8(AT_ERR)>>4)&0xF]); serial_putc(hb[io_in8(AT_ERR)&0xF]);
              serial_putc('\n'); }
            return -1;
        }
        p += 1024;
    }
    return 0;
}

/* blk 层适配: lba 为 512B 扇区号 */
int atapi_read_sectors(unsigned lba, unsigned count, void *buf)
{
    /* 512B 对齐的 2048B 扇: 逐个 2048 扇读入临时再拷? 直接要求 4 扇对齐;
     * ISO 访问都在 2048 边界, 落在中间的由调用方缓冲 */
    static unsigned char win[2048];
    unsigned wi = 0xFFFFFFFF;
    unsigned char *dst = buf;
    if (!atapi_ok) return -1;
    if (lba % 4 == 0 && count % 4 == 0)
        return atapi_read_sectors_2048(lba / 4, count / 4, buf);
    /* 非对齐: 逐 512 扇走 2048 窗口读 (fs 层单扇访问都是这种) */
    for (unsigned i = 0; i < count; i++) {
        unsigned L = lba + i;
        if (L / 4 != wi) {
            if (atapi_read_sectors_2048(L / 4, 1, win)) return -1;
            wi = L / 4;
        }
        for (int k = 0; k < 512; k++)
            dst[i * 512 + k] = win[(L % 4) * 512 + k];
    }
    return 0;
}
