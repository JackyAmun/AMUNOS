/* iso9660.c — ISO9660 只读文件系统 (v6.5.6 P3)
 *
 * PVD (卷描述符) 在 2048B 扇 16; 根目录记录在 PVD 偏移 156 (34 字节)。
 * 目录记录: [0]长度 [2-5]LBA(2048单位) [10-13]大小 [25]标志(bit1=目录)
 *           [32]名字长 [33]名字 ("FILE.TXT;1" 形式, 分隔符 ';1' 去掉)。
 * 文件名匹配复用 FAT 8.3 大写约定 (to_fat12_name), "NAME.EXT;1" → "NAME   EXT"。
 * 注意: 本轮只读; 目录尺寸上限 32KB (一次解析)。
 */
#include "common.h"
#include "iso9660.h"

static int  iso_ok = 0;
static int  iso_root_lba512 = 0;   /* 根目录起始 (512B 扇单位) */
static int  iso_root_size512 = 0;

/* 目录名 → FAT 8.3 名 ("HELLO.TXT;1" → "HELLO   TXT"; 无 ;1 亦可) */
static void iso_name_to_fat(const unsigned char *iso, int len, char *dest)
{
    int i, j = 0;
    for (i = 0; i < 11; i++) dest[i] = ' ';
    /* 去掉 ";1" 后缀 */
    while (len > 0 && (iso[len-1] == '1' || iso[len-1] == ';')) len--;
    if (len > 0 && iso[len-1] == ';') len--;
    for (i = 0; i < len && j < 8; i++) {
        if (iso[i] == '.') break;
        dest[j++] = (iso[i] >= 'a' && iso[i] <= 'z') ? iso[i] - 32 : iso[i];
    }
    if (i < len && iso[i] == '.') {
        i++; j = 8;
        for (; i < len && j < 11; i++)
            dest[j++] = (iso[i] >= 'a' && iso[i] <= 'z') ? iso[i] - 32 : iso[i];
    }
}

/* 解析一个 2048B 目录扇内的记录, 填 entries; 返回有效条目数 */
static int parse_dir_records(const unsigned char *sec, int seclen,
                             FAT12Entry *out, int *pn, int max)
{
    int off = 0;
    while (off < seclen) {
        unsigned char rec_len = sec[off];
        if (rec_len == 0) break;                     /* 扇内余下为 0 填充 */
        unsigned char flags = sec[off + 25];
        unsigned char name_len = sec[off + 30];   /* 标准: [30]=名长 [31]=名 */
        unsigned int lba = sec[off+2] | (sec[off+3]<<8) |
                           (sec[off+4]<<16) | ((unsigned)sec[off+5]<<24);
        unsigned int size = sec[off+10] | (sec[off+11]<<8) |
                            (sec[off+12]<<16) | ((unsigned)sec[off+13]<<24);
        if (name_len == 1 && (sec[off+31] == 0 || sec[off+31] == 1)) {
            /* . / ..: 记录里的 LBA 即本目录/父目录 extents, 保留供 cd/回退 */
            if (*pn < max) {
                FAT12Entry *e = &out[*pn];
                for (int k = 0; k < 11; k++) e->name[k] = ' ';
                e->name[0] = '.';
                if (sec[off+31] == 1) e->name[1] = '.';
                e->attr = 0x10;
                unsigned int l512 = lba * 4;
                e->start_cluster = l512 & 0xFFFF;
                e->reserved[8] = (unsigned char)((l512 >> 16) & 0xFF);
                e->reserved[9] = (unsigned char)((l512 >> 24) & 0xFF);
                for (int k = 0; k < 8; k++) e->reserved[k] = 0;
                e->size = size;
                (*pn)++;
            }
            off += rec_len;
            continue;
        }
        if (*pn < max) {
            FAT12Entry *e = &out[*pn];
            iso_name_to_fat(sec + off + 31, name_len, e->name);
            e->attr = (flags & 0x02) ? 0x10 : 0x20;
            {
                unsigned int lba512 = lba * 4;
                e->start_cluster = lba512 & 0xFFFF;
                e->reserved[8] = (unsigned char)((lba512 >> 16) & 0xFF);
                e->reserved[9] = (unsigned char)((lba512 >> 24) & 0xFF);
            }
            e->size = size;
            for (int k = 0; k < 8; k++) e->reserved[k] = 0;
            (void)flags;
            (*pn)++;
        }
        off += rec_len;
    }
    return *pn;
}

int iso_root_lba(void)  { return iso_root_lba512; }
int iso_root_size(void) { return iso_root_size512; }

int iso_mount(void)
{
    unsigned char pvd[2048];
    if (atapi_read_sectors_2048(16, 1, pvd)) { serial_puts("[ISO] pvd read fail\n"); return -1; }
    if (pvd[0] != 1 || pvd[1] != 'C' || pvd[2] != 'D' ||
        pvd[3] != '0' || pvd[4] != '0' || pvd[5] != '1') {
        { char hb[] = "0123456789ABCDEF";
          serial_puts("[ISO] pvd bad: ");
          for (int k = 0; k < 8; k++) serial_putc(hb[(pvd[k]>>4)&0xF]), serial_putc(hb[pvd[k]&0xF]), serial_putc(' ');
          serial_putc('\n'); }
        return -1;
    }
    {
        const unsigned char *r = pvd + 156;
        unsigned int lba = r[2] | (r[3]<<8) | (r[4]<<16) | ((unsigned)r[5]<<24);
        unsigned int size = r[10] | (r[11]<<8) | (r[12]<<16) | ((unsigned)r[13]<<24);
        iso_root_lba512 = (int)(lba * 4);
        iso_root_size512 = (int)((size + 511) / 512);
    }
    iso_ok = 1;
    return 0;
}

/* 读目录 (dir_lba512 起, size512 个 512 扇), 列出条目 */
int iso_list(int dir_lba512, int dir_size512, FAT12Entry *out, int max)
{
    unsigned char buf[2048];
    int n = 0;
    if (!iso_ok) return 0;
    for (int s = 0; s * 4 < dir_size512 && n < max; s++) {
        if (atapi_read_sectors_2048(dir_lba512 / 4 + s, 1, buf)) break;
        parse_dir_records(buf, 2048, out, &n, max);
    }
    return n;
}

int iso_find(int dir_lba512, int dir_size512, char *name, FAT12Entry *out)
{
    FAT12Entry ents[32];
    char want[11];
    to_fat12_name(name, want);
    int n = iso_list(dir_lba512, dir_size512, ents, 32);
    for (int i = 0; i < n; i++) {
        int match = 1;
        for (int k = 0; k < 11; k++)
            if (ents[i].name[k] != want[k]) { match = 0; break; }
        if (match) { if (out) *out = ents[i]; return i; }
    }
    return -1;
}
