/* dev.c — 设备管理 (v6.5.6 阶段B):
 *   dev_scan()      4 个 IDE 槽 IDENTIFY → devs[] (presence/容量/型号)
 *   dev_automount() 引导盘恒 A:, 其余按槽序补位; fs_init 成功才分盘符
 *   pci_scan()      conf1 扫 bus0, 记录 01xx 存储 / 02xx 网络 (仅列出)
 *   devs_list()     DEVS 命令输出
 */
#include "common.h"
#include "dev.h"

blkdev_t devs[4];
int boot_drive_slot = -1;

/* 挂载表: mount_letter[d] = 分到的盘符序号 (0='A'), -1 = 未挂载 */
static int mount_letter[4];

/* PCI 发现记录 (本轮只列出, 不编程 BAR) */
#define PCI_MAX 16
static unsigned int pci_dev[PCI_MAX];    /* (dev<<8)|fn */
static unsigned int   pci_cls[PCI_MAX];  /* [23:8] = base class/sub/prog */
static int pci_n = 0;    /* 存储/网络 */
static int pci_total = 0;

static unsigned char idbuf[512];         /* IDENTIFY 回读缓冲 */

/* 串口十进制 (serial.c 无此助手, 本地补) */
static void ser_dec(unsigned long v)
{
    char b[12]; int i = 0;
    if (!v) { serial_putc('0'); return; }
    while (v) { b[i++] = '0' + (char)(v % 10); v /= 10; }
    while (--i >= 0) serial_putc(b[i]);
}

static int mem_eq(const void *a, const void *b, int n)
{
    const unsigned char *x = a, *y = b;
    while (n--) if (*x++ != *y++) return 0;
    return 1;
}

/* 屏幕十进制/两位十六进制 */
static void put_hex2(unsigned v)
{
    static const char *h = "0123456789ABCDEF";
    put_char(h[(v >> 4) & 0xF], 0x07);
    put_char(h[v & 0xF], 0x07);
}

void dev_scan(void)
{
    int d, found = 0;
    unsigned char dl = *(volatile unsigned char *)0x7C24; /* boot.asm 写入 */
    if (dl >= 0x80) boot_drive_slot = dl - 0x80;
    unsigned char serials[4][20];   /* word 10-19, 用于空槽别名去重 */

    for (d = 0; d < 4; d++) {
        devs[d].present = 0;
        devs[d].sectors = 0;
        devs[d].model[0] = 0;
        mount_letter[d] = -1;
        if (identify_drive_asm(d, idbuf) != 0) {
            serial_puts("[DBG] identify fail slot ");
            ser_dec(d);
            serial_puts(" status=");
            ser_dec(idbuf[0]);
            serial_puts("\n");
            continue;
        }
        devs[d].present = 1;
        for (int k = 0; k < 20; k++) serials[d][k] = idbuf[20 + k];
        devs[d].sectors = *(unsigned int *)(idbuf + 120);   /* word 60-61 */
        {
            /* word 27-46 型号: 大端字序 → 逐字节交换, 截 20 字符去尾空格 */
            unsigned char *w = idbuf + 54;
            char *m = devs[d].model;
            int i, j = 0;
            for (i = 0; i < 20; i++)
                m[j++] = w[(i & ~1) + 1 - (i & 1)];  /* 交换相邻字节 */
            while (j > 0 && m[j-1] == ' ') j--;
            m[j] = 0;
            if (!m[0]) { m[0] = '?'; m[1] = 0; }
        }
        found++;
    }
    /* QEMU 怪癖: 通道上不存在从盘时, 选从盘的 IDENTIFY 被别名到主盘
     * (型号/容量/串号逐字节相同)。串号+容量与同通道主盘全同 → 判无盘,
     * 否则空从盘会重复挂载主盘内容。真机无此别名行为, 此去重安全。 */
    for (d = 1; d < 4; d += 2) {
        int m = d - 1;
        if (devs[d].present && devs[m].present &&
            devs[d].sectors == devs[m].sectors &&
            mem_eq(serials[d], serials[m], 20)) {
            devs[d].present = 0;
            serial_puts("[DEVS]  slot ");
            ser_dec(d);
            serial_puts(": empty (alias of master, dropped)\n");
        }
    }
    found = 0;
    for (d = 0; d < 4; d++) if (devs[d].present) found++;
    serial_puts("[DEVS] ide slots present: ");
    ser_dec(found);
    serial_puts(", boot slot: ");
    ser_dec(boot_drive_slot);
    serial_puts("\n");
    for (d = 0; d < 4; d++) {
        if (!devs[d].present) continue;
        serial_puts("[DEVS]  slot ");
        ser_dec(d);
        serial_puts(": ");
        serial_puts(devs[d].model);
        serial_puts(" sectors=");
        ser_dec(devs[d].sectors);
        serial_puts("\n");
    }
}

