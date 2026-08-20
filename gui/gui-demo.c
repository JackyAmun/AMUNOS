/* gui-demo.c — 控件库演示 (v6.9)
 *
 * 在 AMUNOS 图形 syscall (28-43) 上演示内核窗口服务器的全部基础控件:
 *   窗口 / 按钮 / 标签 / 输入框 / 列表 / 弹窗 + 中文显示 + 鼠标 + 键盘。
 * 用户态只调 sys_gui_* (libc/syscall.h 包装), 从不直接写帧缓冲 → 不存在
 * 旧 EDIT 的像素覆盖残留路径。
 */
#include "syscall.h"

/* 列表选项 (演示中文与交互) */
static const char *cities[] = {
    "北京", "上海", "广州", "深圳", "成都", "西安", "东京", "大阪"
};
#define NCITIES 8

/* ── 多行文本区 + 内容读回 (v6.10) ── */
static char edbuf[2048];
static int  ewin = -1, ed_ta = -1, ed_ok = -1, ed_st = -1;

static int gstrlen(const char *s) { const char *p = s; while (*p) p++; return (int)(p - s); }
static void appdec(char *s, int v) { while (*s) s++; char t[12]; int i = 0;
    do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (i--) *s++ = t[i]; *s = 0; }
static void appstr(char *s, const char *p) { while (*p) { s[gstrlen(s)] = *p; s[gstrlen(s)+1] = 0; p++; } }

int main(void) {
    if (sys_gui_enter() < 0)
        return -1;                /* VBE 未就绪 */

    /* 主窗口 (高 340: 状态栏 y=320 在窗内, 否则在底缘外不可见) */
    int win = sys_gui_win(20, 20, 440, 340, "控件演示");
    if (win < 0) { sys_gui_leave(); return -1; }

    /* 按钮 */
    int b_pop   = sys_gui_btn(win,  20, 60, "弹窗");
    int b_cn    = sys_gui_btn(win, 100, 60, "中文");
    int b_clear = sys_gui_btn(win, 180, 60, "清空");
    int b_exit  = sys_gui_btn(win, 260, 60, "退出");
    int b_txt   = sys_gui_btn(win, 320, 60, "编辑器");

    /* 输入框 */
    int l_in   = sys_gui_lbl(win,  20, 110, "输入:");
    int ed     = sys_gui_edit(win, 80, 110, 240);

    /* 列表 */
    int l_list = sys_gui_lbl(win,  20, 150, "列表:");
    int li     = sys_gui_list(win, 80, 150, 280, 160);
    for (int i = 0; i < NCITIES; i++) sys_gui_list_set(win, li, cities[i]);

    /* 状态标签 (反馈) */
    int st = sys_gui_lbl(win, 20, 320, "选择: ");

    int dlg = -1, dlg_ok = -1;
    gui_ev_t ev[16];

    for (;;) {
        int n = sys_gui_events(ev, 16);
        for (int i = 0; i < n; i++) {
            if (ev[i].type == GEV_CLICK) {
                if (dlg >= 0 && ev[i].win == dlg) {
                    if (ev[i].ctl == dlg_ok) { sys_gui_win_close(dlg); dlg = -1; }
                    continue;
                }
                if (ev[i].win == ewin && ed_ok >= 0 && ev[i].ctl == ed_ok) {
                    /* 内容读回 (v6.10): 读回字节数 + 行数显示到状态标签 */
                    int n = sys_gui_tarea_get(ewin, ed_ta, edbuf, sizeof(edbuf));
                    int lines = 1;
                    for (int i = 0; i < n; i++) if (edbuf[i] == '\n') lines++;
                    char s[48]; s[0] = 0;
                    appstr(s, "bytes="); appdec(s, n);
                    appstr(s, " lines="); appdec(s, lines);
                    sys_gui_wnd_text(ewin, ed_st, s);
                    continue;
                }
                if (ev[i].win != win) continue;
                int c = ev[i].ctl;
                if (c == b_pop) {
                    dlg = sys_gui_dialog(0, 300, 120, "消息");
                    sys_gui_lbl(dlg, 16, 40, "中文消息: 你好, AMUNOS!");
                    dlg_ok = sys_gui_btn(dlg, 110, 78, "OK");
                } else if (c == b_cn) {
                    sys_gui_wnd_text(win, st, "选择: 中文显示正常 ✓");
                } else if (c == b_clear) {
                    sys_gui_wnd_text(win, ed, "");
                } else if (c == b_txt) {
                    if (ewin >= 0) { sys_gui_win_raise(ewin); }   /* 已开: 仅置顶 */
                    else {
                        ewin = sys_gui_win(140, 60, 420, 360, "记事簿");
                        ed_ta = sys_gui_tarea(ewin, 8, 30, 404, 260);
                        const char *init =
                            "第一行 Hello 中文\n第二行 中英混合 abc 123\n第三行 你好, AMUNOS!\n";
                        sys_gui_tarea_set(ewin, ed_ta, init, gstrlen(init));
                        ed_ok = sys_gui_btn(ewin, 8, 300, "读回");
                        ed_st = sys_gui_lbl(ewin, 96, 310, "按 读回 看字节/行数");
                    }
                } else if (c == b_exit) {
                    sys_gui_leave(); return 0;
                } else if (c == li) {
                    int idx = ev[i].ch;
                    if (idx >= 0 && idx < NCITIES)
                        sys_gui_wnd_text(win, st, "选择: 你点了");
                    else
                        sys_gui_wnd_text(win, st, "选择: (空)");
                }
            } else if (ev[i].type == GEV_KEY) {
                /* 输入框由内核自行更新; 这里回显到主窗状态栏证明 EV_KEY 送达 */
                if (ev[i].win != win) continue;
                int c = ev[i].ch;
                if (c == '\b') sys_gui_wnd_text(win, st, "输入: 退格");
                else if (c == 128) sys_gui_wnd_text(win, st, "输入: LEFT");
                else if (c == 129) sys_gui_wnd_text(win, st, "输入: RIGHT");
                else if (c == 132) sys_gui_wnd_text(win, st, "输入: HOME");
                else if (c == 133) sys_gui_wnd_text(win, st, "输入: END");
                else if (c == 127) sys_gui_wnd_text(win, st, "输入: DEL");
                else if (c >= 0x20 && c <= 0x7E) {
                    char s[16]; s[0] = '['; s[1] = (char)c; s[2] = ']'; s[3] = 0;
                    sys_gui_wnd_text(win, st, s);
                }
            } else if (ev[i].type == GEV_ENTER) {
                sys_gui_wnd_text(win, st, "输入: 回车");
            } else if (ev[i].type == GEV_CLOSE) {
                /* 标题栏 ✕: 内核已关窗, 这里清理句柄/决定退出 */
                if (ev[i].win == win) { sys_gui_leave(); return 0; }
                if (ev[i].win == ewin) { ewin = -1; ed_ta = ed_ok = ed_st = -1; }
                if (ev[i].win == dlg) { dlg = -1; }
            }
        }
        if (n == 0) sys_sleep(1);   /* 空闲防空转 */
    }
}