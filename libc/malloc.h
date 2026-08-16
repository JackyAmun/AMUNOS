/*
 * malloc.h -- minimal userspace heap for Makar ring-3 apps.
 *
 * First-fit free list over SYS_BRK, with 8-byte-aligned blocks and a 12-byte
 * per-block header (size + free flag + next-free pointer).  Sized for the
 * TCC port preparation slice: enough to drive a compile (many small
 * allocations, occasional large ones, realloc-as-grow), not a high-end
 * general-purpose allocator.
 *
 * Usage:
 *     #include "malloc.h"
 *     void *p = malloc(128);
 *     free(p);
 *
 * Implementation lives in malloc.c -- link it alongside crt0.o + your app.
 */
#ifndef _USERSPACE_MALLOC_H
#define _USERSPACE_MALLOC_H

typedef unsigned int   size_t_u32;   /* avoid pulling stddef in freestanding */

void *malloc(unsigned int sz);
void  free(void *p);
void *realloc(void *p, unsigned int sz);
void *calloc(unsigned int n, unsigned int sz);

#endif /* _USERSPACE_MALLOC_H */
