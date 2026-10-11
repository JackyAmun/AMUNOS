/* fb.c — 软件文本渲染器 (v6.8 中文支持, 80x30 文本网格)
 *
 * 架构: 保留 0xB8000 作为"逻辑文本缓冲" (80×30), 本文件把它的每个字符
 * 格用 8×16 VGA 字形画到 VBE 线性帧缓冲。汉字 (GB2312 双字节) 放不进单字节
 * 0xB8000, 走独立路径用 16×16 HZK16 字形直接画到帧缓冲 (当前由演示命令
 * zh 画到 80×25 网格以下的空行带, 不被 0xB8000 渲染覆盖)。
 *
 * 关键: 现有所有写 0xB8000 的代码 (内核 put_char / EDIT DFLAT / 软件叠加
 * 光标鼠标) 一行不改 — 渲染器每 tick 读 0xB8000 画一遍即可。
 *
 * boot.asm 在进保护模式前切 VBE 640x480x16bpp, fb 参数写到 0x6000:
 *   0x6000 flag(1,=0x01 VBE ok) 0x6002 base(4) 0x6006 w(2) 0x6008 h(2)
 *   0x600A bpp(1) 0x600B bpl(2)
 * 英文字库内嵌 (latin_font.h), 不依赖 BIOS INT 10h 1130h —
 * QEMU SeaBIOS 返回的字库指针错位, 直接复制会得到乱码 (v6.8 修复)。
 */
#include "common.h"
#include "fonts/latin_font.h"
#include "fonts/font_profiles.h"

#define VGA_BASE  0xB8000
#define COLS      80
#define ROWS      30
#define FB_INFO   0x6000
#define LATIN_FONT ((const unsigned char *)latin_font8x16)

static unsigned int fb_base = 0;
static int fb_w = 0, fb_h = 0, fb_bpp = 0, fb_bpl = 0;
static int fb_on = 0;                 /* 1 = 图形渲染器启用 */
static unsigned char *hzk16 = 0;      /* HZK16 字库数据 (堆) */
static unsigned int *u2gb = 0;        /* Unicode→GB2312 表 (堆): 高16位=Unicode 低16位=GB码, 按Unicode升序 */
static int u2gb_n = 0;                /* u2gb 条目数 */
static unsigned char rendered_cells[ROWS][COLS][2];
static unsigned short rendered_cjk[ROWS][COLS];
static unsigned char rendered_style[ROWS][COLS];
static int rendered_valid;
static int rendered_mouse_x = -1, rendered_mouse_y = -1;
static int rendered_cursor_x = -1, rendered_cursor_y = -1;

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
    0x0000, 0x314D, 0x05A0, 0x05BF,   /* black muted-blue green cyan */
    0xB000, 0xB01B, 0x8A40, 0xC618,   /* red magenta brown gray  */
    0x630C, 0x3D7F, 0x57EA, 0x57FF,   /* dgray lblue lgreen lcyan*/
    0xFAAA, 0xFA1F, 0xFC00, 0xFFFF,   /* lred lmag orange white  */
};

int fb_active(void) { return fb_on; }

/* ── GUI 访问器 (gui.c 直接写帧缓冲 / 取字库指针) ── */
unsigned fb_vbe_base(void) { return fb_base; }
int fb_vbe_bpl(void)       { return fb_bpl; }
int fb_vbe_w(void)         { return fb_w; }
int fb_vbe_h(void)         { return fb_h; }
unsigned char *fb_hzk16(void) { return hzk16; }

static inline void fb_put_px(int x, int y, unsigned short c) {
    unsigned char *p = (unsigned char *)(fb_base + (unsigned)y * (unsigned)fb_bpl
                                         + (unsigned)x * (unsigned)(fb_bpp / 8));
    p[0] = (unsigned char)(c & 0xFF);
    p[1] = (unsigned char)(c >> 8);
}

