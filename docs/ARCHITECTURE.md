# AMUNOS 系统架构构思与路线图

> 面向"下一个阶段"的设计指引：完整目录结构、可执行文件概念、设备管理、网络、
> 串口/并口、中文支持，以及分阶段落地顺序。

## 0. 现状盘点（设计的地基）

| 项 | 现状 |
|---|---|
| 执行模型 | Ring 0、平坦内存、**无分页**（VA==PA）、ELF 加载到 `0x100000`、程序作为独立抢占式内核任务运行、`task_exit_current` 结束 |
| 系统调用 | `int 0x30`，号 1–15，fd 表 0/1/2=控制台、3+=文件 |
| 文件系统 | FAT12，A.img(系统盘)+B.img(数据盘)，根目录固定 224 项，`cwd_cluster` 单级子目录 |
| 显示 | VGA 文本模式 `0xB8000`（80×25，8×16 ROM 字库，仅 ASCII） |
| 设备 | 串口/并口**轮询驱动**已实现（serial.c，SER/LPT + 远程控制台）；无设备抽象层（VFS 属 P1） |

---

## 1. 目录结构（完整方案）

FAT12 是 8.3 短名、根目录项数固定，**不宜深嵌套**。推荐 DOS 风味、浅层两级布局：

```
A:\                    系统盘（只读）
├─ BOOT\               boot.bin + kernel.bin 副本
├─ BIN\                内置 ELF 程序：TCC.ELF, SH.ELF, LS.ELF, CAT.ELF,
│                      EDIT.ELF, PING.ELF, WGET.ELF ...
├─ SBIN\               系统管理：FMT.ELF, CHKDSK.ELF, SETUP.ELF
├─ USR\
│  ├─ BIN\             用户自编译程序安装处
│  ├─ LIB\             LIBC.A, LIBTCC1.A, CRT1.O, CRTI.O, CRTN.O
│  ├─ INCLUDE\         STDIO.H, STDLIB.H, STRING.H, MALLOC.H, UNISTD.H ...
│  ├─ SRC\             示例源码：HELLO.C, FIB.C, SNAKE.C
│  └─ SHARE\           FONT16.FNT（中文字库）, LOCALE\, DOC\
├─ ETC\                MOTD, PROFILE, TERMINFO
├─ TMP\                临时文件（编译中间产物）
├─ DEV\                设备节点（见第 3 节）
├─ MNT\                挂载点
└─ PROC\               进程信息（虚拟文件系统，见第 3 节）

B:\                    数据盘（可写，用户文件/文档）
C:\                    （未来）IDE 硬盘，见第 4 节
```

**落地要点**：
- 内核维护"搜索路径"表（`BIN` → `USR\BIN`），执行程序按 PATH 找，等价 `$PATH`。
- `fs.c` 现只支持 `cwd_cluster` 单级，要支撑这棵树需把 `fs_find_entry_in_dir`
  泛化成"按 `/` 逐段下钻"，并给 FAT12 子目录链配深度上限（如 4 级）。

