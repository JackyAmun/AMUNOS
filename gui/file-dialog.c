#include "file-dialog.h"
#include <stdio.h>
#include <string.h>

static void copy_text(char *dst, const char *src, int cap) {
 int i = 0;
 if (!src) src = "";
 while (src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
 dst[i] = 0;
}

static void append_text(char *dst, const char *src, int cap) {
 int n = (int)strlen(dst), i = 0;
 if (!src || cap <= 0 || n >= cap) return;
 while (src[i] && n < cap - 1) dst[n++] = src[i++];
 dst[n] = 0;
}

static void set_message(amun_file_dialog_t *d, const char *text) {
 sys_gui_wnd_text(d->win, d->message, text ? text : "");
}

static void path_root(amun_file_dialog_t *d, char drive) {
 if (drive < 'A' || drive > 'C') drive = 'A';
 d->path[0] = drive; d->path[1] = ':'; d->path[2] = '/'; d->path[3] = 0;
 d->page_start = 0; d->next_start = 0; d->page_no = 0; d->page_history[0] = 0;
}

static void refresh(amun_file_dialog_t *d) {
 char display[64];
 char raw[16];
 int source = d->page_start;
 d->count = 0;
 sys_gui_list_set(d->win, d->list, "");
 while (source < 64 && d->count < AFD_VISIBLE) {
  int type = sys_readdir(d->path, source, raw);
  if (type <= 0) break;
  if (strcmp(raw, ".") && strcmp(raw, "..")) {
   copy_text(d->name[d->count], raw, sizeof(d->name[d->count]));
   d->type[d->count] = type;
   d->source_index[d->count] = source;
   if (type == 2) {
    strcpy(display, "[DIR] ");
    append_text(display, raw, sizeof(display));
   } else copy_text(display, raw, sizeof(display));
   sys_gui_list_set(d->win, d->list, display);
   d->count++;
  }
  source++;
 }
 d->next_start = source;
 sys_gui_wnd_text(d->win, d->path_label, d->path);
 set_message(d, d->count ? "选择文件，或在文件名框中输入 8.3 文件名" : "此目录没有可显示的项目");
}

static void enter_directory(amun_file_dialog_t *d, const char *name) {
 int n = (int)strlen(d->path), m = (int)strlen(name);
 if (n + m + 2 >= (int)sizeof(d->path)) {
  set_message(d, "路径过长");
  return;
 }
 if (n && d->path[n - 1] != '/') d->path[n++] = '/';
 for (int i = 0; i < m; i++) d->path[n++] = name[i];
 d->path[n++] = '/'; d->path[n] = 0;
 d->page_start = 0; d->page_no = 0; d->page_history[0] = 0;
 refresh(d);
}

static void go_up(amun_file_dialog_t *d) {
 int n = (int)strlen(d->path);
 if (n <= 3) return;
 if (d->path[n - 1] == '/') n--;
 while (n > 3 && d->path[n - 1] != '/') n--;
 d->path[n] = 0;
 d->page_start = 0; d->page_no = 0; d->page_history[0] = 0;
 refresh(d);
}

static int valid_filename(const char *name) {
 int n = 0, dot = 0, base = 0, ext = 0;
 while (name[n]) {
  unsigned char c = (unsigned char)name[n++];
  if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == ' ') return 0;
  if (c == '.') { if (dot || base == 0) return 0; dot = 1; continue; }
  if (dot) ext++; else base++;
 }
 return base >= 1 && base <= 8 && ext <= 3;
}

static int build_result(amun_file_dialog_t *d, char *out, int cap) {
 char name[64];
 int n = sys_gui_edit_get(d->win, d->filename, name, sizeof(name));
 if (n <= 0) { set_message(d, "请输入文件名"); return 0; }
 if (!valid_filename(name)) { set_message(d, "文件名必须符合 8.3 格式，且不能包含路径字符"); return 0; }
 int pn = (int)strlen(d->path);
 if (pn + n + 1 > cap) { set_message(d, "完整路径过长"); return 0; }
 copy_text(out, d->path, cap);
 append_text(out, name, cap);
 return 1;
}

void amun_file_dialog_close(amun_file_dialog_t *d) {
 if (!d || !d->active) return;
 sys_gui_win_close(d->win);
 d->active = 0;
}

