/* fb.c — 软件文本渲染器 (v6.8 中文支持, 里程碑 1)
 *
 * 架构: 保留 0xB8000 作为"逻辑文本缓冲" (80×25), 本文件把它的每个字符
 * 格用 8×16 VGA 字形画到 VBE 线性帧缓冲。汉字 (GB2312 双字节) 放不进单字节
 * 0xB8000, 走独立路径用 16×16 HZK16 字形直接画到帧缓冲 (当前由演示命令
 * zh 画到 80×25 网格以下的空行带, 不被 0xB8000 渲染覆盖)。
 *
 * 关键: 现有所有写 0xB8000 的代码 (内核 put_char / EDIT DFLAT / 软件叠加
 * 光标鼠标) 一行不改 — 渲染器每 tick 读 0xB8000 画一遍即可。
 *
 * boot.asm 在进保护模式前切 VBE 640x480x16bpp, fb 参数写到 0x1500:
 *   0x1500 flag(1,=0x01 VBE ok) 0x1502 base(4) 0x1506 w(2) 0x1508 h(2)
 *   0x150A bpp(1) 0x150B bpl(2)
 * 英文字库内嵌 (latin_font.h), 不依赖 BIOS INT 10h 1130h —
 * QEMU SeaBIOS 返回的字库指针错位, 直接复制会得到乱码 (v6.8 修复)。
 */
#include "common.h"
#include "latin_font.h"

#define VGA_BASE  0xB8000
#define COLS      80
#define ROWS      25
#define FB_INFO   0x1500
#define LATIN_FONT ((const unsigned char *)latin_font8x16)

static unsigned int fb_base = 0;
static int fb_w = 0, fb_h = 0, fb_bpp = 0, fb_bpl = 0;
static int fb_on = 0;                 /* 1 = 图形渲染器启用 */
static unsigned char *hzk16 = 0;      /* HZK16 字库数据 (堆) */
static unsigned int *u2gb = 0;        /* Unicode→GB2312 表 (堆): 高16位=Unicode 低16位=GB码, 按Unicode升序 */
static int u2gb_n = 0;                /* u2gb 条目数 */

/* Unicode 码点 → GB2312 码 (二分查找); 返回 0 表示该字符不在 GB2312 字库 */
unsigned fb_uni_to_gb(unsigned uni) {
    if (!u2gb) return 0;
    int lo = 0, hi = u2gb_n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        unsigned u = u2gb[mid] >> 16;
        if (u == uni) return u2gb[mid] & 0xFFFF;
        if (u < uni) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

/* VGA 16 色 → RGB565 (attr 高/低 4 位分别索引) */
static const unsigned short vga_rgb565[16] = {
    0x0000, 0x0015, 0x0540, 0x0555,   /* black blue green cyan   */
    0xA800, 0xA815, 0xAAA0, 0xAAB5,   /* red magenta brown gray  */
    0x52AA, 0x52BF, 0x57EA, 0x57FF,   /* dgray lblue lgreen lcyan*/
    0xFAAA, 0xFABF, 0xFFEA, 0xFFFF,   /* lred lmag yellow white  */
};

int fb_active(void) { return fb_on; }

static inline void fb_put_px(int x, int y, unsigned short c) {
    unsigned char *p = (unsigned char *)(fb_base + (unsigned)y * (unsigned)fb_bpl
                                         + (unsigned)x * (unsigned)(fb_bpp / 8));
    p[0] = (unsigned char)(c & 0xFF);
    p[1] = (unsigned char)(c >> 8);
}

/* 画一个 8×16 拉丁字形 (内嵌字库 latin_font8x16, 256 字形 × 16B) */
static void fb_draw_glyph(int px, int py, unsigned char idx,
                          unsigned short fg, unsigned short bg) {
    const unsigned char *g = LATIN_FONT + idx * 16;
    for (int r = 0; r < 16; r++) {
        unsigned char bits = g[r];
        for (int c = 0; c < 8; c++)
            fb_put_px(px + c, py + r, (bits & (0x80 >> c)) ? fg : bg);
    }
}

/* 画一个 16×16 汉字字形 (HZK16: 32 字节/字, 每行 2 字节位图, MSB 左)
 * GB2312 码 → 字库偏移 = ((gbH-0xA1)*94 + (gbL-0xA1))*32 */
static void fb_draw_cjk(int px, int py, unsigned char gbH, unsigned char gbL,
                        unsigned short fg, unsigned short bg) {
    if (!hzk16 || gbH < 0xA1 || gbH > 0xF7 || gbL < 0xA1) return;
    const unsigned char *g = hzk16 + ((unsigned)(gbH - 0xA1) * 94 + (gbL - 0xA1)) * 32;
    for (int r = 0; r < 16; r++) {
        unsigned char b0 = g[r * 2], b1 = g[r * 2 + 1];
        for (int c = 0; c < 8; c++)
            fb_put_px(px + c, py + r, (b0 & (0x80 >> c)) ? fg : bg);
        for (int c = 0; c < 8; c++)
            fb_put_px(px + 8 + c, py + r, (b1 & (0x80 >> c)) ? fg : bg);
    }
}

/* 画 16×16 空心方框 (Unicode 不在 GB2312 字库时的替换字形 □) */
static void fb_draw_box(int px, int py, unsigned short fg, unsigned short bg) {
    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 16; c++)
            fb_put_px(px + c, py + r,
                      (r == 0 || r == 15 || c == 0 || c == 15) ? fg : bg);
}

