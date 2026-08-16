/*
 * malloc.c -- first-fit free-list allocator over SYS_BRK.
 *
 * Block layout (12 bytes header, 8-byte alignment on payload):
 *
 *     +------+------+----------------------+
 *     | size | free | (next free pointer)  |
 *     |  u32 | u32  |   u32 -- only when   |
 *     |      |      |   the block is free  |
 *     +------+------+----------------------+
 *     |                payload             |
 *
 *   - size  : total block size including header, in bytes (multiple of 8)
 *   - free  : 0 = busy, 1 = free
 *   - next  : free-list link; only valid when free == 1
 *
 * The heap grows on demand: when no free block fits, we call SYS_BRK to
 * extend the user break, then place a fresh free block at the tail.
 *
 * Free blocks are kept in a singly-linked list in address order so the
 * coalescer can merge neighbours in O(n).  Not the fastest allocator
 * possible, but trivial to audit and adequate for the TCC bring-up.
 */
#include "syscall.h"
#include "malloc.h"

#define ALIGN       8u
#define HDR_BYTES   12u
#define MIN_BLOCK   (HDR_BYTES + ALIGN)   /* smallest splittable size */

typedef struct {
    unsigned int size;   /* block size including header (bytes) */
    unsigned int free;   /* 0 = busy, 1 = free                  */
    unsigned int next;   /* free-list link (only valid if free) */
} hdr_t;

static unsigned int s_heap_base = 0;   /* first block (immutable after init) */
static unsigned int s_heap_end  = 0;   /* current break (next unmapped byte) */
static unsigned int s_free_head = 0;   /* head of free list (address-sorted) */

/* ---- SYS_BRK shim -- returns the new break or 0 on failure. */
static unsigned int brk_set(unsigned int new_brk)
{
    long r = sys_brk((void *)(unsigned long)new_brk);
    return (r > 0) ? (unsigned int)r : 0u;
}
static unsigned int brk_get(void) { return brk_set(0); }

/* Lazy init: pin s_heap_base to the current break the first time we're
 * called.  No constructors in ring-3 today, so we do it on the first
 * malloc() call. */
static void heap_init_if_needed(void)
{
    if (s_heap_base) return;
    unsigned int b = brk_get();
    s_heap_base = b;
    s_heap_end  = b;
    s_free_head = 0;
}

static unsigned int round_up(unsigned int v, unsigned int a)
{
    return (v + (a - 1u)) & ~(a - 1u);
}

/* Grow the break by at least `bytes`, return a pointer to the new block
 * placed at the old break (already wired as a free block in the list).
 * Returns 0 on OOM (no kernel pages). */
static hdr_t *grow_heap(unsigned int bytes)
{
    unsigned int need = round_up(bytes, 4096u);
    unsigned int new_end = s_heap_end + need;
    unsigned int got = brk_set(new_end);
    if (got < new_end) return 0;
    hdr_t *b = (hdr_t *)(unsigned long)s_heap_end;
    b->size = need;
    b->free = 1;
    b->next = 0;
    /* Append to the free list (address-sorted -- new block is at the tail). */
    if (!s_free_head) {
        s_free_head = (unsigned int)(unsigned long)b;
    } else {
        hdr_t *p = (hdr_t *)(unsigned long)s_free_head;
        while (p->next) p = (hdr_t *)(unsigned long)p->next;
        p->next = (unsigned int)(unsigned long)b;
    }
    s_heap_end = new_end;
    return b;
}

/* Remove `b` from the free list (called when a free block is taken). */
static void unlink_free(hdr_t *b)
{
    unsigned int target = (unsigned int)(unsigned long)b;
    if (s_free_head == target) { s_free_head = b->next; return; }
    hdr_t *p = (hdr_t *)(unsigned long)s_free_head;
    while (p && p->next != target) p = (hdr_t *)(unsigned long)p->next;
    if (p) p->next = b->next;
}

/* Split `b` into a busy block of `want` bytes and a trailing free block,
 * IF the leftover is >= MIN_BLOCK.  Otherwise leave the slack inside the
 * returned block (lost until free). */
