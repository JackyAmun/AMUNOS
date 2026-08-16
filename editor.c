/* editor.c — AMUNOS 屏幕文本编辑器 (仿 vim 极简版) */

#include "common.h"

#define EBUF 4096
#define VGA  0xB8000

static char e_buf[EBUF];
static int e_size, e_pos, e_top;
static int e_mod;  // 修改标记
static char e_name[12];

/* ── 写 VGA 字符 ── */
static void vga_char(int x, int y, char c, char color) {
    char* v = (char*)VGA + (y * 80 + x) * 2;
    v[0] = c; v[1] = color;
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

/* ── 获取光标所在行号 ── */
static int e_cur_row() {
    int r = 0;
    for (int i = 0; i < e_pos && i < e_size; i++)
        if (e_buf[i] == '\n') r++;
    return r;
}

/* ── 获取光标在当前行的列号 ── */
static int e_cur_col() {
    int c = 0;
    for (int i = e_pos - 1; i >= 0 && e_buf[i] != '\n'; i--) c++;
    return c;
}

/* ── 更新硬件光标 ── */
static void e_setcur(int x, int y) {
    unsigned short p = (unsigned short)(y * 80 + x);
    io_out8(0x3D4, 14); io_out8(0x3D5, (p >> 8) & 0xFF);
    io_out8(0x3D4, 15); io_out8(0x3D5, p & 0xFF);
}

/* ── 重绘整个文本区 ── */
static void e_render() {
    int bi = e_line_off(e_top);
    for (int y = 0; y < 23; y++) {
        int x = 0;
        // 显示行号
        int ln = e_top + y;
        if (ln < 100) { vga_char(x++, y, ' ', 0x07); vga_char(x++, y, '0' + ln/10, 0x07); vga_char(x++, y, '0' + ln%10, 0x07); }
        else { vga_char(x++, y, '0' + ln/100, 0x07); vga_char(x++, y, '0' + (ln/10)%10, 0x07); vga_char(x++, y, '0' + ln%10, 0x07); }
        vga_char(x++, y, ' ', 0x07);
        // 显示文本
        for (; x < 80 && bi < e_size; x++) {
            char c = e_buf[bi++];
            if (c == '\n') { for (; x < 80; x++) vga_char(x, y, ' ', 0x07); break; }
            if (c == '\t') c = ' ';
            vga_char(x, y, c, 0x07);
        }
        for (; x < 80; x++) vga_char(x, y, ' ', 0x07);
    }
    // 状态栏 (第 23 行)
    for (int x = 0; x < 80; x++) vga_char(x, 23, ' ', 0x70);
    int ln = e_cur_row() + 1;
    vga_char(0, 23, '"', 0x70);
    int ni = 0; while (e_name[ni] && ni < 8) { vga_char(1+ni, 23, e_name[ni], 0x70); ni++; }
    vga_char(10, 23, e_mod ? '*' : ' ', 0x70);
    vga_char(12, 23, ' ', 0x70);
    vga_char(13, 23, 'L', 0x70); vga_char(14, 23, 'n', 0x70);
    vga_char(15, 23, ' ', 0x70);
    vga_char(16, 23, '0' + ln/100, 0x70); vga_char(17, 23, '0' + (ln/10)%10, 0x70); vga_char(18, 23, '0' + ln%10, 0x70);
}

/* ── 从光标处显示文件 (用于保存位置检查) ── */
static void e_scroll_to_cursor() {
    int r = e_cur_row();
    if (r < e_top) e_top = r;
    if (r >= e_top + 22) e_top = r - 21;
    if (e_top < 0) e_top = 0;
}

/* ── 加载文件 ── */
static int e_open(char* name) {
    // 复制文件名
    int i = 0; while (name[i] && i < 11) { e_name[i] = name[i]; i++; }
    e_name[i] = 0;
    e_size = 0; e_pos = 0; e_top = 0; e_mod = 0;

    FAT12Entry entry;
    if (fs_find_entry_in_dir(cwd_cluster, name, &entry) >= 0) {
        int sz = entry.size;
        if (sz > EBUF) sz = EBUF;
        fs_read_file(&entry, e_buf);
        e_size = sz;
        return 1;  // 已存在
    }
    return 0;  // 新建
}

/* ── 保存 ── */
static void e_save() {
    // 先删除旧文件再重建 (简化)
    fs_delete_file(e_name);
    if (e_size > 0)
        fs_create_file_in_dir(cwd_cluster, e_name, e_buf, e_size);
    e_mod = 0;
}

/* ── 状态栏消息 ── */
static void e_msg(char* s) {
    for (int x = 0; x < 80; x++) vga_char(x, 24, ' ', 0x07);
    int i = 0;
    while (s[i] && i < 78) { vga_char(i, 24, s[i], 0x07); i++; }
    e_setcur(0, 0);
}

/* ── 辅助：获取行长度 ── */
static int e_line_len(int pos) {
    int len = 0;
    while (pos + len < e_size && e_buf[pos + len] != '\n') len++;
    return len;
}

/* ════════════════════════════════════════
   编辑器主循环
   ════════════════════════════════════════ */
void start_editor(char* name) {
    e_open(name);
    e_clear();
    e_render();
    e_scroll_to_cursor();
    e_render();
    if (e_size == 0) e_msg("[new file]");
    else e_msg("[edit]");

    int running = 1;
    while (running) {
        // 等待键盘
        int kp = 0;
        while (kp == 0) { kbd_poll(); kp = key_pressed; }

        e_scroll_to_cursor();
        int row = e_cur_row();
        int col = e_cur_col();
        int line_start = e_line_off(row);
        int line_len = e_line_len(line_start);

        if (kp == 1) {  // 字符插入
            char c = current_char;
            if (c >= ' ' && e_size < EBUF - 1) {
                // 插入模式：后移字符
                for (int i = e_size; i > e_pos; i--) e_buf[i] = e_buf[i-1];
                e_buf[e_pos] = c;
                e_size++; e_pos++;
                e_mod = 1;
                e_render();
            }
        }
        else if (kp == 2) {  // 回车
            if (e_size < EBUF - 1) {
                for (int i = e_size; i > e_pos; i--) e_buf[i] = e_buf[i-1];
                e_buf[e_pos] = '\n';
                e_size++; e_pos++;
                e_mod = 1;
                e_render();
            }
        }
        else if (kp == 3) {  // 退格
            if (e_pos > 0) {
                for (int i = e_pos - 1; i < e_size - 1; i++) e_buf[i] = e_buf[i+1];
                e_size--; e_pos--;
                e_mod = 1;
                e_render();
            }
        }
        else if (kp == 9) {  // DEL
            if (e_pos < e_size) {
                for (int i = e_pos; i < e_size - 1; i++) e_buf[i] = e_buf[i+1];
                e_size--;
                e_mod = 1;
                e_render();
            }
        }
        else if (kp == 4) {  // ←
            if (e_pos > 0) e_pos--;
        }
        else if (kp == 5) {  // →
            if (e_pos < e_size) e_pos++;
        }
        else if (kp == 6) {  // ↑
            // 上移一行：跳到上一行相同列
            int r = e_cur_row();
            if (r > 0) {
                int prev_start = e_line_off(r - 1);
                int prev_len = e_line_len(prev_start);
                int target_col = col;
                if (target_col > prev_len) target_col = prev_len;
                e_pos = prev_start + target_col;
            }
        }
        else if (kp == 7) {  // ↓
            // 下移一行
            int r = e_cur_row();
            int next_start = e_line_off(r + 1);
            if (next_start < e_size) {
                int next_len = e_line_len(next_start);
                int target_col = col;
                if (target_col > next_len) target_col = next_len;
                e_pos = next_start + target_col;
            }
        }
        else if (kp == 8) {  // ESC — 命令模式
            // 在底行显示 ":"
            e_msg(":");
            int cmd_len = 0;
            char cmd_buf[16];

            // 读命令
            while (1) {
                int ckp = 0;
                while (ckp == 0) { kbd_poll(); ckp = key_pressed; }
                if (ckp == 2) {  // 回车 = 执行命令
                    cmd_buf[cmd_len] = 0;

                    if (strcmp(cmd_buf, "w") == 0) {
                        e_save(); e_msg("[saved]"); e_render();
                    }
                    else if (strcmp(cmd_buf, "q") == 0) {
                        if (!e_mod) { running = 0; break; }
                        e_msg("[unsaved! :q! to force]");
                        e_render();
                    }
                    else if (strcmp(cmd_buf, "q!") == 0) {
                        running = 0; break;
                    }
                    else if (strcmp(cmd_buf, "wq") == 0) {
                        e_save(); running = 0; break;
                    }
                    else if (strcmp(cmd_buf, "wq!") == 0) {
                        e_save(); running = 0; break;
                    }
                    else {
                        e_msg("[unknown command]");
                        e_render();
                    }
                    break;
                }
                else if (ckp == 1) {  // 输入命令字符
                    if (cmd_len < 14) {
                        cmd_buf[cmd_len] = current_char;
                        vga_char(1 + cmd_len, 24, current_char, 0x07);
                        cmd_len++;
                    }
                }
                else if (ckp == 3) {  // 退格
                    if (cmd_len > 0) {
                        cmd_len--;
                        vga_char(1 + cmd_len, 24, ' ', 0x07);
                    }
                }
                key_pressed = 0;
            }
            e_render();
            e_msg("");
        }
        else if (kp == 9) {  // Tab (备用)
        }

        key_pressed = 0;
        e_scroll_to_cursor();
        int new_row = e_cur_row() - e_top;
        if (new_row < 0) new_row = 0;
        if (new_row > 23) new_row = 23;
        e_setcur(e_cur_col() + 4, new_row);  // +4 因为行号
    }

    // 退出编辑器，恢复内核显示
    cls();
    print_prompt();
}
