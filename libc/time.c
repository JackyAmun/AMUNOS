/*
 * time.c -- broken-down time for the hosted libc.  See time.h.
 *
 * All integer math (no x87 FPU init on Makar).  UTC only: localtime is an
 * alias for gmtime since there is no timezone database.  time() reads the
 * CMOS RTC through SYS_GETTIMEOFDAY.
 */
#include "time.h"
#include "syscall.h"

static int is_leap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static const int s_mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

time_t time(time_t *t)
{
    struct timeval tv;
    time_t now = 0;
    if (sys_gettimeofday(&tv) == 0)
        now = (time_t)tv.tv_sec;
    if (t) *t = now;
    return now;
}

struct tm *gmtime_r(const time_t *timep, struct tm *r)
{
    if (!timep || !r) return 0;

    long secs = (long)*timep;
    long days = secs / 86400;
    long rem  = secs % 86400;
    if (rem < 0) { rem += 86400; days -= 1; }

    r->tm_hour = (int)(rem / 3600);
    r->tm_min  = (int)((rem % 3600) / 60);
    r->tm_sec  = (int)(rem % 60);

    /* 1970-01-01 was a Thursday (wday 4). */
    int wday = (int)((days % 7 + 4) % 7);
    if (wday < 0) wday += 7;
    r->tm_wday = wday;

    int year = 1970;
    for (;;) {
        int dy = is_leap(year) ? 366 : 365;
        if (days >= dy) { days -= dy; year++; }
        else if (days < 0) { year--; days += is_leap(year) ? 366 : 365; }
        else break;
    }
    r->tm_year = year - 1900;
    r->tm_yday = (int)days;

    int mon = 0;
    for (;;) {
        int dm = s_mdays[mon];
        if (mon == 1 && is_leap(year)) dm = 29;
        if (days >= dm) { days -= dm; mon++; }
        else break;
    }
    r->tm_mon  = mon;
    r->tm_mday = (int)days + 1;
    r->tm_isdst = 0;
    return r;
}

struct tm *gmtime(const time_t *timep)
{
    static struct tm tmbuf;
    return gmtime_r(timep, &tmbuf);
}

/* No timezone database -- local time is UTC. */
struct tm *localtime_r(const time_t *timep, struct tm *r) { return gmtime_r(timep, r); }
struct tm *localtime  (const time_t *timep)               { return gmtime(timep);     }

time_t mktime(struct tm *tm)
{
    if (!tm) return (time_t)-1;

    int year = tm->tm_year + 1900;
    int mon  = tm->tm_mon;
    /* Normalise the month into [0,11] with a year carry. */
    year += mon / 12;
    mon  %= 12;
    if (mon < 0) { mon += 12; year -= 1; }

    long days = 0;
    if (year >= 1970)
        for (int y = 1970; y < year; y++) days += is_leap(y) ? 366 : 365;
    else
        for (int y = year; y < 1970; y++) days -= is_leap(y) ? 366 : 365;

    for (int m = 0; m < mon; m++) {
        days += s_mdays[m];
        if (m == 1 && is_leap(year)) days += 1;
    }
    days += tm->tm_mday - 1;

    long secs = days * 86400L
              + (long)tm->tm_hour * 3600L
              + (long)tm->tm_min  * 60L
              + (long)tm->tm_sec;

    /* Write back the normalised fields (wday/yday included), like glibc. */
    time_t tt = (time_t)secs;
    gmtime_r(&tt, tm);
    return tt;
}

/* ---- strftime (subset) ---------------------------------------------- */

static void st_putc(char *s, unsigned int max, unsigned int *o, char c)
{
    if (*o < max) s[*o] = c;
    (*o)++;
}

static void st_putnum(char *s, unsigned int max, unsigned int *o, int val, int width)
{
    char tmp[12];
    int n = 0, v = val < 0 ? 0 : val;
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n < width) tmp[n++] = '0';
    while (n > 0) st_putc(s, max, o, tmp[--n]);
}

static void st_puts(char *s, unsigned int max, unsigned int *o, const char *str)
{
    for (; *str; str++) st_putc(s, max, o, *str);
}

unsigned int strftime(char *s, unsigned int max, const char *fmt,
                      const struct tm *tm)
{
    static const char *const wday3[7] =
        { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    static const char *const mon3[12] =
        { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    unsigned int o = 0;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { st_putc(s, max, &o, *p); continue; }
        p++;
        switch (*p) {
        case 'Y': st_putnum(s, max, &o, tm->tm_year + 1900, 1); break;
        case 'y': st_putnum(s, max, &o, (tm->tm_year + 1900) % 100, 2); break;
        case 'm': st_putnum(s, max, &o, tm->tm_mon + 1, 2); break;
        case 'd': st_putnum(s, max, &o, tm->tm_mday, 2); break;
        case 'H': st_putnum(s, max, &o, tm->tm_hour, 2); break;
        case 'M': st_putnum(s, max, &o, tm->tm_min, 2); break;
        case 'S': st_putnum(s, max, &o, tm->tm_sec, 2); break;
        case 'j': st_putnum(s, max, &o, tm->tm_yday + 1, 3); break;
        case 'p': st_puts(s, max, &o, tm->tm_hour < 12 ? "AM" : "PM"); break;
        case 'a':
            if (tm->tm_wday >= 0 && tm->tm_wday < 7)
                st_puts(s, max, &o, wday3[tm->tm_wday]);
            break;
        case 'b':
        case 'h':
            if (tm->tm_mon >= 0 && tm->tm_mon < 12)
                st_puts(s, max, &o, mon3[tm->tm_mon]);
            break;
        case 'F':  /* %Y-%m-%d */
            st_putnum(s, max, &o, tm->tm_year + 1900, 4); st_putc(s, max, &o, '-');
            st_putnum(s, max, &o, tm->tm_mon + 1, 2);     st_putc(s, max, &o, '-');
            st_putnum(s, max, &o, tm->tm_mday, 2);
            break;
        case 'T':  /* %H:%M:%S */
            st_putnum(s, max, &o, tm->tm_hour, 2); st_putc(s, max, &o, ':');
            st_putnum(s, max, &o, tm->tm_min, 2);  st_putc(s, max, &o, ':');
            st_putnum(s, max, &o, tm->tm_sec, 2);
            break;
        case '%': st_putc(s, max, &o, '%'); break;
        case '\0': p--; break;   /* trailing '%' -- stop */
        default:  st_putc(s, max, &o, '%'); st_putc(s, max, &o, *p); break;
        }
    }

    if (o < max) { s[o] = '\0'; return o; }
    if (max) s[max - 1] = '\0';
    return 0;   /* result + NUL did not fit */
}