/* ── v6.8.1 框线字形 (DOS 伪图形): 修 EDIT 等窗口边框被误判成汉字/字母的乱码 ──
 * 根因: DFLAT 窗框用 CP437 框线码 (┌┐└┘│─ = 0xB3-0xDA), 全部落在 GB2312 高位区
 *   (0xA1-0xF7) → 被 wputs/put_cjk_str 当成双字节汉字成对误判 → 画成真汉字。
 *   因为同一字节 (如 0xC4) 既是 '─' 又是 "你"(0xC4E3) 的 lead, 字节级白名单必误伤
 *   真中文 → 不能用"短路识别旧码"。
 * 根本解法 (edit-fdos/dflat.h): 把 DFLAT 窗框码**改到非 GB2312 高位的专用带**
 *   0x80-0x91 (GB2312 只占 0xA1-0xF7; ASCII 只占 0x20-0x7E)。于是框线码是单字节、
 *   永不进 CJK 分组, 天然单格; 由下面按像素重建 8×16 框线形状。 */
#define BOX_VLN   0x18    /* 竖线: 中 2 列 (col3-4) */
#define BOX_HLN   0xFF    /* 横线: 中 2 行 (row7-8) */

/* 框线字节白名单: 非 0 表示 b 是 (已改到专用带的) 框线/滑块/箭头, 按像素画。
 * 与 edit-fdos/dflat.h 的 NW/NE/SW/SE/SIDE/LINE 及 FOCUS_ 系列、SCROLL 系列、
 * BARCHAR/BOXCHAR 对应。 */
int fb_is_boxcode(unsigned char b) {
    if (b >= 0x80 && b <= 0x91) return 1;
    return 0;
}

/* 在 (px,py) 画一个 8×16 框线字形 (像素位图由形状几何生成) */
static void fb_draw_boxglyph(int px, int py, unsigned char g,
                             unsigned short fg, unsigned short bg) {
    int i, c;
    unsigned char row[16];
    int is_line   = (g == 0x85 || g == 0x8B);            /* LINE / FOCUS_LINE     ─ */
    int is_side   = (g == 0x84 || g == 0x8A);            /* SIDE / FOCUS_SIDE     │ */
    int is_corner = ((g >= 0x80 && g <= 0x83) || (g >= 0x86 && g <= 0x89));  /* ┌┐┘└ */
    for (i = 0; i < 16; i++) row[i] = 0;
    if (is_corner) {                                      /* 角 = 竖(全高)+横(行7-8) */
        for (i = 0; i < 16; i++) row[i] = BOX_VLN;
        row[7] |= BOX_HLN; row[8] |= BOX_HLN;
    } else if (is_line) {
        row[7] = row[8] = BOX_HLN;
    } else if (is_side) {
        for (i = 0; i < 16; i++) row[i] = BOX_VLN;
    } else if (g == 0x8C) {                               /* UPSCROLL ▲ */
        for (i = 0; i < 8; i++) { int w = (2 * i + 1 < 8) ? (2 * i + 1) : 8; int l = (8 - w) >> 1;
            for (c = 0; c < w; c++) row[4 + i] |= (0x80 >> (l + c)); }
    } else if (g == 0x8D) {                               /* DOWNSCROLL ▼ */
        for (i = 0; i < 8; i++) { int w = (2 * i + 1 < 8) ? (2 * i + 1) : 8; int l = (8 - w) >> 1;
            for (c = 0; c < w; c++) row[11 - i] |= (0x80 >> (l + c)); }
    } else if (g == 0x8E) {                               /* RIGHTSCROLL ► */
        for (i = 0; i < 16; i++) row[i] = 0xF0;
    } else if (g == 0x8F) {                               /* LEFTSCROLL ◄ */
        for (i = 0; i < 16; i++) row[i] = 0x0F;
    } else if (g == 0x90) {                               /* SCROLLBARCHAR ░ */
        for (i = 0; i < 16; i++) row[i] = (i & 1) ? 0x00 : 0xA4;
    } else if (g == 0x91) {                               /* SCROLLBOXCHAR ▒ */
        for (i = 0; i < 16; i++) row[i] = (i & 1) ? 0x55 : 0xAA;
    }
    for (i = 0; i < 16; i++)
        for (c = 0; c < 8; c++)
            fb_put_px(px + c, py + i, (row[i] & (0x80 >> c)) ? fg : bg);
}

/* 全屏渲染: 读 0xB8000 80×25 网格 → 画到帧缓冲顶部。
 * 挂到定时器 (task.c timer_schedule), 每 3 tick (~30Hz) 重绘一次。
 * 软件叠加 '_'/'█' 就在 0xB8000 里, 随网格一起渲染, 天然可见。 */
