/* AMUN SHEET 1.0 Dev: bounded DFLAT spreadsheet. */
#include "dflat.h"
#include "syscall.h"

#define COLS 26
#define ROWS 64
#define CELL_LEN 40
#define FILE_LEN 64
#define DEFAULT_WIDTH 10
#define MIN_WIDTH 6
#define MAX_WIDTH 20
#define GRID_X 4
#define FORMULA_ROWS 1
#define HEADER_Y 2
#define GRID_Y 3
#define INPUT_LEFT 10
#define INPUT_RIGHT_GAP 0
#define SCALE 1000LL
#define VALUE_LIMIT 999999999000LL

enum { OK, ERR_SYNTAX, ERR_VALUE, ERR_DIVZERO, ERR_CYCLE, ERR_NUM, ERR_REF };
enum { TYPE_AUTO, TYPE_TEXT, TYPE_NUMBER };
enum { SHEET_GOTO = 1000, SHEET_SUM, SHEET_AVERAGE, SHEET_MIN,
       SHEET_MAX, SHEET_COUNT, SHEET_INSERT_ROW, SHEET_DELETE_ROW,
       SHEET_INSERT_COL, SHEET_DELETE_COL, SHEET_WIDTH, SHEET_SORT_UP,
       SHEET_SORT_DOWN, SHEET_TYPE_AUTO, SHEET_TYPE_TEXT,
       SHEET_TYPE_NUMBER, SHEET_NEXT, SHEET_THEME_AMBER,
       SHEET_THEME_BLUE };

typedef struct {
    char text[CELL_LEN];
    long long value;
    unsigned cache_generation;
    unsigned char busy;
    unsigned char error;
    unsigned char type;
} Cell;

typedef struct {
    const char *p;
    int error;
    int depth;
} Parser;

typedef struct {
    Cell grid[ROWS][COLS];
    unsigned char widths[COLS];
    unsigned generation_id;
    int current_row, current_col, first_row, first_col;
    int editing, dirty, was_dirty, dragging, closing_ok;
    int undo_r, undo_c, can_undo, undo_kind;
    char edit_buf[CELL_LEN], name[FILE_LEN], message[64];
    char before_edit[CELL_LEN], previous[CELL_LEN], search[CELL_LEN];
    WINDOW input;
} SheetDoc;

static SheetDoc *doc;
static WINDOW app_window, active_window;
static int sheet_theme;
#define cells (doc->grid)
#define col_width (doc->widths)
#define generation (doc->generation_id)
#define row (doc->current_row)
#define col (doc->current_col)
#define top_row (doc->first_row)
#define left_col (doc->first_col)
#define edit_mode (doc->editing)
#define changed (doc->dirty)
#define edit_was_changed (doc->was_dirty)
#define scrollbar_drag (doc->dragging)
#define undo_row (doc->undo_r)
#define undo_col (doc->undo_c)
#define undo_valid (doc->can_undo)
#define undo_type (doc->undo_kind)
#define edit (doc->edit_buf)
#define filename (doc->name)
#define notice (doc->message)
#define edit_original (doc->before_edit)
#define undo_text (doc->previous)
#define find_text (doc->search)
#define formula_box (doc->input)

static char pending[ROWS][COLS][CELL_LEN];
static unsigned char pending_type[ROWS][COLS], pending_width[COLS];
static Cell sort_cells[ROWS];
static long long sort_values[ROWS];
static void draw_grid(WINDOW w);
static void draw_input(WINDOW w);
static void draw_status(WINDOW w);
static WINDOW open_sheet(WINDOW app, const char *path);

char DFlatApplication[] = "AMUN SHEET 1.0 Dev";
extern int ApplicationProc(WINDOW, MESSAGE, PARAM, PARAM);

DEFMENU(SheetMenu)
    POPDOWN("~File", NULL, "Workbooks")
        SELECTION("~New", ID_NEW, CTRL_N, 0)
        SELECTION("~Open...", ID_OPEN, CTRL_O, 0)
        SELECTION("~Save", ID_SAVE, CTRL_S, 0)
        SELECTION("Save ~As...", ID_SAVEAS, 0, 0)
        SELECTION("~Close", ID_CLOSE, CTRL_F4, 0)
        SEPARATOR
        SELECTION("E~xit", ID_EXIT, ALT_X, 0)
    ENDPOPDOWN
    POPDOWN("~Edit", NULL, "Cell editing")
        SELECTION("~Undo", ID_UNDO, CTRL_Z, 0)
        SEPARATOR
        SELECTION("Cu~t", ID_CUT, CTRL_X, 0)
        SELECTION("~Copy", ID_COPY, 0, 0)
        SELECTION("~Paste", ID_PASTE, CTRL_V, 0)
        SELECTION("C~lear cell", ID_CLEAR, DEL, 0)
        SEPARATOR
        SELECTION("~Edit cell", ID_INSERT, F2, 0)
    ENDPOPDOWN
    POPDOWN("~Search", NULL, "Find cells")
        SELECTION("~Find...", ID_SEARCH, CTRL_F, 0)
        SELECTION("Find ~next", ID_SEARCHNEXT, F3, 0)
        SELECTION("~Go to...", SHEET_GOTO, F5, 0)
    ENDPOPDOWN
    POPDOWN("~View", NULL, "Display themes")
        SELECTION("~Amber / Black", SHEET_THEME_AMBER, 0, 0)
        SELECTION("~Deep Blue", SHEET_THEME_BLUE, 0, 0)
    ENDPOPDOWN
    POPDOWN("F~ormula", NULL, "Cell formulas")
        SELECTION("Insert ~SUM...", SHEET_SUM, 0, 0)
        SELECTION("Insert ~AVERAGE...", SHEET_AVERAGE, 0, 0)
        SELECTION("Insert ~MIN...", SHEET_MIN, 0, 0)
        SELECTION("Insert MA~X...", SHEET_MAX, 0, 0)
        SELECTION("Insert ~COUNT...", SHEET_COUNT, 0, 0)
    ENDPOPDOWN
    POPDOWN("~Data", NULL, "Rows and columns")
        SELECTION("Insert ~row", SHEET_INSERT_ROW, 0, 0)
        SELECTION("Delete r~ow", SHEET_DELETE_ROW, 0, 0)
        SELECTION("Insert ~column", SHEET_INSERT_COL, 0, 0)
        SELECTION("Delete co~lumn", SHEET_DELETE_COL, 0, 0)
        SEPARATOR
        SELECTION("Sort ~ascending", SHEET_SORT_UP, 0, 0)
        SELECTION("Sort ~descending", SHEET_SORT_DOWN, 0, 0)
    ENDPOPDOWN
    POPDOWN("F~ormat", NULL, "Cell format")
        SELECTION("Column ~width...", SHEET_WIDTH, 0, 0)
        SEPARATOR
        SELECTION("Type: ~Auto", SHEET_TYPE_AUTO, 0, 0)
        SELECTION("Type: ~Text", SHEET_TYPE_TEXT, 0, 0)
        SELECTION("Type: ~Number", SHEET_TYPE_NUMBER, 0, 0)
    ENDPOPDOWN
    POPDOWN("~Window", NULL, "Open worksheets")
        SELECTION("~Next worksheet", SHEET_NEXT, F6, 0)
        SELECTION("~Close worksheet", ID_CLOSE, CTRL_F4, 0)
    ENDPOPDOWN
    POPDOWN("~Help", NULL, "About")
        SELECTION("~Keys", ID_KEYSHELP, F1, 0)
        SELECTION("~About", ID_ABOUT, 0, 0)
    ENDPOPDOWN
ENDMENU

