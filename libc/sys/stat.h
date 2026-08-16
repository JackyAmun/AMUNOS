/* sys/stat.h -- stat()/mkdir() surface for Makar apps.  struct stat itself is
 * the canonical Linux layout from syscall.h (guarded), so we don't redefine it. */
#ifndef _USERSPACE_SYS_STAT_H
#define _USERSPACE_SYS_STAT_H
#include <syscall.h>   /* struct stat (_MAKAR_STRUCT_STAT_DEFINED) */

#define S_IFMT   0170000
#define S_IFDIR  0040000
#define S_IFREG  0100000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_IRUSR  0400
#define S_IWUSR  0200
#define S_IXUSR  0100
#define S_IRWXU  0700

int stat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
int mkdir(const char *path, unsigned int mode);

#endif