/* 逐槽挂载: 引导盘 (IDE 时) 恒 A:, 其余按槽序 0..3 补位。
 * 只有 fs_init 成功 (BPB 合法) 才分盘符 — 缺盘时后续盘符前移, 兼容旧行为。 */
void dev_automount(void)
{
    int i, next = 0;
    int order[4];
    int n = 0;

    if (boot_drive_slot >= 0 && boot_drive_slot < 4 &&
        devs[boot_drive_slot].present)
        order[n++] = boot_drive_slot;
    for (i = 0; i < 4; i++)
        if (i != boot_drive_slot && devs[i].present)
            order[n++] = i;

    for (i = 0; i < n; i++) {
        int slot = order[i];
        current_drive_idx = slot;
        if (fs_init() == 0) {
            mount_letter[slot] = next;
            serial_puts("[MOUNT] slot ");
            ser_dec(slot);
            serial_puts(" -> ");
            serial_putc('A' + next);
            serial_puts(":\n");
            next++;
        }
    }
    if (next == 0) current_drive_idx = 0;   /* 裸软盘引导: 维持默认 A: */
}

void pci_scan(void)
{
    unsigned long cfg;
    unsigned int dev, fn;

    for (dev = 0; dev < 32 && pci_total < PCI_MAX; dev++) {
        cfg = 0x80000000UL | (dev << 11);
        io_out32(0xCF8, cfg);
        if ((unsigned short)io_in32(0xCFC) == 0xFFFF) continue;  /* 无设备 */
        /* fn0-7 全扫 (读多功能位易漏, 8 次空读代价可忽略) */
        for (fn = 0; fn < 8 && pci_total < PCI_MAX; fn++) {
            cfg = 0x80000000UL | (dev << 11) | (fn << 8);
            io_out32(0xCF8, cfg);
            if ((unsigned short)io_in32(0xCFC) == 0xFFFF) continue;
            io_out32(0xCF8, cfg | 0x08);
            pci_cls[pci_total] = io_in32(0xCFC) >> 8;   /* [23:8] */
            pci_dev[pci_total] = (dev << 8) | fn;
            pci_total++;
            if ((pci_cls[pci_total-1] >> 16) == 0x01 ||
                (pci_cls[pci_total-1] >> 16) == 0x02) pci_n++;
        }
    }
    serial_puts("[PCI] bus0 functions: ");
    ser_dec(pci_total);
    serial_puts("\n");
}

void devs_list(void)
{
    static const char *slotname[4] = { "0:0", "0:1", "1:0", "1:1" };
    int d;
    put_str("IDE devices:\r\n");
    for (d = 0; d < 4; d++) {
        if (!devs[d].present) {
            put_str("  ATA "); put_str((char *)slotname[d]);
            put_str("  -- empty --\r\n");
            continue;
        }
        put_str("  ATA "); put_str((char *)slotname[d]);
        put_str("  "); put_str(devs[d].model);
        put_str("  "); put_num(devs[d].sectors / 2048); put_str(" MB");
        put_str("  ");
        if (mount_letter[d] >= 0) {
            put_char('A' + mount_letter[d], 0x0E);
            put_str(":");
        } else {
            put_str("(no fs)");
        }
        put_str("\r\n");
    }
    {
        int shown = 0;
        for (d = 0; d < pci_total; d++) {
            if ((pci_cls[d] >> 16) != 0x01 && (pci_cls[d] >> 16) != 0x02)
                continue;   /* 只列存储 (01xx) / 网络 (02xx) */
            if (!shown) { put_str("PCI storage/net:\r\n"); shown = 1; }
            put_str("  00:");
            put_hex2((pci_dev[d] >> 8) & 0xFF);
            put_char('.', 0x07);
            put_hex2(pci_dev[d] & 0x07);
            put_str("  class ");
            put_hex2(pci_cls[d] >> 16);
            put_hex2((pci_cls[d] >> 8) & 0xFF);
            put_str("\r\n");
        }
    }
}
