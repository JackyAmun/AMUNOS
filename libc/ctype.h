/*
 * ctype.h -- minimal character classifiers for Makar userspace.
 *
 * ASCII-only.  All functions return non-zero for true, zero for false, and
 * accept any int (taking the low byte) -- matches POSIX's "unsigned char or
 * EOF" contract closely enough for TCC, our tokenisers, and basic stdlib
 * conversions.  Header-only so each .elf gets its own inlined copy without
 * any link step.
 */
#ifndef _USERSPACE_CTYPE_H
#define _USERSPACE_CTYPE_H

static inline int islower(int c) { return c >= 'a' && c <= 'z'; }
static inline int isupper(int c) { return c >= 'A' && c <= 'Z'; }
static inline int isalpha(int c) { return islower(c) || isupper(c); }
static inline int isdigit(int c) { return c >= '0' && c <= '9'; }
static inline int isxdigit(int c)
{
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
static inline int isalnum(int c) { return isalpha(c) || isdigit(c); }
static inline int isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\v' || c == '\f' || c == '\r';
}
static inline int isblank(int c) { return c == ' ' || c == '\t'; }
static inline int iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
static inline int isprint(int c) { return c >= 32 && c < 127; }
static inline int isgraph(int c) { return c > 32 && c < 127; }
static inline int ispunct(int c) { return isgraph(c) && !isalnum(c); }
static inline int tolower(int c) { return isupper(c) ? c + ('a' - 'A') : c; }
static inline int toupper(int c) { return islower(c) ? c - ('a' - 'A') : c; }

#endif /* _USERSPACE_CTYPE_H */
