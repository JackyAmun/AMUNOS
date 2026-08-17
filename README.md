# AMUNOS — 一个 x86 32 位自举操作系统

> 爱好项目 · 从零编写 · 内核版本 **v6.5（ELF Exec + 串口/并口）**

AMUNOS 是一个运行在 **x86 32 位保护模式**下的迷你操作系统，从一个 FAT12 引导扇区
启动。它的独特之处在于**内置了 TinyCC 编译器与 minilibc**——你可以在 OS 的 shell 里
直接编写、编译、运行标准 C 程序，形成"自举"闭环，不必离开系统就能开发新程序。

```
写 HELLO.C  →  TCC HELLO.C -o HELLO.EXE  →  ELF HELLO.EXE
```

---

## 功能特性

- **引导**：FAT12 引导扇区（boot.asm）→ 加载内核 → 进入 32 位保护模式（无分页，VA==PA）。
- **多任务**：PIT 100Hz 抢占式轮转调度（task.c），后台演示任务证明并发运行。
- **可执行文件**：标准 ELF32 加载器（elf.c），加载 PT_LOAD 段到 `0x100000` 并跳转 `e_entry`。
- **编译器**：内置 **TinyCC 0.9.27**（tcc.elf），可把 C 源码编译成 AMUNOS 能运行的静态 ELF。
- **标准库**：minilibc（libc/）— `printf`/`snprintf`/`malloc`/`string`/文件 I/O 等，
  供 TCC 与用户程序链接。
- **文件系统**：FAT12，双盘 A:（系统盘）+ B:（数据盘），**两级目录树已落地**：
  A:\BOOT（引导/内核副本）、A:\BIN（TCC.ELF/EDIT.ELF）、A:\USR\LIB、A:\USR\INCLUDE、
  A:\USR\SRC、B:\USR\SRC；支持子目录 + **路径遍历**
  （`TYPE USR\SRC\HELLO.C`、`COPY S\A.TXT D\B.TXT`、`DEL SUB\F.TXT`、`CD USR\SRC` 等，命令大小写不敏感）。
- **Shell**：命令行 REPL，`DIR -P` 分页、`ELF`、`TCC`、行编辑（←→/Home/End/Del）、
  任意命令加 `/?`（或 `-?`）显示用法。
- **TCC 计时**：`TCC file.c` 显示 `Compiling ...` 与 `TCC done (N.Ns)` 编译耗时。
- **编辑器**：**用户态程序**（edit.c 交叉编译成 EDIT.ELF 放文件系统，内核不再内置）。
  FreeDOS EDIT 风格功能键——F1=帮助 F2=保存 F3=打开 F4=新建 F5=退出，支持路径。
- **按名运行**：当前目录下存在 `XXX.ELF` 时，直接输入 `XXX` 即可运行（自动补 `.ELF`）。
- **命令对照表**：每盘根目录 `CMDS.TXT`，行格式 `命令名 目标ELF`，命令名映射到任意
  可执行文件；**表优先于按名运行**。用 `EDIT CMDS.TXT` 改后即时生效，可自定义命令。
- **强制终止**：全局 Ctrl+C，可中断死循环的前台程序回到提示符。
- **异常处理**：IDT 异常桩 + `*** FAULT ***` 兜底显示。
- **串口/并口**：COM1 UART + LPT1 轮询驱动（serial.c），`SER text`/`LPT text` 命令。
  屏幕输出镜像到 COM1，且键盘与串口 RX 统一为 `input_poll()`——**串口就是远程控制台**。
- **远程控制台**：`make run-serial` 起 QEMU（串口监听 `tcp:127.0.0.1:5555`），另开终端
  `./serial-console.sh` 即可完全用串口输入控制 shell / 编辑器 / 程序 stdin（Ctrl+C 可用）。
  `-parallel file:lpt.log` 记录并口输出。

## 快速开始（WSL 中）

构建工具链全部在 WSL：`nasm`、`gcc-multilib`、`ld`、`python3`、`qemu-system-i386`。

```bash
# 进入项目
cd /mnt/c/Users/XU/Desktop/OSDev

# 全量构建 A.img（boot + kernel + 内置文件）
make all

# 构建 B.img（数据盘 + 示例 ELF）
make B.img

# 双盘图形界面运行
make run-dual-gui
```

> QEMU 只在 WSL 里可用（`/usr/bin/qemu-system-i386`）；Windows 侧的 `python3` 是
> Store 桩，构建/测试一律走 WSL。

## 项目结构

