# AMUNOS — 一个 x86 32 位自举操作系统

> 爱好项目 · 从零编写 · **v6.5.1（FAT12/16 自动识别 · 严格 `/` 分隔符 · TCC 静态链接）**

AMUNOS 运行在 **x86 32 位保护模式**（Ring 0，无分页，VA==PA）下，从一个 FAT12 引导
扇区启动。它**内置 TinyCC 编译器与 minilibc 标准库**——在 OS 的 shell 里就能直接编写、
编译、运行标准 C 程序，形成"自举"闭环：

```
写 HELLO.C  →  TCC HELLO.C -o HELLO.EXE  →  ELF HELLO.EXE
```

---

## 功能特性

- **引导与多任务**：FAT12 引导扇区 → 加载内核 → 32 位保护模式；PIT 100Hz 抢占式轮转
  调度，后台演示任务证明并发运行。
- **ELF 可执行文件**：标准 ELF32 加载器，加载 PT_LOAD 段到 `0x100000` 并跳转 `e_entry`。
- **内置编译器**：**TinyCC 0.9.27**（`TCC.ELF`），把 C 源码编译成**静态 ELF**
  （`TCC` 自动注入 `-static`，输出不含动态段，加载器可直接运行）。
- **标准库**：minilibc（libc/）——`printf`/`snprintf`/`malloc`/`string`/文件 I/O 等，
  供 TCC 与用户程序链接。
- **文件系统**：**FAT12/16 自动识别**（`fs_init` 按簇数判定位宽，FAT16 每簇多扇、
  跨簇读写均正确）。**三盘**：A: 系统盘 + B:/C: 数据盘，两级目录树。
- **路径**：盘符限定绝对路径 + 相对路径 + 目录遍历，分隔符**严格只认 `/`**
  （`TYPE USR/SRC/HELLO.C`、`COPY C:/A.TXT B:/B.TXT`、`COPY A:/X B:`、`CD USR/SRC`），
  命令大小写不敏感。
- **Shell**：命令行 REPL，`DIR -P` 分页、`TCC`、`ELF`、行编辑（←→/Home/End/Del）、
  任意命令加 `-?` 显示用法。
- **编辑器**：用户态程序 `EDIT.ELF`（内核不再内置），FreeDOS EDIT 风格——F1=帮助
  F2=保存 F3=打开 F4=新建 F5=退出，支持路径与 PgUp/PgDn 翻页。
- **按名运行**：输入 `XXX` 自动试 `XXX.ELF/.EXE/.COM/.BIN`（先当前目录，再当前盘根）。
- **命令对照表**：`CMDS.BIN`（每盘根目录），行格式 `命令名 目标ELF`，**全盘搜索**；
  `INSTALL prog[.ext] [name]` 复制程序到 `A:/BIN` 并注册，任何盘/目录敲命令名即运行。
  CMDS.BIN 受内核保护（`DEL`/`REN`/`COPY` 拒绝覆盖），但 `EDIT` 仍可编辑。
- **健壮性**：全局 Ctrl+C 强制终止死循环程序；IDT 异常桩 + `*** FAULT ***` 兜底。
- **远程控制台**：COM1/LPT1 轮询驱动，屏幕镜像到 COM1，键盘与串口 RX 统一为一个输入
  源——串口即远程控制台（见下方 `make run-serial` / `make run-trio-serial`）。

## 快速开始（WSL）

构建工具链（`nasm`、`gcc-multilib`、`ld`、`python3`、`qemu-system-i386`）全部在 WSL。

```bash
cd /mnt/c/Users/XU/Desktop/OSDev

make all               # 构建 A.img（boot + kernel + 内置文件）
make B.img C.img       # 构建数据盘（B: FAT12 / C: FAT16）

make run-trio-gui      # 三盘图形运行（A: 引导 + B:/C: 数据）
make run-serial        # 双盘串口远程控制台（另开终端 ./serial-console.sh 连接）
make run-trio-serial   # 三盘 + 串口远程控制台
```

> QEMU 只在 WSL 里可用；Windows 侧 `python3` 是 Store 桩，构建/测试一律走 WSL。

**内核实测**：`TCC USR/SRC/DEMO.C -o DEMO.EXE` 编译并链接，再 `ELF DEMO.EXE` 输出
`DEMO OK from TCC`（FAT16 C: 上的完整编译→运行闭环）。

## 项目结构

