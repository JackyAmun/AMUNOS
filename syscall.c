/* syscall.c — int 0x30 系统调用分发
 *
 * 程序调用: mov eax, 调用号; mov ebx, arg1; mov ecx, arg2; mov edx, arg3; int 0x30
 * 返回值在 EAX
 *
 * 调用号表:
 *   1  = putchar(char)          — 输出字符
 *   2  = getchar()              — 读一个字符 (阻塞)
 *   3  = puts(char* s)          — 输出字符串
 *   4  = putnum(int n)          — 输出数字
 *   5  = malloc(int size)       — 内核堆分配
 *   6  = free(void* ptr)        — 内核堆释放
 *   7  = sleep(int ticks)       — 睡眠
 *   8  = open(path, flags)      — 打开文件 → fd (>=3)
 *   9  = close(fd)              — 关闭 (脏则落盘)
 *   10 = read(fd, buf, len)     — 读
 *   11 = write(fd, buf, len)    — 写 (fd 1/2 = 控制台)
 *   12 = lseek(fd, off, whence) — 定位
 *   13 = exit(status)           — 结束当前程序任务
 *   14 = brk(addr)              — 用户堆断点 (0=查询)
 *   15 = getkey()               — 读原始键码 (阻塞, 无回显, 全屏编辑器用)
 *   16 = getmods()              — 读修饰键状态 (shift/ctrl/caps/alt)
 *   17 = readdir(path,idx,buf)  — 列目录第 idx 项 (1=文件 2=目录 0=结束 -1=错)
 *   18 = mouse(int out[3])      — 读鼠标: out[0]=按钮位 out[1]=字符列 out[2]=字符行
 *   20 = cursor(x,y)            — 软件输入光标 '|' 画到 (x,y) (EDIT/全屏程序)
 *   21 = curhide()              — 隐藏输入光标 (对话框弹出时)
 *   22 = curshow()              — 恢复显示输入光标
 *
 * fd 表: 0=stdin 1=stdout 2=stderr (控制台), 3+ = 文件 (内存缓冲 + 游标)
 */

#include "common.h"

extern void *mem_alloc(unsigned size);
extern void mem_free(void *ptr);
extern void task_sleep(int ticks);

volatile int force_kill = 0;
volatile int prog_active = 0;
volatile int prog_killed = 0;
int prog_exit_status = 0;

#define MAX_FD        16
#define USER_BRK_BASE 0x200000
#define USER_BRK_TOP  0x400000

typedef struct {
    int used;
    char name[13];          /* 8.3 文件名 (close 落盘用) */
    int dir;                /* 文件所在目录簇 (close 落盘用, 路径支持 v6.5) */
    int drive;              /* 文件所在盘 (v6.5.1: close 落盘目标盘, 修复跨盘写回) */
    unsigned char *buf;     /* 内存缓冲 */
    int size;               /* 逻辑大小 */
    int capacity;           /* 分配容量 */
    int pos;                /* 游标 */
    int dirty;              /* 写脏 */
} fd_t;

static fd_t fds[MAX_FD];
static unsigned user_break = USER_BRK_BASE;

/* ── 旧 syscall ── */
static int sys_putchar(int c) { put_char((char)c, 0x0F); return 0; }

/* 读一个键盘字符: 带回显 + 回车→'\n' + 退格→'\b'。
 * 阻塞等待; hlt 让出 CPU (定时器 10ms 唤醒后重试), 避免忙等空转。 */
static int kbd_read_char(void) {
    for (;;) {
        int kp = 0;
        while (kp == 0) {
            input_poll();
            kp = key_pressed;
            if (kp == 0) __asm__ volatile("hlt");
        }
        key_pressed = 0;
        if (kp == 1) {                 /* 可打印字符: 回显 */
            put_char(current_char, 0x0F);
            return current_char;
        }
        if (kp == 2) {                 /* 回车 → 换行 */
            put_char('\n', 0x07);
            return '\n';
        }
        if (kp == 3) {                 /* 退格: 视觉擦除 + 返回 '\b' */
            put_char('\b', 0x07);
            put_char(' ', 0x07);
            put_char('\b', 0x07);
            return '\b';
        }
        /* 其它键 (方向键/ESC 等) 忽略, 继续等 */
    }
}

static int sys_getchar() {
    return kbd_read_char();
}

static int sys_puts(char *s) { if (s) put_str(s); return 0; }
static int sys_putnum(int n) { put_num(n); return 0; }

