#ifndef AMUN_GUI_FILE_DIALOG_H
#define AMUN_GUI_FILE_DIALOG_H

#include "syscall.h"

#define AFD_OPEN 0
#define AFD_SAVE 1
#define AFD_NEW  2
#define AFD_VISIBLE 9

typedef struct {
 int active;
 int mode;
 int win;
 int list;
 int filename;
 int path_label;
 int message;
 int up;
 int prev;
 int next;
 int accept;
 int cancel;
 int drive[3];
 int page_start;
 int next_start;
 int page_no;
 int page_history[8];
 int count;
 int source_index[AFD_VISIBLE];
 int type[AFD_VISIBLE];
 char name[AFD_VISIBLE][16];
 char path[64];
} amun_file_dialog_t;

int amun_file_dialog_open(amun_file_dialog_t *d, int parent, int mode,
 const char *initial_path, const char *initial_name);
int amun_file_dialog_handle(amun_file_dialog_t *d, const gui_ev_t *ev,
 char *out_path, int out_max);
void amun_file_dialog_close(amun_file_dialog_t *d);

#endif
