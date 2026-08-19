/* ----------- console.c ---------- */
/* AMUNOS 移植: 16 位 DOS BIOS (int 10h/16h) → sys_getkey/sys_getmods +
 * 软件输入光标 '|' (内核 0xB8000 叠加层, sys_cur 系列) + 0xB8000 帧缓冲直写。
 * 硬件文本光标 (0x3D4/0x3D5) 已由内核隐藏, 全局用软件 '|' 表示输入光标。
 * 保留 DFLAT 的 getkey 归一化 (SHIFT+INS=粘贴, SHIFT+DEL=剪切, ALT+BS=撤销,
 * CTRL+INS=复制) 和 AltConvert 表, 只替换平台后端。 */

#include "dflat.h"
#include "syscall.h"

/* ----- table of alt keys for finding shortcut keys ----- */
static int altconvert[] = {
    ALT_A,ALT_B,ALT_C,ALT_D,ALT_E,ALT_F,ALT_G,ALT_H,
    ALT_I,ALT_J,ALT_K,ALT_L,ALT_M,ALT_N,ALT_O,ALT_P,
    ALT_Q,ALT_R,ALT_S,ALT_T,ALT_U,ALT_V,ALT_W,ALT_X,
    ALT_Y,ALT_Z,ALT_0,ALT_1,ALT_2,ALT_3,ALT_4,ALT_5,
    ALT_6,ALT_7,ALT_8,ALT_9
};

unsigned video_mode;
unsigned video_page;

static int cursorpos[MAXSAVES];
static int cursorshape[MAXSAVES];
static int cs;

/* ── AMUNOS 平台后端 ────────────────────────────────────── */

/* 光标位置/形状的当前值 (setcursor/set_cursor_type 维护,
 * getcursor/savecursor/restorecursor 查询) */
static int cur_x, cur_y, cur_shape;

/* 画软件输入光标 '|': 调 sys_cur → 内核叠加层写 0xB8000 对应格 (v6.7)。
 * 不再写 0x3D4/0x3D5 — 硬件文本光标被内核隐藏, 统一由软件 '|' 表示。 */
static void setcursor(int x, int y)
{
    cur_x = x;
    cur_y = y;
    sys_cur(x, y);
}

/* 读光标: 位置/形状用本地跟踪值, 不读端口 (硬件光标已隐藏, 端口无有效值) */
static void getcursor(void)
{
    /* cur_x/cur_y/cur_shape 由 setcursor/set_cursor_type 持续维护 */
}

/* ------------- clear the screen -------------- */
void clearscreen(void)
{
    int y, x;
    for (y = 0; y < SCREENHEIGHT; y++)
        for (x = 0; x < SCREENWIDTH; x++)
            *(volatile unsigned short*)(0xB8000 + vad(x, y)) =
                (unsigned short)(' ' | (clr(LIGHTGRAY, BLACK) << 8));
    setcursor(0, 0);
}

void SwapCursorStack(void)
{
    if (cs > 1)	{
        swap(cursorpos[cs-2], cursorpos[cs-1]);
        swap(cursorshape[cs-2], cursorshape[cs-1]);
    }
}

/* ---- Test for keystroke ----
 * AMUNOS 非阻塞按键查询: sys_keyhit() 轮询一次键盘/串口, 有键待读返回 1。
 * 恒真会让 collect_events 永远卡在 getkey() 阻塞, 鼠标轮询到不了 (v6.7 修复)。
 * 事件驱动: 无键时返回 0, collect_events 继续轮询鼠标/时钟。 */
BOOL keyhit(void)
{
    return sys_keyhit();
}

/* ---- Read a keystroke ----
 * 把 AMUNOS sys_getkey() 的原始码映射成 DFLAT 键:
 *   - 可打印字符 / 控制码 1..26 (Ctrl+字母) 直接透传
 *   - 128+ 方向/Home/End/PgUp/PgDn/F1-F5/INS → FKEY|scancode
 *   - 移位归一化: SHIFT+INS=CTRL_V, SHIFT+DEL=CTRL_X, ALT+BS=CTRL_Z,
 *     CTRL+INS=CTRL_C (与 DFLAT BIOS 版一致)
 *   - Alt+字母/数字 → ALT_<key> (菜单热键用) */
