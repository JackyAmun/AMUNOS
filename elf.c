/* elf.c — AMUNOS ELF32 静态可执行文件加载器 (v6.4)
 *
 * 参考 Makar OS 的 ELF 加载思路, 简化版:
 *   - 仅支持 32 位小端 ET_EXEC (静态链接, 无动态链接/无重定位)
 *   - 无分页, p_vaddr 即物理地址
 *   - 只处理 PT_LOAD 段: 复制 p_filesz 字节到 p_vaddr, 清零 bss 部分
 *   - 返回入口地址, 由调用方 call 进入 (Ring 0, 复用内核栈)
 */

#include "common.h"

static unsigned rd32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
static unsigned short rd16(const unsigned char *p) {
    return (unsigned short)(p[0] | (p[1] << 8));
}

/* 加载静态 ELF, 返回入口地址; 失败返回 0 */
int elf_load(unsigned char *buf, int size) {
    if (size < 52) return 0;

    /* 魔数 0x7F 'E' 'L' 'F' */
    if (buf[0] != 0x7F || buf[1] != 'E' || buf[2] != 'L' || buf[3] != 'F')
        return 0;
    /* EI_CLASS=1 (32位), EI_DATA=1 (小端) */
    if (buf[4] != 1 || buf[5] != 1) return 0;

    unsigned short type    = rd16(buf + 16);
    unsigned short machine = rd16(buf + 18);
    if (type != 2)    return 0;   /* 必须 ET_EXEC */
    if (machine != 3) return 0;   /* 必须 EM_386 */

    unsigned entry  = rd32(buf + 24);
    unsigned phoff  = rd32(buf + 28);
    unsigned short entsize = rd16(buf + 42);
    unsigned short phnum   = rd16(buf + 44);

    for (int i = 0; i < phnum; i++) {
        const unsigned char *ph = buf + phoff + (unsigned)i * entsize;
        unsigned ptype = rd32(ph + 0);
        if (ptype != 1) continue;            /* 只处理 PT_LOAD */

        unsigned poff    = rd32(ph + 4);
        unsigned pvaddr  = rd32(ph + 8);
        unsigned pfilesz = rd32(ph + 16);
        unsigned pmemsz  = rd32(ph + 20);

        if (pvaddr < 0x10000) return 0;      /* 拒绝加载到低内存 (保护内核/IVT) */

        /* 复制文件内容到物理地址 */
        for (unsigned j = 0; j < pfilesz; j++) {
            if (poff + j >= (unsigned)size) break;
            *(unsigned char *)(pvaddr + j) = buf[poff + j];
        }
        /* 清零 bss (p_memsz - p_filesz) */
        for (unsigned j = pfilesz; j < pmemsz; j++)
            *(unsigned char *)(pvaddr + j) = 0;
    }
    return (int)entry;
}
