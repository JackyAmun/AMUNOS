/* AMUN WRITE 0.1 -- GUI TextArea based document editor. */
#include <stdio.h>
#include <string.h>
#include "syscall.h"

#define BUF_SZ 2048

static int slen(const char *s) {
 int n = 0;
 if (!s) return 0;
 while (s[n]) n++;
 return n;
}

static void status_update(int win, int sb, int ta, const char *msg) {
 int info[6];
 char s[96];
 if (sys_gui_tarea_info(win, ta, info) == 0) {
  if (msg && msg[0])
   snprintf(s, sizeof(s), "%s | Ln %d, Col %d | lines=%d bytes=%d",
    msg, info[0], info[1], info[3], info[5]);
  else
   snprintf(s, sizeof(s), "Ln %d, Col %d | top=%d | lines=%d bytes=%d",
    info[0], info[1], info[4], info[3], info[5]);
  sys_gui_wnd_text(win, sb, s);
 }
}

static int save_doc(int win, int ta, const char *path) {
 char buf[BUF_SZ];
 int n = sys_gui_tarea_get(win, ta, buf, sizeof(buf));
 if (n < 0) return -1;
 FILE *f = fopen(path, "w");
 if (!f) return -1;
 fwrite(buf, 1, n, f);
 fclose(f);
 return n;
}

int main(void) {
 gui_ev_t ev[16];
 const char *path = "WRITE.TXT";
 if (sys_gui_enter() < 0) return 1;

 int win = sys_gui_win(70, 28, 520, 410, "AMUN WRITE 0.1");
 if (win < 0) { sys_gui_leave(); return 1; }
 int mb = sys_gui_menubar(win);
 int mfile = sys_gui_menu_add(win, mb, "文件(F)");
 sys_gui_menu_item(win, mb, mfile, "新建");
 sys_gui_menu_item(win, mb, mfile, "保存");
 sys_gui_menu_item(win, mb, mfile, "-");
 sys_gui_menu_item(win, mb, mfile, "退出");
 int medit = sys_gui_menu_add(win, mb, "编辑(E)");
 sys_gui_menu_item(win, mb, medit, "复制全文");
 sys_gui_menu_item(win, mb, medit, "粘贴追加");
 int mhelp = sys_gui_menu_add(win, mb, "帮助(H)");
 sys_gui_menu_item(win, mb, mhelp, "关于");

 sys_gui_lbl(win, 12, 42, "文档:");
 int ta = sys_gui_tarea(win, 12, 62, 496, 300);
 int sb = sys_gui_statusbar(win, 8, 382, 504, "AMUN WRITE ready");
 const char *init =
  "AMUN WRITE 0.1\n"
  "\n"
  "这是一个基于 TextArea 的初始文档编辑器。\n"
  "菜单: 文件/保存, 编辑/复制全文, 编辑/粘贴追加。\n";
 sys_gui_tarea_set(win, ta, init, slen(init));
 status_update(win, sb, ta, "ready");
 sys_gui_win_raise(win);

 for (;;) {
  int n = sys_gui_events(ev, 16);
  for (int i = 0; i < n; i++) {
   if (ev[i].type == GEV_CLOSE && ev[i].win == win) { sys_gui_leave(); return 0; }
   if (ev[i].win != win) continue;
   if (ev[i].type == GEV_CLICK && ev[i].ctl == mb) {
    int m = (ev[i].ch >> 8) & 0xFF;
    int it = ev[i].ch & 0xFF;
    if (m == mfile) {
     if (it == 0) {
      sys_gui_tarea_set(win, ta, "", 0);
      status_update(win, sb, ta, "new");
     } else if (it == 1) {
      int wr = save_doc(win, ta, path);
      if (wr >= 0) {
       char s[64];
       snprintf(s, sizeof(s), "saved %d bytes to %s", wr, path);
       status_update(win, sb, ta, s);
      } else {
       status_update(win, sb, ta, "save failed");
      }
     } else if (it == 3) {
      sys_gui_leave();
      return 0;
     }
    } else if (m == medit) {
     char buf[BUF_SZ];
     if (it == 0) {
      int r = sys_gui_tarea_get(win, ta, buf, sizeof(buf));
      if (r >= 0) {
       sys_clip_set(buf, r);
       status_update(win, sb, ta, "copied");
      }
     } else if (it == 1) {
      int old = sys_gui_tarea_get(win, ta, buf, sizeof(buf));
      if (old < 0) old = 0;
      if (old < BUF_SZ - 1) {
       int got = sys_clip_get(buf + old, BUF_SZ - old);
       if (got > 0) {
        sys_gui_tarea_set(win, ta, buf, old + got);
        status_update(win, sb, ta, "pasted");
       }
      }
     }
    } else if (m == mhelp && it == 0) {
     status_update(win, sb, ta, "AMUN WRITE 0.1 / GUI 0.5");
    }
   } else if (ev[i].type == GEV_KEY || ev[i].type == GEV_ENTER) {
    status_update(win, sb, ta, "");
   }
  }
  sys_sleep(1);
 }
}
