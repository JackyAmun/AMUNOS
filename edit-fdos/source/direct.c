/* ---------- direct.c --------- */
/* AMUNOS 版: 文件对话框的目录/文件/盘列举。
 * 原 DOS 依赖 (findfirst/findnext/getdisk/setdisk/chdir/getcwd/
 * fnsplit/fnmerge/int86/union REGS) 全部换成 libc dirent —— 内核
 * SYS_READDIR(17) 无状态枚举。AMUNOS 无进程级 cwd, 对话框维护一个
 * 全局"浏览目录" BrowsePath (如 "A:/USR/SRC/"), 所有列举基于它。
 * BuildPathDisplay 直接显示 BrowsePath, CreatePath 只负责把用户输入
 * 的路径切到 BrowsePath (Change 模式)。 */

#include "dflat.h"
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

char BrowsePath[64] = "A:/";   /* 非 static: fileopen.c 也访问 */

/* ---- 大小写不敏感比较 (原 stricmp; AMUNOS libc 未提供) ---- */
static int stricmp(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        int cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb)
            return ca - cb;
        a++; b++;
    }
    return (*a ? (*a >= 'A' && *a <= 'Z' ? *a + 32 : *a)
               : (*b >= 'A' && *b <= 'Z' ? *b + 32 : *b));
}

/* ---- 简单 * 和 ? 通配匹配 (fspec 如 "*.*" / "*.C" 过滤) ---- */
static int matchpat(const char *pat, const char *str)
{
    while (*pat) {
        if (*pat == '*') {
            pat++;
            if (!*pat)
                return 1;
            while (*str) {
                if (matchpat(pat, str))
                    return 1;
                str++;
            }
            return 0;
        } else if (*pat == '?') {
            if (!*str)
                return 0;
            pat++; str++;
        } else {
            if (tolower(*pat) != tolower(*str))
                return 0;
            pat++; str++;
        }
    }
    return !*str;
}

static int dircmp(const void *c1, const void *c2)
{
    return stricmp(*(char **)c1, *(char **)c2);
}

/* ---- 把 BrowsePath 切到 fspec 的目录部分 (Change=TRUE 时) ----
 * fspec 可带盘符 "A:/..."、前导 "/"、相对 "USR/SRC/..."、纯文件名
 * 或模式。纯文件名/模式 (无 '/') 时 BrowsePath 不变。 */
void CreatePath(char *spath, char *fspec, int InclName, int Change)
{
    (void)spath; (void)InclName;
    if (!Change || fspec == NULL || !fspec[0])
        return;
    {
        char *p = fspec;
        char drive = 0;
        if (p[0] && p[1] == ':' &&
            ((p[0] >= 'A' && p[0] <= 'D') || (p[0] >= 'a' && p[0] <= 'd'))) {
            drive = p[0] & ~0x20;
            p += 2;
        }
        if (*p == '/')
            p++;
        {
            char nb[64];
            nb[0] = drive ? drive : BrowsePath[0];
            nb[1] = ':'; nb[2] = '/'; nb[3] = 0;
            char *slash = strrchr(p, '/');
            if (slash && slash != p) {       /* 有目录部分 */
                int dlen = (int)(slash - p);
                if (dlen > 0) {
                    memcpy(nb + 3, p, dlen);
                    nb[3 + dlen] = 0;
                }
            } else if (slash == p) {
                /* "/NAME": 根目录下的名字, 目录为空 */
            }
            if (nb[3] == 0) { nb[2] = '/'; nb[3] = 0; }        /* 只有盘符 */
            else if (nb[strlen(nb) - 1] != '/')
                strcat(nb, "/");
            strcpy(BrowsePath, nb);
        }
    }
}

/* ---- 填充 Files / Directories 列表 ----
 * fspec 用于文件过滤; dirs=TRUE 列出子目录 (忽略 fspec)。 */
