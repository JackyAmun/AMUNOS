/* fs.c — FAT12 文件系统驱动 (增强版)
 * 新增: 子目录遍历、文件创建、簇分配 */

#include "common.h"

/* v6.5.6 P2: FAT32 支撑. FAT32 最小卷 (33.5MB) 的 FAT 也有 ~256KB, 固定区
 * (0x70000, 栈在 0x90000) 放不下 → 缓存改为 fs_init 时按盘 FAT 大小从内核
 * 堆动态分配 (mem_init 先于 dev_scan, 堆可用; 4MB 堆余量充足)。 */
#define FAT_CACHE_CAP    0x100000   /* 单张 FAT 缓存上限 1MB (≈2GB FAT32 卷) */
static unsigned char *fat_cache = 0;    /* FAT 镜像 (RAM 缓存, 堆分配) */
static int fat_cached = 0;          /* fat_cache 是否已装载当前盘 FAT */
static int fat_cache_bytes = 0;     /* 已装载的 FAT 字节数 (B1 边界保护用) */

// 全局变量
int fs_root_lba = 0;
int fs_root_entries = 0;
int cwd_cluster = 0;            // 当前目录起始簇 (0=根目录)

// 内部变量
int fs_data_lba = 0;
int fs_spc = 1;                 // 每簇扇区数 (BPB off 13; FAT12=1, FAT16 通常 8)
int fs_fat_bits = 12;           // FAT 位宽: 12/16/32 (v6.5.6 P2 起 FAT32)
int fs_iso = 0;                 // v6.5.6 P3: 当前盘是 ISO9660 只读卷
static int iso_cur_size512 = 0; /* ISO 当前目录 512B 扇数 (find 命中目录时更新) */
static unsigned int fs_root_cluster = 0;  /* FAT32 根目录首簇 (BPB+44) */
static int fs_fat_lba = 0;
static int fs_sectors_per_fat = 0;
static int fs_max_data_cluster = 0;   // 最后一个有效数据簇 (由 BPB 总扇区数算出)

/* ── 8.3 文件名转换 ──
 * v6.8.1: GB2312 中文字节 (0xA1-0xFE) 原样透传 (凑 8 字节名 = 最多 ~4 汉字);
 * 若首字节==0xE5 (FAT "已删条目" 标记) 则存 0x05, 显示/比较时再还原. */
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
    if ((unsigned char)dest[0] == 0xE5) dest[0] = 0x05;
}

static void load_fat_cache(void);   /* 定义见下; fs_init 须先声明 (fs.c 内部) */

