# AMUNOS

> **一个从零编写的 x86 32 位护模式操作系统 · 自举 · v6.5.3**

AMUNOS 从一段 FAT12 引导扇区启动，进入 **x86 32 位保护模式**（Ring 0，无分页，VA==PA）。
它内置 **TinyCC 编译器** 与 **minilibc 标准库**——在 OS 自带的 shell 里就能直接编写、
编译、运行标准 C 程序，形成闭合的"自举"链路：

```
写 HELLO.C  →  TCC HELLO.C -o HELLO.EXE  →  ELF HELLO.EXE  →  HELLO OK from TCC
```

还支持 **GB2312 / UTF-8 中文**（包含中文文件名、EDIT 中文显示、全程点阵字库渲染）。

---

## ✨ 功能特性

- **引导与多任务**：FAT12 引导扇区 → 内核 → 保护模式；PIT 100Hz 抢占式轮转调度，
  后台演示任务证明并发。
- **ELF 可执行文件**：标准 ELF32 加载器，加载 PT_LOAD 段到 `0x100000` 并跳 `e_entry`。
- **内置编译器**：**TinyCC 0.9.27**（`TCC.ELF`），自动注入 `-static -I A:/USR/INCLUDE
  -L A:/USR/LIB -B A:/USR/LIB`，任意盘/目录都能编译链接成可直接运行的静态 ELF。
- **标准库**：minilibc（`libc/`）——`printf`/`snprintf`/`malloc`/`string`/文件 I/O 等，
  供 TCC 与用户程序链接。
- **文件系统**：**FAT12/16 自动识别**（按簇数判定位宽，FAT16 每簇多扇、跨簇读写正确）。
  **三盘制**：A: 系统盘（引导）+ B:/C: 数据盘，两级目录树，支持跨盘操作。
- **路径**：盘符限定绝对路径 + 相对路径 + 目录遍历，分隔符**严格只认 `/`**
  （`TYPE USR/SRC/HELLO.C`、`COPY C:/A.TXT B:/B.TXT`、`COPY A:/X B:`、`CD USR/SRC`），
  命令大小写不敏感。
- **中文（v6.5.3）**：
  - **中文文件名**：GB2312 双字节塞进 FAT 8.3 短名（`MKDIR/COPY/DEL/REN` 全支持，
    `DIR` 显示汉字）。自动处理 FAT 保留字节 `0xE5→0x05`。
  - **EDIT 中文显示**：用户态 `EDIT.ELF` 用图形模式正确渲染 **UTF-8 / GB2312** 中文，
    支持滚动不拆整字、**退格整字删除**（`CJKWCHAR`/`UTF8TOGB` 系统调用）。
  - **中文输入法**：设计方案见 `docs/中文输入法_设计.md`（拼音优先，全用户态，零内核增量）。
- **Shell 与工具**：命令行 REPL，`DIR -P` 分页、`TCC`、`ELF`、`INSTALL`、行编辑
  （←→/Home/End/Del）、任意命令加 `-?` 显示用法；命令→ELF 对照表 `CMDS.BIN`。
- **编辑器**：用户态 `EDIT.ELF`（内核不再内置），FreeDOS EDIT 风格——F1=帮助 F2=保存
  F3=打开 F4=新建 F5=退出，图形窗口/菜单/鼠标支持。
- **图形输出（v6.5.3）**：VBE 640×480×16bpp 帧缓冲 + 软件字符软缓冲 + 双字库
  （内嵌 Latin、A: 盘 `HZK16` 简体中文点阵），全中文/ASCII 像素级渲染；鼠标叠加与
  输入光标均为内核软件绘制。
- **AMUNOS Classic GUI 0.1（v6.5.3）**：内核窗口服务器 `gui.c`（图形 syscall 28-46）。
  窗口/按钮/标签/单行输入框/多行文本区/列表/弹窗 + 全中文渲染 + 多窗口叠放（Z 序/
  焦点）；窗口 chrome ▁最小化 / ▢最大化 / ✕关闭 / 标题栏拖动（drag-copy 快路径抗
  闪烁）+ 文本选中（Shift+方向键 / 鼠标拖选 / 删除替换，整字形不劈 CJK）。用户态
  演示 `gui/gui-demo.c`（`GUI` 命令）。设计见 `docs/AMUNOS_Classic_GUI_设计与实现规划.md`。