/* menus.c is also linked by EDIT and requires these hooks. */
void PrepFileMenu(void *wnd, struct Menu *menu) { (void)wnd; (void)menu; }
void PrepEditMenu(void *wnd, struct Menu *menu) { (void)wnd; (void)menu; }
void PrepSearchMenu(void *wnd, struct Menu *menu) { (void)wnd; (void)menu; }

static int column_x(int c)
{
    int x = GRID_X;
    while (c-- > left_col) x += col_width[c] + 1;
    return x;
}

static int grid_right(WINDOW w)
{
    /* Keep the last client column for the DFLAT vertical scrollbar. */
    return ClientWidth(w) - 2;
}

static int visible_cols(WINDOW w)
{
    int c = left_col, x = GRID_X, right = grid_right(w);
    while (c < COLS && x + col_width[c] + 1 <= right) {
        x += col_width[c] + 1;
        c++;
    }
    /* Stretch the final visible column to the scrollbar instead of leaving
     * an unused black strip on the right side of the worksheet. */
    if (c < COLS && right - x > 2) c++;
    return c > left_col ? c - left_col : 1;
}

static int display_width(WINDOW w, int c)
{
    int last = left_col + visible_cols(w) - 1;
    if (c == last) {
        int width = grid_right(w) - column_x(c) + 1;
        if (width > 0) return width;
    }
    return col_width[c];
}

static int visible_rows(WINDOW w)
{
    int n = ClientHeight(w) - GRID_Y - 2;
    return n < ROWS ? n : ROWS;
}

static void address(int c, int r, char *out)
{
    out[0] = 'A' + c;
    if (r < 9) {
        out[1] = '1' + r;
        out[2] = 0;
    } else {
        out[1] = '0' + (r + 1) / 10;
        out[2] = '0' + (r + 1) % 10;
        out[3] = 0;
    }
}

static void skip_spaces(Parser *p)
{
    while (*p->p == ' ' || *p->p == '\t') p->p++;
}

static long long magnitude(long long x) { return x < 0 ? -x : x; }

static int is_label(const char *text)
{
    return *text && *text != '=' &&
        (*text < '0' || *text > '9') && *text != '-' && *text != '+';
}

static void check_limit(Parser *p, long long v)
{
    if (magnitude(v) > VALUE_LIMIT) p->error = ERR_NUM;
}

static int reference(Parser *p, int *c, int *r)
{
    int n = 0;
    skip_spaces(p);
    if (*p->p < 'A' || *p->p > 'Z') return 0;
    *c = *p->p++ - 'A';
    if (*p->p < '1' || *p->p > '9') return 0;
    while (*p->p >= '0' && *p->p <= '9') {
        n = n * 10 + *p->p++ - '0';
        if (n > ROWS) return 0;
    }
    if (!n) return 0;
    *r = n - 1;
    return 1;
}

static long long expression(Parser *p);

static long long calculate(int r, int c, int depth, int *error)
{
    Cell *cell = &cells[r][c];
    Parser p;
    long long value;
    if (!cell->text[0]) return 0;
    if (cell->type == TYPE_TEXT ||
        (cell->type == TYPE_AUTO && is_label(cell->text))) {
        *error = ERR_VALUE; return 0;
    }
    if (cell->cache_generation == generation) {
        *error = cell->error;
        return cell->value;
    }
    if (cell->busy) { *error = ERR_CYCLE; return 0; }
    if (depth > 32) { *error = ERR_CYCLE; return 0; }
    cell->busy = 1;
    p.p = cell->text + (cell->text[0] == '=');
    p.error = OK;
    p.depth = depth;
    value = expression(&p);
    skip_spaces(&p);
    if (*p.p && !p.error) p.error = ERR_SYNTAX;
    check_limit(&p, value);
    cell->busy = 0;
    cell->value = p.error ? 0 : value;
    cell->error = p.error;
    cell->cache_generation = generation;
    *error = p.error;
    return cell->value;
}

static long long factor(Parser *p)
{
    long long v = 0;
    int c1, r1, c2, r2, c, r, count = 0, error = OK;
    int average;
    skip_spaces(p);
    if (*p->p == '+') { p->p++; return factor(p); }
    if (*p->p == '-') { p->p++; return -factor(p); }
    if (*p->p == '(') {
        p->p++;
        v = expression(p);
        skip_spaces(p);
        if (*p->p == ')') p->p++;
        else p->error = ERR_SYNTAX;
        return v;
    }
    average = !strncmp(p->p, "AVERAGE(", 8);
    if (average || !strncmp(p->p, "SUM(", 4) ||
        !strncmp(p->p, "MIN(", 4) || !strncmp(p->p, "MAX(", 4) ||
        !strncmp(p->p, "COUNT(", 6)) {
        int aggregate = average ? 1 : !strncmp(p->p, "MIN(", 4) ? 2 :
            !strncmp(p->p, "MAX(", 4) ? 3 :
            !strncmp(p->p, "COUNT(", 6) ? 4 : 0;
        p->p += average ? 8 : aggregate == 4 ? 6 : 4;
        if (!reference(p, &c1, &r1)) { p->error = ERR_SYNTAX; return 0; }
        skip_spaces(p);
        c2 = c1; r2 = r1;
        if (*p->p == ':') {
            p->p++;
            if (!reference(p, &c2, &r2)) { p->error = ERR_SYNTAX; return 0; }
        }
        skip_spaces(p);
        if (*p->p != ')') { p->error = ERR_SYNTAX; return 0; }
        p->p++;
        if (c1 > c2) { c = c1; c1 = c2; c2 = c; }
        if (r1 > r2) { r = r1; r1 = r2; r2 = r; }
        for (r = r1; r <= r2; r++)
            for (c = c1; c <= c2; c++) {
                Cell *cell = &cells[r][c];
                long long item;
                if (!cell->text[0] || cell->type == TYPE_TEXT ||
                    (cell->type == TYPE_AUTO && is_label(cell->text))) continue;
                item = calculate(r, c, p->depth + 1, &error);
                if (error) { p->error = error; return 0; }
                if (aggregate == 2) { if (!count || item < v) v = item; }
                else if (aggregate == 3) { if (!count || item > v) v = item; }
                else if (aggregate != 4) v += item;
                count++;
                check_limit(p, v);
                if (p->error) return 0;
            }
        if (aggregate == 4) return count * SCALE;
        return average ? (count ? v / count : 0) : v;
    }
    if (!strncmp(p->p, "#REF!", 5)) {
        p->p += 5; p->error = ERR_REF; return 0;
    }
    if (*p->p >= 'A' && *p->p <= 'Z') {
        if (!reference(p, &c1, &r1)) { p->error = ERR_SYNTAX; return 0; }
        v = calculate(r1, c1, p->depth + 1, &error);
        if (error) p->error = error;
        return v;
    }
    if (*p->p < '0' || *p->p > '9') { p->error = ERR_SYNTAX; return 0; }
    while (*p->p >= '0' && *p->p <= '9') {
        v = v * 10 + (*p->p++ - '0') * SCALE;
        if (v > VALUE_LIMIT) { p->error = ERR_NUM; return 0; }
    }
    if (*p->p == '.') {
        long long place = 100;
        p->p++;
        while (*p->p >= '0' && *p->p <= '9') {
            if (!place) { p->error = ERR_SYNTAX; return 0; }
            v += (*p->p++ - '0') * place;
            place /= 10;
        }
    }
    return v;
}

