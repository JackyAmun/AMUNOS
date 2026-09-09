# AMUNOS Makefile — FAT12 bootable multi-disk (A: boot, B:/C: data, v6.5.3)

ASM  = nasm
CC   = gcc
LD   = ld

ASFLAGS     = -f elf
BOOTFLAGS   = -f bin
CFLAGS      = -m32 -c -fno-builtin -ffreestanding -fno-pie -std=gnu99 -I.
LDFLAGS     = -m elf_i386 -T linker.ld

OBJS = head.o kernel.o command.o fault.o mem.o syscall.o task.o vga.o kbd.o idt.o mouse.o fs.o disk_io.o dev.o fdc.o atapi.o iso9660.o ahci.o elf.o serial.o fb.o gui.o

BOOT_BIN   = boot.bin
KERNEL_BIN = kernel.bin
A_IMG      = A.img
B_IMG      = B.img
C_IMG      = C.img
HELLO_ELF  = hello.elf
INP_ELF    = inp.elf
EDIT_ELF   = edit.elf
GUI_ELF    = gui-demo.elf
SYSINFO_ELF = sysinfo.elf
WRITE_ELF = write.elf
CRT_OBJS   = libc/crt1.o libc/crti.o libc/crtn.o

.PHONY: all clean run run-gui run-dual run-dual-gui run-serial \
        run-trio run-trio-gui run-trio-serial run-floppy floppy

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
# u2gb.bin (Unicode→GB2312 映射) 是 A.img 的依赖: 若被删/重新生成, A.img 会重建,
# 避免内核 fb_font_init 加载不到 U2GB → UTF-8 汉字全画成 □。
u2gb.bin: gen_u2gb.py
	python3 gen_u2gb.py
$(A_IMG): $(BOOT_BIN) $(KERNEL_BIN) tcc.elf $(EDIT_ELF) $(GUI_ELF) $(SYSINFO_ELF) $(WRITE_ELF) $(CRT_OBJS) mka_img.py u2gb.bin
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

# ── GUI 控件库演示 (v6.9, 用户态, 内核图形 syscall 28-43) ──
$(GUI_ELF): gui/gui-demo.c libc/libc.a libc/crt0.o
	@echo "[ELF] Building gui/gui-demo.c -> gui-demo.elf (libc-linked)"
	rm -rf .gui-obj && mkdir -p .gui-obj
	gcc -m32 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
	    -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc \
	    -I libc -c gui/gui-demo.c -o .gui-obj/gui-demo.o
	LIBGCC=$$(gcc -m32 -print-libgcc-file-name); \
	ld -m elf_i386 -no-pie -T libc/link.ld -nostdlib -static \
	    libc/crt0.o .gui-obj/gui-demo.o libc/libc.a $$LIBGCC -o gui-demo.elf
	rm -rf .gui-obj
	@echo "[GUI.ELF] size: $$(wc -c < gui-demo.elf) bytes"

$(SYSINFO_ELF): sysinfo.c libc/libc.a libc/crt0.o
	@echo "[ELF] Building sysinfo.c -> sysinfo.elf (libc-linked)"
	gcc -m32 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
	    -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc \
	    -I libc -c sysinfo.c -o sysinfo.o
	LIBGCC=$$(gcc -m32 -print-libgcc-file-name); \
	ld -m elf_i386 -no-pie -T libc/link.ld -nostdlib -static \
	    libc/crt0.o sysinfo.o libc/libc.a $$LIBGCC -o sysinfo.elf
	@echo "[SYSINFO.ELF] size: $$(wc -c < sysinfo.elf) bytes"

$(WRITE_ELF): write.c libc/libc.a libc/crt0.o
	@echo "[ELF] Building write.c -> write.elf (libc-linked)"
	gcc -m32 -ffreestanding -fno-builtin -fno-pie -fno-stack-protector \
	    -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdinc \
	    -I libc -c write.c -o write.o
	LIBGCC=$$(gcc -m32 -print-libgcc-file-name); \
	ld -m elf_i386 -no-pie -T libc/link.ld -nostdlib -static \
	    libc/crt0.o write.o libc/libc.a $$LIBGCC -o write.elf
	@echo "[WRITE.ELF] size: $$(wc -c < write.elf) bytes"

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

# v6.7: 去掉 -show-cursor — 鼠标指针由内核软件叠加 '█' 绘制
run-trio-gui: $(A_IMG) $(B_IMG) $(C_IMG)
	qemu-system-i386 -rtc base=localtime -hda $(A_IMG) -hdb $(B_IMG) -hdc $(C_IMG)

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

run-fat32: $(A_IMG) $(B_IMG) $(C_IMG) $(D32_IMG)
	qemu-system-i386 -nographic -rtc base=localtime -hda $(A_IMG) -hdb $(B_IMG) -hdc $(C_IMG) -hdd $(D32_IMG) \
	  -monitor unix:/tmp/mon.sock,server,nowait \
	  -serial file:/tmp/ser.log -display none

# ── 软盘引导 (v6.5.6 阶段A): A.img 作软盘 (-fda) 启动, IDE 上挂 B:/C: 数据盘。
#    注意: 内核无 FDC 驱动, 运行时读不了 A: 自身 (无 TCC/BIN) — 数据与字库
#    由 B:/C: 提供 (fb_font_init 跨盘搜索)。CHS 分块引导路径在此模式生效。
run-floppy: $(A_IMG) $(B_IMG) $(C_IMG)
	qemu-system-i386 -rtc base=localtime -fda $(A_IMG) -boot a -hda $(B_IMG) -hdb $(C_IMG)

# floppy: A.img 本身即 1.44MB 软盘几何, 可直接写物理软盘 (Linux):
#   dd if=A.img of=/dev/fd0 bs=512 conv=notrunc
floppy: $(A_IMG)
	@echo "A.img is 1.44MB floppy geometry. Write to real disk with:"
	@echo "  dd if=A.img of=/dev/fd0 bs=512 conv=notrunc"

clean:
	rm -f *.o *.bin *.img
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