- **按名运行**：输入 `XXX` 自动试 `XXX.ELF/.EXE/.COM/.BIN`（先当前目录，再盘根）。
- **健壮性**：全局 Ctrl+C 强制终止死循环程序；IDT 异常桩 + `*** FAULT ***` 兜底。
- **远程控制台**：COM1/LPT1 轮询驱动，屏幕镜像到串口，键盘与串口 RX 统一成一个输入源
  （支持 VT100 方向键），串口即远程控制台。

---

## 🔧 快速开始（WSL）

构建与测试在 WSL 中完成（`nasm` / `gcc-multilib` / `ld` / `python3` / `qemu-system-i386`）：

```bash
cd /mnt/c/Users/XU/Desktop/OSDev

make all              # 构建 A.img（boot + kernel + 内置文件 + 中文测试样本）
make B.img C.img      # 构建数据盘（B: FAT12 / C: FAT16）

make run-trio-gui     # 三盘图形运行（A: 引导 + B:/C: 数据）
make run-serial       # 双盘串口远程控制台（另开终端 ./serial-console.sh 连接）
make run-trio-serial  # 三盘 + 串口远程控制台
```

> 说明：构建/测试一律走 WSL（Windows 侧 `python3` 是 Store 桩，不可用）。

**中文字实验证**（帧缓冲像素级断言，QEMU 内跑）：

```bash
python3 validate_zh.py      # HZK16 加载 / 汉字渲染 / EDIT UI 文本带
python3 validate_editzh.py  # DIR 中文文件名 / EDIT 显示 UTF-8 / GB2312 / 退格整字删
```

**内核实测**：`TCC USR/SRC/DEMO.C -o DEMO.EXE` 编译→链接，再 `ELF DEMO.EXE` 输出
`DEMO OK from TCC`（FAT16 C: 上的完整编译→运行闭环）。

---

## 🗂️ 项目结构

```
OSDev/
├── boot.asm / head.asm   引导扇区 + 32 位入口与中断桩
├── kernel.c              kmain + shell REPL + 演示任务
├── command.c             shell 命令（DIR / ELF / TCC / INSTALL / COPY / MKDIR ...）
├── fs.c                  FAT12/16 文件系统（自动识别 + 跨簇寻址 + RAM FAT 缓存）
├── disk_io.asm           磁盘 I/O
├── vga.c                 字符/软字符缓冲与中文渲染（put_cjk_str / CJK 系统调用）
├── fb.c                  VBE 帧缓冲 + Latin/CJK 点阵绘制 + 光标/鼠标软叠加
├── latin_font.h          内嵌 Latin-15 点阵字库
├── kbd.c / mouse.c       PS/2 键盘（含 COM1 串口统一输入源）/ 鼠标
├── idt.c / task.c        中断描述符表 / 抢占式多任务
├── mem.c                 内核堆分配器（0x400000 起 4MB）
├── syscall.c             int 0x30 分发 + fd 表（21+ 个系统调用）
├── elf.c / fault.c       ELF32 加载器 / CPU 异常处理
├── serial.c              COM1 / LPT1 驱动
├── linker.ld / Makefile  链接脚本 / 构建入口
├── edit-fdos/            用户态编辑器（FreeDOS EDIT 移植，编为 EDIT.ELF）
├── libc/                 用户态标准库（stdio/stdlib/string/malloc/math/...）
├── mka_img.py             A.img 构建器（FAT12 系统盘 + 中文样本 + HZK16/U2GB）
├── mkbimg.py / mkcimg.py / mkfat16.py    B: / C: / 独立 FAT16 镜像工具
├── build-tcc.sh / build-libc.sh   交叉编译 TinyCC / minilibc
├── gen_u2gb.py           生成 u2gb.bin（Unicode→GB2312 映射表）
├── HZK16                 GB2312 简体中文字库（267KB，A: 盘装入，随仓库提供）
├── docs/                 设计文档（中文输入法 / 生态规划 / 路线报告）
├── validate_zh.py / validate_editzh.py    中文字验证脚本
└── README.md / _img.py   本文件 / 镜像与预留
```

> `makar/`、`mikanos/`、`kernel-master/`、`edit-master/` 为第三方参考仓库（已 gitignore，
> 不入库）。`makar/vendor/tinycc/libtcc.c` 打有 `tcc_drive_colon` 补丁（盘符冒号路径支持）。

---

## 🛢️ 磁盘目录结构（A: 系统盘）

