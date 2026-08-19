/* ------------- mouse.c ------------- */
/* AMUNOS 移植: 空桩 → 真实现。
 * 后端 = 内核 PS/2 鼠标驱动 (SYS_MOUSE=18, int 0x30)。
 *
 * 坐标约定与 DOS 一致: DFLAT 全部用字符格 (0-79, 0-24),
 * 内核把像素坐标换算好, 用户态只查不改。
 *
 * 鼠标光标: QEMU 图形窗口的指针由 GUI 自己渲染, 内核不画,
 * 所以 show/hide/set_mouseposition 保持 no-op — 避免每次
 * 画屏 (每字符一次) 触发系统调用的性能开销。
 */

#include "dflat.h"
#include "syscall.h"

static int mx = -1, my = -1, mb = 0;   /* 最近一次查询的缓存 */
static int last_btn = 0;               /* 上次按钮状态 (释放检测用) */

/* 查一次鼠标状态 (syscall), 填缓存 */
static int query(void)
{
    int buf[3];
    if (sys_mouse(buf) != 0) return -1;
    mb = buf[0];
    mx = buf[1];
    my = buf[2];
    return 0;
}

/* ---------- reset the mouse ---------- */
void resetmouse(void)
{
}

/* ----- test to see if the mouse driver is installed ----- */
BOOL mouse_installed(void)
{
    return TRUE;      /* AMUNOS 内核恒有 PS/2 鼠标驱动 */
}

/* ------ return true if mouse buttons are pressed ------- */
int mousebuttons(void)
{
    query();
    return mb;
}

/* ---------- return mouse coordinates ---------- */
void get_mouseposition(int *x, int *y)
{
    if (query() == 0)    {
        *x = mx;
        *y = my;
    }
    else
        *x = *y = -1;
}

/* -------- position the mouse cursor -------- */
void set_mouseposition(int x, int y)
{
    /* QEMU GUI 光标跟随物理鼠标, 不可由 guest 定位 */
}

/* --------- display the mouse cursor -------- */
void show_mousecursor(void)
{
    /* QEMU GUI 已渲染指针 */
}

/* --------- hide the mouse cursor ------- */
void hide_mousecursor(void)
{
    /* QEMU GUI 已渲染指针 */
}

/* --- return true if a mouse button has been released --- */
int button_releases(void)
{
    int b = mousebuttons();
    int rel = last_btn & ~b;   /* 刚松开的按钮位 */
    last_btn = b;
    return rel;
}

/* ----- set mouse travel limits ------- */
void set_mousetravel(int minx, int maxx, int miny, int maxy)
{
    /* 内核已把坐标钳制在 80x25 内 */
}
