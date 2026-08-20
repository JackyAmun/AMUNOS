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
#define SYS_GETMODS  16
#define SYS_READDIR  17
#define SYS_MOUSE    18
#define SYS_KEYHIT   19
#define SYS_CURSOR   20
#define SYS_CURHIDE  21
#define SYS_CURSHOW  22
#define SYS_MOUSEHIDE 23
#define SYS_MOUSESHOW 24
#define SYS_VIDEO_BASE 25   /* 当前文本缓冲基址 (图形模式=softbuf; EDIT 写屏用) */
#define SYS_UTF8TOGB  26    /* Unicode 码点 → GB2312 码 (U2GB 表; 0=不在字库) */
#define SYS_CJKWCHAR  27    /* 绝对格 (x,y) 放汉字: packed=gb|(attr<<16) */

/* ── GUI 窗口服务器 (v6.9, 内核 gui.c) ── */
#define SYS_GUI_ENTER     28   /* 置 gui_active, 接管屏幕 */
#define SYS_GUI_LEAVE     29   /* 关闭所有窗, 回文本渲染 */
#define SYS_GUI_WIN       30   /* 建窗(x,y,w,h,title) → id */
#define SYS_GUI_WIN_CLOSE 31   /* 关窗(id) */
#define SYS_GUI_WIN_RAISE 32   /* 置顶(id) */
#define SYS_GUI_WND_TEXT  33   /* 设控件文本(win,ctl,str) */
#define SYS_GUI_BTN       34   /* 建按钮(win,cx,cy,label) → ctl */
#define SYS_GUI_LBL       35   /* 建标签(win,x,y,text) → ctl */
#define SYS_GUI_EDIT      36   /* 建输入框(win,cx,cy,width_px) → ctl */
#define SYS_GUI_LIST      37   /* 建列表(win,x,y,w,h) → ctl */
#define SYS_GUI_LIST_SET  38   /* 追加/清空列表项(win,ctl,str) */
#define SYS_GUI_FILL      39   /* 填色(win|color<<16, pack(x,y), pack(w,h)) */
#define SYS_GUI_TEXT      40   /* 像素文本(win,pack(x,y),str) */
#define SYS_GUI_DIALOG    41   /* 弹窗(parent,w,h,title) → 新窗 id */
#define SYS_GUI_EDITCHAR  42   /* 输入框编辑(win,ctl,ch): 打印字符光标处插入 / '\b'退格
                                   128← 129→ 132HOME 133END 127DEL → 新长 */
#define SYS_GUI_EVENTS    43   /* 取一批 gui_ev_t{type,win,ctl,ch} → 个数 */
#define SYS_GUI_TAREA     44   /* 多行文本区(win,pack(x,y),pack(w,h)) → ctl */
#define SYS_GUI_TAREA_SET 45   /* 设文本区内容(win|ctl<<8, buf, len) */
#define SYS_GUI_TAREA_GET 46   /* 读回文本区内容(win|ctl<<8, buf, max) → 字节数 */

/* 事件类型 (gui_ev_t.type) */
#define GEV_CLICK 1
#define GEV_KEY   2
#define GEV_ENTER 3
#define GEV_CLOSE 4   /* 标题栏 ✕ 关闭: 内核已关窗, 程序可据此清理/决定退出 */

typedef struct {
    int type;   /* GEV_CLICK / GEV_KEY / GEV_ENTER / GEV_CLOSE */
    int win;    /* 窗口 id */
    int ctl;    /* 控件 id (列表点击: ch 为选中项索引) */
    int ch;     /* GEV_KEY: 键入字符; GEV_CLICK 列表: 项索引 */
} gui_ev_t;

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
static inline void sys_sleep(int ticks)                       { syscall1(SYS_SLEEP, ticks); }
/* 修饰键状态: bit0=shift bit1=ctrl bit2=caps bit3=alt (编辑器块选用) */
static inline int  sys_getmods(void)                          { return (int)syscall0(SYS_GETMODS); }
/* 列目录第 idx 项 (0 起): 返回 1=文件 2=目录 0=列完 -1=错; name_out 填 "NAME.EXT" */
static inline int  sys_readdir(const char *path, int idx, char *name_out) {
    return (int)syscall3(SYS_READDIR, (long)path, idx, (long)name_out);
}
/* 读鼠标: out[0]=按钮位 (bit0 左 bit1 右) out[1]=字符列 0-79 out[2]=字符行 0-24 */
static inline int  sys_mouse(int *out) {
    return (int)syscall1(SYS_MOUSE, (long)out);
}
/* 非阻塞按键查询: 1=有键待读 (随后 sys_getkey 不阻塞) 0=无键 (事件循环继续轮询鼠标) */
static inline int  sys_keyhit(void) {
    return (int)syscall0(SYS_KEYHIT);
}
/* 软件输入光标 '|' (内核 0xB8000 叠加层, v6.7): 屏幕格坐标 0-79 / 0-24 */
static inline int  sys_cur(int x, int y) {
    return (int)syscall2(SYS_CURSOR, x, y);
}
static inline int  sys_curhide(void) {
    return (int)syscall0(SYS_CURHIDE);
}
static inline int  sys_curshow(void) {
    return (int)syscall0(SYS_CURSHOW);
}
static inline int  sys_mousehide(void) {
    return (int)syscall0(SYS_MOUSEHIDE);
}
static inline int  sys_mouseshow(void) {
    return (int)syscall0(SYS_MOUSESHOW);
}
static inline long sys_video_base(void) {   /* 当前文本缓冲基址 (图形模式=softbuf) */
    return syscall0(SYS_VIDEO_BASE);
}
static inline unsigned sys_utf8togb(unsigned cp) {  /* Unicode 码点 → GB2312 (0=无) */
    return (unsigned)syscall1(SYS_UTF8TOGB, (long)cp);
}
static inline void sys_cjkwchar(int x, int y, unsigned gb, int fg, int bg) {
    /* 绝对格 (x,y) 放汉字占两格; fg/bg = VGA 前景/背景 (0-15/0-7) */
    unsigned attr = (unsigned)((fg & 0x0F) | ((bg & 0x07) << 4));
    syscall3(SYS_CJKWCHAR, x, y, (long)(gb | (attr << 16)));
}

