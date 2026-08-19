/*  File-Open Dialog Box

    Part of FreeDOS Edit

*/

/* I N C L U D E S /////////////////////////////////////////////////////// */

#include "dflat.h"

/* G L O B A L S ///////////////////////////////////////////////////////// */

static char FileSpec[15], SrchSpec[15], FileName[15];
extern DBOX FileOpen, SaveAs;
extern char BrowsePath[];   /* AMUNOS: 对话框浏览目录 (direct.c) */

/* P R O T O T Y P E S /////////////////////////////////////////////////// */

static BOOL DlgFileOpen(char *, char *, char *, DBOX *);
static int DlgFnOpen(WINDOW, MESSAGE, PARAM, PARAM);
static void InitDlgBox(WINDOW);
static void EnterDirectory(WINDOW, const char *);
static void EnterDrive(WINDOW, char);
#ifdef STRIPPATH
static void StripPath(char *);
#endif
BOOL BuildFileList(WINDOW, char *);
void BuildDirectoryList(WINDOW);
void BuildDriveList(WINDOW);
void BuildPathDisplay(WINDOW);

/* F U N C T I O N S ///////////////////////////////////////////////////// */

BOOL OpenFileDialogBox(char *Fspec, char *Fname)
{
    return DlgFileOpen(Fspec, Fspec, Fname, &FileOpen);
}

/* Save as Dialog Box */
BOOL SaveAsDialogBox(char *Fspec, char *Sspec, char *Fname)
{
    return DlgFileOpen(Fspec, Sspec ? Sspec : Fspec, Fname, &SaveAs);
}

/* Generic File Open */
static BOOL DlgFileOpen(char *Fspec, char *Sspec, char *Fname, DBOX *db)
{
    BOOL rtn;

    strncpy(FileSpec, Fspec, 15);
    strncpy(SrchSpec, Sspec, 15);
    if ((rtn = DialogBox(NULL, db, TRUE, DlgFnOpen)) != FALSE)
        strcpy(Fname, FileName);

    return rtn;

}

/* Process dialog box messages */
static int DlgFnOpen(WINDOW wnd,MESSAGE msg,PARAM p1,PARAM p2)
{
    switch (msg)
        {
        case CREATE_WINDOW:
            {
            int rtn = DefaultWndProc(wnd, msg, p1, p2);
            DBOX *db = wnd->extension;
            WINDOW cwnd = ControlWindow(db, ID_FILENAME);
            SendMessage(cwnd, SETTEXTLENGTH, 64, 0);
            return rtn;
            }
        case INITIATE_DIALOG:
            InitDlgBox(wnd);
            break;
        case COMMAND:
            switch ((int) p1)
                {
                case ID_OK:
                    {
                    if ((int)p2 == 0)
                        {
                        char fn[MAXPATH+1];
                        int flen;

                    	GetItemText(wnd, ID_FILENAME, fn, MAXPATH);
                        flen = strlen(fn);
                        if (flen == 0 || strchr(fn, '*') || strchr(fn, '?') ||
                            fn[flen-1] == '/' || fn[flen-1] == ':' ||
                            fn[flen-1] == '\\') {
                            /* --- 目录 / 通配符 / 空: 切浏览目录并刷新 --- */
                            DBOX *db = wnd->extension;
                            WINDOW cwnd = ControlWindow(db, ID_FILENAME);

                            CreatePath(NULL, fn, FALSE, TRUE);
                            if (flen && fn[flen-1] != '/' && fn[flen-1] != ':' &&
                                fn[flen-1] != '\\') {
                                /* 通配符模式保留为过滤串 (如 "*.C") */
                                strcpy(FileSpec, fn);
                                strcpy(SrchSpec, fn);
                            }
                            InitDlgBox(wnd);
                            SendMessage(cwnd, SETFOCUS, TRUE, 0);
                            return TRUE;
                            }
                        /* --- 具体文件名: 规范化到 BrowsePath 下 --- */
                        {
                        char *base = fn;   /* 纯文件名 */
                        char *slash = strrchr(fn, '/');
                        if (slash)
                            base = slash + 1;
                        else if (fn[0] && fn[1] == ':')
                            base = (fn[2]) ? fn + 2 : "";
                        CreatePath(NULL, fn, FALSE, TRUE);  /* 目录并入 BrowsePath */
                        strcpy(FileName, BrowsePath);
                        if (base[0] && strcmp(base, "*"))
                            strcat(FileName, base);
                        }
                        }

                    break;
                    }
                case ID_FILES:
                    switch ((int) p2)
                        {
                        case ENTERFOCUS:
                        case LB_SELECTION:
                            /* Selected a different filename */
                            GetDlgListText(wnd, FileName, ID_FILES);
                            PutItemText(wnd, ID_FILENAME, FileName);
                            break;
                        case LB_CHOOSE:
                            /* Choose a file name */
                            GetDlgListText(wnd, FileName, ID_FILES);
                            SendMessage(wnd, COMMAND, ID_OK, 0);
                            break;
                        default:
                            break;

                        }
                    return TRUE;
                case ID_DIRECTORY:
                    switch ((int) p2)
                        {
                        case ENTERFOCUS:
                            PutItemText(wnd, ID_FILENAME, FileSpec);
                            break;
                        case LB_SELECTION:
                        case LB_CHOOSE:
                            /* 单击/双击/回车都: 切目录 + 刷新 Files+Directories,
                             * 不关闭对话框 (原版双击会立刻关, UX bug) */
                            {
                            char dd[15];
                            GetDlgListText(wnd, dd, ID_DIRECTORY);
                            EnterDirectory(wnd, dd);
                            }
                            break;
                        default:
                            break;
                        }
                    return TRUE;

                case ID_DRIVE:
                    switch ((int) p2)
                        {
                        case ENTERFOCUS:
                            PutItemText(wnd, ID_FILENAME, FileSpec);
                            break;
                        case LB_SELECTION:
                        case LB_CHOOSE:
                            /* 单击/双击/回车都: 切盘 + 刷新列表, 不关 */
                            {
                            char dr[15];
                            GetDlgListText(wnd, dr, ID_DRIVE);
                            /* *** 0.6e: string has form "[-X-]" *** */
                            EnterDrive(wnd, dr[2]);
                            }
                            break;
                        default:
                            break;
                        }
                    return TRUE;

                default:
                    break;
                }
        default:
            break;

        }

    return DefaultWndProc(wnd, msg, p1, p2);

}

