/* iso9660.h — ISO9660 只读挂载 (v6.5.6 P3) */
#ifndef _ISO9660_H
#define _ISO9660_H

int  iso_mount(void);   /* 读 PVD (扇16) 验 "CD001" → 记录根目录; 0=成功 */
int  iso_list(int dir_lba512, int dir_size512, FAT12Entry *out, int max);
int  iso_find(int dir_lba512, int dir_size512, char *name, FAT12Entry *out);

#endif