static long long term(Parser *p)
{
    long long v = factor(p), rhs;
    char op;
    while (!p->error) {
        skip_spaces(p);
        op = *p->p;
        if (op != '*' && op != '/') break;
        p->p++;
        rhs = factor(p);
        if (p->error) break;
        if (op == '/') {
            if (!rhs) { p->error = ERR_DIVZERO; break; }
            v = (v * SCALE) / rhs;
        } else {
            if (rhs && magnitude(v) > (VALUE_LIMIT * SCALE) / magnitude(rhs)) {
                p->error = ERR_NUM;
                break;
            }
            v = (v * rhs) / SCALE;
        }
        check_limit(p, v);
    }
    return v;
}

static long long expression(Parser *p)
{
    long long v = term(p), rhs;
    char op;
    while (!p->error) {
        skip_spaces(p);
        op = *p->p;
        if (op != '+' && op != '-') break;
        p->p++;
        rhs = term(p);
        if (p->error) break;
        v += op == '+' ? rhs : -rhs;
        check_limit(p, v);
    }
    return v;
}

static const char *error_text(int error)
{
    if (error == ERR_DIVZERO) return "#DIV/0";
    if (error == ERR_CYCLE) return "#CYCLE";
    if (error == ERR_NUM) return "#NUM";
    if (error == ERR_VALUE) return "#VALUE";
    if (error == ERR_REF) return "#REF!";
    return "#ERR";
}

static void number_text(long long value, char *out)
{
    char digits[18];
    int count = 0, i;
    unsigned long long whole, frac;
    if (value < 0) { *out++ = '-'; value = -value; }
    whole = (unsigned long long)value / SCALE;
    frac = (unsigned long long)value % SCALE;
    do { digits[count++] = '0' + whole % 10; whole /= 10; } while (whole);
    while (count) *out++ = digits[--count];
    if (frac) {
        *out++ = '.';
        out[0] = '0' + frac / 100;
        out[1] = '0' + (frac / 10) % 10;
        out[2] = '0' + frac % 10;
        i = 2;
        while (i >= 0 && out[i] == '0') i--;
        out += i + 1;
    }
    *out = 0;
}

static void mark_changed(void)
{
    generation++;
    if (!generation) {
        int r, c;
        generation = 1;
        for (r = 0; r < ROWS; r++)
            for (c = 0; c < COLS; c++) cells[r][c].cache_generation = 0;
    }
    changed = 1;
}

static void set_cell(WINDOW w, const char *text)
{
    Cell *cell = &cells[row][col];
    if (!strcmp(cell->text, text)) return;
    strcpy(undo_text, cell->text);
    undo_row = row; undo_col = col; undo_type = cell->type; undo_valid = 1;
    strcpy(cell->text, text);
    mark_changed();
    draw_grid(w);
    draw_input(w);
    draw_status(w);
}

static void blank_line(char *line, int width)
{
    memset(line, ' ', width);
    line[width] = 0;
}

static void draw_status(WINDOW w)
{
    char text[80], detail[80], addr[5], value[24];
    Cell *cell = &cells[row][col];
    int error = OK;
    address(col, row, addr);
    if (cell->type == TYPE_TEXT ||
        (cell->type == TYPE_AUTO && is_label(cell->text))) strcpy(value, "Text");
    else if (cell->text[0]) {
        long long result = calculate(row, col, 0, &error);
        if (error) strcpy(value, error_text(error));
        else number_text(result, value);
    } else strcpy(value, "Empty");
    snprintf(text, sizeof text, "%s  %s  %s%s", addr, value, notice,
             changed ? "  *" : "");
    SendMessage(app_window, ADDSTATUS, (PARAM)text, 0);
    snprintf(detail, sizeof detail, " %s%s   %s   %s",
        filename, changed ? " *" : "",
        edit_mode ? "EDIT" : "READY",
        cell->type == TYPE_TEXT ? "TEXT" :
        cell->type == TYPE_NUMBER ? "NUMBER" : "AUTO");
    {
        char line[80];
        int i, width = ClientWidth(app_window);
        blank_line(line, width);
        memcpy(line, detail, strlen(detail) < width ? strlen(detail) : width);
        SetStandardColor(app_window);
        for (i = 0; i < width; i++)
            PutWindowChar(app_window, line[i], i, ClientHeight(app_window)-1);
    }
}

static void draw_input(WINDOW w)
{
    char addr[5];
    int width = ClientWidth(w), i;
    int foreground = WndForeground(w), background = WndBackground(w);

    /* The single-row formula bar is deliberately separate from the grid:
     * address at left, formula field consuming the rest of the window. */
    WindowClientColor(w, BLACK, WHITE);
    SetStandardColor(w);
    for (i = 0; i < width; i++) PutWindowChar(w, ' ', i, 0);
    address(col, row, addr);
    PutWindowChar(w, ' ', 0, 0);
    PutWindowChar(w, addr[0], 1, 0);
    PutWindowChar(w, addr[1], 2, 0);
    if (addr[2]) PutWindowChar(w, addr[2], 3, 0);
    PutWindowChar(w, 'f', 5, 0);
    PutWindowChar(w, 'x', 6, 0);
    PutWindowChar(w, ':', 7, 0);
    if (INPUT_LEFT > 0 && INPUT_LEFT - 1 < width)
        PutWindowChar(w, '|', INPUT_LEFT-1, 0);

    /* One black breathing row separates the formula bar and column labels. */
    WindowClientColor(w, foreground, background);
    SetStandardColor(w);
    for (i = 0; i < width; i++) PutWindowChar(w, ' ', i, FORMULA_ROWS);
    if (formula_box) {
        if (!edit_mode)
            SendMessage(formula_box, SETTEXT, (PARAM)cells[row][col].text, 0);
        SendMessage(formula_box, PAINT, 0, 0);
    }
}

static void draw_header(WINDOW w)
{
    int i, x, j, n = visible_cols(w), right = grid_right(w);
    int foreground = WndForeground(w), background = WndBackground(w);
    SetStandardColor(w);
    for (x = 0; x <= right; x++) PutWindowChar(w, ' ', x, HEADER_Y);
    for (i = 0; i < n; i++) {
        int c = left_col+i;
        int cw = display_width(w, c);
        x = column_x(c);
        if (c == col) {
            WindowClientColor(w, WHITE, BLUE);
            SetStandardColor(w);
            for (j = 0; j < cw; j++) PutWindowChar(w, ' ', x+j, HEADER_Y);
        }
        if (c < COLS && cw > 0)
            PutWindowChar(w, 'A' + c, x + (cw-1)/2, HEADER_Y);
        if (c == col) {
            WindowClientColor(w, foreground, background);
            SetStandardColor(w);
        }
    }
}

