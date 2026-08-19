/* ------------- stub.c ------------- */
/* AMUNOS 移植: 被裁剪子系统的最小桩, 保持原版 UI 结构与调用点不变。
 *
 *   - 帮助系统 (原 helpbox.c + decomp.c): AMUNOS 无 .hlp 帮助文件 →
 *     LoadHelpFile/UnLoadHelpFile 空操作, DisplayHelp 弹提示框,
 *     HelpComment 原样返回菜单注释文字。
 *   - HELPBOX 控件类 (classes.h 要求 wndproc; 运行时不创建实例)。
 *   - WatchIcon (原 watch.c/pictbox.c): 无沙漏动画 → 返回 NULL,
 *     edit.c OpenPadWindow 的 SendMessage(NULL,...) 已被 message.c 判空。
 */

#include "dflat.h"

/* ---- 帮助系统桩 ---- */
void LoadHelpFile(char *fn)
{
    (void)fn;
}

void UnLoadHelpFile(void)
{
}

BOOL DisplayHelp(WINDOW wnd, char *topic)
{
    (void)wnd;
    if (topic != NULL && *topic)
        MessageBox("Help", "Help is not available in the AMUNOS build.");
    return FALSE;
}

char *HelpComment(char *s)
{
    return s;
}

/* ---- HELPBOX 控件类 (原 helpbox.c): 直接走 DIALOG 基类 ---- */
int HelpBoxProc(WINDOW wnd, MESSAGE msg, PARAM p1, PARAM p2)
{
    return BaseWndProc(HELPBOX, wnd, msg, p1, p2);
}

/* ---- 沙漏图标 (原 watch.c/pictbox.c): 返回 NULL, 调用点已判空 ---- */
WINDOW WatchIcon(void)
{
    return NULL;
}

/* ---- 窗口类名字符串 (原 helpbox.c; normal.c 传 DisplayHelp 用) ---- */
char *ClassNames[] = {
    #undef ClassDef
    #define ClassDef(c,b,p,a) #c,
    #include "classes.h"
    NULL
};

/* ---- 状态栏时钟 (原 message.c 读 RTC; AMUNOS 无时钟驱动, 显示空串) ---- */
char time_string[] = "";