> **v6.5 已落地的部分**：A: 上 `BOOT\`、`BIN\`、`USR\LIB\`、`USR\INCLUDE\`、`USR\SRC\`
> 与 B: 上 `USR\SRC\` 已在镜像里建成真实 FAT12 子目录（`fs_resolve_path` 逐段下钻 +
> `CD` 多级导航 + `fs_find_entry_in_dir` 读子目录簇链均已实现）。执行程序的定位改用
> **`CMDS.TXT` 命令→ELF 对照表** + **cwd 下按名运行** 替代 PATH 搜索；`TCC` 内置命令
> 注入 `-I/-L/-B` 定位头/库。完整 `$PATH` 搜索表仍是后续工作。

---

## 2. 可执行文件的概念 + ELF 增强

现在"可执行文件"= 一个加载到 `0x100000` 的静态 ELF。升级成真正"程序概念"分三层：

**A. 加载与元数据（轻量，先做）**
- 保持 ELF 格式，约定 `.amunos` note 段（或读 ELF header 的 `e_entry`/`e_flags`）
  承载：程序名、版本、所需最小 AMUNOS 版本、内存上限。
- 引入 `struct exec_hdr`（魔数 `"AMNX"` + ELF 长度 + CRC32），loader 先验明正身、
  校验完整性再跳转——即"可执行文件"的正式边界。

**B. 运行期语义**
- **argv/envp 完整化**：现在 argc 在 `0x1F0000`，扩成标准 argv 数组 + envp，`crt0.S` 统一读取。
- **返回码**：`exit(n)` → shell 拿到 `$?`（`%ERRORLEVEL%`），支持 `if errorlevel` 判断。
- **无需 .EXE**：`HELLO` 自动按 PATH 补 `.EXE`/`.ELF`；按魔数区分 ELF 与旧 `.COM` 裸二进制。
- **重定向/管道**（shell 增强）：`>  >>  <  |`，把 fd 0/1/2 接到文件或管道
  （管道 = 内核里一对缓冲 + 两个 fd）。

**C. 进程模型（远）**
- 当前是"单前台程序 + 抢占式内核任务"，程序作为独立任务运行、`task_exit_current` 退出。真正的"进程"需
  **分页 + TSS + Ring 3**，每进程独立地址空间/堆栈，才能隔离与并发多用户程序。
  列为终极目标，与"动态链接/共享库/`mmap`"一起。

---

## 3. 设备管理（VFS + 设备抽象）——**最该先做的一块**

当前 `fd_t` 是"扁平"的（`name/buf/pos/dirty` 只对文件有意义）。核心升级：
**把 fd 表改成 vnode + 操作函数指针**：

```c
typedef enum { DT_FILE, DT_CHARDEV, DT_BLOCKDEV, DT_PIPE, DT_PROCFS } dev_type;

