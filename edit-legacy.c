/* edit.c — AMUNOS 屏幕文本编辑器 (FreeDOS EDIT 风格, 用户态 ELF, v6.5)
 *
 * 交叉编译为 EDIT.ELF, 放在 A:/B: 盘的文件系统里 —— 内核不再内置编辑器,
 * 编辑器就是系统里的一个程序: 输入 EDIT file 即运行 (CMDS.TXT 映射,
 * 或 cwd 下 EDIT.ELF 自动发现)。
 *
 * 功能键: F1=Help F2=Save F3=Open F4=New F5/ESC=Quit
 * 方向键/HOME/END/DEL; 行号; 状态栏; 消息栏。
 *
 * 实现要点:
 *   - 直接写 VGA 0xB8000 (Ring0 平坦内存, 用户程序同样可访问)
 *   - 硬件光标走 0x3D4/0x3D5 (outb)
 *   - 输入用 getkey 系统调用 (int 0x30 调用号 15, 无回显原始键码)
 *   - 文件用 libc fopen/fread/fwrite/fclose (底层 fd 层支持路径)
 */
#include <stdio.h>
#include <syscall.h>

#define EBUF 4096
#define VGA  0xB8000

/* ── getkey 返回的键码 (sys_getkey; 128+ 为功能键) ── */
#define KEY_ENTER  '\r'
#define KEY_BS     '\b'
#define KEY_ESC    27
#define KEY_DEL    127
#define KEY_CTRLC  3
#define KEY_LEFT   128
#define KEY_RIGHT  129
#define KEY_UP     130
#define KEY_DOWN   131
#define KEY_HOME   132
#define KEY_END    133
#define KEY_F1     134
#define KEY_F2     135
#define KEY_F3     136
#define KEY_F4     137
#define KEY_F5     138
#define KEY_PGUP   139
#define KEY_PGDN   140

static char e_buf[EBUF];
static int e_size, e_pos, e_top;
static int e_mod;                 /* 修改标记 */
static char e_name[16];

/* 前置声明 (串口镜像函数先于实现使用) */
static int e_line_off(int n);
static int e_line_len(int pos);

/* ── 直接写 VGA 字符 ── */
static void vga_char(int x, int y, char c, char color) {
    char* v = (char*)VGA + (y * 80 + x) * 2;
    v[0] = c; v[1] = color;
}

/* ── 硬件光标 (端口走 DX, 常量若用 "nd" 会被截成 8 位 → 错写到 0x00D4) ── */
static void outb_port(unsigned short port, unsigned char data) {
    __asm__ volatile ("outb %0, %1" : : "a"(data), "d"(port));
}
static void setcur(int x, int y) {
    unsigned short p = (unsigned short)(y * 80 + x);
    outb_port(0x3D4, 14);
    outb_port(0x3D5, (unsigned char)(p >> 8));
    outb_port(0x3D4, 15);
    outb_port(0x3D5, (unsigned char)(p & 0xFF));
}

/* ── 串口远程控制台镜像 (v6.5): 编辑器直写 VRAM 0xB8000, 串口控制台看不到;
 *    把"当前行 + 状态栏 + 消息栏 + 打开列表"镜像到 COM1 (0x3F8), 远程才能操作。
 *    串口输出用 outb 直写 (与内核 serial.c 相同的 115200 8N1), 等 THR 空防丢字。── */
