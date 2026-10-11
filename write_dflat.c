/* AMUN WRITE: DFLAT window chrome with an independent document canvas. */
#include "dflat.h"

#define DOC_CAP 60000
#define PAGE_LINES 18
#define BODY_TOP 2
#define BODY_LEFT 5
#define FONT_SLIM 0x40
#define ALIGN_MASK 0x30
#define ALIGN_LEFT 0x00
#define ALIGN_CENTER 0x10
#define ALIGN_RIGHT 0x20
#define DEFAULT_FORMAT 0x0c

enum {
    W_NEW = 3000, W_OPEN, W_SAVE, W_SAVE_AS, W_EXIT,
    W_FIND, W_FIND_NEXT, W_REPLACE, W_REPLACE_ALL,
    W_BOLD, W_ITALIC, W_FONT, W_SMALL, W_NORMAL, W_LARGE,
    W_ALIGN_LEFT, W_ALIGN_CENTER, W_ALIGN_RIGHT, W_PAGE_BREAK, W_FULLWIDTH,
    W_COPY, W_CUT, W_PASTE, W_SELECT_ALL, W_ABOUT
};

char DFlatApplication[] = "AMUN WRITE";
static WINDOW canvas;
static WINDOW application;
static char filename[64] = "UNTITLED.AWD";
static char document[DOC_CAP + 1];
static unsigned char formatting[DOC_CAP + 1];
static int length, caret, anchor = -1, top_line, left_col;
static int typing_format = DEFAULT_FORMAT, fullwidth_input;
static int dirty, closing_allowed;
static char find_text[80];

DEFMENU(WriteMenu)
    POPDOWN("~FILE", NULL, "DOCUMENT")
        SELECTION("~NEW", W_NEW, CTRL_N, 0)
        SELECTION("~OPEN...", W_OPEN, CTRL_O, 0)
        SEPARATOR
        SELECTION("~SAVE", W_SAVE, CTRL_S, 0)
        SELECTION("SAVE ~AS...", W_SAVE_AS, 0, 0)
        SEPARATOR
        SELECTION("E~XIT", W_EXIT, ALT_X, 0)
    ENDPOPDOWN
    POPDOWN("~EDIT", NULL, "EDIT")
        SELECTION("~COPY", W_COPY, CTRL_C, 0)
        SELECTION("CU~T", W_CUT, CTRL_X, 0)
        SELECTION("~PASTE", W_PASTE, CTRL_V, 0)
        SELECTION("SELECT ~ALL", W_SELECT_ALL, CTRL_A, 0)
    ENDPOPDOWN
    POPDOWN("~SEARCH", NULL, "SEARCH AND REPLACE")
        SELECTION("~FIND...", W_FIND, CTRL_F, 0)
        SELECTION("FIND ~NEXT", W_FIND_NEXT, F3, 0)
        SELECTION("~REPLACE...", W_REPLACE, CTRL_H, 0)
        SELECTION("REPLACE ~ALL...", W_REPLACE_ALL, 0, 0)
    ENDPOPDOWN
    POPDOWN("F~ORMAT", NULL, "CHARACTERS AND PARAGRAPHS")
        SELECTION("~BOLD", W_BOLD, CTRL_B, 0)
        SELECTION("~ITALIC", W_ITALIC, CTRL_I, 0)
        SEPARATOR
        SELECTION("~FONT: MONO / SLIM", W_FONT, 0, 0)
        SELECTION("SIZE: ~SMALL", W_SMALL, 0, 0)
        SELECTION("SIZE: ~NORMAL", W_NORMAL, 0, 0)
        SELECTION("SIZE: ~LARGE", W_LARGE, 0, 0)
        SELECTION("~FULL-WIDTH ASCII", W_FULLWIDTH, 0, TOGGLE)
        SEPARATOR
        SELECTION("ALIGN: ~LEFT", W_ALIGN_LEFT, 0, 0)
        SELECTION("ALIGN: ~CENTER", W_ALIGN_CENTER, 0, 0)
        SELECTION("ALIGN: ~RIGHT", W_ALIGN_RIGHT, 0, 0)
        SELECTION("INSERT ~PAGE BREAK", W_PAGE_BREAK, 0, 0)
    ENDPOPDOWN
    POPDOWN("~HELP", NULL, "ABOUT")
        SELECTION("~ABOUT WRITE", W_ABOUT, 0, 0)
    ENDPOPDOWN