static void draw_row(WINDOW w, int slot)
{
    char value[24];
    int n = visible_cols(w), actual = top_row + slot;
    int i, x, j, error, right = grid_right(w);
    int foreground = WndForeground(w), background = WndBackground(w);
    int y = GRID_Y+slot;
    SetStandardColor(w);
    for (x = 0; x <= right; x++) PutWindowChar(w, ' ', x, y);
    if (actual < ROWS) {
        if (actual == row) {
            WindowClientColor(w, WHITE, BLUE);
            SetStandardColor(w);
            for (j = 0; j < GRID_X-1; j++) PutWindowChar(w, ' ', j, y);
        }
        PutWindowChar(w, actual < 9 ? ' ' : '0' + (actual+1) / 10, 1,
                      y);
        PutWindowChar(w, '1' + actual % 10, 2, y);
        if (actual == row) {
            WindowClientColor(w, foreground, background);
            SetStandardColor(w);
        }
    }
    for (i = 0; i < n; i++) {
        Cell *cell;
        int c = left_col+i;
        int width = display_width(w, c);
        int selected = actual == row && c == col;
        x = column_x(c);
        if (selected) {
            WindowClientColor(w, BLACK, WHITE);
            SetStandardColor(w);
            for (j = 0; j < width; j++) PutWindowChar(w, ' ', x+j, y);
        }
        if (actual >= ROWS || c >= COLS) {
            if (selected) {
                WindowClientColor(w, foreground, background);
                SetStandardColor(w);
            }
            continue;
        }
        cell = &cells[actual][c];
        value[0] = 0;
        if (!cell->text[0]) {
            if (selected) {
                WindowClientColor(w, foreground, background);
                SetStandardColor(w);
            }
            continue;
        }
        if (cell->type == TYPE_TEXT ||
            (cell->type == TYPE_AUTO && is_label(cell->text))) {
            strncpy(value, cell->text, sizeof value-1);
            value[sizeof value-1] = 0;
        } else {
            error = OK;
            number_text(calculate(actual, left_col+i, 0, &error), value);
            if (error) strcpy(value, error_text(error));
        }
        if (strlen(value) > width &&
            cell->type != TYPE_TEXT && !is_label(cell->text)) {
            for (j = 0; j < width; j++)
                PutWindowChar(w, '#', x+j, y);
        } else {
            int offset = cell->type == TYPE_TEXT || is_label(cell->text) ?
                0 : width - (int)strlen(value);
            if (offset < 0) offset = 0;
            for (j = 0; j < width-offset && value[j]; j++)
                PutWindowChar(w, value[j], x+offset+j, y);
        }
        if (selected) {
            WindowClientColor(w, foreground, background);
            SetStandardColor(w);
        }
    }
}

static void draw_scrollbars(WINDOW w)
{
    char line[80];
    int width = ClientWidth(w), track = width-2, max_top;
    int n = visible_rows(w), y = GRID_Y+n;
    blank_line(line, width);
    memset(line, SCROLLBARCHAR, width);
    line[0] = LEFTSCROLLBOX;
    line[width-1] = RIGHTSCROLLBOX;
    line[1 + (left_col * (track-1) / (COLS-1))] = SCROLLBOXCHAR;
    SetStandardColor(w);
    PutWindowLine(w, line, 0, y);
    max_top = ROWS - n;
    w->VScrollBox = 1 + (max_top ? top_row * (WindowHeight(w)-5) / max_top : 0);
    SendMessage(w, BORDER, 0, 0);
    for (y = 2; y < WindowHeight(w)-2; y++)
        wputch(w, y-1 == w->VScrollBox ? '#' : SIDE,
               WindowWidth(w)-1, y);
    wputch(w, '^', WindowWidth(w)-1, 1);
    wputch(w, 'v', WindowWidth(w)-1, WindowHeight(w)-2);
}

static void draw_grid(WINDOW w)
{
    int i, n = visible_rows(w);
    draw_header(w);
    for (i = 0; i < n; i++) draw_row(w, i);
    draw_scrollbars(w);
}

static void draw_all(WINDOW w)
{
    draw_input(w);
    draw_grid(w);
    draw_status(w);
}

static void select_cell(WINDOW w, int new_row, int new_col)
{
    int old_row = row, old_top = top_row, old_left = left_col;
    if (new_row < 0) new_row = 0;
    if (new_row >= ROWS) new_row = ROWS-1;
    if (new_col < 0) new_col = 0;
    if (new_col >= COLS) new_col = COLS-1;
    row = new_row; col = new_col;
    if (row < top_row) top_row = row;
    if (row >= top_row + visible_rows(w)) top_row = row - visible_rows(w) + 1;
    if (col < left_col) left_col = col;
    if (col >= left_col + visible_cols(w)) left_col = col - visible_cols(w) + 1;
    if (old_top != top_row || old_left != left_col) draw_grid(w);
    else {
        if (old_row >= top_row && old_row < top_row+visible_rows(w))
            draw_row(w, old_row-top_row);
        draw_row(w, row-top_row);
        draw_header(w);
    }
    draw_input(w);
    draw_status(w);
}

static void scroll_view(WINDOW w, int new_top, int new_left)
{
    int max_top = ROWS-visible_rows(w);
    if (new_top < 0) new_top = 0;
    if (new_top > max_top) new_top = max_top;
    if (new_left < 0) new_left = 0;
    if (new_left >= COLS) new_left = COLS-1;
    if (top_row == new_top && left_col == new_left) return;
    top_row = new_top; left_col = new_left;
    if (row < top_row) row = top_row;
    if (row >= top_row+visible_rows(w)) row = top_row+visible_rows(w)-1;
    if (col < left_col) col = left_col;
    if (col >= left_col+visible_cols(w)) col = left_col+visible_cols(w)-1;
    draw_grid(w); draw_input(w); draw_status(w);
}

static int write_full(int fd, const char *data, int size)
{
    while (size > 0) {
        long done = sys_write(fd, data, size);
        if (done <= 0) return -1;
        size -= (int)done;
        data += done;
    }
    return 0;
}

static int valid_filename(const char *name)
{
    int i, dot = -1, length = strlen(name);
    if (!length || length >= FILE_LEN) return 0;
    for (i = 0; i < length; i++) {
        if (name[i] == '.' ) dot = i;
        if (name[i] == '\n' || name[i] == '\r' || name[i] == ',') return 0;
    }
    return dot >= 0 && name[dot+1] && name[dot+2] && name[dot+3] &&
        !name[dot+4] && toupper(name[dot+1]) == 'A' &&
        toupper(name[dot+2]) == 'S' && toupper(name[dot+3]) == 'H';
}

static int save_file(const char *path)
{
    int fd, r, c, ok = 1;
    char target[FILE_LEN + 2];
    char line[96];
    if (!valid_filename(path)) return 0;
    /* New worksheets have a plain 8.3 name.  Pin those saves to the
     * mounted volume root so launching SHEET from BIN or another directory
     * cannot hide the workbook from ordinary disk tools. */
    if (!strchr(path, '/') && !strchr(path, ':')) {
        target[0] = '/';
        strncpy(target + 1, path, FILE_LEN);
        target[FILE_LEN + 1] = 0;
    } else {
        strncpy(target, path, FILE_LEN + 1);
        target[FILE_LEN + 1] = 0;
    }
    fd = sys_open(target, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return 0;
    if (write_full(fd, "AMUNSHEET 2\n", 12) < 0) ok = 0;
    for (c = 0; c < COLS && ok; c++) {
        sprintf(line, "W,%d,%d\n", c, col_width[c]);
        if (write_full(fd, line, strlen(line)) < 0) ok = 0;
    }
    for (r = 0; r < ROWS && ok; r++)
        for (c = 0; c < COLS && ok; c++)
            if (cells[r][c].text[0] || cells[r][c].type != TYPE_AUTO) {
                sprintf(line, "%d,%d,%d,%s\n", c, r,
                    cells[r][c].type, cells[r][c].text);
                if (write_full(fd, line, strlen(line)) < 0) ok = 0;
            }
    if (sys_close(fd) < 0) ok = 0;
    if (ok) { strcpy(filename, path); changed = 0; strcpy(notice, "Saved"); }
    return ok;
}

static int parse_record(char *line, int version)
{
    int c = 0, r = 0, type = TYPE_AUTO;
    char *p = line;
    if (version == 2 && *p == 'W' && p[1] == ',') {
        int width = 0;
        p += 2;
        if (*p < '0' || *p > '9') return 0;
        while (*p >= '0' && *p <= '9') {
            c = c*10 + *p++ - '0'; if (c >= COLS) return 0;
        }
        if (*p++ != ',') return 0;
        if (*p < '0' || *p > '9') return 0;
        while (*p >= '0' && *p <= '9') width = width*10 + *p++ - '0';
        if (*p || width < MIN_WIDTH || width > MAX_WIDTH) return 0;
        pending_width[c] = width;
        return 1;
    }
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') {
        c = c*10 + *p++ - '0';
        if (c >= COLS) return 0;
    }
    if (*p != ',') return 0;
    p++;
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') {
        r = r*10 + *p++ - '0';
        if (r >= ROWS) return 0;
    }
    if (*p != ',') return 0;
    p++;
    if (version == 2) {
        if (*p < '0' || *p > '2' || p[1] != ',') return 0;
        type = *p - '0'; p += 2;
    }
    if (strlen(p) >= CELL_LEN) return 0;
    strcpy(pending[r][c], p);
    pending_type[r][c] = type;
    return 1;
}

