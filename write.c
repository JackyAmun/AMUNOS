/* AMUN WRITE 0.3 -- GUI 0.5 single-document text editor. */
#include <stdio.h>
#include <string.h>
#include "syscall.h"
#include "gui/file-dialog.h"

#define BUF_SZ 2048
#define ACT_NONE 0
#define ACT_NEW  1
#define ACT_OPEN 2
#define ACT_EXIT 3

typedef struct {
 int active;
 int win;
 int save;
 int discard;
 int cancel;
 int pending;
} confirm_t;

static void copy_text(char *dst, const char *src, int cap) {
 int i = 0;
 if (!src) src = "";
 while (src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
 dst[i] = 0;
}

static const char *base_name(const char *path) {
 const char *base = path;
 for (const char *p = path; p && *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
 return base && *base ? base : "UNTITLED.TXT";
}

static void split_path(const char *full, char *dir, int dir_cap, char *name, int name_cap) {
 int cut = -1, n = full ? (int)strlen(full) : 0;
 for (int i = 0; i < n; i++) if (full[i] == '/' || full[i] == '\\') cut = i;
 if (cut >= 0) {
  int dn = cut + 1; if (dn > dir_cap - 1) dn = dir_cap - 1;
  for (int i = 0; i < dn; i++) dir[i] = full[i];
  dir[dn] = 0;
  copy_text(name, full + cut + 1, name_cap);
 } else {
  dir[0] = 0;
  copy_text(name, full, name_cap);
 }
}

static void title_update(int win, const char *path, int dirty) {
 char title[32];
 snprintf(title, sizeof(title), "WRITE - %s%s", base_name(path), dirty ? " *" : "");
 sys_gui_win_title(win, title);
}

static void status_update(int win, int sb, int ta, const char *path, int dirty, const char *msg) {
 int info[6];
 char s[96];
 if (sys_gui_tarea_info(win, ta, info) != 0) return;
 snprintf(s, sizeof(s), "%s | %s | Ln %d, Col %d | %d B%s",
  msg && msg[0] ? msg : "Ready", base_name(path), info[0], info[1], info[5], dirty ? " *" : "");
 sys_gui_wnd_text(win, sb, s);
}

static int load_doc(int win, int ta, const char *path) {
 char buf[BUF_SZ];
 FILE *f = fopen(path, "r");
 if (!f) return -1;
 int n = (int)fread(buf, 1, BUF_SZ - 1, f);
 int too_large = fgetc(f) != -1;
 int closed = fclose(f);
 if (closed != 0 || too_large) return too_large ? -2 : -1;
 buf[n] = 0;
 return sys_gui_tarea_set(win, ta, buf, n) == 0 ? n : -1;
}

static int save_doc(int win, int ta, const char *path) {
 char buf[BUF_SZ];
 if (!path || !path[0]) return -1;
 int n = sys_gui_tarea_get(win, ta, buf, sizeof(buf));
 if (n < 0) return -1;
 FILE *f = fopen(path, "w");
 if (!f) return -1;
 int wrote = (int)fwrite(buf, 1, n, f);
 int closed = fclose(f);
 return wrote == n && closed == 0 ? n : -1;
}

static int create_empty_doc(const char *path) {
 if (!path || !path[0]) return -1;
 FILE *f = fopen(path, "w");
 if (!f) return -1;
 return fclose(f) == 0 ? 0 : -1;
}

static int copy_selection(int win, int ta) {
 char buf[BUF_SZ];
 int n = sys_gui_tarea_selection_get(win, ta, buf, sizeof(buf));
 return n > 0 ? sys_clip_set(buf, n) : n;
}

static int paste_at_caret(int win, int ta) {
 char buf[BUF_SZ];
 int selected = sys_gui_tarea_selection_get(win, ta, 0, 0);
 int n = sys_clip_get(buf, sizeof(buf));
 if (n <= 0) return 0;
 int before[6], after[6];
 if (sys_gui_tarea_info(win, ta, before) != 0) return -1;
 if (sys_gui_tarea_insert(win, ta, buf, n) < 0) return -1;
 if (sys_gui_tarea_info(win, ta, after) != 0) return -1;
 return after[5] != before[5] || selected > 0 ? 1 : 0;
}

static int text_edit_key(int ch) {
 return ch == '\n' || ch == '\r' || ch == '\b' || ch == 127 ||
  (ch >= 0x20 && ch <= 0x7E);
}

static int confirm_open(confirm_t *c, int parent, int pending) {
 if (c->active) return 0;
 c->win = sys_gui_dialog(parent, 380, 150, "未保存的更改");
 if (c->win < 0) return -1;
 c->active = 1; c->pending = pending;
 sys_gui_lbl(c->win, 24, 42, "当前文档尚未保存。要先保存吗？");
 c->save = sys_gui_btn(c->win, 42, 92, "保存");
 c->discard = sys_gui_btn(c->win, 142, 92, "不保存");
 c->cancel = sys_gui_btn(c->win, 270, 92, "取消");
 return 0;
}

static void confirm_close(confirm_t *c) {
 if (!c->active) return;
 sys_gui_win_close(c->win);
 c->active = 0;
}

static int confirm_handle(confirm_t *c, const gui_ev_t *ev) {
 if (!c->active || ev->win != c->win) return 0;
 if (ev->type == GEV_CLOSE) { c->active = 0; return -1; }
 if (ev->type != GEV_CLICK) return 0;
 if (ev->ctl == c->save) { confirm_close(c); return 1; }
 if (ev->ctl == c->discard) { confirm_close(c); return 2; }
 if (ev->ctl == c->cancel) { confirm_close(c); return -1; }
 return 0;
}

int main(void) {
 gui_ev_t ev[16];
 amun_file_dialog_t fd = {0};
 confirm_t confirm = {0};
 char current_path[64] = "";
 int dirty = 0, resume_after_save = ACT_NONE;
 if (sys_gui_enter() < 0) return 1;

 int win = sys_gui_win(60, 24, 540, 424, "WRITE - UNTITLED.TXT");
 if (win < 0) { sys_gui_leave(); return 1; }
 sys_gui_win_close_guard(win, 1);
 int mb = sys_gui_menubar(win);
 int mfile = sys_gui_menu_add(win, mb, "文件(F)");
 sys_gui_menu_item(win, mb, mfile, "新建  Ctrl+N");
 sys_gui_menu_item(win, mb, mfile, "打开  Ctrl+O");
 sys_gui_menu_item(win, mb, mfile, "保存  Ctrl+S");
 sys_gui_menu_item(win, mb, mfile, "另存为");
 sys_gui_menu_item(win, mb, mfile, "-");
 sys_gui_menu_item(win, mb, mfile, "退出");
 int medit = sys_gui_menu_add(win, mb, "编辑(E)");
 sys_gui_menu_item(win, mb, medit, "全选  Ctrl+A");
 sys_gui_menu_item(win, mb, medit, "复制  Ctrl+C");
 sys_gui_menu_item(win, mb, medit, "粘贴  Ctrl+V");
 int mhelp = sys_gui_menu_add(win, mb, "帮助(H)");
 sys_gui_menu_item(win, mb, mhelp, "关于");

 int path_label = sys_gui_lbl(win, 12, 42, "未命名文档");
 int ta = sys_gui_tarea(win, 12, 64, 516, 310);
 int sb = sys_gui_statusbar(win, 8, 396, 524, "Ready");
 status_update(win, sb, ta, current_path, dirty, "新建文档");
 sys_gui_win_raise(win);

 for (;;) {
  int n = sys_gui_events(ev, 16);
  for (int i = 0; i < n; i++) {
   int action = ACT_NONE;
   int confirmed_discard = 0;

   if (fd.active) {
    char chosen[64];
    int mode = fd.mode;
    int r = amun_file_dialog_handle(&fd, &ev[i], chosen, sizeof(chosen));
    if (r < 0) resume_after_save = ACT_NONE;
    if (r == 1) {
     if (mode == AFD_OPEN) {
      int got = load_doc(win, ta, chosen);
      if (got >= 0) {
       copy_text(current_path, chosen, sizeof(current_path)); dirty = 0;
       sys_gui_wnd_text(win, path_label, current_path);
       title_update(win, current_path, dirty);
       status_update(win, sb, ta, current_path, dirty, "已打开");
      } else status_update(win, sb, ta, current_path, dirty,
       got == -2 ? "文件超过 2047 字节" : "打开失败");
     } else if (mode == AFD_NEW) {
      int wr = create_empty_doc(chosen);
      if (wr >= 0) {
       copy_text(current_path, chosen, sizeof(current_path));
       sys_gui_tarea_set(win, ta, "", 0);
       dirty = 0;
       sys_gui_wnd_text(win, path_label, current_path);
       title_update(win, current_path, dirty);
      }
      status_update(win, sb, ta, current_path, dirty, wr >= 0 ? "已创建" : "创建失败");
     } else {
      int wr = save_doc(win, ta, chosen);
      if (wr >= 0) {
       copy_text(current_path, chosen, sizeof(current_path));
       dirty = 0;
       sys_gui_wnd_text(win, path_label, current_path);
       title_update(win, current_path, dirty);
      } else resume_after_save = ACT_NONE;
      status_update(win, sb, ta, current_path, dirty, wr >= 0 ? "已保存" : "保存失败");
      if (wr >= 0 && resume_after_save != ACT_NONE) {
       action = resume_after_save; resume_after_save = ACT_NONE;
      }
     }
    }
    if ((r != 0 || ev[i].win == fd.win) && action == ACT_NONE) continue;
   }

   if (confirm.active) {
    int pending = confirm.pending;
    int r = confirm_handle(&confirm, &ev[i]);
    if (r == 1) {
     if (current_path[0]) {
      int wr = save_doc(win, ta, current_path);
      if (wr >= 0) { dirty = 0; action = pending; }
      else status_update(win, sb, ta, current_path, dirty, "保存失败");
     } else {
      char dir[64], name[16];
      split_path(current_path, dir, sizeof(dir), name, sizeof(name));
      if (!name[0]) copy_text(name, "UNTITLED.TXT", sizeof(name));
      resume_after_save = pending;
      amun_file_dialog_open(&fd, win, AFD_SAVE, dir, name);
     }
    } else if (r == 2) { action = pending; confirmed_discard = 1; }
    if ((r != 0 || ev[i].win == confirm.win) && action == ACT_NONE) continue;
   }

   if (ev[i].win == win) {
    if (ev[i].type == GEV_CLOSE) action = ACT_EXIT;
    else if (ev[i].type == GEV_CLICK && ev[i].ctl == mb) {
     int m = (ev[i].ch >> 8) & 0xFF, it = ev[i].ch & 0xFF;
     if (m == mfile) {
      if (it == 0) action = ACT_NEW;
      else if (it == 1) action = ACT_OPEN;
      else if (it == 2) {
       if (current_path[0]) {
        int wr = save_doc(win, ta, current_path);
        if (wr >= 0) dirty = 0;
        title_update(win, current_path, dirty);
        status_update(win, sb, ta, current_path, dirty, wr >= 0 ? "已保存" : "保存失败");
       } else {
        amun_file_dialog_open(&fd, win, AFD_SAVE, 0, "UNTITLED.TXT");
       }
      } else if (it == 3) {
       char dir[64], name[16]; split_path(current_path, dir, sizeof(dir), name, sizeof(name));
       amun_file_dialog_open(&fd, win, AFD_SAVE, dir, name);
      } else if (it == 5) action = ACT_EXIT;
     } else if (m == medit) {
      if (it == 0) { sys_gui_tarea_select_all(win, ta); status_update(win, sb, ta, current_path, dirty, "已全选"); }
      else if (it == 1) status_update(win, sb, ta, current_path, dirty,
       copy_selection(win, ta) > 0 ? "已复制" : "没有选择内容");
      else if (it == 2) {
       int pasted = paste_at_caret(win, ta);
       if (pasted > 0) dirty = 1;
       title_update(win, current_path, dirty);
       status_update(win, sb, ta, current_path, dirty, pasted > 0 ? "已粘贴" : "无法粘贴");
      }
     } else if (m == mhelp && it == 0) {
      status_update(win, sb, ta, current_path, dirty, "AMUN WRITE 0.3 / GUI 0.5");
     }
    } else if ((ev[i].type == GEV_KEY || ev[i].type == GEV_ENTER) && ev[i].ctl == ta) {
     int ch = ev[i].ch;
     if (ch == 1) { sys_gui_tarea_select_all(win, ta); status_update(win, sb, ta, current_path, dirty, "已全选"); }
     else if (ch == 3) status_update(win, sb, ta, current_path, dirty,
      copy_selection(win, ta) > 0 ? "已复制" : "没有选择内容");
     else if (ch == 14) action = ACT_NEW;
     else if (ch == 15) action = ACT_OPEN;
     else if (ch == 19) {
      if (current_path[0]) {
       int wr = save_doc(win, ta, current_path); if (wr >= 0) dirty = 0;
       title_update(win, current_path, dirty);
       status_update(win, sb, ta, current_path, dirty, wr >= 0 ? "已保存" : "保存失败");
      } else amun_file_dialog_open(&fd, win, AFD_SAVE, 0, "UNTITLED.TXT");
     } else if (ch == 22) {
      int pasted = paste_at_caret(win, ta); if (pasted > 0) dirty = 1;
      title_update(win, current_path, dirty);
      status_update(win, sb, ta, current_path, dirty, pasted > 0 ? "已粘贴" : "无法粘贴");
     } else {
      if (text_edit_key(ch)) dirty = 1;
      title_update(win, current_path, dirty);
      status_update(win, sb, ta, current_path, dirty, "");
     }
    } else if (ev[i].type == GEV_CLICK && ev[i].ctl == ta) {
     status_update(win, sb, ta, current_path, dirty, "");
    }
   }

   if ((action == ACT_NEW || action == ACT_OPEN || action == ACT_EXIT) && dirty && !confirmed_discard) {
    confirm_open(&confirm, win, action);
    action = ACT_NONE;
   }
   if (action == ACT_NEW) amun_file_dialog_open(&fd, win, AFD_NEW, 0, "UNTITLED.TXT");
   else if (action == ACT_OPEN) amun_file_dialog_open(&fd, win, AFD_OPEN, 0, "");
   else if (action == ACT_EXIT) { sys_gui_leave(); return 0; }
  }
  sys_sleep(1);
 }
}
