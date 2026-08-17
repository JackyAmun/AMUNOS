/* fs.c — FAT12 文件系统驱动 (增强版)
 * 新增: 子目录遍历、文件创建、簇分配 */

#include "common.h"

#define FAT_CACHE_ADDR    0x70000

// 全局变量
int fs_root_lba = 0;
int fs_root_entries = 0;
int cwd_cluster = 0;            // 当前目录起始簇 (0=根目录)

// 内部变量
int fs_data_lba = 0;
static int fs_fat_lba = 0;
static int fs_sectors_per_fat = 0;
static int fs_max_data_cluster = 0;   // 最后一个有效数据簇 (由 BPB 总扇区数算出)

/* ── 8.3 文件名转换 ── */
void to_fat12_name(char* src, char* dest) {
    for (int i = 0; i < 11; i++) dest[i] = ' ';
    int i = 0, j = 0;
    while (src[i] && src[i] != '.' && j < 8) {
        dest[j++] = (src[i] >= 'a' && src[i] <= 'z') ? src[i] - 32 : src[i];
        i++;
    }
    if (src[i] == '.') {
        i++; j = 8;
        while (src[i] && j < 11) {
            dest[j++] = (src[i] >= 'a' && src[i] <= 'z') ? src[i] - 32 : src[i];
            i++;
        }
    }
}

/* ── 初始化: 读取 BPB ── */
void fs_init() {
    unsigned char bpb[512];
    int ret = read_sector_asm(0, bpb, current_drive_idx);
    if (ret != 0) {
        put_str("Error: Disk read failed\n");
        return;
    }
    if (bpb[510] != 0x55 || bpb[511] != 0xAA) {
        put_str("Error: Invalid Disk Format\n");
        return;
    }
    int reserved_sectors = *(unsigned short*)(bpb + 14);
    int fat_count = bpb[16];
    int sectors_per_fat = *(unsigned short*)(bpb + 22);
    int total_sectors = *(unsigned short*)(bpb + 19);
    fs_sectors_per_fat = sectors_per_fat;
    fs_root_entries = *(unsigned short*)(bpb + 17);

    fs_fat_lba = reserved_sectors;
    fs_root_lba = fs_fat_lba + (fat_count * sectors_per_fat);
    fs_data_lba = fs_root_lba + ((fs_root_entries * 32 + 511) / 512);
    if (total_sectors == 0) total_sectors = *(unsigned int*)(bpb + 32);  /* FAT32 大容量 */
    fs_max_data_cluster = total_sectors - fs_data_lba + 1;  /* 簇 N → 扇区 fs_data_lba+(N-2) */
    cwd_cluster = 0;  // 切盘后回到根目录
}

/* ── 盘符限定路径 (v6.5.1): "A:\..." / "B:..." / "./..." 统一入口 ──
 * drive_ctx_t 类型见 common.h §5.1 (勿在此重复 typedef, 匿名结构会冲突) */

/* 解析路径开头的盘符 "[A-D]:" (大小写均可): 命中则前移 *pp 并返回 0-3, 否则 -1 */
int parse_drive(char **pp) {
    char *p = *pp;
    if ((p[0] >= 'A' && p[0] <= 'D' && p[1] == ':') ||
        (p[0] >= 'a' && p[0] <= 'd' && p[1] == ':')) {
        *pp = p + 2;
        return (p[0] & ~0x20) - 'A';        /* 0..3 */
    }
    return -1;
}

/* 临时切到目标盘: 记住原盘与 cwd; fs_init() 重载该盘 BPB 几何
 * (A: reserved=105, B:/C: =1, 不同!), 并把 cwd_cluster 清 0 → 天然得到 X:path==X:\path */
drive_ctx_t fs_drive_enter(int drive) {
    drive_ctx_t c = { current_drive_idx, cwd_cluster };
    current_drive_idx = drive;
    fs_init();
    return c;
}

/* 还原: 先 fs_init() 重载原盘几何, 再设回 cwd (顺序不能反!) */
void fs_drive_restore(drive_ctx_t c) {
    current_drive_idx = c.drive;
    fs_init();
    cwd_cluster = c.cwd;
}

