/* ahci.h — AHCI (SATA) poll 只读 (v6.5.6 P4) */
#ifndef _AHCI_H
#define _AHCI_H

#define AHCI_SLOT 6   /* devs[] 槽号 */

int  ahci_scan(void);   /* PCI 0106 → BAR5 → 复位/检测/rebase; 0=找到 SATA 盘 */
int  ahci_ready(void);
int  ahci_port(void);  /* 首个 SATA 盘端口号 (-1 无) */
int  ahci_read_sectors(int portidx, unsigned lba, unsigned count, void *buf);

#endif