```
A:/（FAT12，引导盘）
├─ BOOT/             BOOT.BIN, KERNEL.BIN（副本）
├─ BIN/              TCC.ELF, EDIT.ELF（系统可执行程序；INSTALL 装到这里）
├─ USR/
│  ├─ INCLUDE/       TCC 内置头 + libc 头（STDIO.H, STDLIB.H ...）
│  ├─ LIB/           LIBC.A, LIBTCC1.A（TCC 链接库）
│  └─ SRC/           HELLO.C, INP.C（示例源码）
├─ CRT1.O CRTI.O CRTN.O    TCC crt 文件
├─ HZK16  U2GB.BIN          中文字库 + Unicode→GB2312 映射
├─ 中文.TXT  UTF8_CN.TXT  GB_CN.TXT    中文演示/验证样本
└─ CMDS.BIN          命令→ELF 对照表（内核保护，EDIT/INSTALL 可写）

B:/（FAT12，可写数据盘）   C:/（FAT16 32MB，可写数据盘）
└─ HELLO.ELF / USR/SRC / CMDS.BIN     用户程序与测试源码
```

**TCC 前缀**：`TCC` 自动注入 `A:/BIN/TCC.ELF -static -I A:/USR/INCLUDE -L A:/USR/LIB
-B A:/USR/LIB`，在任何盘/目录下 `TCC B:/USR/SRC/HELLO.C -o HELLO.EXE` 都能编译链接。

---

## 🧠 内存布局

| 地址 | 用途 |
|------|------|
| `0x8000 .. ~0x15000` | 内核（<52KB，受 boot 104 扇区硬限制） |
| `0x14000 .. 0x90000` | 内核栈（`esp=0x90000`，向下生长） |
| `0x70000`            | FAT 缓存 |
| `0x100000 .. 0x160000` | ELF 载入地址（tcc.elf ~300KB） |
| `0x1F0000`           | argv 块（argc/argv） |
| `0x200000 .. 0x400000` | 用户 brk 堆（2MB，libc malloc 用） |
| `0x400000 .. 0x800000` | 内核堆（mem.c，4MB，fd 缓冲/ELF 暂存） |
| 帧缓冲               | VBE 640×480×16bpp（软件软缓冲 + 每帧重绘） |

---

## 🔌 系统调用（int 0x30）

ABI：`eax=调用号, ebx=arg1, ecx=arg2, edx=arg3`，返回值在 EAX。

| 号 | 函数 | 说明 |
|----|------|------|
| 1 | putchar | 输出字符 |
| 2 | getchar | 读一个字符（阻塞） |
| 3 | puts    | 输出字符串 |
| 4 | putnum  | 输出数字 |
| 5/6 | malloc/free | 内核堆分配/释放 |
| 7 | sleep   | 睡眠（tick） |
| 8–12 | open/close/read/write/lseek | 文件 I/O（fd 0-2 控制台，3+ 文件） |
| 13 | exit    | 结束程序（返回码） |
| 14 | brk     | 用户堆断点 |
| 15 | getkey  | 原始键码（无回显阻塞；128+ 功能/方向键） |
| 16 | getmods | Shift/Ctrl/Caps/Alt 状态 |
| 17 | readdir | 列目录（返回原始 8.3 字节，含 GB2312） |
| 18 | mouse   | 读鼠标状态 |
| 19 | keyhit  | 非阻塞按键查询（编辑器事件循环用） |
| 20–22 | cursor/curhide/curshow | 软件输入光标定位/隐藏/恢复 |
| 23–24 | mousehide/mouseshows | 鼠标叠加隐藏/恢复 |
| 25 | video_base | 文本软缓冲基址 |
| 26 | utf8togb | Unicode 码点 → GB2312（U2GB 表查，0=不在字库） |
| 27 | cjkwchar | 在绝对格 (x,y) 画一个汉字（占两格） |

fd 表：`0=stdin 1=stdout 2=stderr`（控制台），`3+ = 文件`（内存缓冲 + 游标 + 脏标志，
close 时写回所在目录与盘）。

---

## 🧭 历史与文档

- `CHANGELOG` 见 git 提交信息（每 commit 标版本，v6.5.2 为当前中文版本）。
- `docs/`：`中文输入法_设计.md`（拼音 IME 数据表/交互/落地路径）、生态规划、未来路线。

## ⚖️ 许可

本仓库为个人学习教育项目。含第三方组件：
- **FreeDOS Edit / DFLAT** → `edit-fdos/`，GPL / 其自身许可；
- **TinyCC** → `makar/vendor/`，LGPL；
- **HZK16** GB2312 字库 → 常见公开简体点阵字库（随仓库提供）。

请遵守各自许可条款。