static int sys_malloc(int size) {
    if (size <= 0) return 0;
    return (int)mem_alloc((unsigned)size);
}

static int sys_free(void *ptr) { mem_free(ptr); return 0; }
static int sys_sleep(int ticks) { task_sleep(ticks); return 0; }

/* ── 8. open ──
 * flags: O_RDONLY=0 O_WRONLY=1 O_RDWR=2 O_CREAT=64 O_TRUNC=512
 * 路径 (v6.5): 支持 "SUB\FILE.EXT" / "\ROOT\FILE" — 目录部分经 fs_resolve_path
 * 解析, 纯文件名在 cwd 中查找; 落盘目录记在 fd.dir, close 时写回那里。 */
static int sys_open(char *path, int flags) {
    if (!path) return -1;
    char full[64];
    int i;
    for (i = 0; path[i] && i < 62; i++) full[i] = path[i];
    full[i] = 0;

    /* v6.5.1: 盘符限定路径 "A:\..." — 切盘解析, 记入 fd.drive, 退出还原 */
    int saved_drive = current_drive_idx, saved_cwd = cwd_cluster;
    int drive = -1;
    { char *p = full; drive = parse_drive(&p);
      if (drive >= 0) {
          current_drive_idx = drive; fs_init();          /* 切盘 (cwd 清零 → X:path==X:\path) */
          { char *d = full; while (*p) *d++ = *p++; *d = 0; }   /* 手工左移剥前缀 */
      } }

    int has_sep = 0;
    for (i = 0; full[i]; i++)
        if (full[i] == '/') { has_sep = 1; break; }   /* v6.5.1: 严格只认 / */

    int dc = cwd_cluster;
    if (has_sep) {
        dc = fs_resolve_path(full);   /* full 变为纯文件名, dc=目录簇; -1=目录不存在 */
        if (dc < 0) goto fail;
    }

    char name[13];
    for (i = 0; i < 12 && full[i]; i++) name[i] = full[i];
    name[i] = 0;

    int fd = 3;
    while (fd < MAX_FD && fds[fd].used) fd++;
    if (fd >= MAX_FD) goto fail;

    fd_t *f = &fds[fd];
    for (i = 0; i < 12 && name[i]; i++) f->name[i] = name[i];
    f->name[i] = 0;
    f->dir = dc;                        /* close 落盘目录 */
    f->drive = (drive >= 0) ? drive : saved_drive;   /* close 落盘目标盘 */
    f->dirty = 0;
    f->pos = 0;

    int writable = (flags & 3) != 0;      /* O_WRONLY/O_RDWR */
    int trunc    = (flags & 0x200) != 0;  /* O_TRUNC */

    FAT12Entry e;
    int found = (fs_find_entry_in_dir(dc, name, &e) >= 0);

    if (!found) {
        if (writable || (flags & 0x40)) {  /* 写或 O_CREAT: 新建空文件 */
            f->capacity = 512;
            f->buf = (unsigned char*)mem_alloc(512);
            f->size = 0;
        } else {
            goto fail;                      /* 读不存在的文件 */
        }
    } else {
        int cap = (e.size + 511) & ~511;
        if (cap < 512) cap = 512;
        f->buf = (unsigned char*)mem_alloc((unsigned)cap + 1); /* +1 容纳 fs_read_file 的 '\0' 终止符 */
        f->capacity = cap;
        fs_read_file(&e, (char*)f->buf);
        f->size = e.size;
        if (writable && trunc) f->size = 0;
    }
    f->used = 1;
    goto ok;
fail:
    if (drive >= 0) { current_drive_idx = saved_drive; fs_init(); cwd_cluster = saved_cwd; }
    return -1;
ok:
    if (drive >= 0) { current_drive_idx = saved_drive; fs_init(); cwd_cluster = saved_cwd; }
    return fd;
}

/* ── 9. close ── */
static int sys_close(int fd) {
    if (fd < 3 || fd >= MAX_FD) return -1;
    fd_t *f = &fds[fd];
    if (!f->used) return -1;
    if (f->dirty) {
        int sd = current_drive_idx, sc = cwd_cluster;
        current_drive_idx = f->drive; fs_init();         /* 写回目标盘 */
        fs_write_file_in_dir(f->dir, f->name, (char*)f->buf, f->size);
        current_drive_idx = sd; fs_init(); cwd_cluster = sc;
    }
    mem_free(f->buf);
    f->used = 0;
    f->buf = 0;
    return 0;
}