static int load_file(const char *path)
{
    char chunk[512], line[96];
    int fd, pos = 0, first = 1, version = 0, n, i, r, c, ok = 1;
    if (!valid_filename(path)) return 0;
    fd = sys_open(path, O_RDONLY);
    if (fd < 0) return 0;
    memset(pending, 0, sizeof pending);
    memset(pending_type, 0, sizeof pending_type);
    for (c = 0; c < COLS; c++) pending_width[c] = DEFAULT_WIDTH;
    while ((n = (int)sys_read(fd, chunk, sizeof chunk)) > 0 && ok)
        for (i = 0; i < n && ok; i++) {
            if (chunk[i] == '\r') continue;
            if (chunk[i] == '\n') {
                line[pos] = 0;
                if (first) {
                    if (!strcmp(line, "AMUNSHEET 1")) version = 1;
                    else if (!strcmp(line, "AMUNSHEET 2")) version = 2;
                    else ok = 0;
                    first = 0;
                } else if (pos) ok = parse_record(line, version);
                pos = 0;
            } else if (pos < (int)sizeof line - 1) line[pos++] = chunk[i];
            else ok = 0;
        }
    if (n < 0 || first || pos) ok = 0;
    sys_close(fd);
    if (!ok) return 0;
    for (r = 0; r < ROWS; r++)
        for (c = 0; c < COLS; c++) {
            strcpy(cells[r][c].text, pending[r][c]);
            cells[r][c].busy = 0;
            cells[r][c].type = pending_type[r][c];
        }
    memcpy(col_width, pending_width, sizeof col_width);
    generation++;
    strcpy(filename, path);
    row = col = top_row = left_col = edit_mode = changed = 0;
    strcpy(notice, "Opened");
    return 1;
}

static int rewrite_formula(char *dst, const char *src, int axis, int at, int delta)
{
    int used = 0;
    if (*src != '=') { strcpy(dst, src); return 1; }
    while (*src) {
        if (*src >= 'A' && *src <= 'Z' &&
            src[1] >= '0' && src[1] <= '9') {
            int c = *src++ - 'A', r = 0, target;
            char replacement[12];
            while (*src >= '0' && *src <= '9') r = r*10 + *src++ - '0';
            target = axis == 0 ? r-1 : c;
            if (target == at && delta < 0) strcpy(replacement, "#REF!");
            else {
                if (target >= at) target += delta;
                if (axis == 0) r = target + 1;
                else c = target;
                if (r < 1 || r > ROWS || c < 0 || c >= COLS)
                    strcpy(replacement, "#REF!");
                else address(c, r-1, replacement);
            }
            if (used + (int)strlen(replacement) >= CELL_LEN) return 0;
            strcpy(dst + used, replacement);
            used += strlen(replacement);
        } else {
            if (used + 1 >= CELL_LEN) return 0;
            dst[used++] = *src++;
        }
    }
    dst[used] = 0;
    return 1;
}

static int structural_change(WINDOW w, int axis, int insert)
{
    int at = axis == 0 ? row : col;
    int r, c, i, delta = insert ? 1 : -1;
    char rewritten[CELL_LEN];
    if (insert) {
        for (i = 0; i < (axis == 0 ? COLS : ROWS); i++) {
            Cell *tail = axis == 0 ? &cells[ROWS-1][i] : &cells[i][COLS-1];
            if (tail->text[0] || tail->type != TYPE_AUTO) {
                ErrorMessage("Last row/column is not empty."); return 0;
            }
        }
    }
    for (r = 0; r < ROWS; r++)
        for (c = 0; c < COLS; c++)
            if (cells[r][c].text[0] == '=' &&
                !rewrite_formula(rewritten, cells[r][c].text, axis, at, delta)) {
                ErrorMessage("Formula would exceed cell limit."); return 0;
            }
    if (axis == 0) {
        if (insert) for (r = ROWS-1; r > at; r--)
            memcpy(cells[r], cells[r-1], sizeof cells[r]);
        else for (r = at; r < ROWS-1; r++)
            memcpy(cells[r], cells[r+1], sizeof cells[r]);
        memset(cells[insert ? at : ROWS-1], 0, sizeof cells[0]);
    } else {
        for (r = 0; r < ROWS; r++) {
            if (insert) for (c = COLS-1; c > at; c--)
                cells[r][c] = cells[r][c-1];
            else for (c = at; c < COLS-1; c++)
                cells[r][c] = cells[r][c+1];
            memset(&cells[r][insert ? at : COLS-1], 0, sizeof cells[r][0]);
        }
        if (insert) for (c = COLS-1; c > at; c--)
            col_width[c] = col_width[c-1];
        else for (c = at; c < COLS-1; c++)
            col_width[c] = col_width[c+1];
        col_width[insert ? at : COLS-1] = DEFAULT_WIDTH;
    }
    for (r = 0; r < ROWS; r++)
        for (c = 0; c < COLS; c++)
            if (cells[r][c].text[0] == '=' &&
                rewrite_formula(rewritten, cells[r][c].text, axis, at, delta))
                strcpy(cells[r][c].text, rewritten);
    undo_valid = 0;
    mark_changed();
    draw_all(w);
    return 1;
}

static int compare_sort(int a, int b, int descending)
{
    Cell *x = &sort_cells[a], *y = &sort_cells[b];
    int tx, ty, result;
    if (!x->text[0]) return y->text[0] ? 1 : 0;
    if (!y->text[0]) return -1;
    tx = x->type == TYPE_TEXT || (x->type == TYPE_AUTO && is_label(x->text));
    ty = y->type == TYPE_TEXT || (y->type == TYPE_AUTO && is_label(y->text));
    if (tx != ty) return tx - ty;
    result = tx ? strcmp(x->text, y->text) :
        (sort_values[a] > sort_values[b]) - (sort_values[a] < sort_values[b]);
    return descending ? -result : result;
}

static void sort_column(WINDOW w, int descending)
{
    int r, i, err;
    for (r = 0; r < ROWS; r++) {
        sort_cells[r] = cells[r][col];
        err = OK;
        sort_values[r] = calculate(r, col, 0, &err);
        if (err) sort_values[r] = 0;
    }
    for (r = 1; r < ROWS; r++)
        for (i = r; i > 0 && compare_sort(i, i-1, descending) < 0; i--) {
            Cell cell = sort_cells[i];
            long long value = sort_values[i];
            sort_cells[i] = sort_cells[i-1]; sort_values[i] = sort_values[i-1];
            sort_cells[i-1] = cell; sort_values[i-1] = value;
        }
    for (r = 0; r < ROWS; r++) cells[r][col] = sort_cells[r];
    undo_valid = 0;
    mark_changed();
    strcpy(notice, "Column sorted");
    draw_all(w);
}

static int leave_dirty(void)
{
    if (!changed) return 1;
    return YesNoBox("Discard unsaved changes?");
}

