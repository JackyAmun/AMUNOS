#include "dflat.h"
#include "syscall.h"

char DFlatApplication[] = "DFLAT 0.1";

DEFMENU(DemoMenu)
    POPDOWN("~File", NULL, "Demo controls")
        SELECTION("~Reset controls", ID_CLEAR, 0, 0)
        SELECTION("E~xit", ID_EXIT, ALT_X, 0)
    ENDPOPDOWN
    POPDOWN("~Tools", NULL, "Show demo information")
        SELECTION("Show ~Value", ID_OPEN, 0, 0)
        SELECTION("~About DFLAT", ID_ABOUT, 0, 0)
    ENDPOPDOWN
ENDMENU

/* menus.c also owns DFLAT's SystemMenu; EDIT normally supplies these hooks. */
void PrepFileMenu(void *wnd, struct Menu *menu) { (void)wnd; (void)menu; }
void PrepEditMenu(void *wnd, struct Menu *menu) { (void)wnd; (void)menu; }
void PrepSearchMenu(void *wnd, struct Menu *menu) { (void)wnd; (void)menu; }

DIALOGBOX(ControlsDemo)
    DB_TITLE("DFLAT 0.1 Control Showcase", -1, -1, 23, 62)
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
    CONTROL(BUTTON, " ~Choose... ",    41, 9, 1, 14, ID_PRINTERPORT)
    CONTROL(TEXT, "First choice",      32,11, 1, 25, ID_INPUTTEXT)
    CONTROL(TEXT, "Value:",             3, 8, 1, 8, ID_LEFTMARGIN)
    CONTROL(SPINBUTTON, NULL,           12, 8, 1,10, ID_LEFTMARGIN)
    CONTROL(BOX, "Keyboard",            1, 11, 8, 27, 0)
    CONTROL(TEXTBOX, NULL,               3, 12, 5, 23, ID_HELPTEXT)
    CONTROL(BOX, "Actions",            30, 15, 5, 29, 0)
    CONTROL(BUTTON, "   ~OK   ",       34, 17, 1, 8, ID_OK)
    CONTROL(BUTTON, " ~Cancel ",       46, 17, 1, 8, ID_CANCEL)
ENDDB

DIALOGBOX(ChoiceDialog)
    DB_TITLE("Choose a control", -1, -1, 15, 50)
    CONTROL(TEXT, "Select a control for the preview:", 2, 1, 1, 40, ID_HELPTEXT)
    CONTROL(LISTBOX, NULL,            2, 3, 8, 44, ID_FILES)
    CONTROL(BUTTON, "   ~OK   ",    12,12, 1,  8, ID_OK)
    CONTROL(BUTTON, " ~Cancel ",    28,12, 1, 10, ID_CANCEL)
ENDDB

static WINDOW demo_dialog;
static char selected_choice[32] = "First choice";

static void reset_demo(WINDOW wnd)
{
    WINDOW value = ControlWindow(&ControlsDemo, ID_LEFTMARGIN);

    PutItemText(wnd, ID_FILENAME, "AMUNOS");
    PutItemText(wnd, ID_INPUTTEXT, selected_choice);
    SetCheckBox(&ControlsDemo, ID_MATCHCASE);
    PushRadioButton(&ControlsDemo, ID_COLOR);
    if (value != NULL) {
        SendMessage(value, LB_SETSELECTION, 0, 0);
        SendMessage(value, PAINT, 0, 0);
    }
}

static int ChoiceProc(WINDOW wnd, MESSAGE msg, PARAM p1, PARAM p2)
{
    if (msg == INITIATE_DIALOG) {
        PutItemText(wnd, ID_FILES, "Button");
        PutItemText(wnd, ID_FILES, "CheckBox");
        PutItemText(wnd, ID_FILES, "RadioButton");
        PutItemText(wnd, ID_FILES, "SpinButton");
        SendMessage(ControlWindow(&ChoiceDialog, ID_FILES), LB_SETSELECTION, 0, 0);
        ControlWindow(&ChoiceDialog, ID_FILES)->wtop = 0;
        SendMessage(ControlWindow(&ChoiceDialog, ID_FILES), PAINT, 0, 0);
    } else if (msg == COMMAND) {
        if ((int)p1 == ID_FILES && (int)p2 == LB_CHOOSE) {
            SendMessage(wnd, COMMAND, ID_OK, 0);
            return TRUE;
        }
        if ((int)p1 == ID_OK && (int)p2 == 0)
            GetDlgListText(wnd, selected_choice, ID_FILES);
    }
    return DefaultWndProc(wnd, msg, p1, p2);
}

static void choose_control(WINDOW wnd)
{
    if (DialogBox(wnd, &ChoiceDialog, TRUE, ChoiceProc))
        PutItemText(wnd, ID_INPUTTEXT, selected_choice);
}

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
        PutItemText(wnd, ID_LEFTMARGIN, "42");
        PutItemText(wnd, ID_LEFTMARGIN, "64");
        PutItemText(wnd, ID_LEFTMARGIN, "80");
        strcpy(info, "Tab/arrows: focus.\nType in EditBox.");
        if (sys_dflat_cursor_get(state) == 0) {
            sprintf(info + strlen(info), "\nXY=%d,%d shape=%d vis=%d",
                state[0], state[1], state[2], state[3]);
        }
        PutItemText(wnd, ID_HELPTEXT, info);
        SendMessage(ControlWindow(&ControlsDemo, ID_HELPTEXT), PAINT, 0, 0);
        demo_dialog = wnd;
        reset_demo(wnd);
    } else if (msg == COMMAND && (int)p1 == ID_PRINTERPORT && (int)p2 == 0) {
        choose_control(wnd);
        return TRUE;
    }
    return DefaultWndProc(wnd, msg, p1, p2);
}

static int DemoAppProc(WINDOW wnd, MESSAGE msg, PARAM p1, PARAM p2)
{
    if (msg == COMMAND && (int)p2 == 0) {
        if ((int)p1 == ID_CLEAR && demo_dialog != NULL) {
            strcpy(selected_choice, "First choice");
            reset_demo(demo_dialog);
            return TRUE;
        }
        if ((int)p1 == ID_OPEN) {
            MessageBox("Current value", selected_choice);
            return TRUE;
        }
        if ((int)p1 == ID_ABOUT) {
            MessageBox("DFLAT 0.1", "AMUNOS text controls\nwith modal selection dialogs.");
            return TRUE;
        }
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
    wnd = CreateWindow(APPLICATION, "DFLAT 0.1", 0, 0, -1, -1,
        &DemoMenu, NULL, DemoAppProc, MOVEABLE | SIZEABLE | HASBORDER | HASSTATUSBAR);
    if (wnd == NULL)
        return 1;
    SendMessage(wnd, SETFOCUS, TRUE, 0);
    DialogBox(wnd, &ControlsDemo, FALSE, ControlsDemoProc);
    while (dispatch_message())
        ;
    return 0;
}
