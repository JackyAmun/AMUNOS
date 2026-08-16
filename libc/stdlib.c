#include "stdlib.h"
#include "stdarg.h"

int sscanf(const char *s, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int matched = 0;
    while (*fmt) {
        if (isspace((unsigned char)*fmt)) {
            while (isspace((unsigned char)*s)) s++;
            fmt++;
            continue;
        }
        if (*fmt != '%') {
            if (*s != *fmt) break;
            s++;
            fmt++;
            continue;
        }
        fmt++;
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        if (*fmt == 'd' || *fmt == 'i' || *fmt == 'u' || *fmt == 'x') {
            int base = (*fmt == 'x') ? 16 : 10;
            while (isspace((unsigned char)*s)) s++;
            char *end;
            long v = strtol(s, &end, base);
            if (end == s) break;
            if (*fmt == 'u' || *fmt == 'x') *va_arg(ap, unsigned int *) = (unsigned int)v;
            else                            *va_arg(ap, int *) = (int)v;
            s = end;
            matched++;
            fmt++;
        } else if (*fmt == 's') {
            while (isspace((unsigned char)*s)) s++;
            char *out = va_arg(ap, char *);
            int wrote = 0;
            while (*s && !isspace((unsigned char)*s) && (width == 0 || wrote < width))
                out[wrote++] = *s++;
            out[wrote] = '\0';
            if (wrote == 0) break;
            matched++;
            fmt++;
        } else if (*fmt == 'c') {
            char *out = va_arg(ap, char *);
            if (!*s) break;
            *out = *s++;
            matched++;
            fmt++;
        } else {
            break;
        }
    }
    va_end(ap);
    return matched;
}

int abs(int x) { return x < 0 ? -x : x; }
double atof(const char *s) { return (double)strtold(s, (char **)0); }
double fabs(double x) { return x < 0.0 ? -x : x; }
