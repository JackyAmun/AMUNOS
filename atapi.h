/* atapi.h — ATAPI 光驱 (v6.5.6 P3) */
#ifndef _ATAPI_H
#define _ATAPI_H

int  atapi_probe(void);            /* IDE1 从属 IDENTIFY PACKET + READ CAPACITY */
int  atapi_ready(void);
unsigned int atapi_capacity(void); /* 2048B 扇区数 */
int  atapi_read_sectors_2048(unsigned lba, unsigned count, void *buf);
int  atapi_read_sectors(unsigned lba512, unsigned count, void *buf);

#endif