static void fb_overlay_bg_px(int x, int y, unsigned short bg, unsigned short color) {
    unsigned short *p = (unsigned short *)(fb_base + (unsigned)y * (unsigned)fb_bpl
                                           + (unsigned)x * 2);
    if (*p == bg) *p = color;
}

static void fb_text_overlays(const unsigned char *vram) {
    int x, y, shape;
    if (vga_text_mouse_state(&x, &y)) {
        int px = x * 8, py = y * 16;
        /* Keep the mouse pointer as a stable white text-cell block. */
        for (int row = 0; row < 16; row++)
            for (int col = 0; col < 8; col++)
                fb_put_px(px + col, py + row, vga_rgb565[15]);
    }
    if (vga_text_cursor_state(&x, &y, &shape)) {
        unsigned char attr = vram[(y * COLS + x) * 2 + 1];
        unsigned short bg = vga_rgb565[(attr >> 4) & 15];
        /* Cursor contrast is independent from the orange shortcut color. */
        unsigned short color = bg == vga_rgb565[15] ? vga_rgb565[0]
                                                   : vga_rgb565[15];
        int px = x * 8, py = y * 16;
        if (shape == 0) {
            for (int i = 0; i < 8; i++) fb_overlay_bg_px(px + i, py + 15, bg, color);
        } else {
            for (int i = 0; i < 16; i++) fb_overlay_bg_px(px, py + i, bg, color);
        }
    }
}

static void fb_draw_glyph(int px, int py, unsigned char idx,
                          unsigned short fg, unsigned short bg, int style);
static void fb_draw_cjk(int px, int py, unsigned char gbH, unsigned char gbL,
                        unsigned short fg, unsigned short bg, int style);
static void fb_draw_box(int px, int py, unsigned short fg, unsigned short bg);
static void fb_draw_boxglyph(int px, int py, unsigned char g,
                             unsigned short fg, unsigned short bg);

static void fb_draw_text_cell(const unsigned char *vram, int col, int row) {
    const unsigned char *cell;
    unsigned short fg, bg, ck;
    int style;
    if (col < 0 || col >= COLS || row < 0 || row >= ROWS) return;
    ck = vga_cjk_at(col, row);
    style = vga_style_at(col, row);
    if (ck == 0xFFFF && col > 0) {
        col--;
        ck = vga_cjk_at(col, row);
    }
    cell = vram + (row * COLS + col) * 2;
    fg = vga_rgb565[cell[1] & 0x0F];
    bg = vga_rgb565[(cell[1] >> 4) & 0x0F];
    if (ck == 0xFFFF) return;
    if (ck == 0xFFFE)
        fb_draw_box(col * 8, row * 16, fg, bg);
    else if (ck != 0)
        fb_draw_cjk(col * 8, row * 16, (unsigned char)(ck >> 8),
                    (unsigned char)ck, fg, bg, style);
    else if (fb_is_boxcode(cell[0]))
        fb_draw_boxglyph(col * 8, row * 16, cell[0], fg, bg);
    else
        fb_draw_glyph(col * 8, row * 16, cell[0], fg, bg, style);
}

/* 画一个 8×16 拉丁字形 (内嵌字库 latin_font8x16, 256 字形 × 16B) */
static void fb_draw_glyph(int px, int py, unsigned char idx,
                          unsigned short fg, unsigned short bg, int style) {
    const unsigned char *g = LATIN_FONT + idx * 16;
    int size = style & 0x0c;
    int profile = size == 0x04 ? 0 : size == 0x0c ? 1 : 2;
    int width = amun_font_profiles[profile].latin_width;
    int height = amun_font_profiles[profile].latin_height;
    int top = (16 - height) / 2;
    int left;
    if ((style & 0x40) && width > 5) width--;
    left = (8 - width) / 2;
    for (int r = 0; r < 16; r++) {
        unsigned char bits = g[r];
        for (int c = 0; c < 8; c++) fb_put_px(px + c, py + r, bg);
    }
    for (int r = 0; r < height; r++) {
        int src_r = (r * 16) / height;
        unsigned char bits = g[src_r];
        int skew = (style & 0x02) ? (height - 1 - r) / 6 : 0;
        for (int c = 0; c < width; c++) {
            int src_c = (c * 8) / width;
            int on = bits & (0x80 >> src_c);
            if (!on && (style & 0x01) && src_c > 0)
                on = bits & (0x80 >> (src_c - 1));
            if (on && left + c + skew < 8)
                fb_put_px(px + left + c + skew, py + top + r, fg);
        }
    }
}