/* ── 10. read ── */
static int sys_read(int fd, void *buf, int len) {
    if (fd == 0) {                       /* 控制台输入 (逐字符, 回车结束, 回显) */
        char *p = (char*)buf;
        for (int i = 0; i < len; i++) {
            char c = (char)kbd_read_char();
            p[i] = c;
            if (c == '\n' || c == '\r') return i + 1;
        }
        return len;
    }
    if (fd == 1 || fd == 2) return -1;
    if (fd < 3 || fd >= MAX_FD || !fds[fd].used) return -1;
    fd_t *f = &fds[fd];
    int avail = f->size - f->pos;
    if (avail <= 0) return 0;            /* EOF */
    if (len > avail) len = avail;
    for (int i = 0; i < len; i++) ((char*)buf)[i] = (char)f->buf[f->pos + i];
    f->pos += len;
    return len;
}

/* ── 11. write ── */
static int sys_write(int fd, const void *buf, int len) {
    if (fd == 1 || fd == 2) {            /* 控制台输出 */
        const char *p = (const char*)buf;
        for (int i = 0; i < len; i++) put_char(p[i], 0x0F);
        return len;
    }
    if (fd == 0) return -1;
    if (fd < 3 || fd >= MAX_FD || !fds[fd].used) return -1;
    fd_t *f = &fds[fd];
    int need = f->pos + len;
    if (need > f->capacity) {            /* 扩容 (512 对齐) */
        int newcap = (need + 511) & ~511;
        unsigned char *nb = (unsigned char*)mem_alloc((unsigned)newcap);
        if (!nb) return -1;
        for (int i = 0; i < f->size; i++) nb[i] = f->buf[i];
        mem_free(f->buf);
        f->buf = nb;
        f->capacity = newcap;
    }
    for (int i = 0; i < len; i++) f->buf[f->pos + i] = ((const char*)buf)[i];
    f->pos += len;
    if (f->pos > f->size) f->size = f->pos;
    f->dirty = 1;
    return len;
}

/* ── 12. lseek ── */
static int sys_lseek(int fd, int offset, int whence) {
    if (fd < 3 || fd >= MAX_FD || !fds[fd].used) return -1;
    fd_t *f = &fds[fd];
    int base;
    if (whence == 0) base = 0;           /* SEEK_SET */
    else if (whence == 1) base = f->pos; /* SEEK_CUR */
    else if (whence == 2) base = f->size;/* SEEK_END */
    else return -1;
    int np = base + offset;
    if (np < 0) np = 0;
    f->pos = np;
    return np;
}

/* ── 关闭所有已打开的文件 fd (程序退出时回收缓冲, 脏则落盘) ── */
static void close_all_fds(void) {
    for (int fd = 3; fd < MAX_FD; fd++) {
        fd_t *f = &fds[fd];
        if (!f->used) continue;
        if (f->dirty) {
            int sd = current_drive_idx, sc = cwd_cluster;
            current_drive_idx = f->drive; fs_init();         /* 写回目标盘 */
            fs_write_file_in_dir(f->dir, f->name, (char*)f->buf, f->size);
            current_drive_idx = sd; fs_init(); cwd_cluster = sc;
        }
        mem_free(f->buf);
        f->used = 0;
        f->buf = 0;
    }
}

/* ── 前台程序退出清理: 回收 fd 缓冲 + 重置用户堆断点 ──
 * 修复: 多次运行程序后 user_break 顶到 0x400000 导致后续 malloc 失败 */
static void prog_cleanup(void) {
    close_all_fds();
    user_break = USER_BRK_BASE;
}

/* ── 13. exit ── */
static int sys_exit(int status) {
    prog_exit_status = status;
    prog_killed = 0;
    prog_cleanup();
    task_exit_current();                 /* 永不返回 */
    return 0;
}

/* ── 定时器中断强制终止入口 ──
 * 由 task.c 的 timer_schedule 把前台程序任务的返回 EIP 重定向到这里,
 * 用于 CPU 密集死循环 (不调 syscall) 的 Ctrl+C 强杀。iret 进入, 无返回地址,
 * 直接标记 EXITED 并让出 CPU, shell 的 task_wait 会唤醒。 */
void force_terminate(void) {
    prog_killed = 1;
    prog_exit_status = 0;
    prog_cleanup();
    task_exit_current();                 /* 永不返回 */
}

