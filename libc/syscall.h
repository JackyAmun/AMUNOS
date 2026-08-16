/*
 * syscall.h — AMUNOS 用户态 libc 的 syscall 封装层 (int 0x30)
 *
 * 借自 Makar OS 的 userspace/syscall.h, 但把 int 0x80 换成 int 0x30,
 * 调用号重映射到 AMUNOS 的 syscall.c。核心 7 个 (open/close/read/write/
 * lseek/exit/brk) 是真实现; 其余 POSIX 表层 (stat/fork/mmap/...) 是
 * 桩 (返回 -1/0), 供 tcc_compat.c 编译通过 -- 链接时 --gc-sections 会
 * 丢弃未被 TCC 实际引用的部分。
 *
 * 仅用裸 int/unsigned 类型 (与 makar_abi.h 一致), 不依赖 <stdint.h>。
 */

#ifndef _AMUNOS_SYSCALL_H
#define _AMUNOS_SYSCALL_H

/* ── AMUNOS 调用号 (int 0x30, 与 syscall.c 一致) ── */
#define SYS_PUTCHAR  1
#define SYS_GETCHAR  2
#define SYS_PUTS     3
#define SYS_PUTNUM   4
#define SYS_MALLOC   5
#define SYS_FREE     6
#define SYS_SLEEP    7
#define SYS_OPEN     8
#define SYS_CLOSE    9
#define SYS_READ     10
#define SYS_WRITE    11
#define SYS_LSEEK    12
#define SYS_EXIT     13
#define SYS_BRK      14
#define SYS_GETKEY   15

/* ── 内联汇编封装 ── */
static inline long syscall0(long nr) {
    long ret;
    __asm__ volatile ("int $0x30" : "=a"(ret) : "0"(nr) : "memory");
    return ret;
}
static inline long syscall1(long nr, long a1) {
    long ret;
    __asm__ volatile ("int $0x30" : "=a"(ret) : "0"(nr), "b"(a1) : "memory");
    return ret;
}
static inline long syscall2(long nr, long a1, long a2) {
    long ret;
    __asm__ volatile ("int $0x30" : "=a"(ret) : "0"(nr), "b"(a1), "c"(a2) : "memory");
    return ret;
}
static inline long syscall3(long nr, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile ("int $0x30" : "=a"(ret) : "0"(nr), "b"(a1), "c"(a2), "d"(a3) : "memory");
    return ret;
}

/* ── 类型 ABI (与内核/文件系统一致) ── */
#ifndef _AMUNOS_STRUCT_TIMEVAL_DEFINED
#define _AMUNOS_STRUCT_TIMEVAL_DEFINED
struct timeval  { int tv_sec; int tv_usec; };
#endif
#ifndef _AMUNOS_STRUCT_TIMESPEC_DEFINED
#define _AMUNOS_STRUCT_TIMESPEC_DEFINED
struct timespec { int tv_sec; int tv_nsec; };
#endif
#ifndef _AMUNOS_STRUCT_STAT_DEFINED
#define _AMUNOS_STRUCT_STAT_DEFINED
struct stat {
    unsigned int   st_dev;
    unsigned int   st_ino;
    unsigned short st_mode;
    unsigned short st_nlink;
    unsigned short st_uid;
    unsigned short st_gid;
    unsigned int   st_rdev;
    unsigned int   st_size;
    unsigned int   st_blksize;
    unsigned int   st_blocks;
    unsigned int   st_atime;
    unsigned int   st_atime_nsec;
    unsigned int   st_mtime;
    unsigned int   st_mtime_nsec;
    unsigned int   st_ctime;
    unsigned int   st_ctime_nsec;
    unsigned int   __unused4;
    unsigned int   __unused5;
};
#endif
#ifndef _AMUNOS_STRUCT_DIRENT_DEFINED
#define _AMUNOS_STRUCT_DIRENT_DEFINED
#define DIRENT_NAME_MAX 256
#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8
struct dirent {
    unsigned int   d_ino;
    unsigned char  d_type;
    unsigned char  __pad[3];
    char           d_name[DIRENT_NAME_MAX];
};
#endif