/* 画一个 16×16 汉字字形 (HZK16: 32 字节/字, 每行 2 字节位图, MSB 左)
 * GB2312 码 → 字库偏移 = ((gbH-0xA1)*94 + (gbL-0xA1))*32 */
static void fb_draw_cjk(int px, int py, unsigned char gbH, unsigned char gbL,
                        unsigned short fg, unsigned short bg, int style) {
    if (!hzk16 || gbH < 0xA1 || gbH > 0xF7 || gbL < 0xA1) return;
    const unsigned char *g = hzk16 + ((unsigned)(gbH - 0xA1) * 94 + (gbL - 0xA1)) * 32;
    int size = style & 0x0c;
    int profile = size == 0x04 ? 0 : size == 0x0c ? 1 : 2;
    int width = amun_font_profiles[profile].cjk_width;
    int height = amun_font_profiles[profile].cjk_height;
    int top = (16 - height) / 2, left;
    if ((style & 0x40) && width > 12) width -= 2;
    left = (16 - width) / 2;
    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 16; c++) fb_put_px(px + c, py + r, bg);
    for (int r = 0; r < height; r++) {
        int sr = (r * 16) / height;
        unsigned char b0 = g[sr * 2], b1 = g[sr * 2 + 1];
        int skew = (style & 0x02) ? (height - 1 - r) / 6 : 0;
        for (int c = 0; c < width; c++) {
            int sc = (c * 16) / width;
            int on = sc < 8 ? (b0 & (0x80 >> sc)) : (b1 & (0x80 >> (sc - 8)));
            if (!on && (style & 0x01) && sc > 0) {
                int pc = sc - 1;
                on = pc < 8 ? (b0 & (0x80 >> pc)) : (b1 & (0x80 >> (pc - 8)));
            }
            if (on && left + c + skew < 16)
                fb_put_px(px + left + c + skew, py + top + r, fg);
        }
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
    if (b >= 0x80 && b <= 0x92) return 1;
    return 0;
}

/* 在 (px,py) 画一个 8×16 框线字形 (像素位图由形状几何生成) */
static void fb_draw_boxglyph(int px, int py, unsigned char g,
                             unsigned short fg, unsigned short bg) {
    int i, c;
    unsigned char row[16];
    int is_line   = (g == 0x85 || g == 0x8B);            /* LINE / FOCUS_LINE     ─ */
    int is_side   = (g == 0x84 || g == 0x8A);            /* SIDE / FOCUS_SIDE     │ */
    int is_corner = ((g >= 0x80 && g <= 0x83) || (g >= 0x86 && g <= 0x89) || g == 0x92);
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

/* 全屏渲染: 读 0xB8000 80×30 网格 → 画到帧缓冲顶部。
 * 挂到定时器 (task.c timer_schedule), 每 3 tick (~30Hz) 重绘一次。
 * 文本与光标分层绘制, 光标只落在字形之外的背景像素。 */
void fb_render(void) {
    static unsigned tick = 0;
    if (!fb_on) return;
    if ((++tick % 3) != 0) return;
    const unsigned char *vram = vga_textbuf();   /* 软件文本缓冲 (图形模式) */
    int mx = -1, my = -1, cx = -1, cy = -1, shape;
    int has_mouse = vga_text_mouse_state(&mx, &my);
    int has_cursor = vga_text_cursor_state(&cx, &cy, &shape);

    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            const unsigned char *cell = vram + (row * COLS + col) * 2;
            unsigned short ck = vga_cjk_at(col, row);
            unsigned char style = vga_style_at(col, row);
            int dirty = !rendered_valid || cell[0] != rendered_cells[row][col][0] ||
                        cell[1] != rendered_cells[row][col][1] ||
                        ck != rendered_cjk[row][col] || style != rendered_style[row][col];
            if ((col == rendered_mouse_x && row == rendered_mouse_y) ||
                (col == rendered_cursor_x && row == rendered_cursor_y)) dirty = 1;
            if (dirty) fb_draw_text_cell(vram, col, row);
            rendered_cells[row][col][0] = cell[0];
            rendered_cells[row][col][1] = cell[1];
            rendered_cjk[row][col] = ck;
            rendered_style[row][col] = style;
        }
    }
    rendered_valid = 1;
    rendered_mouse_x = has_mouse ? mx : -1;
    rendered_mouse_y = has_mouse ? my : -1;
    rendered_cursor_x = has_cursor ? cx : -1;
    rendered_cursor_y = has_cursor ? cy : -1;
    fb_text_overlays(vram);
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
            fb_draw_cjk(px, py, (unsigned char)s[0], (unsigned char)s[1], fg, bg, 0);
            px += 16; s += 2;
        } else {                                          /* ASCII */
            fb_draw_glyph(px, py, (unsigned char)s[0], fg, bg, 0);
            px += 16; s++;
        }
        cnt++;
    }
}

