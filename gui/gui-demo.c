/* gui-demo.c — 控件库演示 (v6.6)
 *
 * AMUNOS Classic GUI 内核窗口服务器的控件库 demo, 用 syscall (28-54):
 *   Window / Button / Label / Edit / List / Dialog / Textarea
 *   v6.6 复选框 Checkbox / 单选 Radio / 菜单栏 Menu (Alt+字母)
 *   TAB 焦点循环 / 列表方向键+Enter 选中并读回
 * 用户态只调 sys_gui_*, 从不直接写帧缓冲。
 */
#include "syscall.h"

/* 列表选项 */
static const char *cities[] = {
    "北京", "上海", "广州", "深圳", "成都", "西安", "东京", "大阪"
};
#define NCITIES 8

/* 多行文本区演示 (保留 v6.10) */
static char edbuf[2048];
static int ewin = -1, ed_ta = -1, ed_ok = -1, ed_st = -1;

static int gstrlen(const char *s) { const char *p = s; while (*p) p++; return (int)(p - s); }
static void appdec(char *s, int v) { while (*s) s++; char t[12]; int i = 0;
    do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (i--) *s++ = t[i]; *s = 0; }
static void appstr(char *s, const char *p) { while (*p) { s[gstrlen(s)] = *p; s[gstrlen(s)+1] = 0; p++; } }

