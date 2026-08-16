/* sys/mman.h -- mmap/munmap 桩声明 (AMUNOS 无分页, 返回 MAP_FAILED) */
#ifndef _AMUNOS_SYS_MMAN_H
#define _AMUNOS_SYS_MMAN_H
#define PROT_READ      1
#define PROT_WRITE     2
#define PROT_EXEC      4
#define MAP_PRIVATE    2
#define MAP_ANONYMOUS  0x20
#define MAP_FAILED     ((void *)-1)
void *mmap(void *addr, unsigned int sz, int prot, int flags, int fd, long off);
int   munmap(void *addr, unsigned int sz);
#endif
