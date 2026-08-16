/*
 * stdarg.h -- varargs support for freestanding userspace.
 *
 * Makar's ring-3 ABI is i386 cdecl, so variadic arguments live on the
 * caller stack and va_list is conceptually a byte pointer.  We prefer
 * GCC's __builtin_va_* intrinsics when compiled by GCC: at -O2 GCC may
 * move parameters out of the predictable cdecl positions (e.g. across
 * an inlined `varargs -> vfunc -> vfunc` chain), in which case the
 * hand-rolled `&last + sizeof(last)` walk reads zeros instead of the
 * caller's args.
 *
 * TCC on i386 doesn't have a working __builtin_va_list — its own
 * stdarg.h uses the manual byte-pointer macros — so we keep that as
 * the fallback for TCC (and for any other toolchain that ships here).
 *
 * Without the GCC branch, kend.S and any other diagnostic emitted
 * through libtcc.c's strcat_printf -> strcat_vprintf -> vsnprintf
 * chain showed "(null):<huge>: ..." because vsnprintf's va_arg read
 * garbage when tcc.c was built at -O2.
 */
#ifndef _USERSPACE_STDARG_H
#define _USERSPACE_STDARG_H

#if defined(__GNUC__) && !defined(__TINYC__)

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start((ap), (last))
#define va_arg(ap, type)   __builtin_va_arg((ap), type)
#define va_copy(dst, src)  __builtin_va_copy((dst), (src))
#define va_end(ap)         __builtin_va_end((ap))

#else  /* TCC i386 / unknown compiler: manual cdecl walk */

typedef char *va_list;
#define va_start(ap, last) ((ap) = ((char *)&(last)) + ((sizeof(last) + 3u) & ~3u))
#define va_arg(ap, type)   ((ap) += ((sizeof(type) + 3u) & ~3u), \
                            *(type *)((ap) - ((sizeof(type) + 3u) & ~3u)))
#define va_copy(dst, src)  ((dst) = (src))
#define va_end(ap)         ((void)(ap))

#endif

typedef va_list __gnuc_va_list;

#endif /* _USERSPACE_STDARG_H */
