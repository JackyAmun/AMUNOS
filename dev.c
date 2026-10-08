/* dev.c — 设备管理 (v6.5.6 阶段B):
 *   dev_scan()      4 个 IDE 槽 IDENTIFY → devs[] (presence/容量/型号)
 *   dev_automount() 引导盘恒 A:, 其余按槽序补位; fs_init 成功才分盘符
 *   pci_scan()      conf1 扫 bus0, 记录 01xx 存储 / 02xx 网络 (仅列出)
 *   devs_list()     DEVS 命令输出
 */
#include "common.h"
#include "dev.h"

blkdev_t devs[DEV_SLOT_COUNT];
int boot_drive_slot = -1;

/* 挂载表: mount_letter[d] = 分到的盘符序号 (0='A'), -1 = 未挂载 */
static int mount_letter[DEV_SLOT_COUNT];

/* 盘符序号 → 槽号: 查挂载表 (软盘引导时盘符≠槽号), 未挂载回退恒等映射 */
int dev_slot_from_letter(int li)
{
    int d;
    for (d = 0; d < DEV_SLOT_COUNT; d++)
        if (mount_letter[d] == li) return d;
    return -1;
}

/* 槽号 → 盘符序号 (drive_letter/提示符用; 未挂载回退旧恒等映射) */
int dev_letter_from_slot(int slot)
{
    if (slot >= 0 && slot < DEV_SLOT_COUNT && mount_letter[slot] >= 0) return mount_letter[slot];
    return (slot >= 0 && slot < 4) ? slot : 0;
}

/* PCI 发现记录 (本轮只列出, 不编程 BAR) */
#define PCI_MAX 16
static unsigned int pci_dev[PCI_MAX];    /* (dev<<8)|fn */
static unsigned int   pci_cls[PCI_MAX];  /* [23:8] = base class/sub/prog */
static int pci_n = 0;    /* 存储/网络 */
static int pci_total = 0;

static unsigned char idbuf[512];         /* IDENTIFY 回读缓冲 */
static volatile int blk_io_busy;
static unsigned char partition_sector[512];

static int blk_lock(void)
{
    return __sync_lock_test_and_set(&blk_io_busy, 1) ? BLK_ERR_BUSY : BLK_OK;
}

static void blk_unlock(void)
{
    __sync_lock_release(&blk_io_busy);
}

const char *blk_error_name(int error)
{
    switch (error) {
    case BLK_OK: return "ok";
    case BLK_ERR_IO: return "io";
    case BLK_ERR_NODEV: return "no-device";
    case BLK_ERR_NO_MEDIA: return "no-media";
    case BLK_ERR_READ_ONLY: return "read-only";
    case BLK_ERR_RANGE: return "range";
    case BLK_ERR_BUSY: return "busy";
    case BLK_ERR_CRC: return "crc";
    case BLK_ERR_SEEK: return "seek";
    default: return "unknown";
    }
}

/* 串口十进制 (serial.c 无此助手, 本地补) */
static void ser_dec(unsigned long v)
{
    char b[12]; int i = 0;
    if (!v) { serial_putc('0'); return; }
    while (v) { b[i++] = '0' + (char)(v % 10); v /= 10; }
    while (--i >= 0) serial_putc(b[i]);
}