ENDMENU

void PrepFileMenu(void *wnd, struct Menu *menu) { (void)wnd; (void)menu; }
void PrepEditMenu(void *wnd, struct Menu *menu) { (void)wnd; (void)menu; }
void PrepSearchMenu(void *wnd, struct Menu *menu) { (void)wnd; (void)menu; }

static int char_size(int i)
{
    unsigned char c = (unsigned char)document[i];
    if (c >= 0xe0 && c <= 0xef && i + 2 < length &&
        (unsigned char)document[i + 1] >= 0x80 &&
        (unsigned char)document[i + 1] <= 0xbf &&
        (unsigned char)document[i + 2] >= 0x80 &&
        (unsigned char)document[i + 2] <= 0xbf) return 3;
    if (c >= 0xa1 && c <= 0xf7 && i + 1 < length &&
        (unsigned char)document[i + 1] >= 0xa1 &&
        (unsigned char)document[i + 1] <= 0xfe) return 2;
    return 1;
}

static int next_char(int i) { return i < length ? i + char_size(i) : length; }

static int prev_char(int i)
{
    int j = i - 1;
    if (j <= 0) return 0;
    if ((unsigned char)document[j] >= 0x80 &&
        (unsigned char)document[j] <= 0xbf) {
        if (j >= 2 && (unsigned char)document[j - 2] >= 0xe0 &&
            (unsigned char)document[j - 2] <= 0xef) return j - 2;
        if (j >= 1 && (unsigned char)document[j - 1] >= 0xa1 &&
            (unsigned char)document[j - 1] <= 0xf7) return j - 1;
    }
    return j;
}

static int line_number(int offset)
{
    int i, n = 0;
    for (i = 0; i < offset && i < length; i++) {
        if (document[i] == '\n') n++;
        if (document[i] == '\f') n = ((n / PAGE_LINES) + 1) * PAGE_LINES;
    }
    return n;
}

static int line_start(int target)
{
    int i, n = 0;
    if (target <= 0) return 0;
    for (i = 0; i < length; i++) {
        if (document[i] == '\n') n++;
        if (document[i] == '\f') n = ((n / PAGE_LINES) + 1) * PAGE_LINES;
        if (n >= target) return i + 1;
    }
    return length;
}

static int line_end(int start)
{
    int i = start;
    while (i < length && document[i] != '\n' && document[i] != '\f')
        i = next_char(i);
    return i;
}

static int cell_width(int start, int end)
{
    int i, col = 0;
    for (i = start; i < end; i = next_char(i))
        col += char_size(i) == 1 ? 1 : 2;
    return col;
}

static int has_selection(void) { return anchor >= 0 && anchor != caret; }
static int selection_start(void) { return anchor < caret ? anchor : caret; }
static int selection_end(void) { return anchor > caret ? anchor : caret; }

static int body_rows(void)
{
    int rows = ClientHeight(canvas) - BODY_TOP - 1;
    return rows > 0 ? rows : 1;
}

static void ensure_visible(void)
{
    int row = line_number(caret);
    int col = cell_width(line_start(row), caret);
    int visible = ClientWidth(canvas) - BODY_LEFT - 2;
    if (row < top_line) top_line = row;
    if (row >= top_line + body_rows()) top_line = row - body_rows() + 1;
    if (top_line < 0) top_line = 0;
    if (visible < 1) visible = 1;
    if (col < left_col) left_col = col;
    if (col >= left_col + visible) left_col = col - visible + 1;
}

static void set_color(int fg, int bg) { foreground = fg; background = bg; }

static void fill_row(WINDOW wnd, int y, int fg, int bg)
{
    char buf[SCREENWIDTH + 1];
    int width = ClientWidth(wnd);
    if (width > SCREENWIDTH) width = SCREENWIDTH;
    memset(buf, ' ', width);
    buf[width] = 0;
    set_color(fg, bg);
    PutWindowLine(wnd, buf, 0, y);
}

static int aligned_x(int row, int start, int end)
{
    int available = ClientWidth(canvas) - BODY_LEFT - 2;
    int width = cell_width(start, end);
    int align = start < length ? formatting[start] & ALIGN_MASK : ALIGN_LEFT;
    (void)row;
    if (available < 1) available = 1;
    if (align == ALIGN_CENTER && width < available)
        return BODY_LEFT + (available - width) / 2;
    if (align == ALIGN_RIGHT && width < available)
        return BODY_LEFT + available - width;
    return BODY_LEFT;
}

