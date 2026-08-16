/* sys/time.h -- gettimeofday 声明 (struct timeval 来自 <syscall.h>) */
#ifndef _AMUNOS_SYS_TIME_H
#define _AMUNOS_SYS_TIME_H
#include "syscall.h"
int gettimeofday(struct timeval *tv, void *tz);
#endif