int main(void) {
    if (sys_gui_enter() < 0) return -1;

    int win = sys_gui_win(20, 20, 480, 360, "控件演示 v6.13");
    if (win < 0) { sys_gui_leave(); return -1; }

    /* 菜单栏 (v6.6/v6.13): File(F) / Edit(E) / View(V) / Help(H)
     * 多菜单 → 可验证"点不同标题, 弹层跟到该标题下方" */
    int mb = sys_gui_menubar(win);
    int mfile = sys_gui_menu_add(win, mb, "文件(F)");
        sys_gui_menu_item(win, mb, mfile, "新建");
        sys_gui_menu_item(win, mb, mfile, "打开");
        sys_gui_menu_item(win, mb, mfile, "保存");
        sys_gui_menu_item(win, mb, mfile, "-");
        sys_gui_menu_item(win, mb, mfile, "退出");
    int medit = sys_gui_menu_add(win, mb, "编辑(E)");
        sys_gui_menu_item(win, mb, medit, "剪切");
        sys_gui_menu_item(win, mb, medit, "复制");
        sys_gui_menu_item(win, mb, medit, "粘贴");
        sys_gui_menu_item(win, mb, medit, "-");
        sys_gui_menu_item(win, mb, medit, "全选");
    int mview = sys_gui_menu_add(win, mb, "视图(V)");
        sys_gui_menu_item(win, mb, mview, "工具栏");
        sys_gui_menu_item(win, mb, mview, "状态栏");
    int mhelp = sys_gui_menu_add(win, mb, "帮助(H)");
        sys_gui_menu_item(win, mb, mhelp, "关于");
        sys_gui_menu_item(win, mb, mhelp, "-");
        sys_gui_menu_item(win, mb, mhelp, "说明");

    /* 按钮 (顶排 y=60) */
    int b_pop   = sys_gui_btn(win,  20, 60, "弹窗");
    int b_cn    = sys_gui_btn(win, 100, 60, "中文");
    int b_clear = sys_gui_btn(win, 180, 60, "清空");
    int b_txt   = sys_gui_btn(win, 260, 60, "编辑器");
    int b_exit  = sys_gui_btn(win, 360, 60, "退出");

    /* 输入框 y=95 */
    sys_gui_lbl(win,  20, 95, "输入:");
    int ed = sys_gui_edit(win, 80, 95, 280);

    /* 复选 + 单选 (y=125, 同一行节省纵向) */
    sys_gui_lbl(win, 20, 125, "复选:");
    int chk_b = sys_gui_check(win, 80, 125, "粗体");
    int chk_i = sys_gui_check(win, 160, 125, "斜体");
    sys_gui_check_set(win, chk_b, 1);
    sys_gui_lbl(win, 240, 125, "字号:");
    int r_s = sys_gui_radio(win, 290, 125, "小");
    int r_m = sys_gui_radio(win, 330, 125, "中");
    int r_l = sys_gui_radio(win, 370, 125, "大");
    sys_gui_check_set(win, r_m, 1);                    /* 默认选中"中" */

    /* 列表 (v6.13: 突出展示, 大尺寸居中) */
    sys_gui_lbl(win,  20, 150, "城市列表(↑↓+Enter 选中, 状态栏回显):");
    int li = sys_gui_list(win, 20, 170, 440, 150);
    for (int i = 0; i < NCITIES; i++) sys_gui_list_set(win, li, cities[i]);

    /* 状态标签 (反馈) */
    int st = sys_gui_lbl(win, 20, 332, "状态: TAB 切焦点 / Alt+F 开菜单 / 点标题弹层跟随");

    int dlg = -1, dlg_ok = -1;
    gui_ev_t ev[16];

    for (;;) {
        int n = sys_gui_events(ev, 16);
        for (int i = 0; i < n; i++) {
            if (ev[i].type == GEV_CLICK) {
                /* 菜单激活 (ch = (menu<<8) | item) */
                if (ev[i].ctl == mb) {
                    int m = (ev[i].ch >> 8) & 0xFF, it = ev[i].ch & 0xFF;
                    char s[64]; s[0] = 0;
                    appstr(s, "菜单: "); appdec(s, m); appstr(s, "/");
                    appdec(s, it);
                    if (m == mfile && it == 4) { sys_gui_leave(); return 0; } /* 退出 */
                    if (m == mfile) {
                        if (it == 0) sys_gui_wnd_text(win, ed, "新建文件");
                        else if (it == 1) sys_gui_wnd_text(win, ed, "打开文件");
                        else if (it == 2) sys_gui_wnd_text(win, ed, "保存文件");
                    }
                    if (m == medit) {
                        if (it == 0) sys_gui_wnd_text(win, ed, "剪切");
                        else if (it == 1) sys_gui_wnd_text(win, ed, "复制");
                        else if (it == 2) sys_gui_wnd_text(win, ed, "粘贴");
                        else if (it == 4) sys_gui_wnd_text(win, ed, "全选 Ctrl+A");
                    }
                    if (m == mview) {
                        if (it == 0) sys_gui_wnd_text(win, st, "视图: 工具栏 (待实现)");
                        else if (it == 1) sys_gui_wnd_text(win, st, "视图: 状态栏 (待实现)");
                    }
                    if (m == mhelp) {
                        if (it == 0) sys_gui_wnd_text(win, st, "关于: AMUNOS Classic GUI 0.1 v6.13");
                        else if (it == 2) sys_gui_wnd_text(win, st, "说明: TAB 切焦点, Alt+字母 开菜单, ↑↓ 列表");
                    }
                    if (ev[i].ch == ((mfile<<8)|4)) continue;
                    if (s[0]) sys_gui_wnd_text(win, st, s);
                    continue;
                }
                if (dlg >= 0 && ev[i].win == dlg) {
                    if (ev[i].ctl == dlg_ok) { sys_gui_win_close(dlg); dlg = -1; }
                    continue;
                }
                if (ev[i].win == ewin && ed_ok >= 0 && ev[i].ctl == ed_ok) {
                    int n = sys_gui_tarea_get(ewin, ed_ta, edbuf, sizeof(edbuf));
                    int lines = 1;
                    for (int i = 0; i < n; i++) if (edbuf[i] == '\n') lines++;
                    char s[48]; s[0] = 0;
                    appstr(s, "textarea bytes="); appdec(s, n);
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
                } else if (c == b_cn) sys_gui_wnd_text(win, st, "中文显示正常 ✓");
                else if (c == b_clear) sys_gui_wnd_text(win, ed, "");
                else if (c == b_txt) {
                    if (ewin >= 0) sys_gui_win_raise(ewin);
                    else {
                        ewin = sys_gui_win(140, 60, 420, 360, "记事簿");
                        ed_ta = sys_gui_tarea(ewin, 8, 30, 404, 260);
                        const char *init =
                            "第一行 Hello 中文\n第二行 中英混合 abc 123\n第三行 你好, AMUNOS!\n";
                        sys_gui_tarea_set(ewin, ed_ta, init, gstrlen(init));
                        ed_ok = sys_gui_btn(ewin, 8, 300, "读回");
                        ed_st = sys_gui_lbl(ewin, 96, 310, "按 读回 看字节/行数");
                    }
                } else if (c == b_exit) { sys_gui_leave(); return 0; }
                else if (c == chk_b || c == chk_i) {
                    char s[32]; s[0] = 0;
                    appstr(s, "复选 ev.ch="); appdec(s, ev[i].ch);
                    sys_gui_wnd_text(win, st, s);
                } else if (c == r_s || c == r_m || c == r_l) {
                    sys_gui_wnd_text(win, st, "单选: 选中 (互斥 OK)");
                } else if (c == li) {
                    char s[48]; s[0] = 0;
                    char buf[32]; int sel = sys_gui_list_get(win, li, buf, 32);
                    int n = sys_gui_list_n(win, li);
                    appstr(s, "列表选: ");
                    if (sel >= 0) appstr(s, buf);
                    else appstr(s, "(空)");
                    appstr(s, " "); appdec(s, sel);
                    appstr(s, "/"); appdec(s, n);
                    sys_gui_wnd_text(win, st, s);
                }
            } else if (ev[i].type == GEV_KEY) {
                if (ev[i].win != win) continue;
                int c = ev[i].ch;
                char s[48]; s[0] = 0;
                if (c == '\b') appstr(s, "键: 退格");
                else if (c == 128) appstr(s, "键: LEFT");
                else if (c == 129) appstr(s, "键: RIGHT");
                else if (c == 132) appstr(s, "键: HOME");
                else if (c == 133) appstr(s, "键: END");
                else if (c == 127) appstr(s, "键: DEL");
                else if (c == '\t') {
                    appstr(s, "TAB 焦点 → 控件 "); appdec(s, ev[i].ctl);
                } else if (c >= 0x20 && c <= 0x7E) {
                    appstr(s, "键: ["); s[gstrlen(s)] = (char)c; s[gstrlen(s)+1] = 0;
                    appstr(s, "]");
                }
                if (s[0]) sys_gui_wnd_text(win, st, s);
            } else if (ev[i].type == GEV_ENTER) {
                if (ev[i].win == win) sys_gui_wnd_text(win, st, "回车 (确认列表/编辑)");
            } else if (ev[i].type == GEV_CLOSE) {
                if (ev[i].win == win) { sys_gui_leave(); return 0; }
                if (ev[i].win == ewin) { ewin = -1; ed_ta = ed_ok = ed_st = -1; }
                if (ev[i].win == dlg) { dlg = -1; }
            }
        }
        if (n == 0) sys_sleep(1);
    }
}
