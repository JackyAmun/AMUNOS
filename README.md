# AMUNOS Classic

![language](https://img.shields.io/badge/language-C%20%2F%20x86%20ASM-blue) ![kernel](https://img.shields.io/badge/kernel-v6.5.5-8A2BE2) ![gui](https://img.shields.io/badge/GUI-0.3-008080) ![license](https://img.shields.io/badge/license-MIT--style%20%2B%20third--party-lightgrey)

> **一个从零编写的 x86 32 位保护模式操作系统 · 自举 · 中文原生**

AMUNOS 从一段 FAT12 引导扇区启动，进入 **x86 32 位保护模式**（Ring 0，无分页，VA==PA）。
它内置 **TinyCC 编译器** 与 **minilibc 标准库**——在 OS 自带的 shell 里就能直接编写、
编译、运行标准 C 程序，形成闭合的"自举"链路：

```
写 HELLO.C  →  TCC HELLO.C -o HELLO.EXE  →  ELF HELLO.EXE  →  HELLO OK from TCC
```

在此基础上提供 **内核驻留的图形窗口服务器（AMUNOS Classic GUI 0.3）**：
窗口 / 控件 / 菜单 / 多行编辑 / 鼠标 / 中文点阵渲染，全部经 `int 0x30` 系统调用
对用户态暴露——用户程序几十行 C 就能写出带菜单栏、复选框、文本编辑器的完整 GUI。

---

## ✨ 功能总览

| 子系统 | 能力 |
|--------|------|
| **引导** | FAT12 引导扇区 → 双段加载内核（AH=02h 128 扇 + AH=42h LBA 扩展读 64 扇）→ 保护模式 |
| **调度** | PIT 100Hz 抢占式轮转多任务，后台演示任务证明并发 |
| **内存** | 内核堆 `mem.c`（0x400000 起 4MB）+ 用户 brk 堆 2MB + ELF 载入 0x100000 |
| **文件系统** | FAT12/16 自动识别（按簇数判定），三盘制 A:/B:/C:，跨盘路径，`/` 分隔 |
| **ELF** | 标准 ELF32 加载器，PT_LOAD 段 → `0x100000`，跳 `e_entry` |
| **编译器** | TinyCC 0.9.27（`TCC.ELF`），任意盘/目录 `TCC x.c -o x.exe` 编译→链接→运行闭环 |
| **Shell** | REPL、行编辑（←→/Home/End/Del）、`DIR -P` 分页、按名运行 `XXX.ELF/.EXE/...` |
| **中文** | GB2312/UTF-8 全链路：中文文件名（8.3 短名塞 GB 双字节）、点阵渲染（HZK16）、U2GB 映射表 |
| **串口** | COM1 屏幕镜像 + 键盘/串口统一输入源，串口即远程控制台 |
| **GUI 0.3** | 内核窗口服务器：多窗叠放 / 拖动 / 最小化·最大化·关闭 / 8 类控件 / 菜单栏 / 文本编辑 / 全中文 |

---

## 🪟 AMUNOS Classic GUI 0.3

图形栈：**VBE 640×480×16bpp 帧缓冲**。内核 `gui.c` 是一个 retained-mode 窗口服务器——
每个窗口一块离屏 RGB565 缓冲，变更后按 Z 序增量合成到 LFB（单窗 blit / 区域暴露 /
拖动快路径三档，全程无整屏闪烁）。用户程序只调 syscall，从不直接碰显存。

**窗口管理**

- 多窗口叠放（Z 序）+ **活跃窗口模型**：点击只作用于活跃窗；活跃窗恒在最顶层，蓝色标题，非活跃灰色
- Chrome 三钮 ▁最小化 / ▢最大化 / ✕关闭，标题栏拖动（drag-copy 快路径，拖动零闪烁）
- 最小化 = 18px 标题条（可再拖动/还原）；最大化/还原；Win9x 风格边框与硬边阴影
- 菜单栏 + 下拉弹层（Alt+字母助记符、方向键+Enter、弹层恒在 Z 顶、跟随点击位置）

**控件（8 类）**

| 控件 | 交互 |
|------|------|
| Button | 点击 / Tab 聚焦 / 空格·Enter 激活，Win9x 凸起边 |
| Label / StatusBar | 文本标签 / 窗底状态栏（实时回显） |
| Edit | 单行输入：光标处插入、←→/Home/End/DEL、整字形退格（不劈 CJK） |
| Textarea | 多行编辑：↑↓←→/PgUp/PgDn、回车换行、内容可经 syscall 读回 |
| List | 点击/方向键选择，选中项与文本读回 |
| Checkbox / Radio | √ 勾选 / 同组互斥单选 |
| Menu | 菜单栏 + 下拉弹层，GEV_CLICK 事件 `(menu<<8)\|item` |

**文本选中**：Shift+方向键扩展 / 鼠标拖选，选区字节对齐整字形（不劈开 UTF-8/GB 汉字），
键入/删除替换选区。

控件演示程序 `gui/gui-demo.c`（shell 里输入 `GUI`）囊括以上全部能力。

---

## 🔧 快速开始（WSL）

构建与测试在 WSL 中完成（依赖 `nasm` / `gcc-multilib` / `ld` / `python3` / `qemu-system-i386`）：

```bash
cd /mnt/c/Users/XU/Desktop/OSdev

make kernel.bin       # 构建内核（双段引导，<96KB）
python3 mka_img.py A.img   # 打包 A: 系统盘（boot + kernel + TCC + 字库 + 样本）
make B.img C.img      # B: FAT12 / C: FAT16 数据盘

make run-trio-gui     # 三盘图形运行（A: 引导 + B:/C: 数据）
make run-serial       # 串口远程控制台（另开终端 ./serial-console.sh）
```

启动后：

```
A:/> GUI        ← 控件演示（窗口/菜单/编辑器/复选/列表）
A:/> EDIT       ← FreeDOS EDIT 风格编辑器（中文显示）
A:/> TCC USR/SRC/HELLO.C -o HELLO.EXE && HELLO
```

### 回归测试（QEMU 像素级断言）

```bash
python3 validate_gui.py      # GUI 16 项：渲染/弹窗复原/列表/输入/多行/选中/chrome/拖动/复选/菜单/Tab
python3 validate_zh.py       # HZK16 加载 / 汉字渲染
python3 validate_editzh.py   # DIR 中文文件名 / EDIT 中文 / 退格整字删
python3 validate_box.py      # 框线字形
```

`validate_gui.py` 全自动：启动 QEMU（`-display none -monitor tcp`），`sendkey`/`mouse_move`
注入交互，`pmemsave` 抓帧缓冲做**像素级断言**（颜色/字形数/选区高亮/拖动暴露区），无需人工盯屏。

---

## 🗂️ 项目结构

```
OSDev/
├── boot.asm / head.asm    引导扇区（双段加载） + 32 位入口与中断桩
├── kernel.c               kmain + shell REPL
├── command.c              shell 命令（DIR / ELF / TCC / COPY / MKDIR ...）
├── gui.c                  ★ 内核 GUI 窗口服务器（窗口/控件/菜单/合成器）
├── gui/gui-demo.c         控件演示程序（GUI.ELF）
├── fb.c / vga.c           VBE 帧缓冲 + Latin/CJK 点阵绘制 / 文本模式中文渲染
├── latin_font.h           内嵌 Latin 点阵字库（不依赖 BIOS INT 10h/1130h）
├── fs.c / disk_io.asm     FAT12/16 文件系统 / 磁盘 I/O
├── kbd.c / mouse.c        PS/2 键盘 / 鼠标（IRQ 中断门 + 排干循环，GUI 鼠标三层修复）
├── idt.c / task.c         中断描述符表 / 抢占式多任务
├── mem.c                  内核堆分配器（0x400000 起 4MB）
├── syscall.c              int 0x30 分发（55+ 系统调用）
├── elf.c / fault.c / serial.c
├── edit-fdos/             FreeDOS EDIT 0.7d 移植（EDIT.ELF）
├── libc/                  用户态 minilibc + syscall 内联封装
├── mka_img.py / mkbimg.py / mkcimg.py / mkfat16.py   镜像构建器
├── build-tcc.sh / build-libc.sh                      交叉编译 TinyCC / minilibc
├── HZK16                  GB2312 简体点阵字库（A: 盘装入）
├── validate_*.py          QEMU 像素级回归测试
└── docs/                  设计文档（GUI 规划 / 输入法 / 生态 / 路线）
```

---

## 🧠 内存布局

| 地址 | 用途 |
|------|------|
| `0x07C00 .. 0x08000` | 引导扇区 |
| `0x08000 .. ~0x20000` | 内核（双段加载，<96KB；rsvd 193 扇 = 1 boot + 192 kernel） |
| `0x90000`            | 内核栈顶（向下生长） |
| `0x70000`            | FAT 缓存 |
| `0x100000 ..`        | ELF 载入地址 |
| `0x200000 .. 0x400000` | 用户 brk 堆（libc malloc） |
| `0x400000 .. 0x800000` | 内核堆（fd 缓冲 / GUI 窗口离屏缓冲） |
| `0xFD000000`         | VBE 线性帧缓冲 640×480×16bpp |

---

## 🔌 系统调用（int 0x30）

ABI：`eax=调用号, ebx=arg1, ecx=arg2, edx=arg3`，返回值在 EAX。

### 基础（1–27）

| 号 | 函数 | 说明 |
|----|------|------|
| 1–4 | putchar/getchar/puts/putnum | 控制台 |
| 5/6 | malloc/free | 内核堆 |
| 7 | sleep | 睡眠（tick） |
| 8–12 | open/close/read/write/lseek | 文件 I/O（fd 0-2 控制台） |
| 13/14 | exit/brk | 退出码 / 用户堆断点 |
| 15/16 | getkey/getmods | 阻塞键码 / 修饰键状态 |
| 17 | readdir | 列目录（原始 8.3 字节，含 GB2312） |
| 18/19 | mouse/keyhit | 鼠标状态 / 非阻塞按键查询 |
| 20–24 | cursor/mouse 叠加控制 | 软件光标与鼠标 |
| 25 | video_base | 文本软缓冲基址 |
| 26/27 | utf8togb/cjkwchar | Unicode→GB2312 / 绝对格画汉字 |

### GUI 窗口服务器（28–55）

| 号 | 函数 | 说明 |
|----|------|------|
| 28/29 | gui_enter/leave | 接管屏幕 / 退出回文本渲染 |
| 30/31/32 | gui_win/win_close/win_raise | 建窗（自动激活置顶）/ 关窗 / 置顶 |
| 33 | gui_wnd_text | 设控件文本 |
| 34–38 | gui_btn/lbl/edit/list/list_set | 建按钮/标签/输入框/列表，加列表项 |
| 39/40 | gui_fill/gui_text | 窗内填色 / 像素文本 |
| 41 | gui_dialog | 居中弹窗（自动激活） |
| 42 | gui_editchar | 输入框编辑：光标处插入 / 退格 / ←→HOME/END/DEL |
| 43 | gui_events | 取事件批 `gui_ev_t{type,win,ctl,ch}`（CLICK/KEY/ENTER/CLOSE） |
| 44–46 | gui_tarea/set/get | 多行文本区：建 / 设内容 / **读回内容** |
| 47/48 | gui_list_get/n | 列表选中项文本+索引 / 项数 |
| 49/50 | gui_check/check_set | 复选框建 / 置状态 |
| 51 | gui_radio | 单选钮（同窗同组互斥） |
| 52–54 | gui_menubar/menu_add/menu_item | 菜单栏 / 加菜单（`文件(F)` 助记符解析）/ 加项（`-` 分隔线） |
| 55 | gui_statusbar | 窗底状态栏 |

事件类型：`GEV_CLICK=1`（列表点击 ch=项索引，菜单点击 ch=`(menu<<8)|item`）、
`GEV_KEY=2`、`GEV_ENTER=3`、`GEV_CLOSE=4`（✕ 关闭通知，程序自决退出）。

---

## 🧭 设计文档

- `docs/AMUNOS_Classic_GUI_设计与实现规划.md` — GUI 设计规范（Win9x 配色/控件层次/路线图）
- `docs/中文输入法_设计.md` — 拼音 IME 方案（全用户态，零内核增量）
- `docs/AMUNOS_操作逻辑_生产工具_应用生态规划.md` / `docs/AMUNOS_未来路线报告.md`

版本号约定：内核 `vX.Y.Z`（串口与 shell 启动横幅一致），GUI 独立版本 `GUI 0.3`。
变更历史见 git 提交信息（每 commit 标注版本）。

## ⚖️ 许可

个人学习教育项目。含第三方组件：

- **FreeDOS Edit / DFLAT** → `edit-fdos/`（GPL，遵其自身许可）
- **TinyCC** → `makar/vendor/`（LGPL，含盘符冒号路径补丁）
- **HZK16** GB2312 点阵字库 → 公开简体点阵字库

请遵守各自许可条款。
