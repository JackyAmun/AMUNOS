/*
 * setjmp.h -- minimal non-local jump for Makar userspace.
 *
 * jmp_buf layout (i386 SysV-ish, callee-saved + return-address slot):
 *   [0] = ebx, [1] = esi, [2] = edi, [3] = ebp, [4] = esp, [5] = eip
 *
 * Used by TCC for error recovery during parsing/codegen, and by any
 * future userspace tool that wants escape-from-deep-call semantics.
 */
#ifndef _USERSPACE_SETJMP_H
#define _USERSPACE_SETJMP_H

typedef unsigned int jmp_buf[6];

int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

#endif /* _USERSPACE_SETJMP_H */