/* Initialize the dialog box */
static void InitDlgBox(WINDOW wnd)
{
    if (*FileSpec)
        PutItemText(wnd, ID_FILENAME, FileSpec);

    BuildPathDisplay(wnd);
    if (BuildFileList(wnd, SrchSpec))
        BuildDirectoryList(wnd);

    BuildDriveList(wnd);
}

/* ---- Enter directory: append name to BrowsePath, refresh all 3 lists ----
 * AMUNOS 改进: 单击/双击/回车都只切目录 + 刷新, 不发 ID_OK,
 * 原版 DFLAT 双击会立刻关闭对话框, 用户无法连续浏览多层子目录。 */
static void EnterDirectory(WINDOW wnd, const char *name)
{
    if (!name || !*name) return;
    /* 跳过 "." 和 ".." — AMUNOS 无进程 cwd, ".." 不支持回上级 */
    if (!strcmp(name, ".") || !strcmp(name, "..")) return;

    /* 防重入: 如果 BrowsePath 已经以 "<name>/" 结尾, 则跳过 (避免 sendkey ret
     * 双触发 / DFLAT 重复派发导致路径变成 A:/BIN/BIN/ 然后 opendir 失败) */
    {
        size_t blen = strlen(BrowsePath);
        size_t nlen = strlen(name);
        if (blen > nlen + 3 &&  /* 至少 "X:/" + name + "/" */
            BrowsePath[blen - 1] == '/' &&
            strncmp(BrowsePath + blen - 1 - nlen, name, nlen) == 0) {
            /* 已经在这个目录, 不重复入 — 仍调 BuildFileList 触发一次刷新 (清旧项) */
            BuildPathDisplay(wnd);
            BuildFileList(wnd, SrchSpec);
            BuildDirectoryList(wnd);
            return;
        }
    }

    {
        char nb[64];
        strcpy(nb, BrowsePath);
        if (nb[strlen(nb)-1] != '/')
            strcat(nb, "/");
        strcat(nb, name);
        strcat(nb, "/");
        strcpy(BrowsePath, nb);
    }

    /* 只刷 Files+Directories+Path, 不动焦点, 不关对话框 */
    BuildPathDisplay(wnd);
    BuildFileList(wnd, SrchSpec);
    BuildDirectoryList(wnd);
}

/* ---- Switch to drive (A/B/C/D): set BrowsePath, refresh ---- */
static void EnterDrive(WINDOW wnd, char drvchar)
{
    if (drvchar < 'A' || drvchar > 'D') return;
    BrowsePath[0] = drvchar;
    BrowsePath[1] = ':';
    BrowsePath[2] = '/';
    BrowsePath[3] = '\0';
    BuildPathDisplay(wnd);
    BuildFileList(wnd, SrchSpec);
    BuildDirectoryList(wnd);
}

/* Strip the drive and path information from a file spec */
#ifdef STRIPPATH /* normally not used... */
static void StripPath(char *filespec)
{
    char *cp, *cp1;

    cp = strchr(filespec, ':');
    if (cp != NULL)
        cp++;
    else
        cp = filespec;

    while (TRUE)
        {
        cp1 = strchr(cp, '\\');
        if (cp1 == NULL)
            break;

        cp = cp1+1;
        }

    strcpy(filespec, cp);

}
#endif