struct dev_ops {
    int (*open) (void *ctx, int flags);
    int (*close)(void *ctx);
    int (*read) (void *ctx, char *buf, int len);
    int (*write)(void *ctx, const char *buf, int len);
    int (*ioctl)(void *ctx, int cmd, int arg);   // 新 syscall 15
    int (*lseek)(void *ctx, int off, int whence);
};
```

- 内核维护**驱动注册表**，各驱动（串口/并口/IDE/网卡/FAT/管道/procfs）
  `register_dev(name, ops)`。
- `/dev` 变"名字 → 设备节点"映射：`DEV\CONSOLE`、`DEV\TTYS0..3`、`DEV\LPT1`、
  `DEV\NULL`、`DEV\ZERO`、`DEV\RANDOM`、`DEV\HD0`、`DEV\FD0`。
- 新增 **`ioctl`（syscall 15）**，承载一切"非字节流"控制：波特率、终端原始模式、
  屏幕尺寸、块设备几何、网卡状态。
- `DEV\PROC` → 伪文件系统，读 `PROC\CPUINFO`、`PROC\MEMINFO` 返回内核统计。

**意义**：做完这层，串口/并口/硬盘/网络全挂进同一 fd 框架，`printf` 能直接写串口
（`fopen("/dev/ttys0","w")`），后面所有子系统省一半力。

---

## 4. 串口 / 并口（最快见效）

**串口（16550 UART）**
- COM1=`0x3F8`、COM2=`0x2F8`、COM3=`0x3E8`、COM4=`0x2E8`；IRQ4(COM1/3)、IRQ3(COM2/4)。
- 初始化：置 DLAB → 写波特率除数（115200→1）→ 8N1 → 开 FIFO。先轮询收发，暂不碰 IRQ。
- QEMU：`-serial file:log.txt`（日志）、`-serial tcp:127.0.0.1:PORT,server`（交互远程控制台）。
- 价值：**远程控制台（v6.5 已实现：`input_poll()` 统一键盘+串口 RX，`make run-serial` + `./serial-console.sh`）**、内核日志、以及第 5 节 SLIP 网络的地基。

**并口（LPT）**
- LPT1=`0x378`、LPT2=`0x278`：数据(基址)/状态(+1)/控制(+2)。
- QEMU `-parallel file:lpt.txt`。用途有限，但作为"字符设备"第二个实例验证设备抽象。

---

## 5. 网络（两条路 + 一条协议栈）

**路径一（先做，零新硬件）：SLIP/PPP over 串口**
- 把 IP 包用 SLIP 帧封装走 COM1，QEMU 用 `-serial` 接到主机。**没有网卡驱动就能跑 IP**。

**路径二（真正网络）：网卡驱动 + TCP/IP**
- 网卡选型：**NE2000**（`ne2k_isa`/`ne2k_pci`，教科书经典）或 **RTL8139**（`rtl8139`，寄存器更简单）。
  QEMU 用户态网络 `-net user`（SLIRP）让 guest 通过 `10.0.2.x` 直达主机与外网。
- 协议栈自底向上：**ARP → IPv4 → ICMP → UDP → TCP**。
  - 自写 ARP/IP/ICMP/UDP 是绝佳练习，先拿到 **ping、DHCP、DNS、UDP 应用**；
  - **TCP 很难**，到 TCP 这步**移植 lwIP/uIP**（别手搓重传/拥塞控制）。
- **socket 系统调用**（号 20 起）：`socket/bind/listen/accept/connect/send/recv/close/select`。
  或更简单走 BSD 式 `open("tcp:10.0.2.2:80")`。
- 用户程序：`PING.ELF`、`WGET.ELF`（HTTP GET 下载文件到 B:）、`DNS`、微型 HTTP 服务器、
  `TELNET`/`IRC` 客户端。

---

## 6. 中文支持（最大单项，价值最高）

**现状死结**：`0xB8000` 文本模式字模是 8×16 ROM ASCII，单元格 8 像素宽，**装不下 16×16 汉字**。

**唯一正路：切到图形模式 + 软件帧缓冲控制台**
- 显示从文本模式改为 **VGA/VBE 图形模式 + 线性帧缓冲（LFB）**。建议在 `boot.asm` 里
  （进保护模式前）用 BIOS `int 0x10`/VBE 设 `640×480×32` 或 `800×600`，内核接管 LFB 地址。
- 代价：`put_char`/`put_str` 全部改成往 LFB 画点（写 `fb_putc` + 8×16 ASCII 字模 +
  16×16 汉字字模）。地基级改动，波及整个显示子系统。
- **字库**：GB2312 16×16 点阵（HZK16 风格，~256KB）作为 `USR\SHARE\FONT16.FNT` 放 A 盘，
  按需把字形加载进内核堆；混排半角 8×16 + 全角 16×16。
- **编码**：内部统一 **UTF-8/Unicode 码点**（面向未来、和 Linux 工具一致）；但宿主是
  GBK 环境（CP936），宿主写的源文件可能是 GBK，所以**同时支持 GBK 解码**
  （GBK→UCS2 主区算法化偏移映射，成本低）。`printf`/`cat` 能直接渲染中文文本。
- **明确边界**：中文"显示"可行；中文"输入"（拼音 IME）是另一量级工程，属远期。
  前期只做"能看中文"，不做"能打中文"。

---

## 7. 分阶段路线图（按依赖排序）

| 阶段 | 内容 | 量级 |
|---|---|---|
| **P0** | ① 修输入缺口（回车→`\n`+回显）→ 闭环"输入输出 C 程序" ✅v6.4　② 串口 UART（+远程控制台）✅v6.5　③ 并口 LPT ✅v6.5 | 小时级 |
| **P1** | VFS 设备抽象（vnode+ops+ioctl+`/dev`+procfs），把 P0 的设备挂进去 | 天级 |
| **P2** | 可执行文件概念：argv/envp、返回码 `$?`、PATH 搜索、无 `.EXE` 执行、`>`/`>>`/`<`/`\|`、目录树泛化（注：路径遍历已部分落地 v6.5——文件命令支持 `SUB\X.C` 两级目录与 `..`） | 天级 |
| **P3** | **中文**：boot 设 VBE 模式 → LFB 控制台 → 16×16 字库 → UTF-8/GBK 解码 | 周级 XL |
| **P4** | 块设备抽象 + IDE(PATA) + FAT16/32 + `C:` 盘 | 周级 |
| **P5** | **网络**：SLIP（复用 P0 串口）→ NE2000/RTL8139 → ARP/IP/ICMP/UDP → TCP(移植 lwIP) → socket syscall → PING/WGET/DNS/HTTP | 周–月级 XL |
| **P6** | 长远：Ring 3 + 分页 + TSS + 真进程隔离 + 动态链接/共享库/mmap | 月级 |

**建议起步**：先做 **P0-① 输入修复**（十几行，立刻闭环），紧接着 **P1 设备抽象**
——它是最小的一块地基，串口/并口/硬盘/网络全部站在它上面。