/* 路径带盘符则切盘并原地剥掉前缀 (内核无 memmove, 手工左移), 返回盘号; 否则 -1 */
int fs_drive_open(char *path, drive_ctx_t *ctx) {
    char *p = path;
    int d = parse_drive(&p);
    if (d < 0) return -1;
    *ctx = fs_drive_enter(d);
    { char *dst = path; while (*p) *dst++ = *p++; *dst = 0; }
    return d;
}

/* 系统保护文件 CMDS.BIN (比较 to_fat12_name 的 11 字节填充形 "CMDS    BIN") */
int is_cmds_file(char *fat11) {
    static const char p[] = "CMDS    BIN";
    for (int i = 0; i < 11; i++) if (fat11[i] != p[i]) return 0;
    return 1;
}

/* 磁盘是否存在 (读扇区 0 验 0x55AA), 供 cmd_custom 跳过失盘, 避免 fs_init 报错刷屏 */
int fs_drive_present(int d) {
    unsigned char b[512];
    int sv = current_drive_idx, sc = cwd_cluster;
    current_drive_idx = d;
    int ret = read_sector_asm(0, b, d);
    current_drive_idx = sv;
    cwd_cluster = sc;
    return (ret == 0 && b[510] == 0x55 && b[511] == 0xAA);
}

/* ── FAT 表操作 ── */
/* FAT12 条目只有 12 位, 两个条目挤占 3 字节; 一个条目可能横跨两个
 * FAT 扇区 (奇数簇落在扇区末字节时), 所以读/写都要处理边界。 */
unsigned short fat12_get_next_cluster(unsigned short cluster) {
    if (cluster < 2 || cluster >= 0xFF8) return 0xFFF;
    unsigned char fat_buf[512];
    unsigned int fat_offset = cluster + (cluster / 2);
    unsigned int fat_sector = fs_fat_lba + (fat_offset / 512);
    unsigned int ent_offset = fat_offset % 512;

    read_sector_asm(fat_sector, fat_buf, current_drive_idx);
    unsigned char lo = fat_buf[ent_offset];
    unsigned char hi;
    if (ent_offset == 511) {
        /* 条目低字节在本扇区末, 高字节在下一扇区首字节 */
        read_sector_asm(fat_sector + 1, fat_buf, current_drive_idx);
        hi = fat_buf[0];
    } else {
        hi = fat_buf[ent_offset + 1];
    }
    unsigned short val = lo | ((unsigned short)hi << 8);
    return (cluster & 1) ? (val >> 4) : (val & 0x0FFF);
}

/* 写入一个 FAT 条目 */
static void fat12_set_cluster(unsigned short cluster, unsigned short value) {
    unsigned char fat_buf[512];
    unsigned int fat_offset = cluster + (cluster / 2);
    unsigned int fat_sector = fs_fat_lba + (fat_offset / 512);
    unsigned int ent_offset = fat_offset % 512;

    read_sector_asm(fat_sector, fat_buf, current_drive_idx);
    unsigned short cur = fat_buf[ent_offset];
    if (ent_offset == 511) {
        unsigned char nb[512];
        read_sector_asm(fat_sector + 1, nb, current_drive_idx);
        cur |= (unsigned short)nb[0] << 8;
    } else {
        cur |= (unsigned short)fat_buf[ent_offset + 1] << 8;
    }
    if (cluster & 1)
        cur = (cur & 0x000F) | (value << 4);
    else
        cur = (cur & 0xF000) | (value & 0x0FFF);

    fat_buf[ent_offset] = cur & 0xFF;
    if (ent_offset == 511) {
        unsigned char nb[512];
        read_sector_asm(fat_sector + 1, nb, current_drive_idx);
        nb[0] = (cur >> 8) & 0xFF;
        write_sector_asm(fat_sector, fat_buf, current_drive_idx);
        write_sector_asm(fs_fat_lba + fs_sectors_per_fat + (fat_sector - fs_fat_lba),
                         fat_buf, current_drive_idx);
        write_sector_asm(fat_sector + 1, nb, current_drive_idx);
        write_sector_asm(fs_fat_lba + fs_sectors_per_fat + (fat_sector + 1 - fs_fat_lba),
                         nb, current_drive_idx);
    } else {
        fat_buf[ent_offset + 1] = (cur >> 8) & 0xFF;
        write_sector_asm(fat_sector, fat_buf, current_drive_idx);
        write_sector_asm(fs_fat_lba + fs_sectors_per_fat + (fat_sector - fs_fat_lba),
                         fat_buf, current_drive_idx);
    }
}