static unsigned char ser_inb(unsigned short port) {
    unsigned char v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static void ser_putc(char c) {
    while ((ser_inb(0x3FD) & 0x20) == 0);   /* 等 THR 空; 无串口时 LSR 读 0xFF 不死锁 */
    __asm__ volatile ("outb %0, %1" : : "a"((unsigned char)c), "Nd"((unsigned short)0x3F8));
}
static void ser_puts(const char *s) {
    while (*s) { if (*s == '\n') ser_putc('\r'); ser_putc(*s); s++; }
}

/* ── 清除编辑器区域 (前 24 行) ── */
static void e_clear() {
    for (int y = 0; y < 24; y++)
        for (int x = 0; x < 80; x++) vga_char(x, y, ' ', 0x07);
}

/* ── 获取第 n 行的缓冲区偏移 ── */
static int e_line_off(int n) {
    int pos = 0;
    for (int l = 0; l < n && pos < e_size; pos++)
        if (e_buf[pos] == '\n') l++;
    return pos;
}

/* ── 总行数 (换行符个数; 末行为第 total_lines 行) ── */
static int e_total_lines() {
    int n = 0;
    for (int i = 0; i < e_size; i++)
        if (e_buf[i] == '\n') n++;
    return n;
}

/* ── 光标所在行号 / 列号 ── */
static int e_cur_row() {
    int r = 0;
    for (int i = 0; i < e_pos && i < e_size; i++)
        if (e_buf[i] == '\n') r++;
    return r;
}
static int e_cur_col() {
    int c = 0;
    for (int i = e_pos - 1; i >= 0 && e_buf[i] != '\n'; i--) c++;
    return c;
}

/* ── 重绘整个文本区 ── */
static void e_render() {
    int bi = e_line_off(e_top);
    for (int y = 0; y < 23; y++) {
        int x = 0;
        int ln = e_top + y;
        vga_char(x++, y, ' ', 0x07);
        if (ln < 100) {
            vga_char(x++, y, '0' + ln / 10, 0x07);
            vga_char(x++, y, '0' + ln % 10, 0x07);
        } else {
            vga_char(x++, y, '0' + ln / 100, 0x07);
            vga_char(x++, y, '0' + (ln / 10) % 10, 0x07);
            vga_char(x++, y, '0' + ln % 10, 0x07);
        }
        vga_char(x++, y, ' ', 0x07);
        for (; x < 80 && bi < e_size; x++) {
            char c = e_buf[bi++];
            if (c == '\n') { for (; x < 80; x++) vga_char(x, y, ' ', 0x07); break; }
            if (c == '\t') c = ' ';
            vga_char(x, y, c, 0x07);
        }
        for (; x < 80; x++) vga_char(x, y, ' ', 0x07);
    }
    /* 状态栏 (第 23 行) */
    for (int x = 0; x < 80; x++) vga_char(x, 23, ' ', 0x70);
    int ln = e_cur_row() + 1;
    vga_char(0, 23, '"', 0x70);
    int ni = 0; while (e_name[ni] && ni < 8) { vga_char(1 + ni, 23, e_name[ni], 0x70); ni++; }
    vga_char(10, 23, e_mod ? '*' : ' ', 0x70);
    vga_char(13, 23, 'L', 0x70); vga_char(14, 23, 'n', 0x70);
    vga_char(16, 23, '0' + ln / 100, 0x70); vga_char(17, 23, '0' + (ln / 10) % 10, 0x70); vga_char(18, 23, '0' + ln % 10, 0x70);
}

/* ── 滚动到光标 ── */
static void e_scroll_to_cursor() {
    int r = e_cur_row();
    if (r < e_top) e_top = r;
    if (r >= e_top + 22) e_top = r - 21;
    if (e_top < 0) e_top = 0;
}

/* ── 消息栏 (第 24 行) ── */
static void e_msg(char* s) {
    for (int x = 0; x < 80; x++) vga_char(x, 24, ' ', 0x07);
    int i = 0;
    while (s[i] && i < 78) { vga_char(i, 24, s[i], 0x07); i++; }
    setcur(0, 0);
    ser_puts("\n"); ser_puts(s); ser_puts("\n");   /* 串口镜像消息 */
}

/* ── 串口镜像: 光标所在行 + 状态栏 ── */
static void e_ser_refresh(void) {
    int row = e_cur_row();
    int ls = e_line_off(row);
    int len = e_line_len(ls);
    ser_puts("\n");
    for (int i = 0; i < len; i++) {
        char c = e_buf[ls + i];
        if (c == '\t') c = ' ';
        ser_putc(c);
    }
    ser_puts("\n\"");
    for (int k = 0; k < 8 && e_name[k]; k++) ser_putc(e_name[k]);
    ser_puts("   ");
    ser_putc(e_mod ? '*' : ' ');
    ser_puts("  Ln  ");
    int ln = row + 1;
    ser_putc('0' + ln / 100);
    ser_putc('0' + (ln / 10) % 10);
    ser_putc('0' + ln % 10);
    ser_puts("\n");
}

/* ── 打开文件后串口列出全部内容 (带行号) ── */
static void e_ser_dump(void) {
    int bi = 0, ln = 0;
    ser_puts("\n");
    while (bi < e_size && ln < 1000) {
        ser_putc('[');
        ser_putc('0' + (ln / 100) % 10); ser_putc('0' + (ln / 10) % 10); ser_putc('0' + ln % 10);
        ser_putc(']');
        while (bi < e_size && e_buf[bi] != '\n') { char c = e_buf[bi++]; if (c == '\t') c = ' '; ser_putc(c); }
        ser_puts("\n");
        if (bi < e_size && e_buf[bi] == '\n') bi++;
        ln++;
    }
}

/* ── 行长度 ── */
static int e_line_len(int pos) {
    int len = 0;
    while (pos + len < e_size && e_buf[pos + len] != '\n') len++;
    return len;
}

/* ── 加载文件: 返回 1=已存在, 0=新建 ── */
static int e_load(char* name) {
    FILE* f = fopen(name, "rb");
    if (!f) return 0;
    e_size = (int)fread(e_buf, 1, EBUF, f);
    fclose(f);
    return 1;
}

/* ── 保存 ── */
static void e_save() {
    FILE* f = fopen(e_name, "wb");
    if (!f) { e_msg("[save failed]"); return; }
    if (e_size > 0) fwrite(e_buf, 1, (unsigned int)e_size, f);
    fclose(f);
    e_mod = 0;
}

/* ── 底行读入文件名 (F3 打开用) ── */
static void e_read_name(char* out, int max) {
    int len = 0; out[0] = 0;
    for (int x = 0; x < 80; x++) vga_char(x, 24, ' ', 0x70);
    const char* lb = "Open: ";
    for (int i = 0; lb[i]; i++) vga_char(i, 24, lb[i], 0x70);
    ser_puts("\n"); ser_puts(lb);                 /* 串口镜像提示 */
    while (1) {
        int k = sys_getkey();
        if (k == KEY_ENTER) break;
        else if (k >= 32 && k < 127) {
            if (len < max - 1) {
                out[len] = (char)k;
                vga_char(6 + len, 24, (char)k, 0x70);
                ser_putc((char)k);                  /* 串口回显键入 */
                len++; out[len] = 0;
            }
        }
        else if (k == KEY_BS) {
            if (len > 0) { len--; out[len] = 0; vga_char(6 + len, 24, ' ', 0x70); ser_puts("\b \b"); }
        }
        else if (k == KEY_ESC || k == KEY_F5) { out[0] = 0; break; }
    }
}

/* ════════════════════════════════════════
   编辑器主循环
   ════════════════════════════════════════ */
int main(int argc, char** argv) {
    if (argc < 2 || !argv[1][0]) {
        printf("Usage: EDIT file\n");
        return 1;
    }
    int ni = 0;
    while (argv[1][ni] && ni < 14) { e_name[ni] = argv[1][ni]; ni++; }
    e_name[ni] = 0;

    e_size = 0; e_pos = 0; e_top = 0; e_mod = 0;
    e_clear();
    int r = e_load(e_name);
    e_render();
    e_scroll_to_cursor();
    e_render();
    e_msg(r ? "[edit]" : "[new file]");
    e_ser_dump();          /* 串口: 列出全部内容 */

    int running = 1;
    while (running) {
        int k = sys_getkey();

        e_scroll_to_cursor();
        int row = e_cur_row();
        int col = e_cur_col();
        int line_start = e_line_off(row);
        int line_len = e_line_len(line_start);

        if (k >= 32 && k < 127) {           /* 可打印字符: 插入 */
            if (e_size < EBUF - 1) {
                for (int i = e_size; i > e_pos; i--) e_buf[i] = e_buf[i - 1];
                e_buf[e_pos] = (char)k;
                e_size++; e_pos++;
                e_mod = 1;
                e_render();
            }
        }
        else if (k == KEY_ENTER) {          /* 回车 */
            if (e_size < EBUF - 1) {
                for (int i = e_size; i > e_pos; i--) e_buf[i] = e_buf[i - 1];
                e_buf[e_pos] = '\n';
                e_size++; e_pos++;
                e_mod = 1;
                e_render();
            }
        }
        else if (k == KEY_BS) {             /* 退格 */
            if (e_pos > 0) {
                for (int i = e_pos - 1; i < e_size - 1; i++) e_buf[i] = e_buf[i + 1];
                e_size--; e_pos--;
                e_mod = 1;
                e_render();
            }
        }
        else if (k == KEY_DEL) {            /* DEL */
            if (e_pos < e_size) {
                for (int i = e_pos; i < e_size - 1; i++) e_buf[i] = e_buf[i + 1];
                e_size--;
                e_mod = 1;
                e_render();
            }
        }
        else if (k == KEY_LEFT) { if (e_pos > 0) e_pos--; }
        else if (k == KEY_RIGHT) { if (e_pos < e_size) e_pos++; }
        else if (k == KEY_UP) {             /* 上移一行, 保持列 */
            int r = e_cur_row();
            if (r > 0) {
                int prev_start = e_line_off(r - 1);
                int prev_len = e_line_len(prev_start);
                int tc = col; if (tc > prev_len) tc = prev_len;
                e_pos = prev_start + tc;
            }
        }
        else if (k == KEY_DOWN) {           /* 下移一行, 保持列 */
            int r = e_cur_row();
            int next_start = e_line_off(r + 1);
            if (next_start < e_size) {
                int next_len = e_line_len(next_start);
                int tc = col; if (tc > next_len) tc = next_len;
                e_pos = next_start + tc;
            }
        }
        else if (k == KEY_HOME) { int r = e_cur_row(); e_pos = e_line_off(r); }
        else if (k == KEY_END)  { int r = e_cur_row(); int ls = e_line_off(r); e_pos = ls + e_line_len(ls); }
        else if (k == KEY_PGUP) {                   /* 上翻一页 (22 行) */
            e_top -= 22;
            if (e_top < 0) e_top = 0;
            e_pos = e_line_off(e_top);
            e_render();
        }
        else if (k == KEY_PGDN) {                   /* 下翻一页 (22 行) */
            int tl = e_total_lines();
            e_top += 22;
            if (e_top > tl) e_top = tl;
            e_pos = e_line_off(e_top);
            e_render();
        }
        else if (k == KEY_ESC || k == KEY_F5) {     /* 退出 (未保存则提示) */
            if (e_mod) e_msg("[unsaved! Press F2 to save]");
            else { running = 0; break; }
        }
        else if (k == KEY_CTRLC) {          /* Ctrl+C: 同退出 */
            if (e_mod) e_msg("[unsaved! Press F2 to save]");
            else { running = 0; break; }
        }
        else if (k == KEY_F1) { e_msg("F1=Help  F2=Save  F3=Open  F4=New  F5/ESC=Quit"); }
        else if (k == KEY_F2) { e_save(); e_msg("[saved]"); e_render(); }
        else if (k == KEY_F3) {             /* 打开 */
            char nb[16];
            e_read_name(nb, 16);
            if (nb[0]) {
                int ni2 = 0;
                while (nb[ni2] && ni2 < 14) { e_name[ni2] = nb[ni2]; ni2++; }
                e_name[ni2] = 0;
                int r2 = e_load(e_name);
                e_clear(); e_render();
                e_msg(r2 ? "[edit]" : "[new file]");
            } else e_render();
        }
        else if (k == KEY_F4) {             /* 新建 */
            e_size = 0; e_pos = 0; e_top = 0; e_mod = 1;
            e_clear(); e_render(); e_msg("[new file]");
        }

        e_scroll_to_cursor();
        int nr = e_cur_row() - e_top;
        if (nr < 0) nr = 0;
        if (nr > 23) nr = 23;
        setcur(e_cur_col() + 4, nr);
        e_ser_refresh();   /* 串口: 每次按键后镜像当前行 + 状态栏 */
    }

    /* 退出: 清全屏, 光标归零, 让 shell 提示符干净出现 */
    for (int y = 0; y < 25; y++)
        for (int x = 0; x < 80; x++) vga_char(x, y, ' ', 0x07);
    setcur(0, 0);
    return 0;
}
