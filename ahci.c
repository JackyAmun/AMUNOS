/* ahci.c — AHCI (SATA) 只读 poll 驱动 (v6.5.6 P4)
 *
 * PCI class 0106 → BAR5 MMIO (无分页, 指针即物理)。
 * HBA 复位 (GHC.HR) → AE; PI 掩码逐 port: SSTS DET=3/IPM=1 检测;
 * rebase: command list 1KB (FIS 256B + cmd table 256B, 手工对齐);
 * 读: READ DMA EXT (0x25) 单命令, 轮询 CI (无中断)。
 * 仅 SATA 盘 (SIG 0xEB140101); SATAPI/端口复用不支持。
 */
#include "common.h"
#include "ahci.h"

#define AHCI_MAX_PORTS 6

static volatile unsigned char *hba = 0;   /* BAR5 */
static volatile unsigned char *port[AHCI_MAX_PORTS];
static int ahci_ok = 0;
static int sata_port = -1;   /* 首个 SATA 盘的端口号 */

/* 寄存器偏移: port 相对 (port 基址 = hba+0x100+0x80*i 已含 +0x100) */
#define PxCLB  0x00
#define PxFB   0x08
#define PxIS   0x10
#define PxCMD  0x18
#define PxTFD  0x20
#define PxSIG  0x24
#define PxSSTS 0x28
#define PxCI   0x38

#define PxCMD_ST  0x01
#define PxCMD_FRE 0x10
#define PxCMD_FR  0x4000
#define PxCMD_CR  0x8000

static unsigned int rd32(const volatile unsigned char *p, int off)
{
    return *(volatile unsigned int *)(p + off);
}
static void wr32(volatile unsigned char *p, int off, unsigned int v)
{
    *(volatile unsigned int *)(p + off) = v;
}

/* 堆分配 + 1KB 对齐 (mem_alloc 只有 8 对齐, 多要 1KB 自行对齐) */
static void *alloc_aligned(unsigned size)
{
    unsigned char *raw = (unsigned char *)mem_alloc(size + 1024);
    if (!raw) return 0;
    unsigned int v = ((unsigned int)raw + 1023) & ~1023u;
    return (void *)v;
}

int ahci_scan(void)
{
    ahci_ok = 0;
    /* PCI 枚举: 找 class 010106h (SATA AHCI), 取 BAR5 (offset 0x24) */
    unsigned int bar5 = 0;
    for (unsigned int dev = 0; dev < 32 && !bar5; dev++) {
        for (unsigned int fn = 0; fn < 8 && !bar5; fn++) {
            unsigned int cfg = 0x80000000u | (dev << 11) | (fn << 8);
            io_out32(0xCF8, cfg);
            if ((unsigned short)io_in32(0xCFC) == 0xFFFF) continue;
            io_out32(0xCF8, cfg | 0x08);
            unsigned int cls = io_in32(0xCFC) >> 8;
            if ((cls >> 8) != 0x0106) continue;   /* base<<8|sub = 0106h */
            io_out32(0xCF8, cfg | 0x24);
            bar5 = io_in32(0xCFC) & ~0xFu;
        }
    }
    if (!bar5) { serial_puts("[AHCI] no controller\n"); return -1; }
    hba = (volatile unsigned char *)bar5;

    /* QEMU ICH9: 跳过 GHC.HR 复位 (复位后 port 半初始化, 命令不被取),
       只确保 AE 开启 */
    wr32(hba, 0x04, rd32(hba, 0x04) | 0x80000000u); /* GHC.AE */
    serial_puts("[AHCI] bar5=");
    { char hb[] = "0123456789ABCDEF"; unsigned v = bar5;
      for (int k = 28; k >= 0; k -= 4) serial_putc(hb[(v>>k)&0xF]); }
    serial_putc('\n');

    unsigned int pi = rd32(hba, 0x0C);
    int found = 0;
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (!(pi & (1u << i))) continue;
        volatile unsigned char *p = hba + 0x100 + 0x80 * i;
        unsigned int ssts = rd32(p, PxSSTS);
        unsigned int sig0 = rd32(p, PxSIG);
        if ((ssts & 0x0F) != 3 || ((ssts >> 8) & 0x0F) != 1) continue;
        unsigned int sig = rd32(p, PxSIG);
        if (sig == 0xEB140101u) continue;   /* SATAPI 本轮不支持 */
        /* 停 port */
        unsigned int cmd = rd32(p, PxCMD);
        cmd &= ~(PxCMD_ST | PxCMD_FRE);
        wr32(p, PxCMD, cmd);
        for (int w = 0; w < 500000 && (rd32(p, PxCMD) & (PxCMD_CR | PxCMD_FR)); w++)
            io_in8(0x80);
        /* rebase */
        void *clb = alloc_aligned(1024);
        void *fb  = alloc_aligned(256);
        void *ctb = alloc_aligned(256);
        if (!clb || !fb || !ctb) { serial_puts("[AHCI] no mem\n"); continue; }
        for (int k = 0; k < 256; k++) ((unsigned char *)clb)[k] = 0;
        for (int k = 0; k < 256; k++) ((unsigned char *)fb)[k] = 0;
        for (int k = 0; k < 256; k++) ((unsigned char *)ctb)[k] = 0;
        wr32(p, PxCLB, (unsigned int)clb);
        wr32(p, PxFB,  (unsigned int)fb);
        /* command header 0 → cmd table (PRDT 在 +0x80) */
        volatile unsigned int *hdr = (volatile unsigned int *)clb;
        hdr[2] = (unsigned int)ctb;                    /* CTBA */
        hdr[3] = 0;                                    /* CTBAU (32位) */
        wr32(p, PxCMD, rd32(p, PxCMD) | PxCMD_FRE | PxCMD_ST);
        port[i] = p;
        if (sata_port < 0) sata_port = i;
        found++;
        serial_puts("[AHCI] port ");
        serial_putc('0' + i);
        serial_puts(" sata ok\n");
    }
    if (!found) { serial_puts("[AHCI] no disk\n"); return -1; }
    ahci_ok = 1;
    return 0;
}