/* 分配一个空闲簇
 *
 * 直接按簇号线性扫描, 用 fat12_get_next_cluster 读取每个条目的值。
 * (旧实现按 "每 3 字节一组" 直接解 12 位条目, 有两个致命错误:
 *  1) 奇数簇算成 (lo>>4)|(hi_byte<<4) 而不是 (hi_byte>>4)|(next_byte<<4), 读错值;
 *  2) 512 字节扇区边界与 3 字节组不对齐, 扇区起始字节常落在组中间, 解出垃圾值。
 *  两者都会把"已用簇"误判为"空闲簇"返回, 导致写输出文件时覆盖其它文件的簇。)
 */
static unsigned short fat12_alloc_cluster() {
    int limit = fs_max_data_cluster;
    if (limit <= 0 || limit > 0xFEF) limit = 0xFEF;  /* 未读到 BPB 时兜底 */
    for (int c = 2; c <= limit; c++) {
        if (fat12_get_next_cluster((unsigned short)c) == 0) {
            fat12_set_cluster((unsigned short)c, 0xFFF);
            return (unsigned short)c;
        }
    }
    return 0;  // 盘满
}

/* ── 目录遍历辅助 ── */
#define MAX_DIR_SECTORS 256  // 目录最大扇区数 (安全上限)

/* 目录占用扇区数 (根目录固定, 子目录沿链到 EOF) */
int fs_dir_secs(int dc) {
    if (dc == 0) return (fs_root_entries * 32 + 511) / 512;
    int n = 1;
    unsigned short c = dc;
    while (n < MAX_DIR_SECTORS) {
        unsigned short nx = fat12_get_next_cluster(c);
        if (nx >= 0xFF8) break;
        c = nx; n++;
    }
    return n;
}

/* 目录第 idx 个扇区的 LBA (越界返回 -1) */
int fs_dir_lba(int dc, int idx) {
    if (dc == 0) {
        int max = (fs_root_entries * 32 + 511) / 512;
        return (idx < max) ? fs_root_lba + idx : -1;
    }
    unsigned short c = dc;
    for (int i = 0; i < idx; i++) {
        c = fat12_get_next_cluster(c);
        if (c >= 0xFF8) return -1;
    }
    return fs_data_lba + (c - 2);
}

/* 读取指定目录的扇区 */
static int read_dir_sector(int dir_cluster, int sector_idx, void* buf) {
    int lba = fs_dir_lba(dir_cluster, sector_idx);
    if (lba < 0) return -1;
    return read_sector_asm(lba, buf, current_drive_idx);
}

/* ── 在指定目录中查找条目 ── */
int fs_find_entry(char* name, FAT12Entry* out_entry) {
    return fs_find_entry_in_dir(cwd_cluster, name, out_entry);
}

int fs_find_entry_in_dir(int dir_cluster, char* name, FAT12Entry* out_entry) {
    char fat_name[11];
    to_fat12_name(name, fat_name);
    FAT12Entry buf[16];
    int max_sectors = fs_dir_secs(dir_cluster);

    for (int s = 0; s < max_sectors; s++) {
        int ret = read_dir_sector(dir_cluster, s, buf);
        if (ret != 0) break;
        for (int i = 0; i < 16; i++) {
            if (buf[i].name[0] == 0) return -1;
            if ((unsigned char)buf[i].name[0] == 0xE5) continue;
            int match = 1;
            for (int k = 0; k < 11; k++) {
                if (buf[i].name[k] != fat_name[k]) { match = 0; break; }
            }
            if (match) {
                if (out_entry) *out_entry = buf[i];
                return (s * 16) + i;
            }
        }
    }
    return -1;
}