/* ── 初始化: 读取 BPB ── */
/* 返回 0=成功, -1=失败 (dev_automount 依此判定挂载) */
int fs_init() {
    unsigned char bpb[512];
    /* v6.5.6 P3: 光驱 → ISO9660 检测 (PVD 扇16 验 CD001), 不走 FAT/BPB */
    fs_iso = 0;
    if (current_drive_idx == 5 && atapi_ready()) {
        if (iso_mount() == 0) {
            fs_iso = 1;
            iso_cur_size512 = iso_root_size();
            return 0;
        }
    }
    int ret = blk_read(0, bpb, current_drive_idx);
    if (ret != 0) {
        put_str("Error: Disk read failed\n");
        return -1;
    }
    if (bpb[510] != 0x55 || bpb[511] != 0xAA) {
        put_str("Error: Invalid Disk Format\n");
        return -1;
    }
    int reserved_sectors = *(unsigned short*)(bpb + 14);
    int fat_count = bpb[16];
    int sectors_per_fat = *(unsigned short*)(bpb + 22);
    int total_sectors = *(unsigned short*)(bpb + 19);
    fs_root_entries = *(unsigned short*)(bpb + 17);
    { int spc = bpb[13]; fs_spc = (spc >= 1 && spc <= 128) ? spc : 1; }

    /* v6.5.6 P2: FAT32 判定 — 16 位 sectors_per_fat==0 且 32 位字段合法 */
    unsigned int spf32 = *(unsigned int*)(bpb + 36);
    int is32 = (sectors_per_fat == 0 && spf32 > 0 && spf32 <= FAT_CACHE_CAP / 512);
    if (is32) {
        sectors_per_fat = (int)spf32;
        fs_fat_bits = 32;
        fs_root_cluster = *(unsigned int*)(bpb + 44);
        if (fs_root_cluster < 2) {
            put_str("Error: Invalid BPB geometry\n");
            fat_cached = 0;
            return -1;
        }
    }

    /* B10: BPB 几何合理性校验 — 非 FAT 盘/垃圾 BPB 不再照单全收
     * (防 0 扇区 FAT 死循环、缓存越界; FAT32 根目录为簇链, root_entries=0 合法) */
    if (reserved_sectors < 1 || fat_count < 1 || fat_count > 4 ||
        sectors_per_fat < 1 || sectors_per_fat > FAT_CACHE_CAP / 512 ||
        (!is32 && fs_root_entries < 1)) {
        put_str("Error: Invalid BPB geometry\n");
        fat_cached = 0;
        return -1;
    }
    fs_sectors_per_fat = sectors_per_fat;

    fs_fat_lba = reserved_sectors;
    fs_root_lba = fs_fat_lba + (fat_count * sectors_per_fat);
    fs_data_lba = fs_root_lba + ((fs_root_entries * 32 + 511) / 512);
    if (total_sectors == 0) total_sectors = *(unsigned int*)(bpb + 32);  /* >65535 扇区大容量 */
    {   /* v6.5.1: 按簇数判定 FAT 位宽 (FAT12≤4084 簇, FAT16≤65524; FAT32 上面已定) */
        unsigned int data_secs = (total_sectors > fs_data_lba) ? (total_sectors - fs_data_lba) : 0;
        unsigned int clusters = data_secs / fs_spc;
        if (!is32) {
            fs_fat_bits = (clusters <= 4084) ? 12 : 16;
            fs_root_cluster = 0;
        }
        fs_max_data_cluster = (int)clusters + 1;
    }
    cwd_cluster = 0;  // 切盘后回到根目录
    load_fat_cache();            // FAT 读入堆缓存, 快读少读盘
    return 0;
}

/* ── 盘符限定路径 (v6.5.1): "A:\..." / "B:..." / "./..." 统一入口 ──
 * drive_ctx_t 类型见 common.h §5.1 (勿在此重复 typedef, 匿名结构会冲突) */

/* 解析路径开头的盘符 "[A-D]:" (大小写均可): 命中则前移 *pp 并返回 0-3, 否则 -1 */
int parse_drive(char **pp) {
    char *p = *pp;
    if ((p[0] >= 'A' && p[0] <= 'F' && p[1] == ':') ||
        (p[0] >= 'a' && p[0] <= 'f' && p[1] == ':')) {
        *pp = p + 2;
        /* v6.5.6 P1: 盘符 → 槽号走挂载表 (软盘引导时 A:=软盘槽4, IDE 顺延) */
        return dev_slot_from_letter((p[0] & ~0x20) - 'A');
    }
    return -1;
}

/* 临时切到目标盘: 记住原盘与 cwd; fs_init() 重载该盘 BPB 几何
 * (A: rsvd=193 (1 引导+192 内核), B:/C: 数据盘几何不同), 并把 cwd_cluster 清 0 → 天然得到 X:path==X:\path */
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

/* 磁盘是否存在: 先查 dev_scan() 的 IDENTIFY 表 (空槽直接否, 不刷屏),
 * 再读扇区 0 验 0x55AA (IDENTIFY 在但非 FAT 盘仍由这里兜底) */
int fs_drive_present(int d) {
    unsigned char b[512];
    if (d >= 0 && d < 6 && !devs[d].present) return 0;
    int sv = current_drive_idx, sc = cwd_cluster;
    current_drive_idx = d;
    int ret = blk_read(0, b, d);
    current_drive_idx = sv;
    cwd_cluster = sc;
    return (ret == 0 && b[510] == 0x55 && b[511] == 0xAA);
}

/* 把整张 FAT 读入堆缓存. 写透/读都基于缓存 (少读盘).
 * v6.5.6 P2: FAT32 FAT 可达几百 KB → 每次挂载按需分配 (先释放旧缓存). */