static void split_block(hdr_t *b, unsigned int want)
{
    if (b->size < want + MIN_BLOCK) return;
    hdr_t *tail = (hdr_t *)((unsigned char *)b + want);
    tail->size = b->size - want;
    tail->free = 1;
    tail->next = b->next;
    b->size = want;
    /* Patch the free list so it points at the new tail in place of b. */
    if (s_free_head == (unsigned int)(unsigned long)b) {
        s_free_head = (unsigned int)(unsigned long)tail;
    } else {
        hdr_t *p = (hdr_t *)(unsigned long)s_free_head;
        while (p && p->next != (unsigned int)(unsigned long)b)
            p = (hdr_t *)(unsigned long)p->next;
        if (p) p->next = (unsigned int)(unsigned long)tail;
    }
    /* b is now the busy block; the free-list pointer used to come from
     * unlink_free's caller before this helper -- here split runs BEFORE
     * the unlink so we patch the chain in place. */
}

void *malloc(unsigned int sz)
{
    if (!sz) return 0;
    heap_init_if_needed();

    unsigned int want = round_up(sz + HDR_BYTES, ALIGN);
    if (want < MIN_BLOCK) want = MIN_BLOCK;

    /* First-fit walk over the free list. */
    hdr_t *prev = 0;
    hdr_t *cur  = (hdr_t *)(unsigned long)s_free_head;
    while (cur) {
        if (cur->size >= want) {
            split_block(cur, want);
            /* Re-find cur in case split moved the chain; cur's address is
             * unchanged, just possibly no longer the free-list node. */
            (void)prev;
            unlink_free(cur);
            cur->free = 0;
            return (void *)((unsigned char *)cur + HDR_BYTES);
        }
        prev = cur;
        cur = (hdr_t *)(unsigned long)cur->next;
    }

    /* No fit -- grow. */
    hdr_t *b = grow_heap(want);
    if (!b) return 0;
    if (b->size >= want + MIN_BLOCK) split_block(b, want);
    unlink_free(b);
    b->free = 0;
    return (void *)((unsigned char *)b + HDR_BYTES);
}

/* Insert `b` into the free list at the address-sorted position,
 * coalescing with neighbours if contiguous. */
static void free_insert_coalesce(hdr_t *b)
{
    unsigned int baddr = (unsigned int)(unsigned long)b;
    b->free = 1;

    if (!s_free_head || baddr < s_free_head) {
        b->next = s_free_head;
        s_free_head = baddr;
    } else {
        hdr_t *p = (hdr_t *)(unsigned long)s_free_head;
        while (p->next && p->next < baddr)
            p = (hdr_t *)(unsigned long)p->next;
        b->next = p->next;
        p->next = baddr;
    }

    /* Forward merge with neighbour. */
    if (b->next) {
        hdr_t *nx = (hdr_t *)(unsigned long)b->next;
        if ((unsigned char *)b + b->size == (unsigned char *)nx) {
            b->size += nx->size;
            b->next  = nx->next;
        }
    }
    /* Backward merge: walk to find predecessor (sorted list). */
    if (s_free_head != baddr) {
        hdr_t *p = (hdr_t *)(unsigned long)s_free_head;
        while (p->next != baddr) p = (hdr_t *)(unsigned long)p->next;
        if ((unsigned char *)p + p->size == (unsigned char *)b) {
            p->size += b->size;
            p->next  = b->next;
        }
    }
}

void free(void *p)
{
    if (!p) return;
    hdr_t *b = (hdr_t *)((unsigned char *)p - HDR_BYTES);
    if (b->free) return;   /* double-free guard */
    free_insert_coalesce(b);
}

void *realloc(void *p, unsigned int sz)
{
    if (!p)   return malloc(sz);
    if (!sz) { free(p); return 0; }
    hdr_t *b = (hdr_t *)((unsigned char *)p - HDR_BYTES);
    unsigned int want = round_up(sz + HDR_BYTES, ALIGN);
    if (b->size >= want) {
        if (b->size >= want + MIN_BLOCK) split_block(b, want);
        return p;
    }
    /* Out-of-place: malloc + copy + free. */
    void *q = malloc(sz);
    if (!q) return 0;
    unsigned int copy = b->size - HDR_BYTES;
    if (copy > sz) copy = sz;
    unsigned char *dst = (unsigned char *)q;
    unsigned char *src = (unsigned char *)p;
    for (unsigned int i = 0; i < copy; i++) dst[i] = src[i];
    free(p);
    return q;
}

void *calloc(unsigned int n, unsigned int sz)
{
    unsigned long total = (unsigned long)n * (unsigned long)sz;
    if (total > 0xFFFFFFFFul) return 0;
    void *p = malloc((unsigned int)total);
    if (!p) return 0;
    unsigned char *q = (unsigned char *)p;
    for (unsigned long i = 0; i < total; i++) q[i] = 0;
    return p;
}
