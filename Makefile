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

.PHONY: all clean run run-gui run-net run-net-gui run-flp-gui run-install run_install install-image run-dual run-serial run-trio run-trio-serial run-floppy floppy storage-test-images

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

# 图形模式：默认从 A.img 硬盘启动，启动扇区会请求 VBE 640x480x16bpp。
run-gui: $(A_IMG) $(B_IMG) $(C_IMG)
	qemu-system-i386 -drive file=$(A_IMG),format=raw,if=ide,index=0 \
	  -drive file=$(B_IMG),format=raw,if=ide,index=1 \
	  -drive file=$(C_IMG),format=raw,if=ide,index=2 \
	  -audiodev driver=sdl,id=audio0 -machine pc,pcspk-audiodev=audio0 -display gtk

# 网络模式：默认从 A.img 硬盘启动，B:/C: 作为后续 IDE 数据盘。
run-net: $(A_IMG) $(B_IMG) $(C_IMG)
	qemu-system-i386 -drive file=$(A_IMG),format=raw,if=ide,index=0 \
	  -drive file=$(B_IMG),format=raw,if=ide,index=1 \
	  -drive file=$(C_IMG),format=raw,if=ide,index=2 \
	  -netdev user,id=n0 -device rtl8139,netdev=n0 \
	  -audiodev driver=sdl,id=audio0 -machine pc,pcspk-audiodev=audio0 -nographic

# 网络 + GUI：从 A.img 硬盘启动，B:/C: 作为后续 IDE 数据盘。
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

# 安装盘 GUI 调试：从软盘启动安装程序，B.img 作为可写目标硬盘。
run-install: $(INSTALL_IMG) $(B_IMG)
	qemu-system-i386 -drive file=$(INSTALL_IMG),format=raw,if=floppy,index=0 -boot a \
	  -drive file=$(B_IMG),format=raw,if=ide,index=0 \
	  -display gtk

# 兼容旧的下划线写法。
run_install: run-install

run-dual: $(A_IMG) $(B_IMG)
	qemu-system-i386 -hda $(A_IMG) -hdb $(B_IMG) -nographic

# ── 串口远程控制台运行 (v6.5): 交互式串口, 另开终端 ./serial-console.sh 连接 ──
run-serial: $(A_IMG) $(B_IMG)
	qemu-system-i386 -nographic -hda $(A_IMG) -hdb $(B_IMG) \
	  -monitor telnet:127.0.0.1:45454,server,nowait \
	  -serial tcp:127.0.0.1:5555,server,nowait \
	  -parallel file:lpt.log

# ── 三盘启动 (v6.5.1): A: 引导 (-hda) + B: (-hdb) + C: 次通道 (-hdc) ──
run-trio: $(A_IMG) $(B_IMG) $(C_IMG)
	qemu-system-i386 -rtc base=localtime -hda $(A_IMG) -hdb $(B_IMG) -hdc $(C_IMG) -nographic

# v6.7: 去掉 -show-cursor — 鼠标指针由内核软件叠加 '█' 绘制
run-trio-serial: $(A_IMG) $(B_IMG) $(C_IMG)
	qemu-system-i386 -nographic -rtc base=localtime -hda $(A_IMG) -hdb $(B_IMG) -hdc $(C_IMG) \
	  -monitor telnet:127.0.0.1:45454,server,nowait \
	  -serial tcp:127.0.0.1:5555,server,nowait \
	  -parallel file:lpt.log

# ── FAT32 数据盘 (v6.5.6 P2): D32.img (36MB FAT32) 挂次从盘 D: ──
D32_IMG = D32.img
$(D32_IMG): mkd32.py
	@echo "[IMG] Building FAT32 D32.img..."
	python3 mkd32.py $(D32_IMG)

MBR_IMG = MBR.img
$(MBR_IMG): C.img mkmbr.py
	cp C.img $(MBR_IMG)

storage-test-images: $(A_IMG) $(B_IMG) $(C_IMG) $(D32_IMG) $(CD_IMG) $(MBR_IMG)

run-fat32: $(A_IMG) $(B_IMG) $(C_IMG) $(D32_IMG)
	qemu-system-i386 -nographic -rtc base=localtime -hda $(A_IMG) -hdb $(B_IMG) -hdc $(C_IMG) -hdd $(D32_IMG) \
	  -monitor unix:/tmp/mon.sock,server,nowait \
	  -serial file:/tmp/ser.log -display none

# ── 软盘引导: A.img 作软盘 (-fda) 启动, IDE 上挂 B:/C: 数据盘。
#    FDC 运行时支持常见 FAT 软盘几何的读写；CHS 分块引导路径在此模式生效。
run-floppy: A.flp $(B_IMG) $(C_IMG)
	qemu-system-i386 -rtc base=localtime -fda A.flp -boot a -hda $(B_IMG) -hdb $(C_IMG)

# floppy: A.img 本身即 1.44MB 软盘几何, 可直接写物理软盘 (Linux):
#   dd if=A.img of=/dev/fd0 bs=512 conv=notrunc
floppy: $(A_IMG)
	@echo "A.img is 1.44MB floppy geometry. Write to real disk with:"
	@echo "  dd if=A.img of=/dev/fd0 bs=512 conv=notrunc"

clean:
	rm -f *.o *.bin *.img *.vol
	@echo "[CLEAN] Done"

# ── ATAPI 光驱 (v6.5.6 P3): CD.iso 挂 -cdrom (IDE1 从属, CD0), ISO9660 只读 ──
CD_IMG = CD.iso
$(CD_IMG): mkiso.py
	@echo "[IMG] Building ISO9660 $(CD_IMG)..."
	python3 mkiso.py $(CD_IMG)

run-cdrom: $(A_IMG) $(B_IMG) $(C_IMG) $(D32_IMG) $(CD_IMG)
	qemu-system-i386 -nographic -rtc base=localtime -hda $(A_IMG) -hdb $(B_IMG) -hdc $(C_IMG) -hdd $(D32_IMG) 	  -cdrom $(CD_IMG) 	  -monitor unix:/tmp/mon.sock,server,nowait 	  -serial file:/tmp/ser.log -display none

# ── AHCI SATA (v6.5.6 P4): q35 + ich9-ahci, D32.img 挂 AHCI (SA0) ──
run-ahci: $(A_IMG) $(B_IMG) $(C_IMG) $(D32_IMG)
	qemu-system-i386 -nographic -rtc base=localtime -M q35 -device ich9-ahci,id=ahci \
	  -hda $(A_IMG) -hdb $(B_IMG) -hdc $(C_IMG) \
	  -drive if=none,id=s0,file=$(D32_IMG),format=raw \
	  -device ide-hd,drive=s0,bus=ahci.0 \
	  -monitor unix:/tmp/mon.sock,server,nowait \
	  -serial file:/tmp/ser.log -display none
