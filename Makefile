# AMUNOS Makefile — FAT12 bootable multi-disk (A: boot, B:/C: data, v6.5.3)

ASM  = nasm
CC   = gcc
LD   = ld

ASFLAGS     = -f elf
BOOTFLAGS   = -f bin
CFLAGS      = -m32 -c -fno-builtin -ffreestanding -fno-pie -std=gnu99 -I.
LDFLAGS     = -m elf_i386 -T linker.ld

OBJS = head.o kernel.o command.o fault.o mem.o syscall.o task.o vga.o kbd.o idt.o mouse.o fs.o disk_io.o dev.o fdc.o atapi.o iso9660.o ahci.o nic.o elf.o serial.o fb.o

BOOT_BIN   = boot.bin
STAGE2_BIN = stage2.bin
KERNEL_BIN = kernel.bin
A_IMG      = A.img
B_IMG      = B.img
C_IMG      = C.img
HELLO_ELF  = hello.elf
INP_ELF    = inp.elf
EDIT_ELF   = edit.elf
SYSINFO_ELF = sysinfo.elf
DFLAT_ELF = dflat-demo.elf
SOUND_ELF = beep.elf
NET_ELF = net.elf
INSTALL_ELF = sysinstall/sysinstall.elf
INSTALL_IMG = AMUNOS.flp
CRT_OBJS   = libc/crt1.o libc/crti.o libc/crtn.o

.PHONY: all clean run run-gui run-net run-net-gui run-flp-gui run-install install-image run-floppy

all: $(A_IMG)

# ── Boot sector ──
$(BOOT_BIN): boot_stage1.asm
	@echo "[BOOT] Compiling..."
	$(ASM) $(BOOTFLAGS) -o $@ $<

$(STAGE2_BIN): boot_stage2.asm boot.asm
	@echo "[BOOT] Compiling stage2..."
	$(ASM) $(BOOTFLAGS) -o $@ $<

mbr_boot.bin: mbr_boot.asm
	$(ASM) $(BOOTFLAGS) -o $@ $<

# ── Kernel ──
$(KERNEL_BIN): $(OBJS) linker.ld
	@echo "[LD] Linking..."
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# ── A.img: FAT12 system disk (boot+kernel + built-in files) ──
# u2gb.bin (Unicode→GB2312 映射) 是 A.img 的依赖: 若被删/重新生成, A.img 会重建,
# 避免内核 fb_font_init 加载不到 U2GB → UTF-8 汉字全画成 □。
u2gb.bin: gen_u2gb.py
	python3 gen_u2gb.py
# Release image intentionally ships the text stack only. GUI sources and
# the previous build configuration are preserved in the GUI archive.
$(A_IMG): A.vol mbr_boot.bin mkmbr.py
	python3 mkmbr.py A.vol $@ mbr_boot.bin

A.vol: $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN) tcc.elf $(EDIT_ELF) $(SYSINFO_ELF) $(DFLAT_ELF) $(SOUND_ELF) $(NET_ELF) $(INSTALL_ELF) $(CRT_OBJS) mka_img.py u2gb.bin
	@echo "[IMG] Building A FAT boot volume..."
	python3 mka_img.py $@ 2048

A.flp: $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN) tcc.elf $(EDIT_ELF) $(SYSINFO_ELF) $(DFLAT_ELF) $(SOUND_ELF) $(NET_ELF) $(INSTALL_ELF) $(CRT_OBJS) mka_img.py u2gb.bin
	python3 mka_img.py $@ 0

$(INSTALL_IMG): $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_BIN) $(INSTALL_ELF) mbr_boot.bin mka_img.py u2gb.bin
	@echo "[IMG] Building minimal AMUNOS installation floppy..."
	python3 mka_img.py $@ 0 install

install-image: $(INSTALL_IMG)

$(INSTALL_ELF): sysinstall/sysinstall.c libc/libc.a libc/crt0.o
	@echo "[ELF] Building sysinstall/sysinstall.c -> $@"
	gcc -m32 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
	    -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc \
	    -I libc -c sysinstall/sysinstall.c -o sysinstall/sysinstall.o
	LIBGCC=$$(gcc -m32 -print-libgcc-file-name); \
	ld -m elf_i386 -no-pie -T libc/link.ld -nostdlib -static \
	    libc/crt0.o sysinstall/sysinstall.o libc/libc.a $$LIBGCC -o $@
	rm -f sysinstall/sysinstall.o
	@echo "[SYSINSTALL.ELF] size: $$(wc -c < $@) bytes"