static void boot_check_result(int slot, int mounted, int letter)
{
    serial_puts("[CHECK] slot ");
    ser_dec(slot);
    serial_puts(" ");
    serial_puts(devs[slot].model);
    serial_puts(": ");
    if (mounted) {
        serial_puts("filesystem -> ");
        serial_putc('A' + letter);
        serial_puts(":\n");
    } else {
        serial_puts("no mountable filesystem\n");
    }
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

static int dev_refresh_media_locked(int slot)
{
    int rc;
    if (slot < 0 || slot >= DEV_SLOT_COUNT || !devs[slot].present) return BLK_ERR_NODEV;
    if (!(devs[slot].caps & BLK_CAP_REMOVABLE)) return BLK_OK;

    if (slot == 4) {
        unsigned char boot[512];
        rc = fdc_probe_media(boot);
        if (rc == BLK_OK) {
            devs[slot].media_present = 1;
            devs[slot].sectors = fdc_capacity();
            strcpy(devs[slot].model, fdc_media_name());
            devs[slot].media_generation++;
        } else {
            devs[slot].media_present = 0;
            devs[slot].sectors = 0;
        }
    } else if (slot == 5) {
        rc = atapi_refresh_media();
        devs[slot].media_present = (rc == BLK_OK);
        devs[slot].sectors = devs[slot].media_present ? atapi_capacity() * 4u : 0;
        if (rc == BLK_OK) devs[slot].media_generation++;
    } else {
        rc = BLK_OK;
    }
    devs[slot].last_error = rc;
    return rc;
}

int dev_refresh_media(int slot)
{
    int rc = blk_lock();
    if (rc != BLK_OK) return rc;
    rc = dev_refresh_media_locked(slot);
    blk_unlock();
    return rc;
}

int blk_read_n(unsigned int lba, unsigned int count, void *buf, int drive_idx)
{
    int rc;
    blkdev_t *dev;
    int physical;
    unsigned int physical_lba;
    if (drive_idx < 0 || drive_idx >= DEV_SLOT_COUNT) return BLK_ERR_NODEV;
    dev = &devs[drive_idx];
    if (!dev->present) return BLK_ERR_NODEV;
    if (!(dev->caps & BLK_CAP_READ)) return BLK_ERR_IO;
    if (!buf || !count) return BLK_ERR_IO;
    if ((rc = blk_lock()) != BLK_OK) return rc;
    if (drive_idx == 4 && dev->media_present && fdc_media_changed() > 0)
        dev->media_present = 0;
    if ((dev->caps & BLK_CAP_REMOVABLE) && !dev->media_present) {
        rc = dev_refresh_media_locked(drive_idx);
        if (rc != BLK_OK) {
            blk_unlock();
            return rc;
        }
    }
    if (lba >= dev->sectors || count > dev->sectors - lba) {
        blk_unlock();
        return BLK_ERR_RANGE;
    }
    physical = dev->parent_slot >= 0 ? dev->parent_slot : drive_idx;
    physical_lba = lba + (dev->parent_slot >= 0 ? dev->start_lba : 0);

    if (physical <= 3) {
        unsigned char *p = (unsigned char *)buf;
        rc = BLK_OK;
        for (unsigned i = 0; i < count; i++)
            if (read_sector_asm(physical_lba + i, p + i * 512, physical) != 0) {
                rc = BLK_ERR_IO; break;
            }
    } else if (physical == 4) {
        rc = fdc_read_sectors(physical_lba, count, buf);
    } else if (physical == 5) {
        rc = atapi_read_sectors(physical_lba, count, buf);
    } else {
        rc = ahci_read_sectors(ahci_port(), physical_lba, count, buf) == 0 ? BLK_OK : BLK_ERR_IO;
    }
    blk_unlock();
    dev->last_error = rc;
    if (rc == BLK_ERR_NO_MEDIA) {
        dev->media_present = 0;
        dev->sectors = 0;
    }
    return rc;
}

int blk_write_n(unsigned int lba, unsigned int count, const void *buf, int drive_idx)
{
    int rc;
    blkdev_t *dev;
    if (drive_idx < 0 || drive_idx >= DEV_SLOT_COUNT) return BLK_ERR_NODEV;
    dev = &devs[drive_idx];
    if (!dev->present) return BLK_ERR_NODEV;
    if (!(dev->caps & BLK_CAP_WRITE)) return BLK_ERR_READ_ONLY;
    if (!buf || !count) return BLK_ERR_IO;
    if ((rc = blk_lock()) != BLK_OK) return rc;
    if (dev->parent_slot < 0 && drive_idx == 4 && dev->media_present && fdc_media_changed() > 0)
        dev->media_present = 0;
    if ((dev->caps & BLK_CAP_REMOVABLE) && !dev->media_present) {
        rc = dev_refresh_media_locked(drive_idx);
        if (rc != BLK_OK) {
            blk_unlock();
            return rc;
        }
    }
    if (lba >= dev->sectors || count > dev->sectors - lba) {
        blk_unlock();
        return BLK_ERR_RANGE;
    }

    {
        int physical = dev->parent_slot >= 0 ? dev->parent_slot : drive_idx;
        unsigned int physical_lba = lba + (dev->parent_slot >= 0 ? dev->start_lba : 0);
        if (physical <= 3) {
        const unsigned char *p = (const unsigned char *)buf;
        rc = BLK_OK;
        for (unsigned i = 0; i < count; i++)
            if (write_sector_asm(physical_lba + i, (void *)(p + i * 512), physical) != 0) {
                rc = BLK_ERR_IO; break;
            }
        } else if (physical == 4) {
            rc = fdc_write_sectors(physical_lba, count, buf);
        } else if (physical == 5) {
            rc = atapi_write_sectors(physical_lba, count, buf);
        } else {
            rc = BLK_ERR_READ_ONLY;
        }
    }
    blk_unlock();
    dev->last_error = rc;
    return rc;
}

int blk_read(unsigned int lba, void *buf, int drive_idx)
{
    return blk_read_n(lba, 1, buf, drive_idx);
}

int blk_write(unsigned int lba, const void *buf, int drive_idx)
{
    return blk_write_n(lba, 1, buf, drive_idx);
}

static int add_partition(int parent, unsigned int start, unsigned int sectors,
                         unsigned char active)
{
    int slot;
    if (!sectors || start >= devs[parent].sectors ||
        sectors > devs[parent].sectors - start) return -1;
    for (slot = DEV_PHYSICAL_COUNT; slot < DEV_SLOT_COUNT; slot++)
        if (!devs[slot].present) break;
    if (slot == DEV_SLOT_COUNT) return -1;
    devs[slot] = devs[parent];
    devs[slot].parent_slot = parent;
    devs[slot].start_lba = start;
    devs[slot].sectors = sectors;
    devs[slot].active_partition = active;
    devs[slot].media_present = 1;
    devs[slot].present = 1;
    devs[slot].last_error = BLK_OK;
    return slot;
}

void dev_enumerate_partitions(void)
{
    int parent;
    for (parent = 0; parent < DEV_PHYSICAL_COUNT; parent++) {
        unsigned int extended_base = 0, extended_size = 0, ebr_lba;
        if (parent == 4 || parent == 5) continue; /* floppy and ATAPI */
        if (!devs[parent].present || blk_read(0, partition_sector, parent) != BLK_OK ||
            partition_sector[510] != 0x55 || partition_sector[511] != 0xAA)
            continue;
        for (int i = 0; i < 4; i++) {
            unsigned char *p = partition_sector + 446 + i * 16;
            unsigned char type = p[4];
            unsigned int start = (unsigned int)p[8] | ((unsigned int)p[9] << 8) |
                                 ((unsigned int)p[10] << 16) | ((unsigned int)p[11] << 24);
            unsigned int size = (unsigned int)p[12] | ((unsigned int)p[13] << 8) |
                                ((unsigned int)p[14] << 16) | ((unsigned int)p[15] << 24);
            if (type == 0x05 || type == 0x0F || type == 0x85) {
                if (start < devs[parent].sectors && size <= devs[parent].sectors - start) {
                    extended_base = start;
                    extended_size = size;
                }
                continue;
            }
            if (type && (p[0] == 0 || p[0] == 0x80))
                add_partition(parent, start, size, p[0] == 0x80);
        }
        ebr_lba = extended_base;
        for (int links = 0; extended_base && links < 16; links++) {
            unsigned char *p0, *p1;
            unsigned int rel_start, size, next;
            if (ebr_lba >= devs[parent].sectors ||
                blk_read(ebr_lba, partition_sector, parent) != BLK_OK ||
                partition_sector[510] != 0x55 || partition_sector[511] != 0xAA)
                break;
            p0 = partition_sector + 446;
            p1 = partition_sector + 462;
            rel_start = (unsigned int)p0[8] | ((unsigned int)p0[9] << 8) |
                        ((unsigned int)p0[10] << 16) | ((unsigned int)p0[11] << 24);
            size = (unsigned int)p0[12] | ((unsigned int)p0[13] << 8) |
                   ((unsigned int)p0[14] << 16) | ((unsigned int)p0[15] << 24);
            if (p0[4] && rel_start && size && ebr_lba <= 0xFFFFFFFFu - rel_start) {
                unsigned int logical_start = ebr_lba + rel_start;
                if (logical_start >= extended_base &&
                    logical_start - extended_base < extended_size &&
                    size <= extended_size - (logical_start - extended_base))
                    add_partition(parent, logical_start, size, 0);
            }
            next = (unsigned int)p1[8] | ((unsigned int)p1[9] << 8) |
                   ((unsigned int)p1[10] << 16) | ((unsigned int)p1[11] << 24);
            if (!p1[4] || !next || next >= extended_size ||
                extended_base > 0xFFFFFFFFu - next) break;
            ebr_lba = extended_base + next;
        }
    }
}

void dev_scan(void)
{
    int d, found = 0;
    unsigned char dl = *(volatile unsigned char *)0x7C24; /* boot.asm 写入 */
    if (dl >= 0x80) boot_drive_slot = dl - 0x80;
    unsigned char serials[4][20];   /* word 10-19, 用于空槽别名去重 */

    /* FreeDOS 风格的设备/介质分离: 控制器存在不等于介质已插入。 */
    for (d = 0; d < DEV_SLOT_COUNT; d++) {
        devs[d].present = 0;
        devs[d].media_present = 0;
        devs[d].sectors = 0;
        devs[d].sector_size = 512;
        devs[d].type = 0;
        devs[d].caps = 0;
        devs[d].last_error = BLK_OK;
        devs[d].media_generation = 0;
        devs[d].parent_slot = -1;
        devs[d].start_lba = 0;
        devs[d].active_partition = 0;
        devs[d].model[0] = 0;
        mount_letter[d] = -1;
    }
    put_str("[FDC] ");
    if (fdc_init() == 0) {
        unsigned char fb0[512];
        devs[4].present = 1;
        devs[4].type = BLKDEV_FDC;
        devs[4].caps = BLK_CAP_READ | BLK_CAP_WRITE |
                       BLK_CAP_REMOVABLE | BLK_CAP_MEDIA_CHG;
        if (fdc_probe_media(fb0) == BLK_OK) {
            devs[4].media_present = 1;
            devs[4].sectors = fdc_capacity();
            strcpy(devs[4].model, fdc_media_name());
        } else strcpy(devs[4].model, "FLOPPY FD0");
    }
    put_str("[ATAPI] ");
    /* 槽 5 = 扫描传统 IDE 两通道上的 ATAPI 光驱。 */
    if (atapi_probe() == BLK_OK) {
        devs[5].present = 1;
        devs[5].media_present = atapi_media_present();
        devs[5].type = BLKDEV_ATAPI;
        devs[5].caps = BLK_CAP_READ | BLK_CAP_REMOVABLE | BLK_CAP_MEDIA_CHG;
        devs[5].sectors = devs[5].media_present ? atapi_capacity() * 4u : 0;
        strcpy(devs[5].model, atapi_model());
        serial_puts("[DEVS] atapi present, media sectors2048=");
        ser_dec(devs[5].media_present ? atapi_capacity() : 0);
        serial_puts("\n");
    }
    /* 槽 6 = AHCI SATA (q35 + ich9-ahci) */
    /* 槽 6 = AHCI SATA (q35 + ich9-ahci) */
    devs[6].present = 0;
    devs[6].sectors = 0;
    devs[6].model[0] = 0;
    if (ahci_scan() == 0) {
        devs[6].present = 1;
        devs[6].media_present = 1;
        devs[6].type = BLKDEV_AHCI;
        devs[6].caps = BLK_CAP_READ;
        devs[6].sectors = 0xFFFFFFFFu; /* IDENTIFY capacity is a P1 follow-up. */
        strcpy(devs[6].model, "AHCI SATA");
    }
    for (d = 0; d < 4; d++) {
        unsigned int sectors;
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
        sectors = *(unsigned int *)(idbuf + 120);
        /* disk_io.asm issues 28-bit LBA commands; reject floating-bus and
         * unsupported IDENTIFY capacities instead of exposing fake targets. */
        if (!sectors || sectors > 0x0FFFFFFFu) {
            serial_puts("[DEVS]  slot ");
            ser_dec(d);
            serial_puts(": invalid IDENTIFY capacity\n");
            continue;
        }
        devs[d].present = 1;
        devs[d].media_present = 1;
        devs[d].type = BLKDEV_IDE;
        devs[d].caps = BLK_CAP_READ | BLK_CAP_WRITE;
        for (int k = 0; k < 20; k++) serials[d][k] = idbuf[20 + k];
        devs[d].sectors = sectors;                         /* word 60-61 */
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
    dev_enumerate_partitions();
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
    int order[DEV_SLOT_COUNT];
    int n = 0;

    if (boot_drive_slot == -1 && devs[4].present)
        order[n++] = 4;
    if (boot_drive_slot >= 0 && boot_drive_slot < 4) {
        int boot_partition = -1;
        for (i = DEV_PHYSICAL_COUNT; i < DEV_SLOT_COUNT; i++)
            if (devs[i].present && devs[i].parent_slot == boot_drive_slot &&
                devs[i].active_partition) { boot_partition = i; break; }
        if (boot_partition < 0)
            for (i = DEV_PHYSICAL_COUNT; i < DEV_SLOT_COUNT; i++)
                if (devs[i].present && devs[i].parent_slot == boot_drive_slot) {
                    boot_partition = i; break;
                }
        if (boot_partition >= 0) order[n++] = boot_partition;
        else if (devs[boot_drive_slot].present) order[n++] = boot_drive_slot;
    }
    for (i = DEV_PHYSICAL_COUNT; i < DEV_SLOT_COUNT; i++)
        if (devs[i].present && (n == 0 || i != order[0])) order[n++] = i;
    for (i = 0; i < 4; i++)
        if (i != boot_drive_slot && devs[i].present) {
            int has_parts = 0;
            for (int p = DEV_PHYSICAL_COUNT; p < DEV_SLOT_COUNT; p++)
                if (devs[p].present && devs[p].parent_slot == i) { has_parts = 1; break; }
            if (!has_parts) order[n++] = i;
        }
    if (boot_drive_slot >= 0 && devs[4].present) order[n++] = 4;
    if (devs[5].present && devs[5].media_present)  /* 有介质的光驱排最末 */
        order[n++] = 5;
    if (devs[6].present) {                          /* AHCI 盘其后 */
        int has_parts = 0;
        for (int p = DEV_PHYSICAL_COUNT; p < DEV_SLOT_COUNT; p++)
            if (devs[p].present && devs[p].parent_slot == 6) { has_parts = 1; break; }
        if (!has_parts) order[n++] = 6;
    }

    for (i = 0; i < n; i++) {
        int slot = order[i];
        current_drive_idx = slot;
        if (next < 26 && fs_probe_init() == 0) {
            mount_letter[slot] = next;
            serial_puts("[MOUNT] slot ");
            ser_dec(slot);
            serial_puts(" -> ");
            serial_putc('A' + next);
            serial_puts(":\n");
            boot_check_result(slot, 1, next);
            next++;
        } else {
            boot_check_result(slot, 0, 0);
        }
    }
    if (next == 0) current_drive_idx = 0;   /* 裸软盘引导: 维持默认 A: */
    else {                                   /* shell 初始盘 = 挂载到 A: 的槽 */
        for (i = 0; i < DEV_SLOT_COUNT; i++)
            if (mount_letter[i] == 0) { current_drive_idx = i; break; }
    }
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
    static const char *slotname[DEV_PHYSICAL_COUNT] = { "0:0", "0:1", "1:0", "1:1", "FD0", "CD0", "SA0" };
    int d;
    put_str("Block devices:\r\n");
    for (d = 0; d < DEV_SLOT_COUNT; d++) {
        const char *kind = d == 4 ? "FDC " : (d == 5 ? "CD  " : (d == 6 ? "AHCI" : "IDE "));
        if (d >= DEV_PHYSICAL_COUNT && devs[d].parent_slot < 0) continue;
        if (!devs[d].present) {
            put_str("  "); put_str(kind); put_str(" ");
            put_str(d < DEV_PHYSICAL_COUNT ? (char *)slotname[d] : "PART");
            put_str("  -- empty --\r\n");
            continue;
        }
        put_str("  "); put_str(kind); put_str(" ");
        put_str(d < DEV_PHYSICAL_COUNT ? (char *)slotname[d] : "PART");
        put_str("  "); put_str(devs[d].model);
        if (!devs[d].media_present) put_str("  [no media]");
        else { put_str("  "); put_num(devs[d].sectors / 2048); put_str(" MB"); }
        put_str((devs[d].caps & BLK_CAP_WRITE) ? "  RW" : "  RO");
        if (devs[d].caps & BLK_CAP_REMOVABLE) put_str(" REM");
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
