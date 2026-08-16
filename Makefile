# AMUNOS Makefile — FAT12 bootable dual-disk

ASM  = nasm
CC   = gcc
LD   = ld

ASFLAGS     = -f elf
BOOTFLAGS   = -f bin
CFLAGS      = -m32 -c -fno-builtin -ffreestanding -fno-pie -std=gnu99 -I.
LDFLAGS     = -m elf_i386 -T linker.ld

OBJS = head.o kernel.o command.o editor.o cc.o x86gen.o native.o fault.o mem.o syscall.o task.o vga.o kbd.o idt.o fs.o disk_io.o elf.o

BOOT_BIN   = boot.bin
KERNEL_BIN = kernel.bin
A_IMG      = A.img
B_IMG      = B.img
HELLO_ELF  = hello.elf
INP_ELF    = inp.elf

.PHONY: all clean run run-gui run-dual run-dual-gui

all: $(A_IMG)

# ── Boot sector ──
$(BOOT_BIN): boot.asm
	@echo "[BOOT] Compiling..."
	$(ASM) $(BOOTFLAGS) -o $@ $<

# ── Kernel ──
$(KERNEL_BIN): $(OBJS) linker.ld
	@echo "[LD] Linking..."
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# ── A.img: FAT12 system disk (boot+kernel + built-in files) ──
$(A_IMG): $(BOOT_BIN) $(KERNEL_BIN) tcc.elf mka_img.py
	@echo "[IMG] Building A.img..."
	python3 mka_img.py $@

# ── B.img: FAT12 data disk ──
$(B_IMG): mkbimg.py $(HELLO_ELF) $(INP_ELF)
	@echo "[IMG] Building B.img..."
	python3 mkbimg.py $@

# ── TinyCC 交叉编译 (vendor/tinycc -> tcc.elf + libtcc1.a + crt*) ──
tcc.elf: build-tcc.sh
	@echo "[TCC] Cross-building TinyCC..."
	sh build-tcc.sh

# ── 交叉编译 ELF 测试程序 (host gcc -m32; tcc 亦可用同参数) ──
$(HELLO_ELF): hello.c
	@echo "[ELF] Cross-compiling hello.c -> hello.elf"
	gcc -m32 -nostdlib -static -no-pie -fno-pie -fno-pic -fno-builtin \
	    -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables \
	    -Wl,-Ttext=0x100000 -Wl,--build-id=none -o $@ $<

# ── 输入测试程序 (libc 链接, 与 build-tcc.sh 相同链接方式) ──
$(INP_ELF): inp.c libc/libc.a libc/crt0.o
	@echo "[ELF] Building inp.c -> inp.elf (libc-linked)"
	gcc -m32 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
	    -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc -I libc -c inp.c -o inp.o
	LIBGCC=$$(gcc -m32 -print-libgcc-file-name); \
	ld -m elf_i386 -no-pie -T libc/link.ld -nostdlib -static \
	    libc/crt0.o inp.o libc/libc.a $$LIBGCC -o inp.elf

# ── Compile rules ──
%.o: %.c common.h
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -o $@ $<

%.o: %.asm
	@echo "[ASM] $<"
	$(ASM) $(ASFLAGS) -o $@ $<

# ── Run ──
run: $(A_IMG)
	qemu-system-i386 -hda $(A_IMG) -nographic

run-gui: $(A_IMG)
	qemu-system-i386 -hda $(A_IMG)

run-dual: $(A_IMG) $(B_IMG)
	qemu-system-i386 -hda $(A_IMG) -hdb $(B_IMG) -nographic

run-dual-gui: $(A_IMG) $(B_IMG)
	qemu-system-i386 -rtc base=localtime -hda $(A_IMG) -hdb $(B_IMG)

clean:
	rm -f *.o *.bin *.img
	@echo "[CLEAN] Done"