static void load_fat_cache(void) {
    int n = fs_sectors_per_fat;
    if (fat_cache) { mem_free(fat_cache); fat_cache = 0; }
    if (n <= 0) { fat_cached = 0; return; }
    if (n * 512 > FAT_CACHE_CAP) n = FAT_CACHE_CAP / 512;   /* 超限只缓前段兜底 */
    fat_cache = (unsigned char *)mem_alloc((unsigned)(n * 512));
    if (!fat_cache) { fat_cached = 0; fat_cache_bytes = 0; return; }
    for (int i = 0; i < n; i++)
        if (blk_read(fs_fat_lba + i, fat_cache + i * 512,
                            current_drive_idx) != 0) {
            fat_cached = 0;      /* B2: 读失败 → 缓存视为未装载, 走保护路径 */
            fat_cache_bytes = 0;
            return;
        }
    fat_cache_bytes = n * 512;
    fat_cached = 1;
}

/* ── FAT 表操作 (v6.5.1: FAT12/16 自动识别; v6.8.1 走 RAM 缓存写透) ──
 * FAT12 条目 12 位, 两个条目挤占 3 字节; 一个条目可能横跨两个
 * FAT 扇区 (奇数簇落在扇区末字节时), 所以读/写都要处理边界。
 * FAT16 条目 16 位 LE (offset=cluster*2), 奇数字节偏移 511 时同样跨扇区。
 * 读全部命中 fat_cache (缓存连续, 无跨扇区分割问题); 写更新缓存并把
 * 涉及的 FAT 扇区写透到 副本 1 + 副本 2 (见 flush_fat_sector). */
static int fat_is_eoc(unsigned int c)
{
    if (fs_fat_bits == 12) return c >= 0xFF8u;
    if (fs_fat_bits == 16) return c >= 0xFFF8u;
    return c >= 0x0FFFFFF8u;                        /* FAT32 */
}
static unsigned int fat_eoc_marker(void)
{
    return (fs_fat_bits == 12) ? 0xFFFu :
           (fs_fat_bits == 16) ? 0xFFFFu : 0x0FFFFFFFu;
}

/* v6.5.6 P2: 目录起始簇统一入口 — FAT32 根目录无固定扇区, dc==0 翻译为首簇 */
static unsigned int fs_root_first(int dc)
{
    return (dc == 0 && fs_fat_bits == 32) ? fs_root_cluster : (unsigned int)dc;
}

/* 目录项 32 位起始簇: FAT12/16 只有低 16 位 (reserved[8..9]=0);
 * FAT32 高 16 位在 on-disk 偏移 20-21 (即 FAT12Entry.reserved[8..9]) */
unsigned int fat_entry_cluster(FAT12Entry *e)
{
    return (unsigned int)e->start_cluster |
           ((unsigned int)e->reserved[8] << 16) |
           ((unsigned int)e->reserved[9] << 24);
}

/* FAT32 根目录有两种表示: dc==0 与 dc==root_cluster; 其余位宽只有 0 */
int fs_is_root_dir(int dc)
{
    return dc == 0 || (fs_fat_bits == 32 && (unsigned int)dc == fs_root_cluster);
}

/* 写透 FAT 扇区 s 到两张 FAT 副本 */
static void flush_fat_sector(unsigned int s) {
    blk_write(fs_fat_lba + s, fat_cache + s * 512, current_drive_idx);
    blk_write(fs_fat_lba + fs_sectors_per_fat + s, fat_cache + s * 512, current_drive_idx);
}

/* 读取簇 c 的 FAT 条目值 (FAT12/16/32), 基于缓存 */
static unsigned fat_get_entry(unsigned int c) {
    unsigned char lo, hi;
    /* B1: 条目超出已装载缓存 → 按 EOC 处理 (读链提前终止, 不越界取垃圾) */
    { unsigned int need = (fs_fat_bits == 16) ? ((c + 1) * 2u) :
                          (fs_fat_bits == 32) ? ((c + 1) * 4u) : (c + c / 2 + 2);
      if (!fat_cached || need > (unsigned)fat_cache_bytes) return fat_eoc_marker(); }
    if (fs_fat_bits == 16) {
        unsigned int off = (unsigned int)c * 2;
        lo = fat_cache[off]; hi = fat_cache[off + 1];
        return lo | ((unsigned)hi << 8);
    }
    if (fs_fat_bits == 32) {   /* v6.5.6 P2: 32 位 LE, 高 4 位保留清零 */
        unsigned int off = (unsigned int)c * 4;
        return (fat_cache[off] | ((unsigned)fat_cache[off+1] << 8) |
                ((unsigned)fat_cache[off+2] << 16) |
                ((unsigned)fat_cache[off+3] << 24)) & 0x0FFFFFFFu;
    }
    unsigned int off = c + (c / 2);
    unsigned short w = fat_cache[off] | ((unsigned short)fat_cache[off + 1] << 8);
    return (c & 1) ? (w >> 4) : (w & 0x0FFF);
}

