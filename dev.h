/* dev.h — 块设备抽象 + IDE IDENTIFY 自动发现 + PCI 枚举 (v6.5.6 阶段B) */
#ifndef _DEV_H
#define _DEV_H

#define BLKDEV_IDE    1
#define BLKDEV_FDC    2
#define BLKDEV_ATAPI  3
#define BLKDEV_AHCI   4
#define DEV_PHYSICAL_COUNT 7
#define DEV_SLOT_COUNT 32

#define BLK_CAP_READ       0x01
#define BLK_CAP_WRITE      0x02
#define BLK_CAP_REMOVABLE  0x04
#define BLK_CAP_MEDIA_CHG  0x08

#define BLK_OK             0
#define BLK_ERR_IO        -1
#define BLK_ERR_NODEV     -2
#define BLK_ERR_NO_MEDIA  -3
#define BLK_ERR_READ_ONLY -4
#define BLK_ERR_RANGE     -5
#define BLK_ERR_BUSY      -6
#define BLK_ERR_CRC       -7
#define BLK_ERR_SEEK      -8

typedef struct {
    int present;               /* 控制器上的设备存在 */
    int media_present;         /* 可移除介质当前可读 */
    unsigned int sectors;      /* 统一为 512B 逻辑扇区 */
    unsigned short sector_size;
    unsigned char type;
    unsigned char caps;
    int last_error;
    unsigned int media_generation;
    int parent_slot;             /* logical partition -> physical slot, -1 otherwise */
    unsigned int start_lba;
    unsigned char active_partition;
    char model[21];
} blkdev_t;

extern blkdev_t devs[DEV_SLOT_COUNT];
extern int boot_drive_slot;/* 引导盘 IDE 槽 (DL>=0x80 时 = DL-0x80); 软盘 = -1 */

int  dev_slot_from_letter(int li);   /* 盘符序号 → 槽号 (查挂载表) */
int  dev_letter_from_slot(int slot); /* 槽号 → 盘符序号 (提示符/DIR 头) */
int  identify_drive_asm(int drive_idx, void *buf512); /* disk_io.asm */
int  blk_read_n(unsigned int lba, unsigned int count, void *buf, int drive_idx);
int  blk_write_n(unsigned int lba, unsigned int count, const void *buf, int drive_idx);
int  dev_refresh_media(int slot);
void dev_enumerate_partitions(void);
const char *blk_error_name(int error);
void dev_scan(void);       /* discover physical devices and MBR/EBR volumes */
void dev_automount(void);  /* mount valid volumes and assign A..Z */
void pci_scan(void);       /* bus0 枚举, 记录 01xx/02xx */
void devs_list(void);      /* shell DEVS 命令输出 */

#endif
