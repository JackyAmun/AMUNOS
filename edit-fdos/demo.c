#include "dflat.h"
#include "syscall.h"

char DFlatApplication[] = "DFLAT Controls";

DEFMENU(DemoMenu)
    POPDOWN("~File", NULL, "Close the showcase")
        SELECTION("E~xit", ID_EXIT, ALT_X, 0)
    ENDPOPDOWN
ENDMENU

/* menus.c also owns DFLAT's SystemMenu; EDIT normally supplies these hooks. */
void PrepFileMenu(void *wnd, struct Menu *menu) { (void)wnd; (void)menu; }
void PrepEditMenu(void *wnd, struct Menu *menu) { (void)wnd; (void)menu; }
void PrepSearchMenu(void *wnd, struct Menu *menu) { (void)wnd; (void)menu; }

DIALOGBOX(ControlsDemo)
    DB_TITLE("DFLAT Control Showcase", -1, -1, 23, 62)
    CONTROL(BOX, "Input",              1, 1, 9, 27, 0)
    CONTROL(TEXT, "Name:",             3, 2, 1, 7, ID_FILENAME)
    CONTROL(EDITBOX, NULL,             11, 2, 1, 15, ID_FILENAME)
    CONTROL(CHECKBOX, NULL,             3, 4, 1, 3, ID_MATCHCASE)
    CONTROL(TEXT, "Enable option",      7, 4, 1, 17, ID_MATCHCASE)
    CONTROL(RADIOBUTTON, NULL,           3, 5, 1, 3, ID_COLOR)
    CONTROL(TEXT, "Mode A",             7, 5, 1, 8, ID_COLOR)
    CONTROL(RADIOBUTTON, NULL,           3, 6, 1, 3, ID_MONO)
    CONTROL(TEXT, "Mode B",             7, 6, 1, 8, ID_MONO)
    CONTROL(BOX, "Selection",          30, 1, 13, 29, 0)
    CONTROL(TEXT, "Items:",            32, 2, 1, 8, ID_FILES)
    CONTROL(LISTBOX, NULL,              32, 3, 5, 25, ID_FILES)
    CONTROL(TEXT, "Choice:",           32, 9, 1, 8, ID_PRINTERPORT)
    CONTROL(COMBOBOX, NULL,             32, 10, 2, 23, ID_PRINTERPORT)
    CONTROL(TEXT, "Value:",             3, 8, 1, 8, ID_LEFTMARGIN)
    CONTROL(SPINBUTTON, NULL,           12, 8, 1, 8, ID_LEFTMARGIN)
    CONTROL(BOX, "Keyboard",            1, 11, 8, 27, 0)
    CONTROL(TEXTBOX, NULL,               3, 12, 5, 23, ID_HELPTEXT)
    CONTROL(BOX, "Actions",            30, 15, 5, 29, 0)
    CONTROL(BUTTON, "   ~OK   ",       34, 17, 1, 8, ID_OK)
    CONTROL(BUTTON, " ~Cancel ",       46, 17, 1, 8, ID_CANCEL)
ENDDB

static int ControlsDemoProc(WINDOW wnd, MESSAGE msg, PARAM p1, PARAM p2)
{
    if (msg == INITIATE_DIALOG) {
        int state[4];
        char info[160];
        PutItemText(wnd, ID_FILENAME, "AMUNOS");
        PutItemText(wnd, ID_FILES, "Button");
        PutItemText(wnd, ID_FILES, "CheckBox");
        PutItemText(wnd, ID_FILES, "RadioButton");
        PutItemText(wnd, ID_FILES, "ListBox");
        PutItemText(wnd, ID_FILES, "ComboBox");
        PutItemText(wnd, ID_FILES, "SpinButton");
        SendMessage(ControlWindow(&ControlsDemo, ID_FILES), PAINT, 0, 0);
        PutComboListText(wnd, ID_PRINTERPORT, "First choice");
        PutComboListText(wnd, ID_PRINTERPORT, "Second choice");
        strcpy(info, "Tab/arrows: focus.\nType in EditBox.");
        if (sys_dflat_cursor_get(state) == 0) {
            sprintf(info + strlen(info), "\nXY=%d,%d shape=%d vis=%d",
                state[0], state[1], state[2], state[3]);
        }
        PutItemText(wnd, ID_HELPTEXT, info);
        SendMessage(ControlWindow(&ControlsDemo, ID_HELPTEXT), PAINT, 0, 0);
        SetCheckBox(&ControlsDemo, ID_MATCHCASE);
        PushRadioButton(&ControlsDemo, ID_COLOR);
    }
    return DefaultWndProc(wnd, msg, p1, p2);
}

int main(int argc, char **argv)
{
    WINDOW wnd;
    int i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "/U") || !strcmp(argv[i], "/u")) {
            cfg.mono = 0;
            cfg.theme = 1;
        } else if (!strcmp(argv[i], "/A") || !strcmp(argv[i], "/a")) {
            cfg.mono = 0;
            cfg.theme = 0;
        } else if (!strcmp(argv[i], "/B") || !strcmp(argv[i], "/b")) {
            cfg.mono = 1;
        } else if (!strcmp(argv[i], "/I") || !strcmp(argv[i], "/i")) {
            cfg.mono = 2;
        } else {
            printf("Usage: DFLAT [/A|/U|/B|/I]\n");
            return 1;
        }
    }

    cfg.ScreenLines = SCREENHEIGHT;
    if (!init_messages())
        return 1;
    wnd = CreateWindow(APPLICATION, "DFLAT Controls", 0, 0, -1, -1,
        &DemoMenu, NULL, NULL, MOVEABLE | SIZEABLE | HASBORDER | HASSTATUSBAR);
    if (wnd == NULL)
        return 1;
    SendMessage(wnd, SETFOCUS, TRUE, 0);
    DialogBox(wnd, &ControlsDemo, TRUE, ControlsDemoProc);
    PostMessage(wnd, CLOSE_WINDOW, 0, 0);
    while (dispatch_message())
        ;
    return 0;
}
