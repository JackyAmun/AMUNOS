/* fdc.h — 软盘控制器 (82077) 运行时驱动, 非 DMA 全轮询模式 (v6.5.6) */
#ifndef _FDC_H
#define _FDC_H

int  fdc_init(void);                                    /* 复位+SPECIFY+RECAL; 0=OK */
int  fdc_read_sectors(unsigned lba, unsigned count, void *buf);  /* 512B/扇, 1.44MB 几何 */
int  fdc_write_sectors(unsigned lba, unsigned count, const void *buf);
int  fdc_probe_media(void *boot_sector);
int  fdc_media_changed(void);
unsigned int fdc_capacity(void);
const char *fdc_media_name(void);
int  fdc_ready(void);                                   /* init 是否成功过 */

#endif
