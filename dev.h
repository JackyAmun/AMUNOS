/* dev.h — 块设备抽象 + IDE IDENTIFY 自动发现 + PCI 枚举 (v6.5.6 阶段B) */
#ifndef _DEV_H
#define _DEV_H

typedef struct {
    int  present;      /* IDENTIFY 成功 */
    unsigned int sectors;   /* LBA 扇区总数 (word 60-61) */
    char model[21];    /* 型号截 20 字符 + NUL */
} blkdev_t;

extern blkdev_t devs[7];   /* 0-3=IDE, 4=软盘, 5=ATAPI, 6=AHCI (P4) */   /* 0-3=IDE 槽, 4=软盘(FDC), 5=ATAPI (P3) */
extern int boot_drive_slot;/* 引导盘 IDE 槽 (DL>=0x80 时 = DL-0x80); 软盘 = -1 */

int  dev_slot_from_letter(int li);   /* 盘符序号 → 槽号 (查挂载表) */
int  dev_letter_from_slot(int slot); /* 槽号 → 盘符序号 (提示符/DIR 头) */
int  identify_drive_asm(int drive_idx, void *buf512); /* disk_io.asm */
void dev_scan(void);       /* 4 槽 IDENTIFY → devs[] */
void dev_automount(void);  /* 逐盘验签挂载 → 盘符 A..D */
void pci_scan(void);       /* bus0 枚举, 记录 01xx/02xx */
void devs_list(void);      /* shell DEVS 命令输出 */

#endif
