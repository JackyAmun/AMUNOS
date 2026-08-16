/*
 * unistd.h -- POSIX-shaped userspace declarations (hosted libc surface).
 *
 * Exposes the standard names hosted C programs expect.  The file-I/O and
 * path calls (read/write/close/lseek/unlink/rmdir/access/getcwd) are
 * *declared* here but *defined* in tcc_compat.c -- this header just gives
 * them a prototype so programs don't rely on implicit declarations.  The
 * remaining thin syscall wrappers (dup/dup2/pipe, getpid/getppid, chdir,
 * sleep/usleep, _exit) are defined inline over <syscall.h>.
 *
 * NB: the ring-3 shell `sh.c` deliberately does NOT include this header --
 * it stays freestanding on <syscall.h> alone so the in-OS TCC self-rebuild
 * (test_tcc_rebuild_sh) compiles it with no libc.  This header is for the
 * fuller hosted libc.a that ordinary programs (hello.c, future ports) link.
 */
#ifndef _USERSPACE_UNISTD_H
#define _USERSPACE_UNISTD_H

#include "syscall.h"

#ifndef _MAKAR_TYPE_PID_T
#define _MAKAR_TYPE_PID_T
typedef int pid_t;
#endif
#ifndef _MAKAR_TYPE_USECONDS_T
#define _MAKAR_TYPE_USECONDS_T
typedef unsigned int useconds_t;
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* ---- defined in tcc_compat.c (canonical libc implementations) ---- */
int   close(int fd);
long  read (int fd, void *buf, unsigned n);
long  write(int fd, const void *buf, unsigned n);
long  lseek(int fd, long offset, int whence);
int   unlink(const char *path);
int   rmdir (const char *path);
int   access(const char *path, int mode);
char *getcwd(char *buf, unsigned int size);

/* ---- thin syscall wrappers (defined here; nothing else provides them) ---- */

static inline int dup(int oldfd)             { return sys_dup(oldfd);          }
static inline int dup2(int oldfd, int newfd) { return sys_dup2(oldfd, newfd);  }
static inline int pipe(int pipefd[2])        { return sys_pipe(pipefd);        }
static inline int chdir(const char *path)    { return sys_chdir(path);         }

static inline pid_t getpid(void)  { return (pid_t)sys_getpid();  }
static inline pid_t getppid(void) { return (pid_t)sys_getppid(); }

/* sleep/usleep: no SYS_NANOSLEEP -- spin on the 100 Hz tick counter,
 * yielding the CPU, exactly like sh.c's `sleep` builtin.  100 ticks == 1 s,
 * so the granularity is 10 ms. */
static inline unsigned int sleep(unsigned int seconds)
{
    unsigned int target = sys_uptime() + seconds * 100u;
    while (sys_uptime() < target)
        sys_yield();
    return 0;   /* never interrupted: no SIGALRM delivery on this path */
}

static inline int usleep(useconds_t usec)
{
    unsigned int ticks  = (usec + 9999u) / 10000u;   /* round up to 10 ms */
    unsigned int target = sys_uptime() + ticks;
    while (sys_uptime() < target)
        sys_yield();
    return 0;
}

static inline void _exit(int status) { sys_exit(status); }

#endif /* _USERSPACE_UNISTD_H */