/* 写入簇 c 的 FAT 条目值, 更新缓存 + 写透两张 FAT */
static void fat_set_entry(unsigned int c, unsigned int value) {
    /* B1: 越界拒绝写 (只缓前段的大盘不写超界条目) */
    { unsigned int need = (fs_fat_bits == 16) ? ((c + 1) * 2u) :
                          (fs_fat_bits == 32) ? ((c + 1) * 4u) : (c + c / 2 + 2);
      if (!fat_cached || need > (unsigned)fat_cache_bytes) return; }
    if (fs_fat_bits == 32) {   /* v6.5.6 P2: 高 4 位保留, 写前清零 */
        unsigned int off = (unsigned int)c * 4;
        value &= 0x0FFFFFFFu;
        fat_cache[off]   = value & 0xFF;
        fat_cache[off+1] = (value >> 8) & 0xFF;
        fat_cache[off+2] = (value >> 16) & 0xFF;
        fat_cache[off+3] = (value >> 24) & 0xFF;
        flush_fat_sector(off / 512);
        if (off % 512 > 508) flush_fat_sector(off / 512 + 1);   /* 条目跨扇区 */
        return;
    }
    if (fs_fat_bits == 16) {
        unsigned int off = (unsigned int)c * 2;
        fat_cache[off] = value & 0xFF;
        fat_cache[off + 1] = (value >> 8) & 0xFF;
        flush_fat_sector(off / 512);
        if (off % 512 == 511) flush_fat_sector(off / 512 + 1);   /* 跨扇区高字节 */
        return;
    }
    /* FAT12: 覆盖该条目所在的 2 字节 (可能跨扇区末字节), 随后刷涉及的扇区 */
    unsigned int off = c + (c / 2);
    unsigned short cur = fat_cache[off] | ((unsigned short)fat_cache[off + 1] << 8);
    if (c & 1)
        cur = (cur & 0x000F) | ((value & 0x0FFF) << 4);
    else
        cur = (cur & 0xF000) | (value & 0x0FFF);
    fat_cache[off] = cur & 0xFF;
    fat_cache[off + 1] = (cur >> 8) & 0xFF;
    flush_fat_sector(off / 512);
    if (off % 512 == 511) flush_fat_sector(off / 512 + 1);
}

unsigned int fat12_get_next_cluster(unsigned int cluster) {
    if (cluster < 2 || fat_is_eoc(cluster)) return fat_eoc_marker();
    return (unsigned short)fat_get_entry(cluster);
}

/* 写入一个 FAT 条目 (FAT12/16 自动; 更新缓存 + 写透两张 FAT) */
static void fat_set_cluster(unsigned int cluster, unsigned int value) {
    fat_set_entry(cluster, value);
}

/* 分配一个空闲簇
 *
 * 直接按簇号线性扫描, 用 fat12_get_next_cluster 读取每个条目的值。
 * (旧实现按 "每 3 字节一组" 直接解 12 位条目, 有两个致命错误:
 *  1) 奇数簇算成 (lo>>4)|(hi_byte<<4) 而不是 (hi_byte>>4)|(next_byte<<4), 读错值;
 *  2) 512 字节扇区边界与 3 字节组不对齐, 扇区起始字节常落在组中间, 解出垃圾值。
 *  两者都会把"已用簇"误判为"空闲簇"返回, 导致写输出文件时覆盖其它文件的簇。)
 */
static unsigned int fat12_alloc_cluster() {
    int limit = fs_max_data_cluster;
    if (limit <= 0) limit = (fs_fat_bits == 12) ? 0xFEF : 0xFFEF;  /* 未读到 BPB 时兜底 */
    for (int c = 2; c <= limit; c++) {
        if (fat12_get_next_cluster(c) == 0) {
            fat_set_cluster(c, fat_eoc_marker());
            return c;
        }
    }
    return 0;  // 盘满
}

/* ── 目录遍历辅助 ── */
#define MAX_DIR_SECTORS 256  // 目录最大扇区数 (安全上限)

