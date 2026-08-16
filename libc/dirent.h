#ifndef _USERSPACE_DIRENT_H
#define _USERSPACE_DIRENT_H

/* POSIX dirent shape over the index-addressed SYS_READDIR(141).
 * struct dirent + DT_* + DIRENT_NAME_MAX come from <syscall.h>. */
#include "syscall.h"

/* Opaque to callers; layout is exposed only so DIR* can live in caller
 * storage (e.g. inside fopen).  Don't poke at fields directly. */
typedef struct {
    char           path[256];   /* matches VFS_PATH_MAX */
    unsigned int   idx;
    struct dirent  de;
} DIR;

/* opendir(path) -- returns a malloc'd DIR* or NULL on error.  Caller
 * must closedir() it to free.  No fd is consumed (the kernel-side
 * SYS_READDIR is stateless: each call passes path + index). */
DIR *opendir(const char *path);

/* readdir(dp) -- returns a pointer to the next dirent (owned by dp,
 * overwritten on the next call) or NULL at end-of-directory / error.
 * Does NOT set errno. */
struct dirent *readdir(DIR *dp);

/* closedir(dp) -- frees dp.  Returns 0 (errors are not surfaced). */
int closedir(DIR *dp);

#endif