int getkey(void)
{
    int c = sys_getkey();
    int theShift = getshift();

    if (theShift & (LEFTSHIFT | RIGHTSHIFT)) {
        if (c == 141) return CTRL_V;    /* SHIFT+INS = 粘贴 */
        if (c == 127) return CTRL_X;    /* SHIFT+DEL = 剪切 */
    }
    if ((theShift & ALTKEY) && c == '\b')
        return CTRL_Z;                  /* ALT+BS = 撤销 */
    if ((theShift & CTRLKEY) && c == 141)
        return CTRL_C;                  /* CTRL+INS = 复制 */

    switch (c) {
    case 128: return LARROW;
    case 129: return RARROW;
    case 130: return UP;
    case 131: return DN;
    case 132: return HOME;
    case 133: return END;
    case 134: return F1;
    case 135: return F2;
    case 136: return F3;
    case 137: return F4;
    case 138: return F5;
    case 139: return PGUP;
    case 140: return PGDN;
    case 141: return INS;
    case 127: return DEL;               /* AMUNOS DEL 键原始码 */
    }

    /* Alt+字母 → ALT_<letter> (QWERTY 行结构; 对照 keys.h ALT_* 值) */
    if (theShift & ALTKEY) {
        static const char *rows[3] = {
            "qwertyuiop",   /* 起始 scancode 0x10 */
            "asdfghjkl",    /* 起始 scancode 0x1E */
            "zxcvbnm"       /* 起始 scancode 0x2C */
        };
        static const int base[3] = { 0x10, 0x1E, 0x2C };
        int r, i;
        for (r = 0; r < 3; r++)
            for (i = 0; rows[r][i]; i++)
                if (c == rows[r][i])
                    return FKEY + base[r] + i;
        if (c >= '0' && c <= '9')
            return ALT_1 + (c - '1');   /* ALT_0 = ALT_1+9 */
    }

    return c;   /* ASCII / Ctrl+字母控制码 */
}

/* ---------- read the keyboard shift status ---------
 * AMUNOS sys_getmods: bit0=shift bit1=ctrl bit2=caps bit3=alt。
 * 转成 DFLAT 位掩码 (keys.h)。AMUNOS 不分左右, 两侧 shift 位都置。 */
int getshift(void)
{
    int m = sys_getmods();
    int r = 0;
    if (m & 1) r |= LEFTSHIFT | RIGHTSHIFT;
    if (m & 2) r |= CTRLKEY;
    if (m & 4) r |= CAPSLOCK;
    if (m & 8) r |= ALTKEY;
    return r;
}

/* -------- sound a buzz tone -------- */
void beep(void)
{
    printf("\a");   /* 经 libc → sys_putchar(7) */
}

/* -------- get the video mode and page -------- */
void videomode(void)
{
    video_mode = 3;     /* 固定 80x25 彩色文本 */
    video_page = 0;
}

/* ------ position the cursor ------ */
void cursor(int x, int y)
{
    if (y >= SCREENHEIGHT) y = SCREENHEIGHT - 1; /* 0.7c */
    setcursor(x, y);
}

/* ------- get the current cursor position ------- */
void curr_cursor(int *x, int *y)
{
    getcursor();
    *x = cur_x;
    *y = cur_y;
}

/* ------ save the current cursor configuration ------ */
void savecursor(void)
{
    if (cs < MAXSAVES)    {
        getcursor();
        cursorshape[cs] = cur_shape;
        cursorpos[cs] = cur_y * SCREENWIDTH + cur_x;
        cs++;
    }
}

/* ---- restore the saved cursor configuration ---- */
void restorecursor(void)
{
    if (cs)    {
        --cs;
        int y = cursorpos[cs] / SCREENWIDTH;
        int x = cursorpos[cs] % SCREENWIDTH;
        if (y >= SCREENHEIGHT)
            y = SCREENHEIGHT - 1;	/* 0.7c */
        setcursor(x, y);
        set_cursor_type(cursorshape[cs]);
    }
}

/* ------ make a normal cursor ------
 * AMUNOS: 全局使用下划线光标 (0x0E0F = 第 15/16 扫描线),
 * DFLAT 内部到处调 normalcursor(), 改成下划线后所有路径都跟随。 */
void normalcursor(void)
{
    set_cursor_type(0x0E0F);
}

/* ------ hide the cursor ------ */
void hidecursor(void)
{
    sys_curhide();      /* 内核叠加层把 '|' 从屏上抹掉 (位置已记, 可恢复) */
}

/* ------ unhide the cursor ------ */
void unhidecursor(void)
{
    sys_curshow();      /* 在内核记住的最后位置重画 '|' */
}

/* ---- set the cursor type: 形状仅记录, 不写端口 (内核统一画 '|') ---- */
void set_cursor_type(unsigned t)
{
    cur_shape = t;
}

/* ---- set underline cursor ---- */
void underline_cursor(void)
{
    set_cursor_type(0x0E0F); /* 最后一行扫描线，下划线光标 */
}

/* ---- set block cursor ---- */
void block_cursor(void)
{
    set_cursor_type(0x0106); /* 块光标 */
}

/* ---- test for EGA / VGA: AMUNOS 固定 VGA 文本 ---- */
BOOL isEGA(void)
{
    return FALSE;
}

BOOL isVGA(void)
{
    return TRUE;
}

/* ---------- 行数切换: AMUNOS 固定 80x25, 空操作 ---------- */
void Set25(void) { clearscreen(); }
void Set43(void) { clearscreen(); }
void Set50(void) { clearscreen(); }

/* ------ convert an Alt+ key to its letter equivalent ----- */
int AltConvert(int c)
{
    int i, a = 0;
    for (i = 0; i < 36; i++)
        if (c == altconvert[i])
            break;
    if (i < 26)
        a = 'a' + i;
    else if (i < 36)
        a = '0' + i - 26;
    return a;
}