/* 簇 N 对应的数据区首扇区 LBA (v6.5.1: 乘每簇扇区数, FAT16 数据盘用) */
unsigned int fs_cluster_lba(unsigned int c) {
    return fs_data_lba + (c - 2) * fs_spc;
}

/* 目录占用扇区数 (根目录固定, 子目录沿链到 EOF; 每簇 fs_spc 扇) */
int fs_dir_secs(int dc) {
    if (fs_iso)
        return (dc == 0) ? iso_root_size() : iso_cur_size512;
    if (fs_fat_bits == 32) {                  /* v6.5.6 P2: FAT32 根=簇链 (dc==0 亦然) */
        unsigned int c = fs_root_first(dc);
        int n = 0;
        while (n < MAX_DIR_SECTORS * 64) {
            if (fat_is_eoc(c)) return n;
            n += fs_spc;
            c = fat12_get_next_cluster(c);
        }
        return n;
    }
    if (dc == 0) return (fs_root_entries * 32 + 511) / 512;
    int n = 0;
    unsigned int c = fs_root_first(dc);
    while (n < MAX_DIR_SECTORS) {
        if (fat_is_eoc(c)) break;
        n += fs_spc;
        c = fat12_get_next_cluster(c);
    }
    return n;
}

/* 目录第 idx 个扇区的 LBA (越界返回 -1) */
int fs_dir_lba(int dc, int idx) {
    if (fs_iso)
        return ((dc == 0) ? iso_root_lba() : dc) + idx;   /* ISO 目录连续 */
    if (fs_fat_bits == 32 && (dc == 0 || (unsigned int)dc == fs_root_cluster)) {
        /* v6.5.6 P2: FAT32 根目录 (含以 root_cluster 表示的 0) 沿链找第 idx 扇 */
        unsigned int c = fs_root_cluster;
        for (int i = 0; i < idx / fs_spc; i++) {
            c = fat12_get_next_cluster(c);
            if (fat_is_eoc(c)) return -1;
        }
        return fs_cluster_lba(c) + (idx % fs_spc);
    }
    if (dc == 0) {
        int max = (fs_root_entries * 32 + 511) / 512;
        return (idx < max) ? fs_root_lba + idx : -1;
    }
    unsigned int c = fs_root_first(dc);
    for (int i = 0; i < idx / fs_spc; i++) {
        c = fat12_get_next_cluster(c);
        if (fat_is_eoc(c)) return -1;
    }
    return fs_cluster_lba(c) + (idx % fs_spc);
}

/* 读取指定目录的扇区 */
static int read_dir_sector(int dir_cluster, int sector_idx, void* buf) {
    int lba = fs_dir_lba(dir_cluster, sector_idx);
    if (lba < 0) return -1;
    return blk_read(lba, buf, current_drive_idx);
}

/* ── 在指定目录中查找条目 ── */
int fs_find_entry(char* name, FAT12Entry* out_entry) {
    return fs_find_entry_in_dir(cwd_cluster, name, out_entry);
}