int ahci_ready(void) { return ahci_ok; }
int ahci_port(void) { return sata_port; }

/* port i 读 count 扇 (512B), poll CI */
int ahci_read_sectors(int portidx, unsigned lba, unsigned count, void *buf)
{
    if (!ahci_ok || portidx < 0 || portidx >= AHCI_MAX_PORTS || !port[portidx])
        return -1;
    volatile unsigned char *p = port[portidx];
    volatile unsigned int *hdr = (volatile unsigned int *)rd32(p, PxCLB);
    volatile unsigned int *ctb = (volatile unsigned int *)hdr[2];
    volatile unsigned int *prdt = ctb + 0x80 / 4;
    unsigned char *fis = (unsigned char *)ctb;

    if (count > 128) return -1;               /* PRDT 单表上限保护 */

    /* command header 0: CFL=5 (words) | W=0 (读) | PRDTL=count */
    hdr[0] = 5 | (count << 16);
    hdr[1] = 0;

    /* FIS host-to-device, READ DMA EXT (0x25) — AHCI 1.3 布局:
       [0]=0x27, [1]=C|PMP, [2]=cmd, [3]=feat, [4..6]=LBA低24, [7]=dev,
       [8..10]=LBA高24, [12..13]=count */
    for (int k = 0; k < 16; k++) fis[k] = 0;
    fis[0] = 0x27;                 /* H2D Register FIS */
    fis[1] = 0x80;                 /* C=1, PMP=0 */
    fis[2] = 0x25;                 /* READ DMA EXT */
    fis[3] = 0;                    /* feature */
    fis[4] = (unsigned char)(lba);
    fis[5] = (unsigned char)(lba >> 8);
    fis[6] = (unsigned char)(lba >> 16);
    fis[7] = 0x40;                 /* device: LBA 模式 */
    fis[8] = (unsigned char)(lba >> 24);
    fis[9] = 0;                    /* 当前接口是 32-bit LBA, 高 16 位清零 */
    fis[10] = 0;
    fis[12] = (unsigned char)(count);
    fis[13] = (unsigned char)(count >> 8);

    prdt[0] = (unsigned int)buf;   /* DBA */
    prdt[1] = 0;                   /* DBAU */
    prdt[2] = 0;                   /* reserved */
    prdt[3] = (count * 512 - 1) | 0x80000000u;   /* DBC+I */

    wr32(p, PxIS, 0xFFFFFFFF);     /* 清状态 */
    wr32(p, PxCI, 1);               /* CI slot 0 */

    for (int w = 0; w < 8000000; w++) {
        if (!(rd32(p, PxCI) & 1)) {          /* CI 清 → 完成 */
            unsigned int tfd = rd32(p, PxTFD);
            if ((tfd & 0x89) == 0) return 0;  /* BSY/DRQ/ERR 清 */
            return -1;
        }
        if ((w & 0xFFFF) == 0xFFFF) io_in8(0x80);
    }
    serial_puts("[AHCI] timeout\n");
    return -1;
}
