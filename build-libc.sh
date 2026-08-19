#!/bin/sh
# build-libc.sh — AMUNOS userspace libc (freestanding, int 0x30 ABI).
# Produces libc/libc.a + libc/crt0.o.  Host deps: gcc -m32 + nasm (WSL).
set -e
cd "$(dirname "$0")/libc"

CFLAGS="-m32 -c -std=gnu99 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
        -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc -I. \
        -Wall -Wno-unused-parameter -Wno-unused-function -Wno-unused-variable"

rm -f *.o libc.a

for src in stdio stdlib string malloc tcc_compat time dirent; do
    echo "[CC] $src.c"
    gcc $CFLAGS -o "$src.o" "$src.c"
done

echo "[ASM] setjmp.asm"
nasm -f elf -o setjmp.o setjmp.asm
echo "[ASM] crt0.asm"
nasm -f elf -o crt0.o crt0.asm

echo "[AR] libc.a"
ar rcs libc.a stdio.o stdlib.o string.o malloc.o tcc_compat.o time.o dirent.o setjmp.o
echo "[OK] libc.a built"