static void paint_document(WINDOW wnd)
{
    int width = ClientWidth(wnd), height = ClientHeight(wnd);
    int y, row, start, end, i, x, n;
    char buf[96];
    if (width < 16 || height < 5) return;
    for (y = 0; y < height; y++) fill_row(wnd, y, WHITE, BLACK);
    fill_row(wnd, 0, BLACK, BROWN);
    sprintf(buf, " WRITE  |  %s  |  PAGE %d", filename,
            line_number(caret) / PAGE_LINES + 1);
    set_color(BLACK, BROWN); PutWindowLine(wnd, buf, 0, 0);
    fill_row(wnd, 1, YELLOW, BLACK);
    sprintf(buf, "  %s  %s  %s  %s  |  SELECT: %s",
            (typing_format & FONT_SLIM) ? "SLIM" : "MONO",
            (typing_format & STYLE_SIZE_MASK) == STYLE_SMALL ? "SMALL" :
            (typing_format & STYLE_SIZE_MASK) == STYLE_LARGE ? "LARGE" : "NORMAL",
            (typing_format & STYLE_BOLD) ? "BOLD" : "REGULAR",
            fullwidth_input ? "FW" : "HW",
            has_selection() ? "ON" : "OFF");
    set_color(YELLOW, BLACK); PutWindowLine(wnd, buf, 0, 1);
    for (y = BODY_TOP; y < height - 1; y++) {
        row = top_line + y - BODY_TOP;
        start = line_start(row);
        if (line_number(start) != row) continue;
        end = line_end(start);
        if (row % PAGE_LINES == 0) {
            set_color(BROWN, BLACK);
            PutWindowLine(wnd, "|", 3, y);
        }
        sprintf(buf, "%2d", row % PAGE_LINES + 1);
        set_color(DARKGRAY, BLACK); PutWindowLine(wnd, buf, 0, y);
        x = aligned_x(row, start, end) - left_col;
        for (i = start; i < end && x < width - 1; i = next_char(i)) {
            char glyph[4] = {0, 0, 0, 0};
            int bytes = char_size(i), cells = bytes == 1 ? 1 : 2;
            int selected = has_selection() && i >= selection_start() &&
                           i < selection_end();
            if (x < BODY_LEFT || x + cells <= BODY_LEFT) {
                x += cells; continue;
            }
            if (x + cells > width - 1) break;
            memcpy(glyph, document + i, bytes);
            set_color(selected ? BLACK : WHITE, selected ? YELLOW : BLACK);
            wputs(wnd, glyph, x + BorderAdj(wnd), y + TopBorderAdj(wnd));
            sys_text_style(GetClientLeft(wnd) + x, GetClientTop(wnd) + y,
                           cells, formatting[i] & (STYLE_BOLD | STYLE_ITALIC |
                           STYLE_SIZE_MASK | FONT_SLIM));
            x += cells;
        }
    }
    fill_row(wnd, height - 1, WHITE, DARKGRAY);
    sprintf(buf, " F1 HELP   CTRL+F FIND   F3 NEXT   CTRL+S SAVE   CTRL+ENTER PAGE");
    set_color(WHITE, DARKGRAY); PutWindowLine(wnd, buf, 0, height - 1);
    row = line_number(caret);
    start = line_start(row);
    end = line_end(start);
    x = aligned_x(row, start, end) + cell_width(start, caret) - left_col;
    y = BODY_TOP + row - top_line;
    if (x < BODY_LEFT) x = BODY_LEFT;
    if (x >= width - 1) x = width - 2;
    if (y >= BODY_TOP && y < height - 1)
        sys_dflat_cursor_set(GetClientLeft(wnd) + x,
                             GetClientTop(wnd) + y, 1);
    /* Dialogs can hide the global overlay cursor. The document canvas owns
     * it whenever it is painted, so it remains visible after an edit. */
    sys_dflat_cursor_visible(TRUE);
    n = has_selection() ? selection_end() - selection_start() : 0;
    (void)n;
}

static void status(void)
{
    char buf[96];
    int row = line_number(caret), start = line_start(row);
    sprintf(buf, "%s  |  PAGE %d  LINE %d  COL %d  |  %s",
            dirty ? "MODIFIED" : "READY", row / PAGE_LINES + 1,
            row + 1, cell_width(start, caret) + 1,
            has_selection() ? "SELECTION" : "INSERT");
    SendMessage(application, ADDSTATUS, (PARAM)buf, 0);
}

