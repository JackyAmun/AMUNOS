/*
 * stdio.h -- minimal userspace stdio.  Buffered FILE* layer over the fd
 * syscalls, plus the snprintf family.  Sized to satisfy TCC + the
 * upcoming uClibc-ng/musl port without growing a real libc footprint.
 *
 * Buffering is line-buffered for stdout/stderr (auto-flush on '\n') and
 * fully-buffered for opened files (flush on fflush/fclose).  Each FILE
 * gets a 1 KiB buffer; no shared pool.
 */
#ifndef _USERSPACE_STDIO_H
#define _USERSPACE_STDIO_H

#include "syscall.h"
#include "stdarg.h"

#define STDIO_BUFSZ 1024

typedef struct {
    int  fd;
    int  err;
    int  eof;
    unsigned int wlen;             /* bytes buffered for write */
    unsigned char wbuf[STDIO_BUFSZ];
} FILE;

extern FILE *stdin, *stdout, *stderr;

FILE *fopen(const char *path, const char *mode);
int   fclose(FILE *f);
unsigned int fread (void *p, unsigned int sz, unsigned int n, FILE *f);
unsigned int fwrite(const void *p, unsigned int sz, unsigned int n, FILE *f);
int   fflush(FILE *f);
int   fputs (const char *s, FILE *f);
int   fputc (int c, FILE *f);
int   putchar(int c);
int   puts  (const char *s);
int   fgetc (FILE *f);
int   feof  (FILE *f);
int   fseek (FILE *f, long offset, int whence);
long  ftell (FILE *f);
FILE *fdopen(int fd, const char *mode);

/* Format helpers.  Supported conversions: %s %c %d %i %u %x %X %p %%
 * and the field-width prefix (e.g. %5d, %08x).  No floats (no FPU). */
int snprintf (char *buf, unsigned int sz, const char *fmt, ...);
int vsnprintf(char *buf, unsigned int sz, const char *fmt, va_list ap);
int sprintf  (char *buf, const char *fmt, ...);
int vsprintf (char *buf, const char *fmt, va_list ap);
int fprintf  (FILE *f, const char *fmt, ...);
int vfprintf (FILE *f, const char *fmt, va_list ap);
int printf   (const char *fmt, ...);

#endif
