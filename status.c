#include "dflat.h"
#include "syscall.h"

char DFlatApplication[] = "STATUS";

enum {
    STATUS_REFRESH = 3000,
    STATUS_ABOUT = 3001
};

DEFMENU(StatusMenu)
    POPDOWN("~FILE", NULL, "STATUS MONITOR")
        SELECTION("~REFRESH", STATUS_REFRESH, F5, 0)
        SELECTION("E~XIT", ID_EXIT, ALT_X, 0)
    ENDPOPDOWN
    POPDOWN("~VIEW", NULL, "SYSTEM RESOURCES")
        SELECTION("REFRESH ~NOW", STATUS_REFRESH, 0, 0)
    ENDPOPDOWN
    POPDOWN("~HELP", NULL, "ABOUT STATUS")
        SELECTION("~ABOUT", STATUS_ABOUT, 0, 0)
    ENDPOPDOWN
ENDMENU

void PrepFileMenu(void *wnd, struct Menu *menu) { (void)wnd; (void)menu; }
void PrepEditMenu(void *wnd, struct Menu *menu) { (void)wnd; (void)menu; }
void PrepSearchMenu(void *wnd, struct Menu *menu) { (void)wnd; (void)menu; }

static sysinfo_t snapshot;
static mem_info_t memory_snapshot;
static task_info_t task_snapshot;
static unsigned long refreshes;
static unsigned clock_ticks;

static void line(WINDOW wnd, int y, const char *text)
{
    char buf[96];
    int width = ClientWidth(wnd);
    int n = strlen(text);
    if (n > width)
        n = width;
    memset(buf, ' ', width);
    memcpy(buf, text, n);
    buf[width] = '\0';
    SetStandardColor(wnd);
    PutWindowLine(wnd, buf, 0, y);
}

static void heading(WINDOW wnd, int y, const char *text)
{
    char buf[96];
    int width = ClientWidth(wnd);
    int n = strlen(text);
    if (n > width)
        n = width;
    memset(buf, ' ', width);
    memcpy(buf, text, n);
    buf[width] = '\0';
    WindowReverseColor(wnd, WHITE, BLUE);
    SetReverseColor(wnd);
    PutWindowLine(wnd, buf, 0, y);
    SetStandardColor(wnd);
}