static void begin_edit(WINDOW w, int replace, int first)
{
    if (!formula_box) return;
    edit_mode = 1;
    edit_was_changed = changed;
    strcpy(edit_original, cells[row][col].text);
    if (replace) { edit[0] = first; edit[1] = 0; }
    else strcpy(edit, cells[row][col].text);
    SendMessage(formula_box, SETTEXT, (PARAM)edit, 0);
    SendMessage(formula_box, PAINT, 0, 0);
    SendMessage(formula_box, SETFOCUS, TRUE, 0);
    SendMessage(formula_box, KEYBOARD_CURSOR, strlen(edit), 0);
    if (replace) set_cell(w, edit);
    strcpy(notice, "Enter commits  Esc cancels");
    draw_status(w);
}

static void finish_edit(WINDOW w)
{
    SendMessage(formula_box, GETTEXT, (PARAM)edit, CELL_LEN-1);
    edit_mode = 0;
    set_cell(w, edit);
    if (strcmp(edit, edit_original)) {
        strcpy(undo_text, edit_original);
        undo_row = row; undo_col = col; undo_valid = 1;
    }
    strcpy(notice, "Ready");
    draw_input(w);
    draw_status(w);
    SendMessage(w, SETFOCUS, TRUE, 0);
}

static void cancel_edit(WINDOW w)
{
    edit_mode = 0;
    if (strcmp(cells[row][col].text, edit_original)) {
        strcpy(cells[row][col].text, edit_original);
        generation++;
    }
    changed = edit_was_changed;
    undo_valid = 0;
    strcpy(notice, "Edit cancelled");
    draw_grid(w); draw_input(w); draw_status(w);
    SendMessage(w, SETFOCUS, TRUE, 0);
}

static int FormulaProc(WINDOW box, MESSAGE msg, PARAM p1, PARAM p2)
{
    WINDOW app = GetParent(box);
    doc = (SheetDoc *)app->extension;
    if (msg == MOUSE_WHEEL)
        return SendMessage(app, MOUSE_WHEEL, p1, p2);
    if (msg == SETFOCUS && p1 && !edit_mode)
        begin_edit(app, 0, 0);
    if (msg == KEYBOARD && edit_mode) {
        int key = (int)p1;
        if (key == '\r') { finish_edit(app); return TRUE; }
        if (key == ESC) { cancel_edit(app); return TRUE; }
        if (key == TAB || key == SHIFT_HT) {
            finish_edit(app);
            select_cell(app, row, col + (key == TAB ? 1 : -1));
            return TRUE;
        }
        if (key == CTRL_S) {
            finish_edit(app);
            SendMessage(app, COMMAND, ID_SAVE, 0);
            return TRUE;
        }
        if (key == CTRL_C) return TRUE;
        {
            int result = EditBoxProc(box, msg, p1, p2);
            char current[CELL_LEN];
            SendMessage(box, GETTEXT, (PARAM)current, CELL_LEN-1);
            if (strcmp(current, cells[row][col].text)) set_cell(app, current);
            return result;
        }
    }
    return EditBoxProc(box, msg, p1, p2);
}

static void find_next(WINDOW w)
{
    int step, index = row*COLS + col;
    if (!find_text[0]) return;
    /* A dialog can return focus to the formula EDITBOX.  Leave that edit
     * session before moving the selection, otherwise its old text may be
     * committed against the newly found cell. */
    if (edit_mode) cancel_edit(w);
    SendMessage(w, SETFOCUS, TRUE, 0);
    for (step = 1; step <= ROWS*COLS; step++) {
        int next = (index + step) % (ROWS*COLS);
        if (strstr(cells[next/COLS][next%COLS].text, find_text)) {
            select_cell(w, next/COLS, next%COLS);
            edit_mode = 0;
            draw_input(w);
            strcpy(notice, "Found"); draw_status(w);
            return;
        }
    }
    strcpy(notice, "Not found"); draw_status(w);
}

static void color_sheet_window(WINDOW w)
{
    if (!w) return;
    if (sheet_theme == 0) {
        WindowClientColor(w, LIGHTGRAY, BLACK);
        WindowReverseColor(w, BLACK, LIGHTGRAY);
        WindowFrameColor(w, YELLOW, BLACK);
        WindowHighlightColor(w, YELLOW, BLACK);
    } else {
        WindowClientColor(w, WHITE, BLUE);
        WindowReverseColor(w, BLACK, LIGHTGRAY);
        WindowFrameColor(w, WHITE, BLUE);
        WindowHighlightColor(w, YELLOW, BLUE);
    }
    if (GetClass(w) == EDITBOX) {
        /* Keep the formula field legible in both themes. */
        WindowClientColor(w, WHITE, BLUE);
        WindowReverseColor(w, WHITE, BLUE);
        WindowFrameColor(w, BLUE, WHITE);
        WindowHighlightColor(w, YELLOW, BLUE);
    }
}

static void repaint_sheet_tree(WINDOW w)
{
    WINDOW child;
    if (!w) return;
    color_sheet_window(w);
    child = FirstWindow(w);
    while (child) {
        repaint_sheet_tree(child);
        child = NextWindow(child);
    }
    SendMessage(w, PAINT, 0, 0);
}

static void apply_sheet_theme(void)
{
    repaint_sheet_tree(app_window);
}

static void insert_formula(WINDOW w, const char *name)
{
    char range[16], formula[CELL_LEN];
    strcpy(range, "A1:B2");
    if (!InputBox(w, "Formula range", "Range:", range, sizeof range, 12)) return;
    if (strlen(range) + strlen(name) + 4 >= CELL_LEN) {
        ErrorMessage("Range is too long."); return;
    }
    sprintf(formula, "=%s(%s)", name, range);
    set_cell(w, formula);
}

static void change_width(WINDOW w)
{
    char value[8];
    int width;
    sprintf(value, "%d", col_width[col]);
    if (!InputBox(w, "Column width", "Width 6-20:", value,
                  sizeof value, 8)) return;
    width = atoi(value);
    if (width < MIN_WIDTH || width > MAX_WIDTH) {
        ErrorMessage("Width must be 6 to 20."); return;
    }
    if (col_width[col] != width) {
        col_width[col] = width; mark_changed(); draw_grid(w); draw_status(w);
    }
}

