#ifndef COMMON_H
#define COMMON_H

// --- 1. 基础 I/O ---
static inline void io_out8(unsigned short port, unsigned char data) {
    __asm__ volatile ("outb %0, %1" : : "a"(data), "nd"(port));
}
static inline unsigned char io_in8(unsigned short port) {
    unsigned char data;
    __asm__ volatile ("inb %1, %0" : "=a"(data) : "nd"(port));
    return data;
}

static inline void io_out16(unsigned short port, unsigned short data) {
    __asm__ volatile ("outw %0, %1" : : "a"(data), "nd"(port));
}
static inline unsigned short io_in16(unsigned short port) {
    unsigned short data;
    __asm__ volatile ("inw %1, %0" : "=a"(data) : "nd"(port));
    return data;
}

// --- 2. FAT12 结构定义 ---
typedef struct {
    char name[8];
    char ext[3];
    unsigned char attr;
    unsigned char reserved[10];
    unsigned short time;
    unsigned short date;
    unsigned short start_cluster;
    unsigned int size;
} __attribute__((packed)) FAT12Entry;

// --- 3. 全局变量 ---
extern volatile int is_shift;
extern volatile int caps_lock;
extern volatile int is_ctrl;
extern volatile int is_alt;
extern volatile int key_pressed;
extern volatile char current_char;

extern int cur_x, cur_y;
extern int cmd_len, cmd_pos;
extern char cmd_buf[128];

// 文件系统全局状态
extern int current_drive_idx; // 0=A(主盘) 1=B(从盘) 2=C(次主盘) 3=D(次从盘)
extern int cwd_cluster;       // 当前目录起始簇 (0=根目录)
extern char cwd_path[128];    // 当前路径字符串

// 导出 FS 布局参数供 DIR 命令使用
extern int fs_root_lba;
extern int fs_root_entries;
extern int fs_data_lba;
extern int fs_spc;        // 每簇扇区数 (BPB off 13; v6.5.1 FAT16)
extern int fs_fat_bits;   // FAT 位宽 12/16 (v6.5.1 自动识别)

extern void put_num(unsigned int n);
extern void update_cursor();

// --- 4. 磁盘底层接口 (ASM) ---
// 修改：增加 drive_idx 参数
extern int read_sector_asm(unsigned int lba, void* buf, int drive_idx);
extern int write_sector_asm(unsigned int lba, void* buf, int drive_idx);

// --- 5. 文件系统接口 (fs.c) ---
void fs_init();
void fs_sync();
int fs_find_entry(char* name, FAT12Entry* out_entry);
int fs_find_entry_in_dir(int dir_cluster, char* name, FAT12Entry* out_entry);
void fs_read_file(FAT12Entry* entry, char* buffer);
void fs_create_directory(char* dirname);
void fs_delete_directory(char* dirname);
int fs_create_file_in_dir(int dir_cluster, char* name, char* data, int size);
void fs_delete_file(char* filename);
int fs_delete_file_in_dir(int dir_cluster, char* name);
int fs_resolve_path(char* path);
int fs_write_file_in_dir(int dir_cluster, char* name, char* data, int size);
int fs_list_dir(int dir_cluster, FAT12Entry* out_buf, int max_entries);
unsigned short fat12_get_next_cluster(unsigned short cluster);
int fs_dir_secs(int dc);
int fs_dir_lba(int dc, int idx);
unsigned int fs_cluster_lba(unsigned int c);   // 簇 → 数据区首扇 LBA (FAT16 每簇多扇)
void to_fat12_name(char* src, char* dest);

// --- 5.1 盘符限定路径 (v6.5.1) ---
typedef struct { int drive; int cwd; } drive_ctx_t;
int parse_drive(char **pp);
drive_ctx_t fs_drive_enter(int drive);
void fs_drive_restore(drive_ctx_t c);
int fs_drive_open(char *path, drive_ctx_t *ctx);
int is_cmds_file(char *fat11);
int fs_drive_present(int d);

// --- 6. 其他模块声明 ---
int strcmp(const char *s1, const char *s2);
int strlen(const char *s);
void strcpy(char *dst, const char *src);
char to_upper(char c);  // 添加这一行
char drive_letter(void);
void cmd_ver();         // 添加这一行

void put_char(char c, char color);
void put_str(char *s);
void cls();