/* ── 路径解析 (v6.5): 支持 "USR\SRC\HELLO.C" / "\ROOT\FILE" / "FILE" ──
 * 入参 path 原地改写为纯文件名, 返回所在目录簇; 目录不存在返回 -1。
 * 支持 .. (上一级) 与 / 或 \ 分隔符。 */
int fs_resolve_path(char* path) {
    int dir = cwd_cluster;
    char *p = path;
    int from_root = 0;
    if (p[0] == '\\' || p[0] == '/') { dir = 0; p++; from_root = 1; }
    if (!*p) return -1;

    char *lastsep = 0;
    for (char *q = p; *q; q++)
        if (*q == '\\' || *q == '/') lastsep = q;
    if (!lastsep) {
        if (from_root) {               /* "\NAME.EXT": 根目录, 剥掉前导反斜杠 */
            char *dst = path;
            while (*p) *dst++ = *p++;
            *dst = 0;
        }
        return dir;                    /* 纯文件名, 就在 cwd */
    }

    *lastsep = 0;                      /* 拆开: 目录部分 | 文件名 */
    char *seg = p;
    while (*seg) {
        char *sep = seg;
        while (*sep && *sep != '\\' && *sep != '/') sep++;
        char save = *sep; if (*sep) *sep = 0;
        if (*seg) {
            if (seg[0] == '.' && seg[1] == '.' && !seg[2]) {   /* .. 上一级 */
                if (dir != 0) {
                    unsigned char d[512];
                    read_sector_asm(fs_data_lba + (dir - 2), d, current_drive_idx);
                    dir = ((FAT12Entry*)d)[1].start_cluster;
                }
            } else if (seg[0] == '.' && !seg[1]) {
                /* 当前目录, 跳过 */
            } else {
                FAT12Entry e;
                if (fs_find_entry_in_dir(dir, seg, &e) < 0 || !(e.attr & 0x10))
                    return -1;         /* 目录不存在或不是目录 */
                dir = e.start_cluster;
            }
        }
        if (save) { *sep = save; seg = sep + 1; } else break;
    }
    /* 文件名搬到 path 开头 */
    char *fname = lastsep + 1;
    char *dst = path;
    while (*fname) *dst++ = *fname++;
    *dst = 0;
    return dir;
}

/* ── 读取文件内容 ── */
void fs_read_file(FAT12Entry* entry, char* buffer) {
    unsigned short cluster = entry->start_cluster;
    int bytes_read = 0;
    unsigned char sec[512];
    while (cluster < 0xFF8 && cluster >= 2 && bytes_read < entry->size) {
        unsigned int lba = fs_data_lba + (cluster - 2);
        read_sector_asm(lba, sec, current_drive_idx);
        int remain = entry->size - bytes_read;
        int n = (remain > 512) ? 512 : remain;
        for (int i = 0; i < n; i++) buffer[bytes_read + i] = sec[i];
        bytes_read += n;
        cluster = fat12_get_next_cluster(cluster);
    }
    buffer[entry->size] = 0;
}

/* ── 在指定目录中找空闲条目 ── */
/* 返回: 条目所在扇区的 LBA (存入 *out_lba) 和条目偏移 (返回值) */
static int find_free_entry(int dir_cluster, int* out_lba, int max_sectors) {
    FAT12Entry buf[16];
    int n = fs_dir_secs(dir_cluster);
    if (max_sectors > 0 && max_sectors < n) n = max_sectors;

    for (int s = 0; s < n; s++) {
        int lba = fs_dir_lba(dir_cluster, s);
        if (lba < 0) break;
        read_sector_asm(lba, buf, current_drive_idx);
        for (int i = 0; i < 16; i++) {
            if (buf[i].name[0] == 0 || (unsigned char)buf[i].name[0] == 0xE5) {
                *out_lba = lba;
                return i;  // 0x00 和 0xE5 都是空闲槽
            }
        }
    }
    return -1;
}

