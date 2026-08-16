/* mem.c — AMUNOS 内存分配器 (free-list block allocator)
 *
 * 堆区域: 0x400000 起, 4MB (QEMU 默认 128MB RAM)
 * 块结构:
 *   [size:4] [data...]          使用块 (size 低1位 = 1 标记使用)
 *   [size:4] [next:4] [data...] 空闲块
 */

#include "common.h"

#define HEAP_START  0x400000
#define HEAP_SIZE   (4 * 1024 * 1024)

typedef struct mem_block {
    unsigned size;          /* 块总大小 (含头), 低1位标记使用 */
    struct mem_block *next; /* 空闲链表 (仅空闲块有效) */
} mem_block_t;

#define BLOCK_HEADER sizeof(unsigned)      /* 只用 size 字段 (使用块) */
#define USED_FLAG   1
#define ALIGN8(x)   (((x) + 7) & ~7u)

static mem_block_t *free_head = 0;

/* ── 初始化: 整个堆变成一个空闲块 ── */
void mem_init() {
    free_head = (mem_block_t*)HEAP_START;
    free_head->size = HEAP_SIZE;
    free_head->next = 0;
}

/* ── 分配 size 字节 ── */
void *mem_alloc(unsigned size) {
    size = ALIGN8(size);
    mem_block_t *prev = 0, *cur = free_head, *nb;

    while (cur) {
        if (cur->size >= size) {
            /* 分裂: 剩余部分成为新的空闲块 nb */
            if (cur->size >= size + sizeof(mem_block_t) + 8) {
                nb = (mem_block_t*)((char*)cur + sizeof(mem_block_t) + size);
                nb->size = cur->size - size - sizeof(mem_block_t);
                nb->next = cur->next;
                cur->size = size + sizeof(mem_block_t);
            } else {
                nb = cur->next;
            }
            /* 从空闲链表摘除, 链表指向分裂出的 nb */
            if (prev) prev->next = nb;
            else free_head = nb;

            cur->size |= USED_FLAG;  /* 标记使用 */
            return (char*)cur + sizeof(mem_block_t);
        }
        prev = cur;
        cur = cur->next;
    }
    return 0;  /* 堆满 */
}

/* ── 释放 ptr ──
 * 按地址序插入空闲链表, 并与前后物理相邻的空闲块合并 (防止碎片化)。
 * 空闲链表始终按地址升序排列; mem_alloc 的 first-fit 分裂也保持该顺序。 */
void mem_free(void *ptr) {
    if (!ptr) return;
    mem_block_t *b = (mem_block_t*)((char*)ptr - sizeof(mem_block_t));
    b->size &= ~USED_FLAG;   /* 清除使用标记 */

    /* 找到地址序中 b 的插入位置 (prev < b <= cur) */
    mem_block_t *prev = 0, *cur = free_head;
    while (cur && cur < b) { prev = cur; cur = cur->next; }

    /* 与后继合并: b 末尾 == cur 开头 */
    if (cur && (char*)b + b->size == (char*)cur) {
        b->size += cur->size;
        b->next = cur->next;
    } else {
        b->next = cur;
    }

    /* 与前驱合并: prev 末尾 == b 开头 */
    if (prev && (char*)prev + prev->size == (char*)b) {
        prev->size += b->size;
        prev->next = b->next;
    } else if (prev) {
        prev->next = b;
    } else {
        free_head = b;
    }
}