static void repaint(void)
{
    ensure_visible();
    if (canvas) SendMessage(canvas, PAINT, 0, 0);
    if (application) status();
}

static int replace_range(int from, int to, const char *bytes, int count,
                         int format)
{
    int removed = to - from;
    if (from < 0 || to > length || from > to || count < 0 ||
        length - removed + count >= DOC_CAP) return FALSE;
    memmove(document + from + count, document + to, length - to + 1);
    memmove(formatting + from + count, formatting + to, length - to);
    if (count) {
        memcpy(document + from, bytes, count);
        memset(formatting + from, format, count);
    }
    length += count;
    caret = from + count;
    anchor = -1;
    dirty = TRUE;
    repaint();
    return TRUE;
}

static void insert_text(const char *bytes, int count)
{
    int from = has_selection() ? selection_start() : caret;
    int to = has_selection() ? selection_end() : caret;
    int format = has_selection() ? formatting[from] : typing_format;
    if (!replace_range(from, to, bytes, count, format)) beep();
}

static void move_to(int pos, int shift)
{
    if (shift) { if (anchor < 0) anchor = caret; }
    else anchor = -1;
    caret = pos < 0 ? 0 : pos > length ? length : pos;
    if (caret < length) typing_format = formatting[caret];
    else if (caret > 0) typing_format = formatting[caret - 1];
    repaint();
}

static void format_selection(int cmd)
{
    int first = has_selection() ? selection_start() : caret;
    int last = has_selection() ? selection_end() : caret;
    int value = first < last ? formatting[first] : typing_format;
    int i;
    if (cmd == W_BOLD) value ^= STYLE_BOLD;
    if (cmd == W_ITALIC) value ^= STYLE_ITALIC;
    if (cmd == W_FONT) value ^= FONT_SLIM;
    if (cmd == W_SMALL) value = (value & ~STYLE_SIZE_MASK) | STYLE_SMALL;
    if (cmd == W_NORMAL) value = (value & ~STYLE_SIZE_MASK) | DEFAULT_FORMAT;
    if (cmd == W_LARGE) value = (value & ~STYLE_SIZE_MASK) | STYLE_LARGE;
    typing_format = value;
    if (first < last) {
        for (i = first; i < last; i++)
            formatting[i] = (formatting[i] & ALIGN_MASK) |
                            (value & ~ALIGN_MASK);
        dirty = TRUE;
        anchor = -1;
    }
    repaint();
}

static void set_alignment(int alignment)
{
    int first = line_start(line_number(caret));
    int end = line_end(first), i;
    for (i = first; i <= end && i < DOC_CAP; i++)
        formatting[i] = (formatting[i] & ~ALIGN_MASK) | alignment;
    typing_format = (typing_format & ~ALIGN_MASK) | alignment;
    dirty = TRUE;
    repaint();
}

static void copy_selection(void)
{
    if (has_selection())
        sys_clip_set(document + selection_start(),
                     selection_end() - selection_start());
}

static int matches_at(int offset, const char *needle, int size)
{
    int j;
    if (offset < 0 || offset + size > length) return FALSE;
    for (j = 0; j < size; j++) {
        unsigned char a = (unsigned char)document[offset + j];
        unsigned char b = (unsigned char)needle[j];
        if (a >= 'a' && a <= 'z') a -= 'a' - 'A';
        if (b >= 'a' && b <= 'z') b -= 'a' - 'A';
        if (a != b) return FALSE;
    }
    return TRUE;
}

static void find_next(int wrap)
{
    int i, size = strlen(find_text), first = caret;
    if (!size) return;
    for (i = first; i + size <= length; i++)
        if (matches_at(i, find_text, size)) goto found;
    if (wrap) for (i = 0; i < first && i + size <= length; i++)
        if (matches_at(i, find_text, size)) goto found;
    MessageBox("SEARCH", "TEXT NOT FOUND.");
    return;
found:
    anchor = i;
    caret = i + size;
    typing_format = formatting[i];
    repaint();
}

