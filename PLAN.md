# AMUNOS 补全计划

## 参考来源
`D:\Documents\flash-4th-os` — 一个完整的 Linux 0.1x 风格内核

## 目标
用 WSL 编译 AMUNOS，用 QEMU 跑起来，修复 FAT12 硬编码问题。

---

## 第一步：WSL 环境安装

```bash
sudo apt install nasm gcc-multilib binutils qemu-system-x86 make
```

验证：
```bash
nasm -v
gcc -m32 -c -x c /dev/null -o /dev/null 2>&1
qemu-system-i386 --version
```

---

## 第二步：新建缺失的源文件

### 2.1 `vga.c` + `vga.h` — 屏幕输出
参考：`flash-4th-os/debug/dprintk.c`
- `put_char(char c, char color)` — 写 VGA 内存 0xB8000
- `put_str(char *s)` — 循环调用 put_char
- `cls()` — 用空格填满 80x25，重置 x,y
- `update_cursor()` — 通过 0x3D4/0x3D5 端口设置光标

### 2.2 `kbd.c` — 键盘中断处理
参考：`flash-4th-os/kernel/keyboard.c`
- 键盘 scancode 映射表 (keymap[][2])
- `keyboard_handler()` — 中断处理函数
- shift / caps_lock 状态跟踪
- 设置 `key_pressed` 和 `current_char` 全局变量

### 2.3 `idt.c` + `intr.asm` — 中断描述符表
参考：`flash-4th-os/kernel/trap.c` + `flash-4th-os/kernel/intr.s` + `flash-4th-os/include/asm/system.h`
- `init_idt()` — 初始化 256 个 IDT 门
- 键盘中断入口 (IRQ1 → 中断向量 0x21)
- 通用中断桩 (push 上下文 → call C 函数 → pop → iretd)

### 2.4 `linker.ld` — 链接脚本
```ld
OUTPUT_FORMAT("binary")
ENTRY(_start)
SECTIONS {
    . = 0x7C00;
    .text : { *(.text) }
    .data : { *(.data) }
    .bss  : { *(.bss) }
}
```

---

## 第三步：创建 Makefile

参考：`flash-4th-os/Makefile` + `flash-4th-os/Rules.make`

```makefile
# WSL 适配版
ASM = nasm
CC  = gcc
LD  = ld

ASFLAGS = -f elf -I include/
CFLAGS  = -m32 -c -fno-builtin -I include/
LDFLAGS = -T linker.ld

OBJS = head.o kernel.o command.o fs.o disk_io.o vga.o kbd.o idt.o intr.o

all: kernel.bin

kernel.bin: $(OBJS)
    $(LD) $(LDFLAGS) -o kernel.bin.large $(OBJS)
    objcopy -O binary kernel.bin.large kernel.bin

%.o: %.asm
    $(ASM) $(ASFLAGS) -o $@ $<

%.o: %.c
    $(CC) $(CFLAGS) -o $@ $<

run: kernel.bin
    qemu-system-i386 -drive file=kernel.bin,format=raw

clean:
    rm -f *.o *.bin *.bin.large
```

---

## 第四步：修复 FAT12 硬编码问题

| 文件 | 问题 | 修复 |
|------|------|------|
| `command.c:32` | `19 + s` 写死 | 改为 `fs_root_lba + s` |
| `command.c:23` | `sectors = 14` 写死 | 改为 `(fs_root_entries * 32 + 511) / 512` |
| `fs.c:104-108` | `fs_sync` FAT 位置写死 1/10 | 使用 `fs_fat_lba` |

---

## 第五步：创建磁盘镜像

需要 bootsect + kernel 合并成一个 .img：
```bash
# 1. 编译 bootsect.bin (512 字节，含 BPB 和 0xAA55)
nasm -I include/ -o boot/bootsect.bin boot/bootsect.s

# 2. 编译内核
make

# 3. dd 合并
dd if=boot/bootsect.bin of=disk.img bs=512 count=1
dd if=kernel.bin of=disk.img bs=512 seek=1

# 4. QEMU 运行
qemu-system-i386 -fda disk.img
```

---

## 文件结构预览

```
OSDev/
├── Makefile
├── linker.ld
├── common.h          (已有，扩展)
├── head.asm          (已有)
├── kernel.c          (已有)
├── command.c         (已有，修复 LBA)
├── fs.c              (已有，修复 fs_sync)
├── disk_io.asm       (已有)
├── vga.c             (新建)
├── vga.h             (新建)
├── kbd.c             (新建)
├── kbd.h             (新建)
├── idt.c             (新建)
├── idt.h             (新建)
├── intr.asm          (新建)
├── boot/
│   └── bootsect.s    (新建，从 flash-4th-os 适配)
└── include/
    └── (可选，整理头文件)
```

---

## 预计工作量

1. 环境安装 — 5 分钟
2. 写 vga.c/kbd.c/idt.c/intr.asm — 3 个文件，每个 ~100 行
3. 写 Makefile + linker.ld — 两个短文件
4. 修复 FAT12 LBA — 3 处改动
5. 适配 bootsect.s — 把 flash-4th-os 的引导扇区简化适配
6. 调试 — 看 QEMU 输出修 bug

总计：**半天以内**
