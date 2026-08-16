/* sys/types.h -- minimal POSIX types for Makar apps (-nostdinc, ILP32).
 * size_t comes from the force-included stddef.h. */
#ifndef _USERSPACE_SYS_TYPES_H
#define _USERSPACE_SYS_TYPES_H
typedef long          ssize_t;
typedef long          off_t;
typedef int           pid_t;
typedef unsigned int  mode_t;
typedef unsigned int  dev_t;
typedef unsigned int  ino_t;
typedef unsigned int  uid_t;
typedef unsigned int  gid_t;
typedef long          time_t;
#endif