void fb_render(void) {
    static unsigned tick = 0;
    if (!fb_on) return;
    if ((++tick % 3) != 0) return;
    const unsigned char *vram = vga_textbuf();   /* 软件文本缓冲 (图形模式) */
    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            const unsigned char *cell = vram + (row * COLS + col) * 2;
            unsigned short fg = vga_rgb565[cell[1] & 0x0F];
            unsigned short bg = vga_rgb565[(cell[1] >> 4) & 0x0F];
            unsigned short ck = vga_cjk_at(col, row);
            if (ck == 0xFFFF) continue;              /* 汉字右格: 左格已画 16×16 覆盖 */
            if (ck == 0xFFFE) {                      /* 替换框 □: 不在 GB2312 字库 */
                fb_draw_box(col * 8, row * 16, fg, bg);
                continue;
            }
            if (ck != 0) {                           /* 汉字左格: 画 16×16 HZK16 */
                fb_draw_cjk(col * 8, row * 16, (unsigned char)(ck >> 8),
                            (unsigned char)(ck & 0xFF), fg, bg);
                continue;
            }
            if (fb_is_boxcode(cell[0]))              /* 框线/滑块/箭头: 像素形状 */
                fb_draw_boxglyph(col * 8, row * 16, cell[0], fg, bg);
            else
                fb_draw_glyph(col * 8, row * 16, cell[0], fg, bg);
        }
    }
}

/* 演示: 画一段 GB2312 字符串 (双字节汉字 16 宽, ASCII 8 宽), 水平排列。
 * celly 用"16px 行"单位 — 里程碑 1 演示画到 0xB8000 网格以下 (行 25+),
 * 不被 fb_render 覆盖, 文字保持常驻。 */
void fb_put_str_cjk(int cellx, int celly, const unsigned char *s,
                    int fg_idx, int bg_idx) {
    if (!fb_on) return;
    int px = cellx * 16, py = celly * 16;
    int cnt = 0;
    unsigned short fg = vga_rgb565[fg_idx & 15];
    unsigned short bg = vga_rgb565[bg_idx & 15];
    while (s[0] && cnt < 40) {
        if ((unsigned char)s[0] >= 0xA1 && s[1]) {        /* GB2312 双字节 */
            fb_draw_cjk(px, py, (unsigned char)s[0], (unsigned char)s[1], fg, bg);
            px += 16; s += 2;
        } else {                                          /* ASCII */
            fb_draw_glyph(px, py, (unsigned char)s[0], fg, bg);
            px += 16; s++;
        }
        cnt++;
    }
}

/* 从 boot.asm 的 0x1500 读 fb 参数, 启用渲染器 */
void fb_init(void) {
    unsigned char *p = (unsigned char *)FB_INFO;
    if (p[0] != 0x01) { fb_on = 0; return; }              /* VBE 未切成功 */
    fb_base = *(unsigned int *)(p + 2);
    fb_w    = *(unsigned short *)(p + 6);
    fb_h    = *(unsigned short *)(p + 8);
    fb_bpp  = *(unsigned char *)(p + 10);
    fb_bpl  = *(unsigned short *)(p + 11);
    fb_on   = (fb_base != 0 && fb_bpp == 16) ? 1 : 0;
    if (fb_on) {
        /* 图形模式下 0xB8000 是显卡图形窗口, 内核改用软件文本缓冲;
         * kmain 随后 cls() 清空该缓冲。 */
        vga_enable_softbuf();
    }
}

/* 从 A:HZK16 加载字库到堆 (262KB; 内核 <52KB 无法内嵌)。字库放系统盘 A:
 * (v6.8) — 文件不落 C 盘。 */
void fb_font_init(void) {
    if (!fb_on) return;
    drive_ctx_t c = fs_drive_enter(0);                    /* 临时切到 A: */
    FAT12Entry e;
    int idx = fs_find_entry("HZK16", &e);
    if (idx >= 0 && e.size > 0) {
        hzk16 = (unsigned char *)mem_alloc((unsigned)e.size);
        if (hzk16) {
            fs_read_file(&e, (char *)hzk16);
            put_str("fb: HZK16 loaded\n");
        } else {
            put_str("fb: no mem for HZK16\n");
        }
    } else {
        put_str("fb: A:HZK16 not found (Latin only)\n");
    }
    /* Unicode→GB2312 映射表 (UTF-8 支持): 每 4 字节 [uni u16][gb u16], 按 uni 升序 */
    idx = fs_find_entry("U2GB.BIN", &e);
    if (idx >= 0 && e.size > 0) {
        u2gb = (unsigned int *)mem_alloc((unsigned)e.size);
        if (u2gb) {
            fs_read_file(&e, (char *)u2gb);
            u2gb_n = (int)(e.size / 4);
            put_str("fb: U2GB loaded\n");
        } else {
            put_str("fb: no mem for U2GB\n");
        }
    } else {
        put_str("fb: A:U2GB.BIN not found (GB2312 only)\n");
    }
    fs_drive_restore(c);
}
