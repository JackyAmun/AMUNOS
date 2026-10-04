# AMUNOS

AMUNOS 是一个面向学习、实验和自举开发的 x86 32 位保护模式操作系统。
项目当前的研发重点是 **DFLAT 文本窗口与控件框架**，而不是图形窗口服务器。

系统可以从 FAT12 启动，在自己的 shell 中使用 TinyCC 编译、链接并运行 C 程序：

```text
HELLO.C -> TCC HELLO.C -o HELLO.ELF -> HELLO.ELF
```

## 当前版本

发行镜像 `A.img` 默认包含：

- TinyCC：系统内自举 C 编译器
- EDIT：基于 FreeDOS EDIT 的文本编辑器移植
- DFLAT：控件展示和文本窗口实验程序
- SYSINFO：查看内核、文件系统和设备信息
- AMUNOS libc、头文件、链接脚本和示例源码

当前版本不启用 GUI syscall，也不把 GUI 演示程序和 WRITE 放入发行镜像。
旧 GUI 窗口服务器和 WRITE 原型仍保留在源码及归档中，便于以后参考或单独恢复。

## 快速构建

推荐在 WSL 中构建。需要 `nasm`、`gcc-multilib`、`binutils`、`python3` 和可选的 QEMU。

```bash
cd /mnt/c/Users/XU/Desktop/OSDev

# 构建完整系统镜像
make -B A.img

# 单独构建主要用户态程序
make -B tcc.elf edit.elf dflat-demo.elf sysinfo.elf

# 文本模式启动
make run
```

生成的主要文件：

| 文件 | 作用 |
| --- | --- |
| `A.img` | 启动盘和系统发行镜像 |
| `kernel.bin` | AMUNOS 内核 |
| `tcc.elf` | TinyCC 用户态编译器 |
| `edit.elf` | EDIT 编辑器 |
| `dflat-demo.elf` | DFLAT 控件展示程序 |
| `sysinfo.elf` | 系统信息工具 |
| `u2gb.bin` | Unicode 到 GB2312 映射表 |

## DFLAT 开发方向

DFLAT 是 FreeDOS EDIT 携带的文本窗口/控件框架。AMUNOS 让它运行在用户态，内核只提供必要的输入、显示、文件和光标服务。

当前展示程序覆盖或复用了以下控件：

- Window、Dialog、Menu、MessageBox
- EditBox、TextBox、多行文本编辑
- Button、CheckBox、RadioButton
- ListBox、ComboBox、SpinButton
- 状态栏、滚动和焦点导航

DFLAT 专用光标 syscall：

| 编号 | 名称 | 作用 |
| --- | --- | --- |
| 68 | `SYS_DFLAT_CURSOR_SET` | 设置坐标和光标形状 |
| 69 | `SYS_DFLAT_CURSOR_GET` | 读取坐标、形状和可见状态 |
| 70 | `SYS_DFLAT_CURSOR_PUSH` | 保存光标状态 |
| 71 | `SYS_DFLAT_CURSOR_POP` | 恢复光标状态 |
| 72 | `SYS_DFLAT_CURSOR_VISIBLE` | 设置光标显隐 |
| 73 | `SYS_DFLAT_CURSOR_SWAP` | 交换两个保存状态 |

其他文本应用服务包括：

- `SYS_CLIP_SET` / `SYS_CLIP_GET`：系统剪贴板
- `SYS_SYSINFO`：系统、文件系统和设备摘要
- `SYS_UTF8TOGB`、`SYS_CJKWCHAR`、`SYS_CJKCLEAR`：中文点阵渲染
- `SYS_VIDEO_BASE`：用户态文本缓冲访问

后续优先完善 TextArea 选择、撤销重做、剪贴板、滚动、中文列宽、焦点协议和用户指针校验。

## 系统结构

```text
boot.asm / head.asm       启动扇区与保护模式入口
kernel.c                  内核初始化和任务启动
syscall.c                 int 0x30 syscall 分发
task.c                    PIT 定时器与任务调度
vga.c / fb.c              文本缓冲、光标、鼠标和中文渲染
kbd.c / mouse.c           键盘与 PS/2 鼠标输入
fs.c / dev.c              FAT 文件系统与设备挂载
elf.c                     ELF32 用户程序加载器
libc/                     用户态 libc、syscall 封装和启动代码
edit-fdos/source/         DFLAT 和 EDIT 源码
edit-fdos/demo.c          DFLAT 展示程序入口
```

## 当前设备兼容性

| 设备 | 当前能力 | 文件系统或格式 |
| --- | --- | --- |
| IDE ATA 主从盘 | 读写 | FAT12/FAT16 稳定，FAT32 受限 |
| AHCI SATA | 只读 | FAT 卷，首个可用端口 |
| ATAPI 光驱 | 只读 | ISO9660 |
| FDC 软盘 | 主要只读 | 1.44MB FAT12 |
| MBR/GPT 分区盘 | 受限 | 暂无统一分区解析层 |

当前文件系统把卷作为整体处理，尚未提供完整 MBR/GPT、UUID、热插拔和跨设备写入事务。软件安装应优先使用可写 FAT 盘。

## 软件安装规划

未来首先实现文本安装器，而不是依赖图形界面：

```text
PKG LIST
PKG INFO NAME
PKG INSTALL A:/PKG/NAME.PKG
PKG VERIFY NAME
PKG REMOVE NAME
```

安装流程计划包含包头校验、临时目录解包、文件校验、依赖检查、`CMDS.BIN` 更新和失败恢复。图形前端如果重新引入，也只作为这个文本安装核心的可选前端。

## GUI 归档

GUI 研发材料保存在：

```text
backups/gui-v0.5-20261004.zip
```

归档包含 GUI/WRITE 相关程序和此前的构建配置。当前内核和 libc 不暴露 GUI syscall；恢复 GUI 时应使用独立分支或单独版本，避免影响 DFLAT ABI。

## 相关文档

- `docs/DFLAT_syscall及控件路线.md`：DFLAT syscall 和控件路线
- `docs/发行版驱动兼容与软件安装方案.md`：设备边界和软件安装设计
- `docs/AMUNOS_当前项目完整分析报告.md`：项目分析与长期设想
- `docs/AMUNOS_开发流程与协作规范.md`：构建和协作约定

## 许可证与第三方代码

AMUNOS 自有代码采用项目现有的 MIT-style 许可约定。DFLAT/EDIT 等第三方代码应以其原始仓库和源文件中的许可说明为准。