static void refresh_status(WINDOW wnd)
{
    char buf[96];
    int y, i;
    const char *names[] = {"IDE0-M", "IDE0-S", "IDE1-M", "IDE1-S",
                           "FDC", "ATAPI", "AHCI"};

    if (sys_sysinfo(&snapshot) < 0) {
        line(wnd, 0, "STATUS: SYSINFO UNAVAILABLE");
        return;
    }

    heading(wnd, 0, " SYSTEM OVERVIEW");
    sprintf(buf, "AMUNOS 6.5.7(DEV2)   REFRESH %lu   TICKS %u",
            ++refreshes, snapshot.ticks);
    line(wnd, 1, buf);
    sprintf(buf, "DRIVE: %c:   CWD CLUSTER: %d   FAT: %d-BIT",
            snapshot.drive_letter, snapshot.cwd_cluster, snapshot.fs_fat_bits);
    line(wnd, 2, buf);
    sprintf(buf, "SECTORS/CLUSTER: %d   ROOT LBA: %d   DATA LBA: %d",
            snapshot.fs_spc, snapshot.fs_root_lba, snapshot.fs_data_lba);
    line(wnd, 3, buf);
    sprintf(buf, "VOLUME SECTORS: %u   DISPLAY: %s",
            snapshot.current_sectors, snapshot.fb_active ? "FRAMEBUFFER" : "TEXT");
    line(wnd, 4, buf);
    line(wnd, 5, "");

    heading(wnd, 6, " STORAGE / DEVICES");
    line(wnd, 7, "DEVICE       STATE       CAPACITY/SECTORS       MODEL");
    y = 8;
    for (i = 0; i < 7 && y < ClientHeight(wnd) - 7; i++, y++) {
        sprintf(buf, "%-10s   %-7s   %10u              %.20s",
                names[i], snapshot.dev_present[i] ? "READY" : "ABSENT",
                snapshot.dev_sectors[i], snapshot.dev_model[i]);
        line(wnd, y, buf);
    }

    heading(wnd, ClientHeight(wnd) - 7, " TASK SCHEDULER");
    if (sys_task_info(&task_snapshot) == 0) {
        sprintf(buf, "TASKS: %u   RUNNING: %u   READY: %u   SLEEPING: %u",
                task_snapshot.total, task_snapshot.running, task_snapshot.ready,
                task_snapshot.sleeping);
        line(wnd, ClientHeight(wnd) - 6, buf);
        sprintf(buf, "EXITED SLOTS: %u   FOREGROUND: %s   TICKS: %u",
                task_snapshot.exited,
                task_snapshot.foreground_active ? "ACTIVE" : "IDLE",
                task_snapshot.ticks);
        line(wnd, ClientHeight(wnd) - 5, buf);
    } else {
        line(wnd, ClientHeight(wnd) - 6, "TASK INFO UNAVAILABLE");
        line(wnd, ClientHeight(wnd) - 5, "");
    }

    heading(wnd, ClientHeight(wnd) - 4, " KERNEL HEAP");
    if (sys_mem_info(&memory_snapshot) == 0) {
        sprintf(buf, "TOTAL: %u KB   FREE: %u KB   USED: %u KB",
                memory_snapshot.total_bytes / 1024,
                memory_snapshot.free_bytes / 1024,
                (memory_snapshot.total_bytes - memory_snapshot.free_bytes) / 1024);
        line(wnd, ClientHeight(wnd) - 3, buf);
        sprintf(buf, "LARGEST FREE BLOCK: %u KB   FREE BLOCKS: %u",
                memory_snapshot.largest_free_bytes / 1024,
                memory_snapshot.free_blocks);
        line(wnd, ClientHeight(wnd) - 2, buf);
    } else {
        line(wnd, ClientHeight(wnd) - 3, "MEM INFO UNAVAILABLE");
        line(wnd, ClientHeight(wnd) - 2, "");
    }
    line(wnd, ClientHeight(wnd) - 1, "F5 REFRESH   ALT+X EXIT");
    SendMessage(wnd, ADDSTATUS, (PARAM) "LIVE SYSTEM SNAPSHOT", 0);
}

static int StatusProc(WINDOW wnd, MESSAGE msg, PARAM p1, PARAM p2)
{
    switch (msg) {
    case CREATE_WINDOW:
        SendMessage(wnd, CAPTURE_CLOCK, 0, 0);
        break;
    case PAINT:
        NormalProc(wnd, msg, p1, p2);
        refresh_status(wnd);
        return TRUE;
    case CLOCKTICK:
        if (++clock_ticks >= 18) {
            clock_ticks = 0;
            refresh_status(wnd);
        }
        return TRUE;
    case KEYBOARD:
        if ((int)p1 == F5) {
            refresh_status(wnd);
            return TRUE;
        }
        if ((int)p1 == ALT_X || (int)p1 == CTRL_F4) {
            PostMessage(wnd, CLOSE_WINDOW, 0, 0);
            return TRUE;
        }
        break;
    case COMMAND:
        if ((int)p1 == STATUS_REFRESH) {
            refresh_status(wnd);
            return TRUE;
        }
        if ((int)p1 == STATUS_ABOUT) {
            MessageBox("STATUS", "AMUNOS DFLAT SYSTEM MONITOR\n"
                       "DEVICE DATA: SYSINFO\nHEAP DATA: MEM INFO");
            return TRUE;
        }
        break;
    case CLOSE_WINDOW:
        SendMessage(wnd, RELEASE_CLOCK, 0, 0);
        break;
    default:
        break;
    }
    return DefaultWndProc(wnd, msg, p1, p2);
}

int main(void)
{
    WINDOW wnd;

    cfg.ScreenLines = SCREENHEIGHT;
    if (!init_messages())
        return 1;
    wnd = CreateWindow(APPLICATION, "STATUS 1.0", 0, 0, -1, -1,
                       &StatusMenu, NULL, StatusProc,
                       MOVEABLE | SIZEABLE | HASBORDER | HASSTATUSBAR);
    if (wnd == NULL)
        return 1;
    SendMessage(wnd, SETFOCUS, TRUE, 0);
    while (dispatch_message())
        ;
    return 0;
}