static void replace_dialog(int all)
{
    char replacement[80] = "";
    int i, count = 0, size;
    if (!InputBox(application, "SEARCH", "FIND:", find_text, 79, 48) ||
        !find_text[0]) return;
    if (!InputBox(application, "REPLACE", "WITH:", replacement, 79, 48)) return;
    size = strlen(find_text);
    if (!all) {
        if (!(has_selection() && selection_end() - selection_start() == size &&
              matches_at(selection_start(), find_text, size)))
            find_next(TRUE);
        if (has_selection() && selection_end() - selection_start() == size &&
            matches_at(selection_start(), find_text, size))
            insert_text(replacement, strlen(replacement));
        return;
    }
    i = 0;
    while (i + size <= length) {
        if (matches_at(i, find_text, size)) {
            if (!replace_range(i, i + size, replacement, strlen(replacement),
                               formatting[i])) break;
            i = caret;
            count++;
        } else i++;
    }
    { char message[64];
      sprintf(message, "REPLACED %d OCCURRENCE(S).", count);
      MessageBox("REPLACE", message);
    }
}

static int hex_digit(int v) { return v < 10 ? '0' + v : 'A' + v - 10; }
static int from_hex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int save_document(const char *path)
{
    FILE *fp;
    char *out;
    int i, n = 0, previous = -1, capacity = DOC_CAP * 5 + 128;
    const char *header = "#!AWD/2\n@PAGE-LINES 18\n@BODY\n";
    int hlen = strlen(header);
    out = DFcalloc(1, capacity);
    if (!out) return FALSE;
    memcpy(out, header, hlen); n = hlen;
    for (i = 0; i < length; i++) {
        int format = formatting[i] & 0x7f;
        if (n + 24 >= capacity) { free(out); return FALSE; }
        if (format != previous) {
            out[n++] = '{'; out[n++] = '@'; out[n++] = 'F';
            out[n++] = hex_digit((format >> 4) & 15);
            out[n++] = hex_digit(format & 15);
            out[n++] = '}'; previous = format;
        }
        if (document[i] == '\f') {
            memcpy(out + n, "{@PAGE}", 7); n += 7;
        } else {
            if (document[i] == '{' || document[i] == '\\') out[n++] = '\\';
            out[n++] = document[i];
        }
    }
    fp = fopen(path, "wb");
    if (!fp) { free(out); return FALSE; }
    i = fwrite(out, 1, n, fp) == (unsigned)n;
    if (fclose(fp) != 0) i = FALSE;
    free(out);
    if (i) { dirty = FALSE; repaint(); }
    return i;
}

static int load_document(const char *path)
{
    FILE *fp = fopen(path, "rb");
    char *input, *body;
    long size;
    int i, format = DEFAULT_FORMAT, n = 0;
    if (!fp) return FALSE;
    if (fseek(fp, 0, 2) != 0) { fclose(fp); return FALSE; }
    size = ftell(fp);
    if (size < 0 || size > DOC_CAP * 5 || fseek(fp, 0, 0) != 0) {
        fclose(fp); return FALSE;
    }
    input = DFcalloc(1, (unsigned)size + 1);
    if (!input) { fclose(fp); return FALSE; }
    if (fread(input, 1, (unsigned)size, fp) != (unsigned)size) {
        fclose(fp); free(input); return FALSE;
    }
    fclose(fp);
    if (size >= 8 && !memcmp(input, "AWD1", 4)) {
        unsigned stored = (unsigned char)input[4] |
                          ((unsigned)(unsigned char)input[5] << 8) |
                          ((unsigned)(unsigned char)input[6] << 16) |
                          ((unsigned)(unsigned char)input[7] << 24);
        if (stored >= DOC_CAP || 8u + stored * 2u > (unsigned)size) {
            free(input); return FALSE;
        }
        memcpy(document, input + 8, stored);
        memcpy(formatting, input + 8 + stored, stored);
        length = stored;
        for (i = 0; i < length; i++)
            if (!(formatting[i] & STYLE_SIZE_MASK))
                formatting[i] |= DEFAULT_FORMAT;
    } else if (size >= 8 && !memcmp(input, "#!AWD/2\n", 8)) {
        body = strstr(input, "@BODY\n");
        if (!body) { free(input); return FALSE; }
        i = (int)(body - input) + 6;
        while (i < size && n < DOC_CAP - 1) {
            if (i + 5 < size && !memcmp(input + i, "{@F", 3) &&
                from_hex(input[i + 3]) >= 0 && from_hex(input[i + 4]) >= 0 &&
                input[i + 5] == '}') {
                format = (from_hex(input[i + 3]) << 4) | from_hex(input[i + 4]);
                i += 6; continue;
            }
            if (i + 7 <= size && !memcmp(input + i, "{@PAGE}", 7)) {
                document[n] = '\f'; formatting[n++] = format; i += 7; continue;
            }
            if (input[i] == '\\' && i + 1 < size &&
                (input[i + 1] == '{' || input[i + 1] == '\\')) i++;
            document[n] = input[i++]; formatting[n++] = format;
        }
        if (i < size) { free(input); return FALSE; }
        length = n;
    } else { free(input); return FALSE; }
    document[length] = 0;
    free(input);
    caret = 0; anchor = -1; top_line = left_col = 0;
    typing_format = length ? formatting[0] : DEFAULT_FORMAT;
    dirty = FALSE;
    repaint();
    return TRUE;
}