static int on_command(WINDOW w, int command)
{
    char path[FILE_LEN];
    doc = (SheetDoc *)w->extension;
    if (edit_mode && command != ID_INSERT) finish_edit(w);
    switch (command) {
    case ID_NEW:
        open_sheet(app_window, NULL);
        return TRUE;
    case ID_OPEN:
        if (OpenFileDialogBox("*.ASH", path)) open_sheet(app_window, path);
        return TRUE;
    case ID_SAVE:
        if (!save_file(filename)) ErrorMessage("Save failed. Check filename and disk space.");
        else AddTitle(w, filename);
        draw_status(w);
        return TRUE;
    case ID_SAVEAS:
        if (SaveAsDialogBox("*.ASH", NULL, path)) {
            if (!save_file(path)) ErrorMessage("Save failed. Use an .ASH filename.");
            else AddTitle(w, filename);
            draw_status(w);
        }
        return TRUE;
    case ID_CLOSE:
        if (leave_dirty()) {
            doc->closing_ok = 1;
            SendMessage(w, CLOSE_WINDOW, 0, 0);
        }
        return TRUE;
    case SHEET_NEXT:
        {
            WINDOW next = PrevWindow(w);
            while (next && (GetClass(next) != NORMAL || !next->extension))
                next = PrevWindow(next);
            if (!next) {
                next = LastWindow(app_window);
                while (next && (GetClass(next) != NORMAL || !next->extension))
                    next = PrevWindow(next);
            }
            if (next && next != w) SendMessage(next, SETFOCUS, TRUE, 0);
        }
        return TRUE;
    case ID_INSERT:
        begin_edit(w, 0, 0);
        return TRUE;
    case ID_CLEAR:
        set_cell(w, "");
        return TRUE;
    case ID_UNDO:
        if (undo_valid) {
            char previous[CELL_LEN];
            int r = undo_row, c = undo_col;
            strcpy(previous, undo_text);
            select_cell(w, r, c);
            set_cell(w, previous);
            cells[r][c].type = undo_type;
            mark_changed(); draw_all(w);
            undo_valid = 0;
        }
        return TRUE;
    case ID_COPY:
        sys_clip_set(cells[row][col].text, strlen(cells[row][col].text));
        strcpy(notice, "Copied cell"); draw_status(w);
        return TRUE;
    case ID_CUT:
        sys_clip_set(cells[row][col].text, strlen(cells[row][col].text));
        set_cell(w, "");
        strcpy(notice, "Cut cell"); draw_status(w);
        return TRUE;
    case ID_PASTE:
        {
            char clip[CELL_LEN];
            int len = sys_clip_get(clip, sizeof clip - 1);
            if (len < 0) len = 0;
            if (len >= CELL_LEN) len = CELL_LEN - 1;
            clip[len] = 0;
            set_cell(w, clip);
        }
        return TRUE;
    case ID_SEARCH:
        if (InputBox(w, "Find cell", "Text:", find_text, sizeof find_text, 32))
            find_next(w);
        return TRUE;
    case ID_SEARCHNEXT:
        find_next(w);
        return TRUE;
    case SHEET_THEME_AMBER:
        sheet_theme = 0;
        apply_sheet_theme();
        return TRUE;
    case SHEET_THEME_BLUE:
        sheet_theme = 1;
        apply_sheet_theme();
        return TRUE;
    case SHEET_GOTO:
        {
            char target[8];
            int c, r = 0, i;
            address(col, row, target);
            if (!InputBox(w, "Go to cell", "Cell:", target, sizeof target, 8)) return TRUE;
            c = toupper(target[0]) - 'A';
            for (i = 1; target[i] >= '0' && target[i] <= '9'; i++)
                r = r*10 + target[i] - '0';
            if (c < 0 || c >= COLS || r < 1 || r > ROWS || target[i])
                ErrorMessage("Use a cell from A1 to Z64.");
            else select_cell(w, r-1, c);
        }
        return TRUE;
    case SHEET_SUM:
        insert_formula(w, "SUM");
        return TRUE;
    case SHEET_AVERAGE:
        insert_formula(w, "AVERAGE");
        return TRUE;
    case SHEET_MIN:
        insert_formula(w, "MIN"); return TRUE;
    case SHEET_MAX:
        insert_formula(w, "MAX"); return TRUE;
    case SHEET_COUNT:
        insert_formula(w, "COUNT"); return TRUE;
    case SHEET_INSERT_ROW:
    case SHEET_DELETE_ROW:
        structural_change(w, 0, command == SHEET_INSERT_ROW);
        return TRUE;
    case SHEET_INSERT_COL:
    case SHEET_DELETE_COL:
        structural_change(w, 1, command == SHEET_INSERT_COL);
        return TRUE;
    case SHEET_WIDTH:
        change_width(w); return TRUE;
    case SHEET_SORT_UP:
    case SHEET_SORT_DOWN:
        sort_column(w, command == SHEET_SORT_DOWN); return TRUE;
    case SHEET_TYPE_AUTO:
    case SHEET_TYPE_TEXT:
    case SHEET_TYPE_NUMBER:
        {
            int type = command - SHEET_TYPE_AUTO;
            if (cells[row][col].type != type) {
                strcpy(undo_text, cells[row][col].text);
                undo_type = cells[row][col].type;
                undo_row = row; undo_col = col; undo_valid = 1;
                cells[row][col].type = type;
                mark_changed(); draw_grid(w); draw_status(w);
            }
        }
        return TRUE;
    case ID_KEYSHELP:
        MessageBox("AMUN SHEET Keys",
            "Arrows/Tab: move\nEnter/F2: formula bar\nEsc: cancel edit\nPgUp/PgDn: scroll\nDel: clear cell\nCopy: Edit menu\nCtrl+X/V/Z: cut/paste/undo\nCtrl+F/F3: find/next\nF5: go to cell\nAlt+F: menu  Ctrl+S: save");
        return TRUE;
    case ID_ABOUT:
        MessageBox("About AMUN SHEET 1.0 Dev",
            "AMUNOS DFLAT spreadsheet\n\nInspired by Lotus 1-2-3 and early DOS workstations.\nBuilt on the DFLAT window and control model.\n\n26 columns x 64 rows\nSUM AVERAGE MIN MAX COUNT\n.ASH v2 workbook format\nAMUNOS Project, 2026.");
        return TRUE;
    case ID_EXIT:
        {
            WINDOW child = FirstWindow(app_window);
            while (child) {
                if (GetClass(child) == NORMAL && child->extension) {
                    doc = (SheetDoc *)child->extension;
                    if (!leave_dirty()) return TRUE;
                }
                child = NextWindow(child);
            }
            child = FirstWindow(app_window);
            while (child) {
                if (GetClass(child) == NORMAL && child->extension)
                    ((SheetDoc *)child->extension)->closing_ok = 1;
                child = NextWindow(child);
            }
            SendMessage(app_window, CLOSE_WINDOW, 0, 0);
        }
        return TRUE;
    default:
        return FALSE;
    }
}

static int on_key(WINDOW w, int key)
{
    switch (key) {
    case UP: select_cell(w, row-1, col); return TRUE;
    case DN: select_cell(w, row+1, col); return TRUE;
    case LARROW: select_cell(w, row, col-1); return TRUE;
    case RARROW: case TAB: select_cell(w, row, col+1); return TRUE;
    case SHIFT_HT: select_cell(w, row, col-1); return TRUE;
    case PGUP: select_cell(w, row-visible_rows(w), col); return TRUE;
    case PGDN: select_cell(w, row+visible_rows(w), col); return TRUE;
    case HOME: select_cell(w, row, 0); return TRUE;
    case END: select_cell(w, row, COLS-1); return TRUE;
    case '\r': case F2: begin_edit(w, 0, 0); return TRUE;
    case DEL: return on_command(w, ID_CLEAR);
    case CTRL_N: return on_command(w, ID_NEW);
    case CTRL_O: return on_command(w, ID_OPEN);
    case CTRL_S: return on_command(w, ID_SAVE);
    case CTRL_Z: return on_command(w, ID_UNDO);
    case CTRL_X: return on_command(w, ID_CUT);
    case CTRL_V: return on_command(w, ID_PASTE);
    case CTRL_F: return on_command(w, ID_SEARCH);
    case F3: return on_command(w, ID_SEARCHNEXT);
    case F5: return on_command(w, SHEET_GOTO);
    case F1: return on_command(w, ID_KEYSHELP);
    case F6: return on_command(w, SHEET_NEXT);
    case F10:
        if (app_window->MenuBarWnd)
            SendMessage(app_window->MenuBarWnd, KEYBOARD, F10, 0);
        return TRUE;
    case CTRL_F4: return on_command(w, ID_CLOSE);
    case ESC: return on_command(w, ID_CLOSE);
    default:
        if (key >= 32 && key < 127) { begin_edit(w, 1, key); return TRUE; }
        return SendMessage(app_window, KEYBOARD, key, 0);
    }
}