```
OSDev/
├── boot.asm         引导扇区（FAT12 BPB + 加载内核）
├── head.asm         32 位入口 + 中断桩（键盘/系统调用/定时器/异常）
├── kernel.c         kmain + shell REPL + 演示任务
├── command.c        shell 命令（DIR / ELF / TCC / INSTALL / COPY ...）
├── fs.c             FAT12/16 文件系统（自动识别 + 每簇多扇寻址）
├── disk_io.asm      磁盘 I/O
├── vga.c            VGA 文本输出（0xB8000）
├── kbd.c            PS/2 键盘（scancode → ASCII）
├── idt.c            IDT 初始化
├── task.c           抢占式多任务调度
├── mem.c            内核堆分配器（0x400000 起 4MB）
├── syscall.c        int 0x30 分发 + fd 表
├── elf.c            ELF32 加载器
├── fault.c          CPU 异常处理
├── edit.c           用户态编辑器源码（交叉编译为 EDIT.ELF）
├── serial.c         COM1 串口 + LPT1 并口驱动
├── linker.ld        内核链接脚本
├── Makefile         构建入口
├── mka_img.py       A.img 镜像构建器（FAT12 系统盘）
├── mkbimg.py        B.img 镜像构建器（FAT12）
├── mkcimg.py        C.img 镜像构建器（FAT16 32MB 数据盘）
├── mkfat16.py       独立 FAT16 镜像生成工具
├── build-tcc.sh     交叉编译 TinyCC
├── build-libc.sh    构建 minilibc
├── libc/            用户态标准库（stdio/stdlib/string/malloc/...）
└── README.md        本文件
```

> `makar/`、`mikanos/`、`kernel-master/` 为第三方参考仓库（已 gitignore），不参与构建。
> `makar/vendor/tinycc/libtcc.c` 打有 `tcc_drive_colon` 补丁（见下方 TCC 说明）。

## 磁盘目录结构

```
A:/（系统盘，FAT12）
├─ BOOT/             BOOT.BIN, KERNEL.BIN（副本）
├─ BIN/              TCC.ELF, EDIT.ELF（系统可执行程序；INSTALL 装到这里）
├─ USR/
│  ├─ INCLUDE/       TCC 内置头 + libc 头（STDIO.H, STDLIB.H ...）
│  ├─ LIB/           LIBC.A, LIBTCC1.A（TCC 链接库）
│  └─ SRC/           HELLO.C, INP.C（示例源码）
├─ CRT1.O CRTI.O CRTN.O   TCC crt 文件
└─ CMDS.BIN          命令→ELF 对照表（EDIT → /BIN/EDIT.ELF；内核保护）

B:/（数据盘，FAT12，可写）
├─ HELLO.ELF INP.ELF EDIT.ELF   用户 ELF
├─ USR/SRC/          HW.C, RETVAL.C, COUNT5.C ...（C 测试源码）
└─ CMDS.BIN          命令→ELF 对照表

C:/（数据盘，FAT16 32MB，可写）
├─ HELLO.ELF                     用户 ELF
├─ USR/SRC/DEMO.C                （TCC 演示源码）
└─ CMDS.BIN          命令→ELF 对照表
```

**TCC 前缀**：`TCC` 自动注入 `A:/BIN/TCC.ELF -static -I A:/USR/INCLUDE -L A:/USR/LIB
-B A:/USR/LIB`，在任何盘/目录下 `TCC B:/USR/SRC/HELLO.C -o HELLO.EXE` 都能编译链接。

> **路径解析细节（v6.5.1 修复）**：TCC 的 `tcc_split_path` 按 Unix `:` 拆分路径列表，
> 会把盘符 `A:/...` 切成 `["A","/..."]`。已打补丁 `tcc_drive_colon`
> （`makar/vendor/tinycc/libtcc.c`）：段首 `X:/` 的冒号视为路径一部分。分隔符严格只认
> `/` 后，`CONFIG_TCC_CRTPREFIX="A:/"` 经 TCC 的 `"%s/%s"` 拼接出 `A://crt1.o`，
> 内核解析器跳过空段命中根目录——任意盘/目录都能解析 crt1.o 等 crt 文件。

文件操作支持跨盘与绝对路径：`COPY C:/USR/SRC/DEMO.C A:/USR/SRC/`、
`COPY B:/HELLO.ELF A:/BIN/`、`COPY A:/USR/SRC/HELLO.C B:`（裸盘目标 = 该盘根）、
`TYPE ./SRC/X.C`；`MOV`/`DEL`/`REN` 同理。

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
| 15 | getkey  | 原始键码（无回显阻塞读；32-126 可打印、128-131 方向键、132/133 Home/End、134-138 F1-F5、139/140 PgUp/PgDn） |

fd 表：`0=stdin 1=stdout 2=stderr`（控制台），`3+ = 文件`（内存缓冲 + 游标 + 脏标志，
close 时写回所在目录与盘）。