/* 从 boot.asm 的 0x6000 读 fb 参数, 启用渲染器 */
void fb_init(void) {
    unsigned char *p = (unsigned char *)FB_INFO;
    if (p[0] != 0x01) { fb_on = 0; return; }              /* VBE 未切成功 */
    fb_base = *(unsigned int *)(p + 2);
    fb_w    = *(unsigned short *)(p + 6);
    fb_h    = *(unsigned short *)(p + 8);
    fb_bpp  = *(unsigned char *)(p + 10);
    fb_bpl  = *(unsigned short *)(p + 11);
    fb_on   = (fb_base != 0 && fb_bpp == 16 && fb_w >= 640 && fb_h >= 480 &&
               fb_bpl >= 1280) ? 1 : 0;
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
    /* Search mounted logical volumes as well as directly-mounted devices. */
    int d;
    for (d = 0; d < DEV_SLOT_COUNT && !hzk16; d++) {
        if (!fs_drive_present(d)) continue;
        drive_ctx_t c = fs_drive_enter(d);
        FAT12Entry e;
        int idx = fs_find_entry("HZK16", &e);
        if (idx >= 0 && e.size > 0) {
            hzk16 = (unsigned char *)mem_alloc((unsigned)e.size);
            if (hzk16) {
                fs_read_file(&e, (char *)hzk16, (int)e.size);
                put_str("fb: HZK16 loaded\n");
            } else {
                put_str("fb: no mem for HZK16\n");
            }
        }
        fs_drive_restore(c);
    }
    if (!hzk16) put_str("fb: HZK16 not found on any drive (Latin only)\n");

    /* Unicode→GB2312 映射表 (UTF-8 支持): 每 4 字节 [uni u16][gb u16], 按 uni 升序 */
    for (d = 0; d < DEV_SLOT_COUNT && !u2gb; d++) {
        if (!fs_drive_present(d)) continue;
        drive_ctx_t c = fs_drive_enter(d);
        FAT12Entry e;
        int idx = fs_find_entry("U2GB.BIN", &e);
        if (idx >= 0 && e.size > 0) {
            u2gb = (unsigned int *)mem_alloc((unsigned)e.size);
            if (u2gb) {
                fs_read_file(&e, (char *)u2gb, (int)e.size);
                u2gb_n = (int)(e.size / 4);
                put_str("fb: U2GB loaded\n");
            } else {
                put_str("fb: no mem for U2GB\n");
            }
        }
        fs_drive_restore(c);
    }
    if (!u2gb) put_str("fb: U2GB.BIN not found on any drive (GB2312 only)\n");
}