static void clear_document(void)
{
    length = caret = top_line = left_col = 0;
    anchor = -1; dirty = FALSE;
    typing_format = DEFAULT_FORMAT;
    document[0] = 0;
    memset(formatting, DEFAULT_FORMAT, sizeof(formatting));
    repaint();
}

static void uppercase_path(char *path)
{
    while (*path) {
        if (*path >= 'a' && *path <= 'z') *path -= 'a' - 'A';
        path++;
    }
}

static int CanvasProc(WINDOW wnd, MESSAGE msg, PARAM p1, PARAM p2)
{
    if (msg == PAINT) {
        paint_document(wnd);
        return TRUE;
    }
    if (msg == KEYBOARD) {
        int key = (int)p1, shift = ((int)p2 & (LEFTSHIFT | RIGHTSHIFT)) != 0;
        int row, start, end, col, target;
        /* CTRL_H and BS share ASCII value 8. Require an actual Ctrl modifier
         * for Replace, then handle plain Backspace before all shortcuts. */
        if (key == CTRL_H && ((int)p2 & CTRLKEY)) {
            SendMessage(application, COMMAND, W_REPLACE, 0); return TRUE;
        }
        if (key == BS || key == CTRL_BS) {
            if (has_selection()) replace_range(selection_start(), selection_end(), "", 0, typing_format);
            else if (caret) replace_range(prev_char(caret), caret, "", 0, typing_format);
            return TRUE;
        }
        if (key == CTRL_B || key == CTRL_I) {
            format_selection(key == CTRL_B ? W_BOLD : W_ITALIC); return TRUE;
        }
        if (key == CTRL_F) { SendMessage(application, COMMAND, W_FIND, 0); return TRUE; }
        if (key == F3) { find_next(TRUE); return TRUE; }
        if (key == CTRL_S) { SendMessage(application, COMMAND, W_SAVE, 0); return TRUE; }
        if (key == CTRL_N) { SendMessage(application, COMMAND, W_NEW, 0); return TRUE; }
        if (key == CTRL_O) { SendMessage(application, COMMAND, W_OPEN, 0); return TRUE; }
        if (key == CTRL_A) { anchor = 0; caret = length; repaint(); return TRUE; }
        if (key == CTRL_C) { copy_selection(); return TRUE; }
        if (key == CTRL_X) {
            if (has_selection()) { copy_selection(); replace_range(selection_start(),
                selection_end(), "", 0, typing_format); }
            return TRUE;
        }
        if (key == CTRL_V) {
            char clip[4096];
            int bytes = sys_clip_get(clip, sizeof(clip));
            if (bytes > 0 && bytes <= (int)sizeof(clip)) insert_text(clip, bytes);
            return TRUE;
        }
        if (key == LARROW) { move_to(prev_char(caret), shift); return TRUE; }
        if (key == RARROW) { move_to(next_char(caret), shift); return TRUE; }
        if (key == HOME) { move_to(line_start(line_number(caret)), shift); return TRUE; }
        if (key == END) { move_to(line_end(line_start(line_number(caret))), shift); return TRUE; }
        if (key == UP || key == DN || key == PGUP || key == PGDN) {
            row = line_number(caret);
            start = line_start(row);
            col = cell_width(start, caret);
            target = row + (key == UP ? -1 : key == DN ? 1 :
                            key == PGUP ? -PAGE_LINES : PAGE_LINES);
            if (target < 0) target = 0;
            if (target > line_number(length)) target = line_number(length);
            start = line_start(target); end = line_end(start);
            while (start < end && cell_width(line_start(target), next_char(start)) <= col)
                start = next_char(start);
            move_to(start, shift); return TRUE;
        }
        if (key == DEL) {
            if (has_selection()) replace_range(selection_start(), selection_end(), "", 0, typing_format);
            else if (caret < length) replace_range(caret, next_char(caret), "", 0, typing_format);
            return TRUE;
        }
        if (key == CTRL_M && ((int)p2 & CTRLKEY)) { insert_text("\f", 1); return TRUE; }
        if (key == '\r' || key == '\n') { insert_text("\n", 1); return TRUE; }
        if (key == '\t') { insert_text("    ", 4); return TRUE; }
        if (key >= 32 && key <= 126 && !((int)p2 & (CTRLKEY | ALTKEY))) {
            if (fullwidth_input) {
                char wide[2];
                wide[0] = key == ' ' ? (char)0xa1 : (char)0xa3;
                wide[1] = key == ' ' ? (char)0xa1 : (char)(key + 0x80);
                insert_text(wide, 2);
            } else {
                char ch = key; insert_text(&ch, 1);
            }
            return TRUE;
        }
    }
    if (msg == LEFT_BUTTON) {
        int x = (int)p1 - GetClientLeft(wnd), y = (int)p2 - GetClientTop(wnd);
        int row = top_line + y - BODY_TOP, pos, end, col;
        if (y >= BODY_TOP && y < ClientHeight(wnd) - 1) {
            if (row > line_number(length)) row = line_number(length);
            pos = line_start(row); end = line_end(pos);
            col = x - aligned_x(row, pos, end) + left_col;
            if (col < 0) col = 0;
            while (pos < end && col > 0) {
                col -= char_size(pos) == 1 ? 1 : 2;
                if (col >= 0) pos = next_char(pos);
            }
            move_to(pos, FALSE);
            SendMessage(wnd, SETFOCUS, TRUE, 0);
            return TRUE;
        }
    }
    if (msg == SETFOCUS) {
        /* NORMAL owns DFLAT's inFocus/childfocus chain.  Do not consume this
         * message here: doing so leaves the painted canvas without keyboard
         * delivery.  The custom part begins only after focus is established. */
        int result = DefaultWndProc(wnd, msg, p1, p2);
        if (p1) {
            sys_dflat_cursor_visible(TRUE);
            repaint();
        }
        return result;
    }
    if (msg == MOUSE_WHEEL) {
        top_line -= (int)p1 > 0 ? 3 : -3;
        if (top_line < 0) top_line = 0;
        if (top_line > line_number(length)) top_line = line_number(length);
        SendMessage(wnd, PAINT, 0, 0); return TRUE;
    }
    return DefaultWndProc(wnd, msg, p1, p2);
}

