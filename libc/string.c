/*
 * string.c -- userspace string / memory helpers.  See string.h.
 *
 * Byte-at-a-time implementations.  Word-at-a-time is a tempting next
 * step, but TCC (the headline consumer of this shim) is bounded by
 * tokenisation latency, not memcpy throughput; the simpler code is
 * easier to audit and saves <50 LoC of word-alignment dance.
 */
#include "string.h"
#include "errno.h"   /* errno code names for strerror() */

void *memcpy(void *dst, const void *src, string_size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    for (string_size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *memmove(void *dst, const void *src, string_size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        for (string_size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (string_size_t i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

void *memset(void *dst, int c, string_size_t n)
{
    unsigned char       *d = (unsigned char *)dst;
    unsigned char        b = (unsigned char)c;
    for (string_size_t i = 0; i < n; i++) d[i] = b;
    return dst;
}

int memcmp(const void *a, const void *b, string_size_t n)
{
    const unsigned char *p = (const unsigned char *)a;
    const unsigned char *q = (const unsigned char *)b;
    for (string_size_t i = 0; i < n; i++) {
        if (p[i] != q[i]) return (int)p[i] - (int)q[i];
    }
    return 0;
}

void *memchr(const void *s, int c, string_size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned char        b = (unsigned char)c;
    for (string_size_t i = 0; i < n; i++) {
        if (p[i] == b) return (void *)(p + i);
    }
    return 0;
}

string_size_t strlen(const char *s)
{
    string_size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, string_size_t n)
{
    for (string_size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca != cb || ca == 0) return (int)ca - (int)cb;
    }
    return 0;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++)) {}
    return dst;
}

char *strncpy(char *dst, const char *src, string_size_t n)
{
    string_size_t i;
    for (i = 0; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = '\0';
    return dst;
}

char *strcat(char *dst, const char *src)
{
    char *d = dst;
    while (*d) d++;
    while ((*d++ = *src++)) {}
    return dst;
}

char *strchr(const char *s, int c)
{
    char target = (char)c;
    for (; *s; s++) {
        if (*s == target) return (char *)s;
    }
    return (target == '\0') ? (char *)s : 0;
}

char *strrchr(const char *s, int c)
{
    char        target = (char)c;
    const char *last   = 0;
    for (; *s; s++) {
        if (*s == target) last = s;
    }
    if (target == '\0') return (char *)s;
    return (char *)last;
}

char *strstr(const char *hay, const char *needle)
{
    if (!*needle) return (char *)hay;
    for (; *hay; hay++) {
        const char *h = hay;
        const char *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char *)hay;
    }
    return 0;
}

/* ---- POSIX tokeniser ------------------------------------------------- */

static int str_in_set(char c, const char *set)
{
    for (; *set; set++) if (*set == c) return 1;
    return 0;
}

char *strtok_r(char *str, const char *delim, char **saveptr)
{
    char *s = str ? str : *saveptr;
    if (!s) return 0;
    /* Skip leading delimiters. */
    while (*s && str_in_set(*s, delim)) s++;
    if (!*s) { *saveptr = 0; return 0; }
    char *tok = s;
    /* Advance to the next delimiter (or end). */
    while (*s && !str_in_set(*s, delim)) s++;
    if (*s) { *s = '\0'; *saveptr = s + 1; }
    else    { *saveptr = 0; }
    return tok;
}

char *strtok(char *str, const char *delim)
{
    static char *save;
    return strtok_r(str, delim, &save);
}

/* ---- strerror -------------------------------------------------------- *
 * Maps the errno values Makar's wrappers set (see errno.h) to short
 * messages.  Unknown codes fall through to a static "Unknown error"
 * string (non-reentrant tail, matching every real libc's strerror). */
char *strerror(int errnum)
{
    switch (errnum) {
    case 0:       return "Success";
    case EPERM:   return "Operation not permitted";
    case ENOENT:  return "No such file or directory";
    case ESRCH:   return "No such process";
    case EINTR:   return "Interrupted system call";
    case EIO:     return "Input/output error";
    case ENXIO:   return "No such device or address";
    case E2BIG:   return "Argument list too long";
    case ENOEXEC: return "Exec format error";
    case EBADF:   return "Bad file descriptor";
    case ECHILD:  return "No child processes";
    case EAGAIN:  return "Resource temporarily unavailable";
    case ENOMEM:  return "Cannot allocate memory";
    case EACCES:  return "Permission denied";
    case EFAULT:  return "Bad address";
    case EBUSY:   return "Device or resource busy";
    case EEXIST:  return "File exists";
    case ENOTDIR: return "Not a directory";
    case EISDIR:  return "Is a directory";
    case EINVAL:  return "Invalid argument";
    case ENFILE:  return "Too many open files in system";
    case EMFILE:  return "Too many open files";
    case ENOTTY:  return "Inappropriate ioctl for device";
    case EFBIG:   return "File too large";
    case ENOSPC:  return "No space left on device";
    case EROFS:   return "Read-only file system";
    case ENOSYS:  return "Function not implemented";
    default:      return "Unknown error";
    }
}

/* BSD case-insensitive compares (doomgeneric's doomtype.h needs <strings.h>). */
static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int strcasecmp(const char *a, const char *b)
{
    while (*a && lc((unsigned char)*a) == lc((unsigned char)*b)) { a++; b++; }
    return lc((unsigned char)*a) - lc((unsigned char)*b);
}

int strncasecmp(const char *a, const char *b, unsigned int n)
{
    while (n && *a && lc((unsigned char)*a) == lc((unsigned char)*b)) { a++; b++; n--; }
    if (n == 0) return 0;
    return lc((unsigned char)*a) - lc((unsigned char)*b);
}