# ── B.img: FAT12 data disk (secondary master) ──
$(B_IMG): B.vol mkmbr.py
	python3 mkmbr.py B.vol $@

B.vol: mkbimg.py $(HELLO_ELF) $(INP_ELF) $(EDIT_ELF)
	@echo "[IMG] Building B FAT volume..."
	python3 mkbimg.py $@

# ── C.img: FAT12 data disk (secondary channel, -hdc) ──
$(C_IMG): C.vol mkmbr.py
	python3 mkmbr.py C.vol $@

C.vol: mkcimg.py $(HELLO_ELF)
	@echo "[IMG] Building C FAT volume..."
	python3 mkcimg.py $@

# ── TinyCC 交叉编译 (vendor/tinycc -> tcc.elf + libtcc1.a + crt*) ──
tcc.elf: build-tcc.sh
	@echo "[TCC] Cross-building TinyCC..."
	sh build-tcc.sh

libc/crt1.o: libc/crt0.o
	cp libc/crt0.o libc/crt1.o

libc/crti.o:
	echo '.section .init' | as --32 -o libc/crti.o

libc/crtn.o:
	echo '.section .fini' | as --32 -o libc/crtn.o

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

# ── 编辑器 (用户态 ELF, v6.6): FreeDOS Edit 0.7d 移植品, 放入 A:/B: 盘 ──
EDIT_SRCS = $(wildcard edit-fdos/source/*.c)
$(EDIT_ELF): $(EDIT_SRCS) libc/libc.a libc/crt0.o
	@echo "[ELF] Building edit-fdos/source -> edit.elf (libc-linked)"
	rm -rf .edit-obj && mkdir -p .edit-obj
	for f in $(EDIT_SRCS); do \
	  gcc -m32 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
	      -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc \
	      -I libc -I edit-fdos/source -funsigned-char -c $$f \
	      -o .edit-obj/$$(basename $$f .c).o || exit 1; \
	done
	LIBGCC=$$(gcc -m32 -print-libgcc-file-name); \
	ld -m elf_i386 -no-pie -T libc/link.ld -nostdlib -static \
	    libc/crt0.o .edit-obj/*.o libc/libc.a $$LIBGCC -o edit.elf
	rm -rf .edit-obj
	@echo "[EDIT.ELF] size: $$(wc -c < edit.elf) bytes"

# ── DFLAT 控件展示 (复用 EDIT 的 DFLAT 控件/消息循环, 独立入口) ──
DFLAT_SRCS = $(filter-out edit-fdos/source/edit.c,$(EDIT_SRCS))
DFLAT_OBJS = $(addprefix .dflat-obj/,$(notdir $(DFLAT_SRCS:.c=.o))) .dflat-obj/demo.o
$(DFLAT_ELF): edit-fdos/demo.c $(DFLAT_SRCS) libc/libc.a libc/crt0.o
	@echo "[ELF] Building DFLAT 0.1 control showcase -> dflat-demo.elf"
	mkdir -p .dflat-obj
	for f in $(DFLAT_SRCS); do \
	  gcc -m32 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
	      -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc \
	      -I libc -I edit-fdos/source -funsigned-char -c $$f \
	      -o .dflat-obj/$$(basename $$f .c).o || exit 1; \
	done
	gcc -m32 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
	    -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc \
	    -I libc -I edit-fdos/source -funsigned-char -c edit-fdos/demo.c \
	    -o .dflat-obj/demo.o
	LIBGCC=$$(gcc -m32 -print-libgcc-file-name); \
	ld -m elf_i386 -no-pie -T libc/link.ld -nostdlib -static \
	    libc/crt0.o $(DFLAT_OBJS) libc/libc.a $$LIBGCC -o $@
	rm -f $(DFLAT_OBJS)
	rmdir .dflat-obj 2>/dev/null || true
	@echo "[DFLAT.ELF] size: $$(wc -c < $@) bytes"

$(SYSINFO_ELF): sysinfo.c libc/libc.a libc/crt0.o
	@echo "[ELF] Building sysinfo.c -> sysinfo.elf (libc-linked)"
	gcc -m32 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
	    -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc \
	    -I libc -c sysinfo.c -o sysinfo.o
	LIBGCC=$$(gcc -m32 -print-libgcc-file-name); \
	ld -m elf_i386 -no-pie -T libc/link.ld -nostdlib -static \
	    libc/crt0.o sysinfo.o libc/libc.a $$LIBGCC -o sysinfo.elf
	@echo "[SYSINFO.ELF] size: $$(wc -c < sysinfo.elf) bytes"

$(SOUND_ELF): beep.c libc/libc.a libc/crt0.o
	@echo "[ELF] Building beep.elf"
	gcc -m32 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
	    -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc \
	    -I libc -c beep.c -o beep.o
	LIBGCC=$$(gcc -m32 -print-libgcc-file-name); \
	ld -m elf_i386 -no-pie -T libc/link.ld -nostdlib -static \
	    libc/crt0.o beep.o libc/libc.a $$LIBGCC -o $@

$(NET_ELF): net.c libc/libc.a libc/crt0.o
	@echo "[ELF] Building net.elf"
	gcc -m32 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
	    -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc \
	    -I libc -c net.c -o net.o
	LIBGCC=$$(gcc -m32 -print-libgcc-file-name); \
	ld -m elf_i386 -no-pie -T libc/link.ld -nostdlib -static \
	    libc/crt0.o net.o libc/libc.a $$LIBGCC -o $@

# ── Compile rules ──
%.o: %.c common.h
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -o $@ $<

%.o: %.asm
	@echo "[ASM] $<"
	$(ASM) $(ASFLAGS) -o $@ $<

# ── Run ──
run: $(A_IMG)
	qemu-system-i386 -drive file=$(A_IMG),format=raw,if=ide,index=0 -nographic

# 图形模式：硬盘启动，A/B/C 均使用 raw 镜像。
run-gui: $(A_IMG) $(B_IMG) $(C_IMG)
	qemu-system-i386 -drive file=$(A_IMG),format=raw,if=ide,index=0 \
	  -drive file=$(B_IMG),format=raw,if=ide,index=1 \
	  -drive file=$(C_IMG),format=raw,if=ide,index=2 \
	  -audiodev driver=sdl,id=audio0 -machine pc,pcspk-audiodev=audio0 -display gtk

# 网络模式：硬盘启动并挂载 B:/C: 数据盘。
run-net: $(A_IMG) $(B_IMG) $(C_IMG)
	qemu-system-i386 -drive file=$(A_IMG),format=raw,if=ide,index=0 \
	  -drive file=$(B_IMG),format=raw,if=ide,index=1 \
	  -drive file=$(C_IMG),format=raw,if=ide,index=2 \
	  -netdev user,id=n0 -device rtl8139,netdev=n0 \
	  -audiodev driver=sdl,id=audio0 -machine pc,pcspk-audiodev=audio0 -nographic

# 网络 + GUI：硬盘启动并挂载 B:/C: 数据盘。
run-net-gui: $(A_IMG) $(B_IMG) $(C_IMG)
	qemu-system-i386 -drive file=$(A_IMG),format=raw,if=ide,index=0 \
	  -drive file=$(B_IMG),format=raw,if=ide,index=1 \
	  -drive file=$(C_IMG),format=raw,if=ide,index=2 \
	  -netdev user,id=n0 -device rtl8139,netdev=n0 \
	  -audiodev driver=sdl,id=audio0 -machine pc,pcspk-audiodev=audio0 -display gtk

# 网络 + GUI：从 A.flp 软盘启动，B:/C: 作为 IDE 数据盘。
run-flp-gui: A.flp $(B_IMG) $(C_IMG)
	qemu-system-i386 -drive file=A.flp,format=raw,if=floppy,index=0 -boot a \
	  -drive file=$(B_IMG),format=raw,if=ide,index=0 \
	  -drive file=$(C_IMG),format=raw,if=ide,index=1 \
	  -netdev user,id=n0 -device rtl8139,netdev=n0 \
	  -audiodev driver=sdl,id=audio0 -machine pc,pcspk-audiodev=audio0 -display gtk

# 从安装软盘启动，B.img 作为可写目标硬盘。
run-install: $(INSTALL_IMG) $(B_IMG)
	qemu-system-i386 -drive file=$(INSTALL_IMG),format=raw,if=floppy,index=0 -boot a \
	  -drive file=$(B_IMG),format=raw,if=ide,index=0 \
	  -display gtk

# 软盘启动，IDE 上挂载 B:/C: 数据盘。
run-floppy: A.flp $(B_IMG) $(C_IMG)
	qemu-system-i386 -rtc base=localtime -fda A.flp -boot a -hda $(B_IMG) -hdb $(C_IMG)

clean:
	rm -f *.o *.bin *.img *.vol
	@echo "[CLEAN] Done"
