/* stddef.h -- size_t / ptrdiff_t / NULL for Makar apps (-nostdinc, ILP32). */
#ifndef _USERSPACE_STDDEF_H
#define _USERSPACE_STDDEF_H
typedef unsigned int size_t;
typedef int          ptrdiff_t;
#ifndef NULL
#define NULL ((void *)0)
#endif
#ifndef offsetof
#define offsetof(t, m) __builtin_offsetof(t, m)
#endif
#endif