/* ── 14. brk ── */
static int sys_brk(int addr) {
    if (addr == 0) return (int)user_break;
    if (addr < USER_BRK_BASE || addr > USER_BRK_TOP) return -1;
    user_break = (unsigned)addr;
    return addr;
}

/* ── 17. readdir — 枚举目录 (v6.6, EDIT 文件对话框用) ──
 * sys_readdir(path, idx, name_out)
 *   path   : 目录路径 ("A:/USR" "/SRC" ""/"/"=根); 空串与 "/" 都列根
 *   idx    : 有效条目序号 (0 起; fs_list_dir 已跳过 0xE5 删除项与 LFN)
 *   返回   : 1 = 第 idx 项是普通文件 (name_out 填 "NAME.EXT")
 *            2 = 第 idx 项是子目录
 *            0 = 目录已列完 / idx 越界
 *           -1 = 路径不存在 / 不是目录 */
static int sys_readdir(char *path, int idx, char *name_out) {
    char full[64];
    int i;
    if (!path || !*path) full[0] = 0;
    else { for (i = 0; path[i] && i < 62; i++) full[i] = path[i]; full[i] = 0; }

    int saved_drive = current_drive_idx, saved_cwd = cwd_cluster;
    int drive = -1;
    { char *p = full; drive = parse_drive(&p);
      if (drive >= 0) {
          current_drive_idx = drive; fs_init();     /* 切盘 (cwd 清零 → X:path==X:\path) */
          { char *d = full; while (*p) *d++ = *p++; *d = 0; }
      } }

    int dc = cwd_cluster;
    if (full[0] && !(full[0] == '/' && !full[1])) {
        /* 非空 / 非 "/": 把最后一段解析成目录本身 */
        int d = cwd_cluster;
        int has_sep = 0;
        for (i = 0; full[i]; i++)
            if (full[i] == '/') { has_sep = 1; break; }
        if (has_sep) {
            d = fs_resolve_path(full);              /* full 原地变最后一段 */
            if (d < 0) goto fail;
        }
        FAT12Entry e;
        if (fs_find_entry_in_dir(d, full, &e) < 0 || !(e.attr & 0x10))
            goto fail;                              /* 不存在或不是目录 */
        dc = e.start_cluster;
    }

    /* 列出目录, 取第 idx 项 */
    FAT12Entry ents[64];
    int n = fs_list_dir(dc, ents, 64);
    if (idx < 0 || idx >= n) goto done;             /* 0 = 已列完 */

    /* 8.3 → "NAME.EXT": 名字/扩展去尾空格, 扩展非全空格才拼 '.' */
    FAT12Entry *e = &ents[idx];
    char *d = name_out;
    for (i = 0; i < 8 && e->name[i] && e->name[i] != ' '; i++) *d++ = e->name[i];
    if (e->ext[0] && e->ext[0] != ' ') {
        *d++ = '.';
        for (i = 0; i < 3 && e->ext[i] && e->ext[i] != ' '; i++) *d++ = e->ext[i];
    }
    *d = 0;

    if (drive >= 0) { current_drive_idx = saved_drive; fs_init(); cwd_cluster = saved_cwd; }
    return (e->attr & 0x10) ? 2 : 1;
fail:
    if (drive >= 0) { current_drive_idx = saved_drive; fs_init(); cwd_cluster = saved_cwd; }
    return -1;
done:
    if (drive >= 0) { current_drive_idx = saved_drive; fs_init(); cwd_cluster = saved_cwd; }
    return 0;
}

/* ── 15. getkey — 读原始键码 (阻塞, 无回显; 供编辑器等全屏程序) ──
 * 返回: 32..126 可打印字符; '\r' 回车; '\b' 退格; 27 ESC; 127 DEL;
 *       1..26 Ctrl+A..Z 控制码 (Ctrl+字母); 3 Ctrl+C; 128+ 方向/Home/End/F1-F5;
 *       141 INS。128+ 值见 edit.c 的常量。
 * 程序显式读 Ctrl+C: 该键由程序接管, 清掉 force_kill, 避免下一次
 * fopen/syscall 被强制终止逻辑误杀。 */