static int SheetProc(WINDOW w, MESSAGE msg, PARAM p1, PARAM p2)
{
    int x, y, c, r;
    SheetDoc *closing;
    doc = (SheetDoc *)w->extension;
    switch (msg) {
    case PAINT:
        NormalProc(w, msg, p1, p2);
        draw_all(w);
        return TRUE;
    case SETFOCUS:
        if (p1) active_window = w;
        NormalProc(w, msg, p1, p2);
        if (p1) draw_status(w);
        return TRUE;
    case CLOSE_WINDOW:
        closing = doc;
        if (!closing->closing_ok && !leave_dirty()) return TRUE;
        if (active_window == w) active_window = NULL;
        NormalProc(w, msg, p1, p2);
        free(closing);
        if (!active_window) {
            WINDOW next = LastWindow(app_window);
            while (next && (GetClass(next) != NORMAL || !next->extension))
                next = PrevWindow(next);
            if (next) SendMessage(next, SETFOCUS, TRUE, 0);
            else SendMessage(app_window, ADDSTATUS,
                             (PARAM)"No worksheet open", 0);
        }
        return TRUE;
    case KEYBOARD:
        return on_key(w, (int)p1);
    case MOUSE_WHEEL:
        scroll_view(w, top_row - 3*(int)p1, left_col);
        return TRUE;
    case LEFT_BUTTON: case DOUBLE_CLICK:
        x = (int)p1 - GetClientLeft(w);
        y = (int)p2 - GetClientTop(w);
        if (x == ClientWidth(w)) {
            int wy = (int)p2 - GetTop(w);
            if (wy <= 1) scroll_view(w, top_row-1, left_col);
            else if (wy >= WindowHeight(w)-2)
                scroll_view(w, top_row+1, left_col);
            else {
                scrollbar_drag = 1;
                scroll_view(w, (wy-2)*(ROWS-visible_rows(w)) /
                    (WindowHeight(w)-5), left_col);
                SendMessage(w, CAPTURE_MOUSE, TRUE, 0);
            }
            return TRUE;
        }
        if (y == GRID_Y+visible_rows(w)) {
            if (x <= 0) scroll_view(w, top_row, left_col-1);
            else if (x >= ClientWidth(w)-1)
                scroll_view(w, top_row, left_col+1);
            else {
                scrollbar_drag = 2;
                scroll_view(w, top_row,
                    (x-1)*(COLS-1)/(ClientWidth(w)-3));
                SendMessage(w, CAPTURE_MOUSE, TRUE, 0);
            }
            return TRUE;
        }
        if (y >= 0 && y < FORMULA_ROWS && x >= INPUT_LEFT-1) {
            begin_edit(w, 0, 0);
            return TRUE;
        }
        if (y >= GRID_Y && y < GRID_Y+visible_rows(w) &&
            x >= GRID_X && x <= grid_right(w)) {
            c = left_col;
            while (c < left_col+visible_cols(w)-1 && column_x(c+1) <= x) c++;
            r = top_row + y-GRID_Y;
            if (c < COLS && r < ROWS) {
                if (edit_mode) finish_edit(w);
                select_cell(w, r, c);
                if (msg == DOUBLE_CLICK) begin_edit(w, 0, 0);
            }
            return TRUE;
        }
        break;
    case MOUSE_MOVED:
        if (scrollbar_drag && (mousebuttons() & 1)) {
            if (scrollbar_drag == 1) {
                int wy = (int)p2-GetTop(w)-2;
                scroll_view(w, wy*(ROWS-visible_rows(w)) /
                    (WindowHeight(w)-5), left_col);
            } else {
                int sx = (int)p1-GetClientLeft(w)-1;
                scroll_view(w, top_row,
                    sx*(COLS-1)/(ClientWidth(w)-3));
            }
            return TRUE;
        }
        break;
    case BUTTON_RELEASED:
        if (scrollbar_drag) {
            scrollbar_drag = 0;
            SendMessage(w, RELEASE_MOUSE, 0, 0);
            return TRUE;
        }
        break;
    case COMMAND:
        if ((int)p2 == 0 && on_command(w, (int)p1)) return TRUE;
        break;
    default:
        break;
    }
    return NormalProc(w, msg, p1, p2);
}

static WINDOW open_sheet(WINDOW app, const char *path)
{
    static int serial;
    SheetDoc *fresh = DFcalloc(1, sizeof *fresh);
    WINDOW w;
    int c, offset;
    if (!fresh) { ErrorMessage("Out of memory."); return NULL; }
    doc = fresh;
    generation = 1;
    strcpy(notice, "Ready");
    for (c = 0; c < COLS; c++) col_width[c] = DEFAULT_WIDTH;
    offset = serial++ % 3;
    if (path) {
        if (!load_file(path)) {
            free(fresh);
            ErrorMessage("Cannot open this .ASH file.");
            return NULL;
        }
    } else {
        sprintf(filename, "SHT%03d.ASH", serial % 1000);
    }
    w = CreateWindow(NORMAL, filename,
        GetClientLeft(app)+offset*3, GetClientTop(app)+offset*2,
        ClientHeight(app)-1-offset*2, ClientWidth(app)-offset*3,
        fresh, app, SheetProc,
        HASBORDER | CONTROLBOX | VSCROLLBAR | MOVEABLE);
    if (!w) { free(fresh); return NULL; }
    fresh->input = CreateWindow(EDITBOX, NULL,
        GetClientLeft(w)+INPUT_LEFT, GetClientTop(w), 1,
        ClientWidth(w)-INPUT_LEFT-INPUT_RIGHT_GAP,
        NULL, w, FormulaProc, 0);
    if (!fresh->input) {
        SendMessage(w, CLOSE_WINDOW, 0, 0);
        return NULL;
    }
    color_sheet_window(fresh->input);
    SendMessage(fresh->input, SETTEXTLENGTH, CELL_LEN-1, 0);
    SendMessage(w, SHOW_WINDOW, 0, 0);
    SendMessage(w, SETFOCUS, TRUE, 0);
    return w;
}

static int AppProc(WINDOW w, MESSAGE msg, PARAM p1, PARAM p2)
{
    if (msg == COMMAND && (int)p2 == 0) {
        if ((int)p1 == ID_NEW) { open_sheet(w, NULL); return TRUE; }
        if ((int)p1 == ID_OPEN) {
            char path[FILE_LEN];
            if (OpenFileDialogBox("*.ASH", path)) open_sheet(w, path);
            return TRUE;
        }
        if (active_window)
            return on_command(active_window, (int)p1);
    }
    return ApplicationProc(w, msg, p1, p2);
}

int main(int argc, char **argv)
{
    cfg.ScreenLines = SCREENHEIGHT;
    cfg.mono = 0;
    cfg.theme = 0;
    if (!init_messages()) return 1;
    app_window = CreateWindow(APPLICATION, "AMUN SHEET 1.0 Dev",
        0, 0, -1, -1, &SheetMenu, NULL, AppProc,
        HASBORDER | HASSTATUSBAR);
    if (!app_window) return 1;
    SendMessage(app_window, SETFOCUS, TRUE, 0);
    if (!open_sheet(app_window, argc > 1 ? argv[1] : NULL)) return 1;
    apply_sheet_theme();
    MessageBox("AMUN SHEET 1.0 Dev",
        "AMUNOS spreadsheet workspace\n\nA compact DFLAT application inspired by\nLotus 1-2-3 and classic DOS software.\n\nFormula engine: SUM AVERAGE MIN MAX COUNT\nWorkbook format: .ASH v2\n\nAMUNOS Project - 2026");
    while (dispatch_message()) ;
    return 0;
}