/* 检查文件名是否已存在 */
static int name_exists(int dir_cluster, char* fat_name) {
    FAT12Entry buf[16];
    int n = fs_dir_secs(dir_cluster);

    for (int s = 0; s < n; s++) {
        int lba = fs_dir_lba(dir_cluster, s);
        if (lba < 0) break;
        read_sector_asm(lba, buf, current_drive_idx);
        for (int i = 0; i < 16; i++) {
            if (buf[i].name[0] == 0) return 0;  // 未到结尾
            if ((unsigned char)buf[i].name[0] == 0xE5) continue;
            int match = 1;
            for (int k = 0; k < 11; k++)
                if (buf[i].name[k] != fat_name[k]) { match = 0; break; }
            if (match) return 1;  // 已存在
        }
    }
    return 0;
}

/* ── 创建文件 (在指定目录中, 支持多簇) ── */
#define MAX_FILE_CLUSTERS 256  // 最大 256 簇 = 128KB
static int fs_create_file_in_dir_inner(int dir_cluster, char* name, char* data, int size) {
    char fat_name[11];
    to_fat12_name(name, fat_name);
    // 检查文件名是否已存在
    if (name_exists(dir_cluster, fat_name)) {
        put_str("File already exists.\n");
        return -1;
    }

    int lba, entry_idx;
    int max_sectors = fs_dir_secs(dir_cluster);
    int found = find_free_entry(dir_cluster, &lba, max_sectors);
    if (found < 0) { put_str("Directory full!\n"); return -1; }
    entry_idx = found;

    // 需要的簇数
    int need = (size + 511) / 512;
    if (need == 0) need = 1;
    if (need > MAX_FILE_CLUSTERS) need = MAX_FILE_CLUSTERS;

    // 分配 need 个簇
    unsigned short clusters[MAX_FILE_CLUSTERS];
    for (int i = 0; i < need; i++) {
        clusters[i] = fat12_alloc_cluster();
        if (clusters[i] == 0) { put_str("Disk full!\n"); return -1; }
    }

    // 链接 FAT 链: 簇[i] → 簇[i+1], 最后一个 → 0xFFF (EOF)
    for (int i = 0; i < need; i++) {
        unsigned short nx = (i < need - 1) ? clusters[i+1] : 0xFFF;
        fat12_set_cluster(clusters[i], nx);
    }

    // 写入文件数据 (跨簇)
    for (int i = 0; i < need; i++) {
        int off = i * 512;
        int remain = size - off;
        if (remain > 512) remain = 512;
        if (remain > 0)
            write_sector_asm(fs_data_lba + (clusters[i] - 2), data + off, current_drive_idx);
        else
            { char zero[512]; for(int k=0;k<512;k++)zero[k]=0;
              write_sector_asm(fs_data_lba + (clusters[i] - 2), zero, current_drive_idx); }
    }

    // 填写目录条目
    FAT12Entry dir_buf[16];
    read_sector_asm(lba, dir_buf, current_drive_idx);
    to_fat12_name(name, dir_buf[entry_idx].name);
    dir_buf[entry_idx].attr = 0x20;
    dir_buf[entry_idx].start_cluster = clusters[0];
    dir_buf[entry_idx].size = size;
    for (int k = 0; k < 10; k++) dir_buf[entry_idx].reserved[k] = 0;
    dir_buf[entry_idx].time = 0;
    dir_buf[entry_idx].date = 0;
    write_sector_asm(lba, dir_buf, current_drive_idx);
    return 0;
}

/* 公开入口: 保护 CMDS.BIN 不被 DEL/REN/COPY 覆盖; EDIT/INSTALL 走 inner (见 fs_write_file_in_dir) */
int fs_create_file_in_dir(int dir_cluster, char* name, char* data, int size) {
    char fn[11]; to_fat12_name(name, fn);
    if (is_cmds_file(fn)) { put_str("CMDS.BIN is protected.\n"); return -1; }
    return fs_create_file_in_dir_inner(dir_cluster, name, data, size);
}

