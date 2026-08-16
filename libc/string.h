/*
 * string.h -- userspace string / memory helpers.
 *
 * Lean byte-at-a-time implementations of the C89 string surface that
 * TCC + the upcoming uClibc-ng port require.  Sized for correctness
 * over throughput; userspace compile-time is not a hot path.
 *
 * Implementation lives in string.c -- link against libc.a (which
 * archives malloc.o, stdio.o, setjmp.o, string.o together) or list
 * string.o explicitly alongside crt0.o.
 */
#ifndef _USERSPACE_STRING_H
#define _USERSPACE_STRING_H

/* All sizes use unsigned int rather than dragging in <stddef.h>. */
typedef unsigned int string_size_t;

void *memcpy (void *dst, const void *src, string_size_t n);
void *memmove(void *dst, const void *src, string_size_t n);
void *memset (void *dst, int c, string_size_t n);
int   memcmp (const void *a, const void *b, string_size_t n);
void *memchr (const void *s, int c, string_size_t n);

string_size_t strlen (const char *s);
int           strcmp (const char *a, const char *b);
int           strncmp(const char *a, const char *b, string_size_t n);
char         *strcpy (char *dst, const char *src);
char         *strncpy(char *dst, const char *src, string_size_t n);
char         *strcat (char *dst, const char *src);
char         *strchr (const char *s, int c);
char         *strrchr(const char *s, int c);
char         *strstr (const char *hay, const char *needle);

/* POSIX tokeniser + error-string lookup (hosted libc surface). */
char         *strtok_r(char *str, const char *delim, char **saveptr);
char         *strtok  (char *str, const char *delim);
char         *strerror(int errnum);

#endif /* _USERSPACE_STRING_H */