static int WriteProc(WINDOW wnd, MESSAGE msg, PARAM p1, PARAM p2)
{
    if (msg == CREATE_WINDOW) {
        int result = DefaultWndProc(wnd, msg, p1, p2);
        application = wnd;
        canvas = CreateWindow(NORMAL, filename, 2, 2, 20, 60, NULL, wnd,
                              CanvasProc, MOVEABLE | SIZEABLE | MINMAXBOX |
                              HASBORDER | CONTROLBOX);
        if (canvas) {
            WindowClientColor(canvas, WHITE, BLACK);
            WindowReverseColor(canvas, BLACK, YELLOW);
            SendMessage(canvas, MAXIMIZE, 0, 0);
        }
        return result;
    }
    if (msg == COMMAND && (int)p2 == 0) {
        char path[64];
        switch ((int)p1) {
        case W_NEW:
            if (dirty && !YesNoBox("DISCARD UNSAVED CHANGES?")) return TRUE;
            strcpy(filename, "UNTITLED.AWD"); clear_document();
            AddTitle(canvas, filename); return TRUE;
        case W_OPEN:
            if (!OpenFileDialogBox("*.AWD", path)) return TRUE;
            if (dirty && !YesNoBox("DISCARD UNSAVED CHANGES?")) return TRUE;
            uppercase_path(path);
            if (!load_document(path)) ErrorMessage("CANNOT OPEN AWD FILE.");
            else { strncpy(filename, path, 63); filename[63] = 0;
                   AddTitle(canvas, filename); repaint(); }
            return TRUE;
        case W_SAVE:
            if (strcmp(filename, "UNTITLED.AWD")) {
                if (!save_document(filename)) ErrorMessage("CANNOT SAVE FILE.");
                return TRUE;
            }
            /* fall through */
        case W_SAVE_AS:
            strcpy(path, filename);
            if (SaveAsDialogBox("*.AWD", NULL, path)) {
                uppercase_path(path);
                if (!save_document(path)) ErrorMessage("CANNOT SAVE FILE.");
                else { strncpy(filename, path, 63); filename[63] = 0;
                       AddTitle(canvas, filename); repaint(); }
            }
            return TRUE;
        case W_EXIT:
            if (!dirty || YesNoBox("DISCARD UNSAVED CHANGES?")) {
                closing_allowed = TRUE; PostMessage(wnd, CLOSE_WINDOW, 0, 0);
            }
            return TRUE;
        case W_FIND:
            if (InputBox(wnd, "SEARCH", "FIND:", find_text, 79, 48))
                find_next(TRUE);
            SendMessage(canvas, SETFOCUS, TRUE, 0); return TRUE;
        case W_FIND_NEXT: find_next(TRUE); return TRUE;
        case W_REPLACE:
            replace_dialog(FALSE);
            SendMessage(canvas, SETFOCUS, TRUE, 0);
            return TRUE;
        case W_REPLACE_ALL:
            replace_dialog(TRUE);
            SendMessage(canvas, SETFOCUS, TRUE, 0);
            return TRUE;
        case W_COPY: copy_selection(); return TRUE;
        case W_CUT:
            if (has_selection()) { copy_selection(); replace_range(selection_start(),
                selection_end(), "", 0, typing_format); }
            return TRUE;
        case W_PASTE: {
            char clip[4096]; int bytes = sys_clip_get(clip, sizeof(clip));
            if (bytes > 0 && bytes <= (int)sizeof(clip)) insert_text(clip, bytes);
            return TRUE;
        }
        case W_SELECT_ALL: anchor = 0; caret = length; repaint(); return TRUE;
        case W_BOLD: case W_ITALIC: case W_FONT:
        case W_SMALL: case W_NORMAL: case W_LARGE:
            format_selection((int)p1); return TRUE;
        case W_ALIGN_LEFT: set_alignment(ALIGN_LEFT); return TRUE;
        case W_ALIGN_CENTER: set_alignment(ALIGN_CENTER); return TRUE;
        case W_ALIGN_RIGHT: set_alignment(ALIGN_RIGHT); return TRUE;
        case W_PAGE_BREAK: insert_text("\f", 1); return TRUE;
        case W_FULLWIDTH:
            fullwidth_input = GetCommandToggle(&WriteMenu, W_FULLWIDTH);
            repaint(); return TRUE;
        case W_ABOUT:
            MessageBox("AMUN WRITE 0.2(DEV)",
                       "DFLAT WINDOW / INDEPENDENT DOCUMENT CANVAS\n"
                       "AWD/2: READABLE MARKUP + INLINE FONT TAGS\n"
                       "FONTS: LATIN 8X16 / HZK16, THREE INK SIZES\n"
                       "PAGES: 18 LINES, EXPLICIT PAGE BREAKS");
            return TRUE;
        default: break;
        }
    }
    if (msg == CLOSE_WINDOW && !closing_allowed && dirty &&
        !YesNoBox("DISCARD UNSAVED CHANGES?")) return TRUE;
    return DefaultWndProc(wnd, msg, p1, p2);
}

int main(int argc, char **argv)
{
    WINDOW wnd;
    cfg.ScreenLines = SCREENHEIGHT;
    if (!init_messages()) return 1;
    wnd = CreateWindow(APPLICATION, "AMUN WRITE 0.2(DEV)", 0, 0, -1, -1,
                       &WriteMenu, NULL, WriteProc,
                       MOVEABLE | SIZEABLE | HASBORDER | MINMAXBOX | HASSTATUSBAR);
    if (!wnd || !canvas) return 1;
    clear_document();
    if (argc > 1 && load_document(argv[1])) {
        strncpy(filename, argv[1], 63); filename[63] = 0;
        uppercase_path(filename);
        AddTitle(canvas, filename);
    }
    SendMessage(wnd, SETFOCUS, TRUE, 0);
    SendMessage(canvas, SETFOCUS, TRUE, 0);
    repaint();
    while (dispatch_message()) ;
    return 0;
}
