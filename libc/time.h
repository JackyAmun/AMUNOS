/*
 * time.h -- broken-down time for the hosted libc.
 *
 * Integer-only civil-date math (no FPU on Makar).  There is no timezone
 * database, so localtime() == gmtime() (everything is UTC, as if TZ=UTC).
 * Implementation in time.c; seconds come from the CMOS RTC via
 * SYS_GETTIMEOFDAY.
 */
#ifndef _USERSPACE_TIME_H
#define _USERSPACE_TIME_H

#ifndef _MAKAR_TYPE_TIME_T
#define _MAKAR_TYPE_TIME_T
typedef long time_t;
#endif

struct tm {
    int tm_sec;    /* seconds        [0,60] (60 for leap second)        */
    int tm_min;    /* minutes        [0,59]                             */
    int tm_hour;   /* hours          [0,23]                             */
    int tm_mday;   /* day of month   [1,31]                             */
    int tm_mon;    /* months since January [0,11]                       */
    int tm_year;   /* years since 1900                                  */
    int tm_wday;   /* days since Sunday    [0,6]                        */
    int tm_yday;   /* days since January 1 [0,365]                      */
    int tm_isdst;  /* DST flag -- always 0 (no zone database)           */
};

time_t       time(time_t *t);
struct tm   *gmtime_r   (const time_t *timep, struct tm *result);
struct tm   *gmtime     (const time_t *timep);
struct tm   *localtime_r(const time_t *timep, struct tm *result);
struct tm   *localtime  (const time_t *timep);
time_t       mktime(struct tm *tm);
unsigned int strftime(char *s, unsigned int max, const char *format,
                      const struct tm *tm);

#endif /* _USERSPACE_TIME_H */
