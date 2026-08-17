# AMUNOS Makefile — FAT12 bootable multi-disk (A: boot, B:/C: data, v6.5.1)

ASM  = nasm
CC   = gcc
LD   = ld

ASFLAGS     = -f elf
BOOTFLAGS   = -f bin
CFLAGS      = -m32 -c -fno-builtin -ffreestanding -fno-pie -std=gnu99 -I.
LDFLAGS     = -m elf_i386 -T linker.ld

OBJS = head.o kernel.o command.o fault.o mem.o syscall.o task.o vga.o kbd.o idt.o fs.o disk_io.o elf.o serial.o

BOOT_BIN   = boot.bin
KERNEL_BIN = kernel.bin
A_IMG      = A.img
B_IMG      = B.img
C_IMG      = C.img
HELLO_ELF  = hello.elf
INP_ELF    = inp.elf
EDIT_ELF   = edit.elf

.PHONY: all clean run run-gui run-dual run-dual-gui run-serial \
        run-trio run-trio-gui run-trio-serial

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
$(A_IMG): $(BOOT_BIN) $(KERNEL_BIN) tcc.elf $(EDIT_ELF) mka_img.py
	@echo "[IMG] Building A.img..."
	python3 mka_img.py $@

# ── B.img: FAT12 data disk (secondary master) ──
$(B_IMG): mkbimg.py $(HELLO_ELF) $(INP_ELF) $(EDIT_ELF)
	@echo "[IMG] Building B.img..."
	python3 mkbimg.py $@

# ── C.img: FAT12 data disk (secondary channel, -hdc) ──
$(C_IMG): mkcimg.py $(HELLO_ELF)
	@echo "[IMG] Building C.img..."
	python3 mkcimg.py $@

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

# ── 编辑器 (用户态 ELF, v6.5): 从内核移除后独立成 EDIT.ELF, 放入 A:/B: 盘 ──
$(EDIT_ELF): edit.c libc/libc.a libc/crt0.o
	@echo "[ELF] Building edit.c -> edit.elf (libc-linked)"
	gcc -m32 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
	    -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc -I libc -c edit.c -o edit.o
	LIBGCC=$$(gcc -m32 -print-libgcc-file-name); \
	ld -m elf_i386 -no-pie -T libc/link.ld -nostdlib -static \
	    libc/crt0.o edit.o libc/libc.a $$LIBGCC -o edit.elf

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

# ── 串口远程控制台运行 (v6.5): 交互式串口, 另开终端 ./serial-console.sh 连接 ──
run-serial: $(A_IMG) $(B_IMG)
	qemu-system-i386 -nographic -hda $(A_IMG) -hdb $(B_IMG) \
	  -monitor telnet:127.0.0.1:45454,server,nowait \
	  -serial tcp:127.0.0.1:5555,server,nowait \
	  -parallel file:lpt.log

# ── 三盘启动 (v6.5.1): A: 引导 (-hda) + B: (-hdb) + C: 次通道 (-hdc) ──
run-trio: $(A_IMG) $(B_IMG) $(C_IMG)
	qemu-system-i386 -rtc base=localtime -hda $(A_IMG) -hdb $(B_IMG) -hdc $(C_IMG) -nographic

run-trio-gui: $(A_IMG) $(B_IMG) $(C_IMG)
	qemu-system-i386 -rtc base=localtime -hda $(A_IMG) -hdb $(B_IMG) -hdc $(C_IMG)

run-trio-serial: $(A_IMG) $(B_IMG) $(C_IMG)
	qemu-system-i386 -nographic -rtc base=localtime -hda $(A_IMG) -hdb $(B_IMG) -hdc $(C_IMG) \
	  -monitor telnet:127.0.0.1:45454,server,nowait \
	  -serial tcp:127.0.0.1:5555,server,nowait \
	  -parallel file:lpt.log

clean:
	rm -f *.o *.bin *.img
	@echo "[CLEAN] Done"
