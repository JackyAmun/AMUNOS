/* signal.h -- 桩 (TCC tccrun.c 引用; AMUNOS 无用户态信号) */
#ifndef _AMUNOS_SIGNAL_H
#define _AMUNOS_SIGNAL_H
typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIGABRT 6
#endif