int fs_find_entry_in_dir(int dir_cluster, char* name, FAT12Entry* out_entry) {
    if (fs_iso) {
        int d = (dir_cluster == 0) ? iso_root_lba() : dir_cluster;
        int sz = (dir_cluster == 0) ? iso_root_size() : iso_cur_size512;
        int r = iso_find(d, sz, name, out_entry);
        if (r >= 0 && out_entry && (out_entry->attr & 0x10))
            iso_cur_size512 = (int)((out_entry->size + 511) / 512);
        return r;
    }
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

/* ── 路径解析 (v6.5.1): 支持 "USR/SRC/HELLO.C" / "/ROOT/FILE" / "FILE" ──
 * 入参 path 原地改写为纯文件名, 返回所在目录簇; 目录不存在返回 -1。
 * 支持 .. (上一级); 分隔符严格只认 / (v6.5.1 决策)。 */
int fs_resolve_path(char* path) {
    int dir = cwd_cluster;
    char *p = path;
    int from_root = 0;
    if (p[0] == '/') { dir = 0; p++; from_root = 1; }
    if (!*p) return -1;

    char *lastsep = 0;
    for (char *q = p; *q; q++)
        if (*q == '/') lastsep = q;
    if (!lastsep) {
        if (from_root) {               /* "/NAME.EXT": 根目录, 剥掉前导斜杠 */
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
        while (*sep && *sep != '/') sep++;
        char save = *sep; if (*sep) *sep = 0;
        if (*seg) {
            if (seg[0] == '.' && seg[1] == '.' && !seg[2]) {   /* .. 上一级 */
                if (!fs_is_root_dir(dir)) {
                    unsigned char d[512];
                    /* B6: 读失败不得把未初始化栈缓冲当目录解析 */
                    if (blk_read(fs_cluster_lba(dir), d, current_drive_idx) != 0)
                        return -1;
                    dir = (int)fat_entry_cluster(&((FAT12Entry*)d)[1]);
                }
            } else if (seg[0] == '.' && !seg[1]) {
                /* 当前目录, 跳过 */
            } else {
                FAT12Entry e;
                if (fs_find_entry_in_dir(dir, seg, &e) < 0 || !(e.attr & 0x10))
                    return -1;         /* 目录不存在或不是目录 */
                dir = (int)fat_entry_cluster(&e);
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

/* ── 读取文件内容 (v6.5.6: 有界读 + 读失败检测)
 * v6.5.1: 每簇 fs_spc 扇都读, FAT16 数据盘用。
 * v6.5.6 修 B4: 无 bufsize 参数时 buffer[entry->size]=0 可越界写 (字库等
 * 二进制按 cap=size 读时曾越界 1 字节); 修 B2: 读盘失败曾把栈垃圾当数据。
 * 语义: 读 min(size, bufsize) 字节; 仅当 size < bufsize 时在尾部写 NUL
 * (字符串调用方传 size+1, 二进制调用方传 size)。返回字节数, 读失败 -1。 */
int fs_read_file(FAT12Entry* entry, char* buffer, int bufsize) {
    if (fs_iso) {   /* ISO extents 连续: 起始 LBA512 即簇字段 */
        unsigned int lba = fat_entry_cluster(entry);
        unsigned char sec[512];
        int total = entry->size;
        if (total > bufsize) total = bufsize;
        for (int off = 0; off < total; off += 512) {
            int n = (total - off > 512) ? 512 : total - off;
            if (blk_read(lba + off / 512, sec, current_drive_idx) != 0)
                return -1;
            for (int i = 0; i < n; i++) buffer[off + i] = sec[i];
        }
        if (entry->size < bufsize) buffer[entry->size] = 0;
        return total;
    }
    unsigned int cluster = fat_entry_cluster(entry);
    int bytes_read = 0;
    int ok = 1;
    unsigned char sec[512];
    if (bufsize < 0) bufsize = 0;
    while (cluster >= 2 && !fat_is_eoc(cluster) && bytes_read < entry->size
           && bytes_read < bufsize) {
        unsigned int lba = fs_cluster_lba(cluster);
        for (int s = 0; s < fs_spc && bytes_read < entry->size
             && bytes_read < bufsize; s++) {
            if (blk_read(lba + s, sec, current_drive_idx) != 0) { ok = 0; break; }
            int remain = entry->size - bytes_read;
            if (remain > bufsize - bytes_read) remain = bufsize - bytes_read;
            int n = (remain > 512) ? 512 : remain;
            for (int i = 0; i < n; i++) buffer[bytes_read + i] = sec[i];
            bytes_read += n;
        }
        if (!ok) break;
        cluster = fat12_get_next_cluster(cluster);
    }
    if (entry->size < bufsize) buffer[entry->size] = 0;
    return ok ? bytes_read : -1;
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
        if (blk_read(lba, buf, current_drive_idx) != 0) break;  /* B2 */
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
        if (blk_read(lba, buf, current_drive_idx) != 0) break;  /* B2 */
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
    if (fs_iso) { put_str("Read-only medium.\n"); return -1; }
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

    // 需要的簇数 (v6.5.1: FAT16 每簇 fs_spc 扇, 按簇容量 512*fs_spc 字节算)
    int clus_bytes = 512 * fs_spc;
    int need = (size + clus_bytes - 1) / clus_bytes;
    if (need == 0) need = 1;
    if (need > MAX_FILE_CLUSTERS) need = MAX_FILE_CLUSTERS;

    // 分配 need 个簇
    unsigned int clusters[MAX_FILE_CLUSTERS];
    for (int i = 0; i < need; i++) {
        clusters[i] = fat12_alloc_cluster();
        if (clusters[i] == 0) { put_str("Disk full!\n"); return -1; }
    }

    // 链接 FAT 链: 簇[i] → 簇[i+1], 最后一个 → EOC (FAT16 为 0xFFFF)
    for (int i = 0; i < need; i++) {
        unsigned int nx = (i < need - 1) ? clusters[i+1] : fat_eoc_marker();
        fat_set_cluster(clusters[i], nx);
    }

    // 写入文件数据 (跨簇; 每簇写满 fs_spc 扇, 尾部/剩余扇区清零)
    for (int i = 0; i < need; i++) {
        int off = i * clus_bytes;
        int remain = size - off;
        if (remain > clus_bytes) remain = clus_bytes;
        unsigned int lba = fs_cluster_lba(clusters[i]);
        for (int s = 0; s < fs_spc; s++) {
            int n = remain - s * 512;
            if (n >= 512) {
                if (blk_write(lba + s, data + off + s * 512,
                                     current_drive_idx) != 0)
                    { put_str("Disk write error\n"); return -1; }
            } else if (n > 0) {
                /* 最后一段: 部分扇区, 剩余字节清零 */
                char tmp[512];
                for (int k = 0; k < 512; k++) tmp[k] = 0;
                for (int k = 0; k < n; k++) tmp[k] = data[off + s * 512 + k];
                if (blk_write(lba + s, tmp, current_drive_idx) != 0)
                    { put_str("Disk write error\n"); return -1; }
            } else {
                /* 簇内超出数据部分: 清零 */
                char zero[512]; for (int k = 0; k < 512; k++) zero[k] = 0;
                if (blk_write(lba + s, zero, current_drive_idx) != 0)
                    { put_str("Disk write error\n"); return -1; }
            }
        }
    }

    // 填写目录条目
    FAT12Entry dir_buf[16];
    if (blk_read(lba, dir_buf, current_drive_idx) != 0)
        { put_str("Disk read error\n"); return -1; }
    to_fat12_name(name, dir_buf[entry_idx].name);
    dir_buf[entry_idx].attr = 0x20;
    dir_buf[entry_idx].start_cluster = clusters[0] & 0xFFFF;            /* 簇号低 16 位 */
    dir_buf[entry_idx].reserved[8] = (unsigned char)((clusters[0] >> 16) & 0xFF);  /* FAT32 簇号高 16 位 */
    dir_buf[entry_idx].reserved[9] = (unsigned char)((clusters[0] >> 24) & 0xFF);
    dir_buf[entry_idx].size = size;
    for (int k = 0; k < 10; k++) dir_buf[entry_idx].reserved[k] = 0;
    dir_buf[entry_idx].time = 0;
    dir_buf[entry_idx].date = 0;
    if (blk_write(lba, dir_buf, current_drive_idx) != 0)
        { put_str("Disk write error\n"); return -1; }
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
    if (fs_iso) { put_str("Read-only medium.\n"); return; }
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
    unsigned int clus = fat12_alloc_cluster();
    if (clus == 0) { put_str("Disk full!\n"); return; }

    // 清空该簇 (FAT16 每簇 fs_spc 扇, 全清防陈旧目录项)
    char zero[512];
    for (int k = 0; k < 512; k++) zero[k] = 0;
    for (int s = 0; s < fs_spc; s++)
        blk_write(fs_cluster_lba(clus) + s, zero, current_drive_idx);

    // 填写父目录条目
    FAT12Entry dir_buf[16];
    if (blk_read(lba, dir_buf, current_drive_idx) != 0) return; /* B2 */
    to_fat12_name(dirname, dir_buf[entry_idx].name);
    dir_buf[entry_idx].attr = 0x10;
    dir_buf[entry_idx].start_cluster = clus;
    dir_buf[entry_idx].size = 0;
    for (int k = 0; k < 10; k++) dir_buf[entry_idx].reserved[k] = 0;
    dir_buf[entry_idx].time = 0;
    dir_buf[entry_idx].date = 0;
    if (blk_write(lba, dir_buf, current_drive_idx) != 0) return; /* B2 */

    // 在子目录中创建 . 和 .. 条目
    FAT12Entry sub[16];
    for (int k = 0; k < 16; k++) {
        for (int j = 0; j < 32; j++) ((char*)&sub[k])[j] = 0;
    }
    // . 条目
    sub[0].name[0] = '.';
    for (int k = 1; k < 11; k++) sub[0].name[k] = ' ';
    sub[0].attr = 0x10;
    sub[0].start_cluster = clus & 0xFFFF;
    sub[0].reserved[8] = (unsigned char)((clus >> 16) & 0xFF);
    sub[0].reserved[9] = (unsigned char)((clus >> 24) & 0xFF);
    // .. 条目
    sub[1].name[0] = '.'; sub[1].name[1] = '.';
    for (int k = 2; k < 11; k++) sub[1].name[k] = ' ';
    sub[1].attr = 0x10;
    sub[1].start_cluster = fs_root_first(cwd_cluster) & 0xFFFF;  // 父目录簇 (FAT32 根=root_cluster)
    sub[1].reserved[8] = (unsigned char)((fs_root_first(cwd_cluster) >> 16) & 0xFF);
    sub[1].reserved[9] = (unsigned char)((fs_root_first(cwd_cluster) >> 24) & 0xFF);

    if (blk_write(fs_cluster_lba(clus), sub, current_drive_idx) != 0)
        return;  /* B2 */
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
        if (blk_read(sector_off, buf, current_drive_idx) != 0) return -1; /* B2 */
        buf[entry_off].name[0] = 0xE5;
        blk_write(sector_off, buf, current_drive_idx);
        /* 释放簇链 */
        unsigned int clus = fat_entry_cluster(&e);
        while (clus >= 2 && !fat_is_eoc(clus)) {
            unsigned int nx = fat12_get_next_cluster(clus);
            fat_set_cluster(clus, 0);
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
    if (fs_iso) { put_str("Read-only medium.\n"); return -1; }
    char fn[11]; to_fat12_name(name, fn);
    if (is_cmds_file(fn)) { put_str("CMDS.BIN is protected.\n"); return -1; }
    FAT12Entry entry;
    int idx = fs_find_entry_in_dir(dir_cluster, name, &entry);
    if (idx == -1) return -1;

    int sector_off, entry_off;
    sector_off = fs_dir_lba(dir_cluster, idx / 16);
    entry_off  = idx % 16;

    FAT12Entry buf[16];
    if (blk_read(sector_off, buf, current_drive_idx) != 0) return -1; /* B2 */
    buf[entry_off].name[0] = 0xE5;
    blk_write(sector_off, buf, current_drive_idx);

    // 释放簇链
    unsigned int clus = fat_entry_cluster(&entry);
    while (clus >= 2 && !fat_is_eoc(clus)) {
        unsigned int next = fat12_get_next_cluster(clus);
        fat_set_cluster(clus, 0);
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
        if (blk_read(lba, buf, current_drive_idx) != 0) break;  /* B2 */
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
    if (blk_read(plba, pbuf, current_drive_idx) != 0) return; /* B2 */
    pbuf[idx % 16].name[0] = 0xE5;
    blk_write(plba, pbuf, current_drive_idx);

    // 释放目录的簇
    unsigned int clus = fat_entry_cluster(&entry);
    while (clus >= 2 && !fat_is_eoc(clus)) {
        unsigned int nx = fat12_get_next_cluster(clus);
        fat_set_cluster(clus, 0);
        clus = nx;
    }
    put_str("Directory removed.\n");
}

/* ── 列出目录 (完整链) ── */
int fs_list_dir(int dir_cluster, FAT12Entry* out_buf, int max_entries) {
    if (fs_iso) {
        int d = (dir_cluster == 0) ? iso_root_lba() : dir_cluster;
        int sz = (dir_cluster == 0) ? iso_root_size() : iso_cur_size512;
        return iso_list(d, sz, out_buf, max_entries);
    }
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

/* ── 同步 (v6.8.1): 从 RAM 缓存刷两张 FAT.
 * 此前 fs_sync 从不被调用, 且 0x70000 从未被填充 —— 一旦调用会把陈旧内存
 * 写进 FAT2 镜像毁盘. 现在 FAT 常驻缓存 (load_fat_cache), 这里等价幂等刷新. */
void fs_sync() {
    for (int i = 0; i < fs_sectors_per_fat && i < FAT_CACHE_CAP / 512; i++) {
        blk_write(fs_fat_lba + i, fat_cache + (i * 512), current_drive_idx);
        blk_write(fs_fat_lba + fs_sectors_per_fat + i,
                         fat_cache + (i * 512), current_drive_idx);
    }
    put_str("\nFile System Synced.\n");
}