static BOOL BuildList(WINDOW wnd, char *fspec, BOOL dirs)
{
    int i = 0;
    CTLWINDOW *ct = FindCommand(wnd->extension,
                                dirs ? ID_DIRECTORY : ID_FILES, LISTBOX);
    WINDOW lwnd;
    char **dirlist = NULL;
    DIR *dp;
    struct dirent *de;

    if (ct == NULL)
        return FALSE;
    lwnd = ct->wnd;
    SendMessage(lwnd, CLEARTEXT, 0, 0);

    /* sys_readdir 不接受路径末尾的 "/", 否则 fs_resolve_path 把
     * "A:/BIN/" 解析成 "BIN/" 找不到 → opendir 返回 NULL。
     * 复制一份, 去掉末尾的 "/", 但保留根 "X:/"。 */
    {
        char pathbuf[64];
        strncpy(pathbuf, BrowsePath, sizeof(pathbuf)-1);
        pathbuf[sizeof(pathbuf)-1] = 0;
        int plen = strlen(pathbuf);
        if (plen > 3 && pathbuf[plen-1] == '/')
            pathbuf[plen-1] = 0;
        dp = opendir(pathbuf);
    }
    if (dp == NULL)
        return FALSE;

    while ((de = readdir(dp)) != NULL) {
        BOOL isdir = (de->d_type == DT_DIR);
        if (isdir != dirs)
            continue;
        if (dirs) {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
                continue;
        } else {
            if (!matchpat(fspec, de->d_name))
                continue;
        }
        dirlist = DFrealloc(dirlist, sizeof(char *) * (i + 1));
        dirlist[i] = DFmalloc(strlen(de->d_name) + 1);
        strcpy(dirlist[i++], de->d_name);
    }
    closedir(dp);

    if (dirlist != NULL) {
        int j;
        qsort(dirlist, i, sizeof(void *), dircmp);
        for (j = 0; j < i; j++) {
            SendMessage(lwnd, ADDTEXT, (PARAM) dirlist[j], 0);
            free(dirlist[j]);
        }
        free(dirlist);
    }
    SendMessage(lwnd, SHOW_WINDOW, 0, 0);
    return TRUE;
}

BOOL BuildFileList(WINDOW wnd, char *fspec)
{
    return BuildList(wnd, fspec, FALSE);
}

void BuildDirectoryList(WINDOW wnd)
{
    BuildList(wnd, "*", TRUE);
}

/* ---- 列出存在的盘 (A/B/C): opendir 成功即盘存在 ---- */
void BuildDriveList(WINDOW wnd)
{
    CTLWINDOW *ct = FindCommand(wnd->extension, ID_DRIVE, LISTBOX);
    int dr;
    if (ct == NULL)
        return;
    {
        WINDOW lwnd = ct->wnd;
        SendMessage(lwnd, CLEARTEXT, 0, 0);
        for (dr = 0; dr < 3; dr++) {
            char dpath[8], drname[15];
            DIR *dp;
            dpath[0] = 'A' + dr; dpath[1] = ':'; dpath[2] = '/'; dpath[3] = 0;
            dp = opendir(dpath);
            if (dp != NULL) {
                closedir(dp);
                sprintf(drname, "[-%c-]", 'A' + dr);
                SendMessage(lwnd, ADDTEXT, (PARAM) drname, 0);
            }
        }
        SendMessage(lwnd, SHOW_WINDOW, 0, 0);
    }
}

/* ---- 在对话框中显示当前浏览目录 ---- */
void BuildPathDisplay(WINDOW wnd)
{
    CTLWINDOW *ct = FindCommand(wnd->extension, ID_PATH, TEXT);
    if (ct == NULL)
        return;
    {
        WINDOW lwnd = ct->wnd;
        char path[64];
        int len;
        strcpy(path, BrowsePath);
        len = strlen(path);
        if (len > 3 && path[len - 1] == '/')
            path[len - 1] = '\0';   /* 保留 "A:/" */
        SendMessage(lwnd, SETTEXT, (PARAM) path, 0);
        SendMessage(lwnd, PAINT, 0, 0);
    }
}