// --- 6.1 软件叠加层 (vga.c, v6.7): 输入光标 | + 鼠标指针 █ ---
// update_cursor() 隐藏硬件文本光标并重绘两个叠加。
// 用户程序 (EDIT) 用 soft_cursor_at/hide/show 定位输入光标。
void vga_overlay_refresh(void);
void vga_overlay_selfheal(void);   // 定时器每 tick 重铺 (EDIT 重绘抹掉后自愈)
void vga_mouse_redraw(void);
void vga_poke(int x, int y, unsigned char ch, unsigned char attr);
void soft_cursor_at(int x, int y);
void soft_cursor_hide(void);
void soft_cursor_show(void);
void soft_mouse_hide(void);           // 隐藏鼠标叠加 (getvideo 捕获背景期间)
void soft_mouse_show(void);
void vga_enable_softbuf(void);        // 图形模式: vram 切到软件缓冲 (v6.8)
const unsigned char *vga_textbuf(void); // 渲染器读取的文本缓冲
unsigned long vga_vram_base(void);     // SYS_VIDEO_BASE: 当前文本缓冲基址 (v6.8)
void vga_cjk_set(int x, int y, unsigned gb); // 汉字占两格, 记 GB 码 (v6.8 中文)
void vga_cjk_box(int x, int y);       // 替换框 □ 占两格 (v6.8 UTF-8 未收录字)
void vga_cjk_ascii(int x, int y);     // 写 ASCII 前清该格汉字标记
unsigned short vga_cjk_at(int x, int y); // 渲染器查询格标记 (0=ASCII/GB=汉字左/0xFFFF=右)
void vga_cjk_clear_all(void);         // 用户程序启动前清空 (防鬼影汉字)
void put_cjk_str(const unsigned char *s, char color); // GB2312 感知输出 (v6.8)
void vga_cjk_place_gb(int x, int y, unsigned gb, int fg, int bg); // v6.8.1 SYS_CJKWCHAR: 绝对格放汉字

void init_idt();
void keyboard_init();
void kbd_poll();
void input_poll();
void exec_cmd(char *cmd_line);
void print_prompt();

// --- 7. 内存管理 (mem.c) ---
void mem_init();
void *mem_alloc(unsigned size);
void mem_free(void *ptr);

// --- 8. 任务调度 (task.c) ---
struct task;
void timer_init();
void task_init();
struct task *task_create(void (*fn)(void), unsigned stack_size);
void task_sleep(int ticks);
void task_exit_current(void);
void task_wait(struct task *t);
void task_set_prog(struct task *t);
unsigned task_ticks();
void *timer_schedule(unsigned *frame);

// --- 9. ELF 加载器 (elf.c) ---
int elf_load(unsigned char *buf, int size);

// --- 10. 用户程序控制 (前台程序任务 + 强制终止) ---
extern volatile int force_kill;   // Ctrl+C 置 1
extern volatile int prog_active;  // 前台程序任务运行中
extern volatile int prog_killed;  // 前台程序被 Ctrl+C 强杀
extern int prog_exit_status;      // SYS_EXIT 的退出码
void force_terminate(void);       // 定时器中断重定向入口 (CPU 密集死循环强杀)

// --- 11. FS 覆盖写 (syscall fd 层落盘用) ---
void fs_write_file(char* name, char* data, int size);

// --- 12. 串口/并口 (serial.c) ---
void serial_init(void);
void serial_putc(char c);
void serial_puts(char *s);
int  serial_getc(void);
void lpt_putc(char c);
void lpt_puts(char *s);

// --- 12.0 软件文本渲染器 (fb.c, v6.8 中文): VBE 帧缓冲 + HZK16 ---
int fb_active(void);                              // 图形渲染器是否启用
void fb_init(void);                               // 读 boot.asm 0x1500 fb 参数
void fb_font_init(void);                          // 从 C:HZK16 加载字库到堆
void fb_render(void);                             // 0xB8000 → 帧缓冲 (定时器钩子)
void fb_put_str_cjk(int cellx, int celly,
                    const unsigned char *s, int fg, int bg);  // GB2312 演示
unsigned fb_uni_to_gb(unsigned uni);   // Unicode→GB2312 二分查找 (v6.8 UTF-8)
int fb_is_boxcode(unsigned char b);    // 框线/滑块/箭头字节? (v6.8.1, CJK 判定前短路用)

// --- 12.1 PS/2 鼠标 (mouse.c, IRQ12 → 0x2C) ---
void mouse_init(void);
int mouse_installed_k(void);   // 驱动是否就绪
int mouse_buttons_state(void); // 按钮位 (bit0 左 bit1 右 bit2 中)
int mouse_char_x(void);        // 字符格列 0-79
int mouse_char_y(void);        // 字符格行 0-24

#endif