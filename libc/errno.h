#ifndef _USERSPACE_ERRNO_H
#define _USERSPACE_ERRNO_H

/* Linux i386 errno values -- subset that Makar wrappers set today.
 * Defined here so a future musl/uClibc port doesn't need a translation
 * shim.  Kernel-side syscalls that fail still return -1 for now; only
 * the new POSIX-shaped wrappers in 30a-30e thread an `errno` value via
 * the global below. */
#define EPERM           1
#define ENOENT          2
#define ESRCH           3
#define EINTR           4
#define EIO             5
#define ENXIO           6
#define E2BIG           7
#define ENOEXEC         8
#define EBADF           9
#define ECHILD         10
/* EAGAIN is also defined in syscall.h (== 11); keep them in lockstep. */
#ifndef EAGAIN
#define EAGAIN         11
#endif
#define ENOMEM         12
#define EACCES         13
#define EFAULT         14
#define EBUSY          16
#define EEXIST         17
#define ENOTDIR        20
#define EISDIR         21
#define EINVAL         22
#define ENFILE         23
#define EMFILE         24
#define ENOTTY         25
#define EFBIG          27
#define ENOSPC         28
#define EROFS          30
#define ENOSYS         38

/* errno itself.  Storage lives in tcc_compat.c. */
extern int errno;

#endif
