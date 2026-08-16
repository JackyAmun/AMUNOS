/* fcntl.h -- open flags for Makar apps (-nostdinc).  Values match the kernel
 * ABI in syscall.h.  open()/fcntl() live in libc (tcc_compat.c). */
#ifndef _USERSPACE_FCNTL_H
#define _USERSPACE_FCNTL_H

#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_ACCMODE   3
#define O_CREAT     0100
#define O_TRUNC     01000
#define O_APPEND    02000
#define O_NONBLOCK  0x800
#define O_BINARY    0          /* no text/binary distinction on Makar */

#define F_GETFL     3
#define F_SETFL     4

int open(const char *path, int flags, ...);
int creat(const char *path, int mode);
int fcntl(int fd, int cmd, ...);

#endif