static int sys_getkey(void) {
    int kp = 0;
    while (kp == 0) {
        input_poll();
        kp = key_pressed;
        if (kp == 0) __asm__ volatile("hlt");
    }
    key_pressed = 0;
    if (kp == 12) force_kill = 0;
    switch (kp) {
    case 1:  return current_char;        /* 可打印字符 */
    case 2:  return '\r';
    case 3:  return '\b';
    case 4:  return 128;                 /* ← */
    case 5:  return 129;                 /* → */
    case 6:  return 130;                 /* ↑ */
    case 7:  return 131;                 /* ↓ */
    case 8:  return 27;                  /* ESC */
    case 9:  return 127;                 /* DEL */
    case 10: return 132;                 /* HOME */
    case 11: return 133;                 /* END */
    case 12: return 3;                   /* Ctrl+C */
    case 13: return 134;                 /* F1 */
    case 14: return 135;                 /* F2 */
    case 15: return 136;                 /* F3 */
    case 16: return 137;                 /* F4 */
    case 17: return 138;                 /* F5 */
    case 18: return 139;                 /* PgUp */
    case 19: return 140;                 /* PgDn */
    case 20: return 141;                 /* INS (0.7 编辑器插入切换) */
    }
    return 0;
}

/* ── 分发器 (由 head.asm 的 asm_syscall_handler 调用) ──
 * frame: [4]=edi [5]=esi [6]=ebp [7]=esp [8]=ebx [9]=edx [10]=ecx [11]=eax */
void syscall_handler(unsigned *frame) {
    int num = frame[11];       /* eax = 调用号 */
    int a1 = frame[8];         /* ebx = arg1 */
    int a2 = frame[10];        /* ecx = arg2 */
    int a3 = frame[9];         /* edx = arg3 */

    /* Ctrl+C 强制终止: 前台程序运行时, 任何 syscall 都是中止点 */
    if (force_kill && prog_active) {
        force_kill = 0;
        prog_killed = 1;
        prog_cleanup();
        task_exit_current();   /* 永不返回 */
    }

    int result = 0;
    switch (num) {
    case 1:  result = sys_putchar(a1); break;
    case 2:  result = sys_getchar(); break;
    case 3:  result = sys_puts((char*)a1); break;
    case 4:  result = sys_putnum(a1); break;
    case 5:  result = sys_malloc(a1); break;
    case 6:  result = sys_free((void*)a1); break;
    case 7:  result = sys_sleep(a1); break;
    case 8:  result = sys_open((char*)a1, a2); break;
    case 9:  result = sys_close(a1); break;
    case 10: result = sys_read(a1, (void*)a2, a3); break;
    case 11: result = sys_write(a1, (void*)a2, a3); break;
    case 12: result = sys_lseek(a1, a2, a3); break;
    case 13: sys_exit(a1); result = 0; break;
    case 14: result = sys_brk(a1); break;
    case 15: result = sys_getkey(); break;
    case 16: result = is_shift | (is_ctrl<<1) | (caps_lock<<2) | (is_alt<<3); break;  /* SYS_GETMODS */
    case 17: result = sys_readdir((char*)a1, a2, (char*)a3); break;  /* SYS_READDIR */
    case 18: {   /* SYS_MOUSE: 读鼠标状态到 int out[3] */
        int *o = (int*)a1;
        if (o) {
            o[0] = mouse_buttons_state();
            o[1] = mouse_char_x();
            o[2] = mouse_char_y();
        }
        /* 每次轮询都重画鼠标指针 '█' — 用户程序重绘整屏后指针仍在 (v6.7) */
        vga_mouse_redraw();
        result = 0;
        break;
    }
    case 20: {   /* SYS_CURSOR: 软件输入光标 '|' 定位 (x,y), 屏幕格坐标 */
        soft_cursor_at(a1, a2);
        result = 0;
        break;
    }
    case 21:     /* SYS_CURHIDE: 隐藏输入光标 */
        soft_cursor_hide();
        result = 0;
        break;
    case 22:     /* SYS_CURSHOW: 恢复输入光标 */
        soft_cursor_show();
        result = 0;
        break;
    case 19: {   /* SYS_KEYHIT: 非阻塞按键查询 — 轮询一次键盘/串口,
                  * 有键待读返回 1, 无键返回 0 (不阻塞)。
                  * FreeDOS Edit 事件循环靠它区分"有键才 getkey()",
                  * 空转时继续轮询鼠标 (v6.7 鼠标联调)。 */
        input_poll();
        result = (key_pressed != 0);
        break;
    }
    default: result = -1; break;
    }

    frame[11] = result;        /* 结果写回 eax 槽 */
}