```
OSDev/
├── boot.asm         引导扇区（FAT12 BPB + 加载内核）
├── head.asm         32 位入口 + 中断桩（键盘/系统调用/定时器/异常）
├── kernel.c         kmain + shell REPL + 演示任务
├── command.c        shell 命令（DIR -P / ELF / TCC ...）
├── fs.c             FAT12 文件系统
├── disk_io.asm      磁盘 I/O
├── vga.c            VGA 文本输出（0xB8000）
├── kbd.c            PS/2 键盘（scancode → ASCII）
├── idt.c            IDT 初始化
├── task.c           抢占式多任务调度
├── mem.c            内核堆分配器（0x400000 起 4MB）
├── syscall.c        int 0x30 分发 + fd 表
├── elf.c            ELF32 加载器
├── fault.c          CPU 异常处理
├── edit.c            用户态文本编辑器源码（交叉编译为 EDIT.ELF 放文件系统）
├── serial.c         COM1 串口 + LPT1 并口驱动（远程控制台）
├── linker.ld        内核链接脚本
├── Makefile         构建入口
├── mka_img.py       A.img 镜像构建器（内置文件）
├── mkbimg.py        B.img 镜像构建器
├── build-tcc.sh     交叉编译 TinyCC
├── build-libc.sh    构建 minilibc
├── libc/            用户态标准库（stdio/stdlib/string/malloc/...）
├── makar/           参考 OS（借用其 libc 与 TCC 配方）
├── mikanos/ kernel-master/   其它参考资料
└── README.md        本文件
```

## 磁盘目录结构（v6.5 落地）

```
A:\（系统盘，TCC 编译在 A: 下进行）
├─ BOOT\             BOOT.BIN, KERNEL.BIN（副本）
├─ BIN\              TCC.ELF, EDIT.ELF（系统可执行程序）
├─ USR\
│  ├─ INCLUDE\       TCC 内置头 + libc 头（STDIO.H, STDLIB.H ...）
│  ├─ LIB\           LIBC.A, LIBTCC1.A（TCC 链接库）
│  └─ SRC\           HELLO.C, INP.C（示例源码）
├─ CRT1.O CRTI.O CRTN.O   TCC crt 文件（TCC 只在当前目录找，故留根）
└─ CMDS.TXT          命令→ELF 对照表（EDIT → \BIN\EDIT.ELF）

B:\（数据盘，可写）
├─ HELLO.ELF INP.ELF EDIT.ELF   用户 ELF（当前目录下有 XXX.ELF，输入 XXX 即运行）
├─ USR\SRC\          HW.C, RETVAL.C, COUNT5.C ...（C 测试源码）
└─ CMDS.TXT          命令→ELF 对照表
```

`TCC` 命令自动注入 `-I USR\INCLUDE -L USR\LIB -B USR\LIB` 定位头/库，并把编译器
指向 `BIN\TCC.ELF`；在 A: 根下 `TCC USR\SRC\HELLO.C -o HELLO.EXE` 即可编译。

## 内存布局

| 地址 | 用途 |
|------|------|
| `0x8000 .. ~0x15000` | 内核（<52KB，受 boot 104 扇区硬限制） |
| `0x14000 .. 0x90000` | 内核栈（`esp=0x90000`，向下生长） |
| `0x70000`            | FAT 缓存 |
| `0x100000 .. 0x160000` | ELF 载入地址（tcc.elf ~300KB） |
| `0x1F0000`           | argv 块（argc/argv） |
| `0x200000 .. 0x400000` | 用户 brk 堆（2MB，libc malloc 用） |
| `0x400000 .. 0x800000` | 内核堆（mem.c，4MB，fd 缓冲/ELF 暂存） |

## 系统调用（int 0x30）

ABI：`eax=调用号, ebx=arg1, ecx=arg2, edx=arg3`，返回值在 EAX。

| 号 | 函数 | 说明 |
|----|------|------|
| 1  | putchar | 输出字符 |
| 2  | getchar | 读一个字符（阻塞） |
| 3  | puts    | 输出字符串 |
| 4  | putnum  | 输出数字 |
| 5  | malloc  | 内核堆分配 |
| 6  | free    | 内核堆释放 |
| 7  | sleep   | 睡眠（tick） |
| 8  | open    | 打开文件 → fd |
| 9  | close   | 关闭（脏则落盘） |
| 10 | read    | 读（fd 0=控制台行缓冲） |
| 11 | write   | 写（fd 1/2=控制台） |
| 12 | lseek   | 定位 |
| 13 | exit    | 结束当前程序任务（返回码） |
| 14 | brk     | 用户堆断点（0=查询） |
| 15 | getkey  | 原始键码（无回显阻塞读；32-126 可打印、128-131 方向键、134-138 F1-F5） |

fd 表：`0=stdin 1=stdout 2=stderr`（控制台），`3+ = 文件`（内存缓冲 + 游标 + 脏标志）。
