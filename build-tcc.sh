#!/bin/sh
# build-tcc.sh — cross-build TinyCC 0.9.27 as an AMUNOS userspace app.
#
# Produces:
#   tcc.elf                              compiler binary (loads at 0x100000)
#   makar/vendor/tinycc/lib/libtcc1.a    TCC runtime library
#   libc/libc.a                          userspace libc (via build-libc.sh)
#   libc/crt1.o libc/crti.o libc/crtn.o  CRT startup stubs
#
# Host deps: gcc -m32 + nasm + ld + as (WSL Ubuntu with gcc-multilib).
# Run `sh build-tcc.sh` from the OSDev root, then `python3 mka_img.py` to
# stage everything onto A.img.
set -e
cd "$(dirname "$0")"

TCC_DIR=makar/vendor/tinycc
LIBC=libc

# 1. userspace libc (stdio/stdlib/string/malloc/tcc_compat/time/setjmp/crt0)
sh build-libc.sh

# 2. config.h — satisfies tcc.h's `#include "config.h"`; real config is -D flags.
cat > "$TCC_DIR/config.h" << 'EOF'
/* Hand-rolled config.h for the AMUNOS cross-build.  All real configuration
 * is supplied via -D flags in build-tcc.sh. */
EOF

# 3. compile tcc.c (ONE_SOURCE: tcc + libtcc + tccpp/tccgen/tccelf/tccrun + asm)
CFLAGS_TCC="-m32 -O2 -std=gnu99 -ffreestanding -fno-stack-protector \
 -fno-pie -fno-pic -fno-asynchronous-unwind-tables -fno-unwind-tables \
 -Wall -Wno-unused-parameter -Wno-pointer-sign -Wno-pointer-to-int-cast \
 -Wno-int-to-pointer-cast -Wno-format -Wno-missing-field-initializers \
 -Wno-unused-function -Wno-unused-variable -Wno-unused-result \
 -nostdinc -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
 -I. -I ../../../libc \
 -DTCC_TARGET_I386 \
 -DTCC_VERSION=\"0.9.27-amunos\" \
 -DCONFIG_TCCDIR=\".\" \
 -DCONFIG_TCC_SYSINCLUDEPATHS=\".\" \
 -DCONFIG_TCC_LIBPATHS=\".\" \
 -DCONFIG_TCC_CRTPREFIX=\"A:/\" \
 -DCONFIG_LDDIR=\"lib\" \
 -DCONFIG_TCC_STATIC -DCONFIG_TCCBOOT -DONE_SOURCE=1"

echo "==> Compiling tcc.c -> tcc.o (one big -O2 compile) ..."
(cd "$TCC_DIR" && gcc $CFLAGS_TCC -c tcc.c -o tcc.o)

# 4. link tcc.elf (crt0.o + tcc.o + libc.a + libgcc, at 0x100000)
echo "==> Linking tcc.elf ..."
LIBGCC=$(gcc -m32 -print-libgcc-file-name)
ld -m elf_i386 -no-pie -T "$LIBC/link.ld" -nostdlib -static \
    "$LIBC/crt0.o" "$TCC_DIR/tcc.o" "$LIBC/libc.a" "$LIBGCC" \
    -o tcc.elf

# 5. libtcc1.a — TCC runtime library (64-bit helpers, alloca, varargs)
echo "==> Building libtcc1.a ..."
(cd "$TCC_DIR/lib" && \
    gcc -m32 -O2 -ffreestanding -fno-stack-protector \
        -fno-asynchronous-unwind-tables -fno-unwind-tables \
        -nostdlib -nostdinc -I ../../../../libc -c libtcc1.c -o libtcc1.o && \
    gcc -m32 -c alloca86.S -o alloca86.o && \
    ar rcs libtcc1.a libtcc1.o alloca86.o)

# 6. CRT stubs — TCC's default link is crt1.o + crti.o + <objs> + -lc + crtn.o
echo "==> Building CRT stubs ..."
cp "$LIBC/crt0.o" "$LIBC/crt1.o"
echo '.section .init' | as --32 -o "$LIBC/crti.o"
echo '.section .fini' | as --32 -o "$LIBC/crtn.o"

echo "==> Done. tcc.elf: $(ls -lh tcc.elf | awk '{print $5}')"