/* ── 创建目录 ── */
void fs_create_directory(char* dirname) {
    char fat_name[11];
    to_fat12_name(dirname, fat_name);
    if (name_exists(cwd_cluster, fat_name)) { put_str("Already exists.\n"); return; }

    // 在 cwd_cluster 中找空闲条目
    int lba, entry_idx;
    int max_sec = (cwd_cluster == 0) ? ((fs_root_entries * 32 + 511) / 512) : 8;
    int found = find_free_entry(cwd_cluster, &lba, max_sec);
    if (found < 0) { put_str("Directory full!\n"); return; }
    entry_idx = found;

    // 分配簇
    unsigned short clus = fat12_alloc_cluster();
    if (clus == 0) { put_str("Disk full!\n"); return; }

    // 清空该簇
    char zero[512];
    for (int k = 0; k < 512; k++) zero[k] = 0;
    write_sector_asm(fs_data_lba + (clus - 2), zero, current_drive_idx);

    // 填写父目录条目
    FAT12Entry dir_buf[16];
    read_sector_asm(lba, dir_buf, current_drive_idx);
    to_fat12_name(dirname, dir_buf[entry_idx].name);
    dir_buf[entry_idx].attr = 0x10;
    dir_buf[entry_idx].start_cluster = clus;
    dir_buf[entry_idx].size = 0;
    for (int k = 0; k < 10; k++) dir_buf[entry_idx].reserved[k] = 0;
    dir_buf[entry_idx].time = 0;
    dir_buf[entry_idx].date = 0;
    write_sector_asm(lba, dir_buf, current_drive_idx);

    // 在子目录中创建 . 和 .. 条目
    FAT12Entry sub[16];
    for (int k = 0; k < 16; k++) {
        for (int j = 0; j < 32; j++) ((char*)&sub[k])[j] = 0;
    }
    // . 条目
    sub[0].name[0] = '.';
    for (int k = 1; k < 11; k++) sub[0].name[k] = ' ';
    sub[0].attr = 0x10;
    sub[0].start_cluster = clus;
    // .. 条目
    sub[1].name[0] = '.'; sub[1].name[1] = '.';
    for (int k = 2; k < 11; k++) sub[1].name[k] = ' ';
    sub[1].attr = 0x10;
    sub[1].start_cluster = cwd_cluster;  // 父目录簇 (0=根)

    write_sector_asm(fs_data_lba + (clus - 2), sub, current_drive_idx);
    put_str("Directory created.\n");
}

/* ── 覆盖写文件到指定目录 (已存在则先静默删除再创建) ──
 * 供 syscall 的 fd 层在 close 时落盘用 (v6.5 支持路径) */
int fs_write_file_in_dir(int dir_cluster, char* name, char* data, int size) {
    FAT12Entry e;
    int idx = fs_find_entry_in_dir(dir_cluster, name, &e);
    if (idx >= 0) {
        /* 标记条目 0xE5 */
        int sector_off = fs_dir_lba(dir_cluster, idx / 16);
        int entry_off  = idx % 16;
        FAT12Entry buf[16];
        read_sector_asm(sector_off, buf, current_drive_idx);
        buf[entry_off].name[0] = 0xE5;
        write_sector_asm(sector_off, buf, current_drive_idx);
        /* 释放簇链 */
        unsigned short clus = e.start_cluster;
        while (clus >= 2 && clus < 0xFF8) {
            unsigned short nx = fat12_get_next_cluster(clus);
            fat12_set_cluster(clus, 0);
            clus = nx;
        }
    }
    /* 走 inner: EDIT/INSTALL 需能改写 CMDS.BIN; 千万别改回公开入口! */
    return fs_create_file_in_dir_inner(dir_cluster, name, data, size);
}

/* ── 覆盖写文件 (cwd, 兼容旧调用) ── */
void fs_write_file(char* name, char* data, int size) {
    fs_write_file_in_dir(cwd_cluster, name, data, size);
}

/* ── 删除文件 (核心: 在指定目录中, 静默不打印, 供命令/编辑器复用) ──
 * 返回: 0=成功, -1=失败 (不存在 / CMDS.BIN 受保护) */