/* ── GUI 窗口服务器包装 (v6.9) ── */
static inline int sys_gui_enter(void)      { return (int)syscall0(SYS_GUI_ENTER); }
static inline int sys_gui_leave(void)      { return (int)syscall0(SYS_GUI_LEAVE); }
static inline int sys_gui_win(int x, int y, int w, int h, const char *title) {
    return (int)syscall3(SYS_GUI_WIN, (long)((x & 0xFFFF) | ((unsigned)y << 16)),
                         (long)((w & 0xFFFF) | ((unsigned)h << 16)), (long)title);
}
static inline int sys_gui_win_close(int id)  { return (int)syscall1(SYS_GUI_WIN_CLOSE, id); }
static inline int sys_gui_win_raise(int id)  { return (int)syscall1(SYS_GUI_WIN_RAISE, id); }
static inline int sys_gui_wnd_text(int win, int ctl, const char *str) {
    return (int)syscall3(SYS_GUI_WND_TEXT, win, ctl, (long)str);
}
static inline int sys_gui_btn(int win, int cx, int cy, const char *label) {
    return (int)syscall3(SYS_GUI_BTN, win, (long)((cx & 0xFFFF) | ((unsigned)cy << 16)), (long)label);
}
static inline int sys_gui_lbl(int win, int x, int y, const char *str) {
    return (int)syscall3(SYS_GUI_LBL, win, (long)((x & 0xFFFF) | ((unsigned)y << 16)), (long)str);
}
static inline int sys_gui_edit(int win, int cx, int cy, int w) {
    return (int)syscall3(SYS_GUI_EDIT, win, (long)((cx & 0xFFFF) | ((unsigned)cy << 16)), w);
}
static inline int sys_gui_list(int win, int x, int y, int w, int h) {
    return (int)syscall3(SYS_GUI_LIST, win, (long)((x & 0xFFFF) | ((unsigned)y << 16)),
                         (long)((w & 0xFFFF) | ((unsigned)h << 16)));
}
static inline int sys_gui_list_set(int win, int ctl, const char *str) {
    return (int)syscall3(SYS_GUI_LIST_SET, win, ctl, (long)str);
}
static inline int sys_gui_fill(int win, int x, int y, int w, int h, unsigned short color) {
    return (int)syscall3(SYS_GUI_FILL, (long)((win & 0xFFFF) | ((unsigned)color << 16)),
                         (long)((x & 0xFFFF) | ((unsigned)y << 16)),
                         (long)((w & 0xFFFF) | ((unsigned)h << 16)));
}
static inline int sys_gui_text(int win, int x, int y, const char *str) {
    return (int)syscall3(SYS_GUI_TEXT, win, (long)((x & 0xFFFF) | ((unsigned)y << 16)), (long)str);
}
static inline int sys_gui_dialog(int parent, int w, int h, const char *title) {
    return (int)syscall3(SYS_GUI_DIALOG, parent, (long)((w & 0xFFFF) | ((unsigned)h << 16)), (long)title);
}
static inline int sys_gui_editchar(int win, int ctl, int ch) {
    return (int)syscall3(SYS_GUI_EDITCHAR, win, ctl, ch);
}
static inline int sys_gui_events(gui_ev_t *ev, int max) {
    return (int)syscall2(SYS_GUI_EVENTS, (long)ev, max);
}
static inline int sys_gui_tarea(int win, int x, int y, int w, int h) {
    return (int)syscall3(SYS_GUI_TAREA, win,
                         (long)((x & 0xFFFF) | ((unsigned)y << 16)),
                         (long)((w & 0xFFFF) | ((unsigned)h << 16)));
}
static inline int sys_gui_tarea_set(int win, int ctl, const char *str, int len) {
    return (int)syscall3(SYS_GUI_TAREA_SET, (long)((win & 0xFF) | ((unsigned)ctl << 8)),
                         (long)str, len);
}
static inline int sys_gui_tarea_get(int win, int ctl, char *buf, int max) {
    return (int)syscall3(SYS_GUI_TAREA_GET, (long)((win & 0xFF) | ((unsigned)ctl << 8)),
                         (long)buf, max);
}

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