int amun_file_dialog_open(amun_file_dialog_t *d, int parent, int mode,
 const char *initial_path, const char *initial_name) {
 if (!d || d->active) return -1;
 memset(d, 0, sizeof(*d));
 d->mode = mode;
 d->win = sys_gui_dialog(parent, 470, 360,
  mode == AFD_OPEN ? "打开文件" : mode == AFD_SAVE ? "另存为" : "新建文档");
 if (d->win < 0) return -1;
 d->active = 1;
 if (initial_path && initial_path[0]) {
  copy_text(d->path, initial_path, sizeof(d->path));
  int n = (int)strlen(d->path);
  if (n > 0 && d->path[n - 1] != '/' && n < (int)sizeof(d->path) - 1) {
   d->path[n++] = '/'; d->path[n] = 0;
  }
 }
 else {
  sysinfo_t si;
  if (sys_sysinfo(&si) == 0) path_root(d, si.drive_letter);
  else path_root(d, 'A');
 }
 d->path_label = sys_gui_lbl(d->win, 16, 34, d->path);
 sys_gui_lbl(d->win, 16, 60, "位置:");
 d->drive[0] = sys_gui_btn(d->win, 72, 55, "A:");
 d->drive[1] = sys_gui_btn(d->win, 134, 55, "B:");
 d->drive[2] = sys_gui_btn(d->win, 196, 55, "C:");
 sys_gui_lbl(d->win, 16, 94, "文件名:");
 d->filename = sys_gui_edit(d->win, 96, 90, 350);
 if (initial_name) sys_gui_wnd_text(d->win, d->filename, initial_name);
 d->list = sys_gui_list(d->win, 16, 120, 438, 154);
 d->message = sys_gui_lbl(d->win, 16, 282, "");
 d->up = sys_gui_btn(d->win, 16, 312, "上一级");
 d->prev = sys_gui_btn(d->win, 94, 312, "上一页");
 d->next = sys_gui_btn(d->win, 172, 312, "下一页");
 d->accept = sys_gui_btn(d->win, 280, 312,
  mode == AFD_OPEN ? "打开" : mode == AFD_SAVE ? "保存" : "创建");
 d->cancel = sys_gui_btn(d->win, 360, 312, "取消");
 refresh(d);
 return 0;
}

int amun_file_dialog_handle(amun_file_dialog_t *d, const gui_ev_t *ev,
 char *out_path, int out_max) {
 if (!d || !d->active || !ev || ev->win != d->win) return 0;
 if (ev->type == GEV_CLOSE) { d->active = 0; return -1; }
 if (ev->type == GEV_CLICK && ev->ctl == d->list) {
  int i = ev->ch;
  if (i >= 0 && i < d->count) {
   if (d->type[i] == 2) enter_directory(d, d->name[i]);
   else { sys_gui_wnd_text(d->win, d->filename, d->name[i]); set_message(d, "已选择文件"); }
  }
  return 0;
 }
 if (ev->type == GEV_CLICK) {
  if (ev->ctl == d->cancel) { amun_file_dialog_close(d); return -1; }
  if (ev->ctl == d->up) { go_up(d); return 0; }
  if (ev->ctl == d->prev) {
   if (d->page_no > 0) d->page_no--;
   d->page_start = d->page_history[d->page_no];
   refresh(d); return 0;
  }
  if (ev->ctl == d->next) {
   if (d->count == AFD_VISIBLE && d->page_no < 7) {
    d->page_start = d->next_start;
    d->page_history[++d->page_no] = d->page_start;
    refresh(d);
   }
   return 0;
  }
  for (int k = 0; k < 3; k++) if (ev->ctl == d->drive[k]) {
   path_root(d, (char)('A' + k)); refresh(d); return 0;
  }
 }
 if ((ev->type == GEV_CLICK && ev->ctl == d->accept) ||
     (ev->type == GEV_ENTER && ev->ctl == d->filename)) {
  if (!build_result(d, out_path, out_max)) return 0;
  if (d->mode == AFD_OPEN) {
   FILE *probe = fopen(out_path, "r");
   if (!probe) { set_message(d, "文件不存在或无法打开"); return 0; }
   fclose(probe);
  }
  amun_file_dialog_close(d);
  return 1;
 }
 return 0;
}
