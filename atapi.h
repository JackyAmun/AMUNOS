/* atapi.h — ATAPI 光驱 (v6.5.6 P3) */
#ifndef _ATAPI_H
#define _ATAPI_H

int  atapi_probe(void);            /* 扫描两条传统 IDE 通道的主/从设备 */
int  atapi_ready(void);
int  atapi_media_present(void);
int  atapi_refresh_media(void);
unsigned int atapi_capacity(void); /* 2048B 扇区数 */
int  atapi_read_sectors_2048(unsigned lba, unsigned count, void *buf);
int  atapi_read_sectors(unsigned lba512, unsigned count, void *buf);
int  atapi_write_sectors(unsigned lba512, unsigned count, const void *buf);
const char *atapi_model(void);

#endif