int fs_delete_file_in_dir(int dir_cluster, char* name) {
    char fn[11]; to_fat12_name(name, fn);
    if (is_cmds_file(fn)) { put_str("CMDS.BIN is protected.\n"); return -1; }
    FAT12Entry entry;
    int idx = fs_find_entry_in_dir(dir_cluster, name, &entry);
    if (idx == -1) return -1;

    int sector_off, entry_off;
    sector_off = fs_dir_lba(dir_cluster, idx / 16);
    entry_off  = idx % 16;

    FAT12Entry buf[16];
    read_sector_asm(sector_off, buf, current_drive_idx);
    buf[entry_off].name[0] = 0xE5;
    write_sector_asm(sector_off, buf, current_drive_idx);

    // 释放簇链
    unsigned short clus = entry.start_cluster;
    while (clus >= 2 && clus < 0xFF8) {
        unsigned short next = fat12_get_next_cluster(clus);
        fat12_set_cluster(clus, 0);
        clus = next;
    }
    return 0;
}

/* ── 删除文件 (路径感知: 支持 "SUB\FILE.EXT" / "A:\...") ── */
void fs_delete_file(char* path) {
    drive_ctx_t octx; int od = fs_drive_open(path, &octx);
    int dc = fs_resolve_path(path);
    if (dc < 0) { if (od >= 0) fs_drive_restore(octx); put_str("Not found.\n"); return; }
    if (fs_delete_file_in_dir(dc, path) == 0) put_str("Deleted.\n");
    if (od >= 0) fs_drive_restore(octx);
}

/* ── 删除目录 (仅空目录) ── */
void fs_delete_directory(char* dirname) {
    FAT12Entry entry;
    int idx = fs_find_entry_in_dir(cwd_cluster, dirname, &entry);
    if (idx < 0) { put_str("Not found.\n"); return; }
    if (!(entry.attr & 0x10)) { put_str("Not a directory.\n"); return; }

    // 检查是否为空 (只有 . 和 .. )
    FAT12Entry buf[16];
    int n = fs_dir_secs(entry.start_cluster);
    int non_empty = 0;
    for (int s = 0; s < n && !non_empty; s++) {
        int lba = fs_dir_lba(entry.start_cluster, s);
        if (lba < 0) break;
        read_sector_asm(lba, buf, current_drive_idx);
        for (int i = 2; i < 16; i++) {  // 跳过 . 和 ..
            if (buf[i].name[0] == 0) break;
            if ((unsigned char)buf[i].name[0] == 0xE5) continue;
            non_empty = 1; break;
        }
    }
    if (non_empty) { put_str("Directory not empty.\n"); return; }

    // 删除父目录条目
    int plba = fs_dir_lba(cwd_cluster, idx / 16);
    FAT12Entry pbuf[16];
    read_sector_asm(plba, pbuf, current_drive_idx);
    pbuf[idx % 16].name[0] = 0xE5;
    write_sector_asm(plba, pbuf, current_drive_idx);

    // 释放目录的簇
    unsigned short clus = entry.start_cluster;
    while (clus >= 2 && clus < 0xFF8) {
        unsigned short nx = fat12_get_next_cluster(clus);
        fat12_set_cluster(clus, 0);
        clus = nx;
    }
    put_str("Directory removed.\n");
}

/* ── 列出目录 (完整链) ── */
int fs_list_dir(int dir_cluster, FAT12Entry* out_buf, int max_entries) {
    FAT12Entry buf[16];
    int max_sectors = fs_dir_secs(dir_cluster);
    int count = 0;

    for (int s = 0; s < max_sectors && count < max_entries; s++) {
        int ret = read_dir_sector(dir_cluster, s, buf);
        if (ret != 0) break;
        for (int i = 0; i < 16 && count < max_entries; i++) {
            if (buf[i].name[0] == 0) return count;
            if ((unsigned char)buf[i].name[0] == 0xE5) continue;
            if (buf[i].attr == 0x0F) continue;  // LFN
            out_buf[count++] = buf[i];
        }
    }
    return count;
}

/* ── 同步 ── */
void fs_sync() {
    void* fat_cache = (void*)FAT_CACHE_ADDR;
    for (int i = 0; i < fs_sectors_per_fat; i++) {
        write_sector_asm(fs_fat_lba + i, fat_cache + (i * 512), current_drive_idx);
        write_sector_asm(fs_fat_lba + fs_sectors_per_fat + i,
                         fat_cache + (i * 512), current_drive_idx);
    }
    put_str("\nFile System Synced.\n");
}
