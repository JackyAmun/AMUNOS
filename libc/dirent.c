/* dirent.c — AMUNOS 用户态目录枚举 (v6.6, EDIT 文件对话框用)
 *
 * 内核 SYS_READDIR(17) 是无状态的: 每次调用传 path + idx, 返回该目录
 * 第 idx 个有效条目 (1=文件, 2=目录, 0=列完, -1=错)。这里把它包装成
 * POSIX 形状的 opendir/readdir/closedir。
 *
 * 注意: readdir 返回的 struct dirent 归 DIR* 所有, 下一次调用会被覆盖;
 * 与标准 POSIX 语义一致。d_ino 未实现, 填 0。 */

#include "dirent.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

DIR *opendir(const char *path)
{
    DIR *dp = (DIR *)malloc(sizeof(DIR));
    char tmp[DIRENT_NAME_MAX];
    if (dp == NULL)
        return NULL;
    memset(dp, 0, sizeof(DIR));
    if (path != NULL) {
        strncpy(dp->path, path, sizeof(dp->path) - 1);
        dp->path[sizeof(dp->path) - 1] = 0;
    }
    dp->idx = 0;
    /* 立即探测: 路径无效 (盘不存在 / 不是目录) 时返回 NULL, 语义同
     * POSIX opendir, 也让编辑器的盘存在性探测 (BuildDriveList) 可靠。 */
    if (sys_readdir(dp->path, 0, tmp) < 0) {
        free(dp);
        return NULL;
    }
    return dp;
}

struct dirent *readdir(DIR *dp)
{
    int r;
    if (dp == NULL)
        return NULL;
    r = sys_readdir(dp->path, (int)dp->idx, dp->de.d_name);
    if (r <= 0)              /* 0 = 列完, -1 = 错误 */
        return NULL;
    dp->idx++;
    dp->de.d_ino = 0;
    if (r == 2)
        dp->de.d_type = DT_DIR;
    else if (r == 1)
        dp->de.d_type = DT_REG;
    else
        dp->de.d_type = DT_UNKNOWN;
    return &dp->de;
}

int closedir(DIR *dp)
{
    if (dp != NULL)
        free(dp);
    return 0;
}