/* ── open() 标志 (与内核 sys_open 的 0x40/0x200 一致) ── */
#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2
#define O_ACCMODE 3
#define O_CREAT   0100
#define O_TRUNC   01000
#define O_APPEND  02000
#define O_NONBLOCK 0x800
#define O_BINARY  0

#define F_GETFL   3
#define F_SETFL   4

/* ── lseek whence ── */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* access(2) 模式位 */
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

/* stat 模式位 */
#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFBLK  0060000
#define S_IFIFO  0010000
#define S_IFLNK  0120000
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)

/* ── 核心 syscall (真实现) ── */
static inline int  sys_open(const char *path, int flags)      { return (int)syscall2(SYS_OPEN, (long)path, flags); }
static inline int  sys_close(int fd)                          { return (int)syscall1(SYS_CLOSE, fd); }
static inline long sys_read(int fd, void *buf, unsigned int len)  { return syscall3(SYS_READ, fd, (long)buf, len); }
static inline long sys_write(int fd, const void *buf, unsigned int len) { return syscall3(SYS_WRITE, fd, (long)buf, len); }
static inline long sys_lseek(int fd, int offset, int whence)  { return syscall3(SYS_LSEEK, fd, offset, whence); }
static inline long sys_brk(void *addr)                        { return syscall1(SYS_BRK, (long)addr); }
static inline void sys_exit(int status)                       { syscall1(SYS_EXIT, status); }
static inline int  sys_getkey(void)                           { return (int)syscall0(SYS_GETKEY); }

/* ── 桩 (TCC 引用的 POSIX 表层, AMUNOS 暂未实现) ── */
static inline int  sys_stat(const char *path, struct stat *st)      { (void)path; (void)st; return -1; }
static inline int  sys_fstat(int fd, struct stat *st)               { (void)fd; (void)st; return -1; }
static inline int  sys_unlink(const char *path)                     { (void)path; return -1; }
static inline int  sys_rmdir(const char *path)                      { (void)path; return -1; }
static inline int  sys_rename(const char *old_p, const char *new_p) { (void)old_p; (void)new_p; return -1; }
static inline int  sys_mkdir(const char *path, unsigned int mode)   { (void)path; (void)mode; return -1; }
static inline int  sys_fork(void)                                   { return -1; }
static inline int  sys_wait4(int pid, int *status, int options)     { (void)pid; (void)status; (void)options; return -1; }
static inline int  sys_execve(const char *path, char *const argv[], char *const envp[]) { (void)path; (void)argv; (void)envp; return -1; }
static inline void *sys_mmap(void *addr, unsigned long len, int prot, int flags, int fd, long off)
                                                                    { (void)addr; (void)len; (void)prot; (void)flags; (void)fd; (void)off; return (void*)-1; }
static inline int  sys_munmap(void *addr, unsigned long len)        { (void)addr; (void)len; return -1; }
static inline int  sys_gettimeofday(struct timeval *tv)             { if (tv) { tv->tv_sec = 0; tv->tv_usec = 0; } return 0; }
static inline int  sys_clock_gettime(int clk, struct timespec *ts)  { (void)clk; (void)ts; return -1; }
static inline int  sys_getcwd(char *buf, unsigned int size)         { (void)buf; (void)size; return -1; }
static inline int  sys_chdir(const char *path)                      { (void)path; return -1; }
static inline int  sys_pipe(int pipefd[2])                          { (void)pipefd; return -1; }
static inline int  sys_dup2(int oldfd, int newfd)                   { (void)oldfd; (void)newfd; return -1; }
static inline int  sys_dup(int oldfd)                               { (void)oldfd; return -1; }
static inline int  sys_getpid(void)                                 { return 1; }
static inline int  sys_getppid(void)                                { return 1; }
static inline unsigned int sys_uptime(void)                         { return 0; }
static inline void sys_yield(void)                                  { }

#endif /* _AMUNOS_SYSCALL_H */
