# AMUNOS 当前项目完整分析报告

> 日期：2026-09-02  
> 范围：基于当前 `OSDev/` 目录、`README.md`、`docs/` 设计文档、核心源码与此前关于“自制操作系统编程能力 / GUI / Studio 应用生态”的讨论整理。  
> 重点：漏洞与工程风险、未来发展、GUI 方向、功能体验、应用场景与阶段设想。

---

## 1. 总体判断

AMUNOS 现在已经不是“能启动、能输出字符”的玩具 OS，而是一个开始具有完整个人计算机气质的 32 位 x86 自制操作系统。

当前项目已经具备几条非常关键的能力链：

```text
FAT12 引导
  -> 32 位保护模式内核
  -> FAT12/16/32 + ISO9660 + IDE/FDC/ATAPI/AHCI
  -> ELF 用户程序
  -> TinyCC + minilibc 自举编译
  -> 中文显示
  -> VBE 图形模式
  -> 内核窗口服务器 GUI
  -> 用户态 GUI 示例程序
```

这意味着 AMUNOS 的核心价值不再只是“自制 OS 学习”，而是可以继续发展成：

> 一个带中文能力、自举 C 编程能力、复古 GUI 和生产力工具生态的小型个人工作站。

最值得继续强化的方向不是盲目增加内核功能，而是把现有能力沉淀成稳定 ABI、稳定 GUI 组件、稳定用户态工具，并围绕 WRITE / SHEET / DRAW / PAINT / SYNTH 这类应用反向推动系统成长。

---

## 2. 当前项目结构分析

当前目录可以分为四类。

### 2.1 AMUNOS 主线源码

核心文件集中在根目录：

```text
boot.asm / head.asm       引导与 32 位入口
kernel.c                  kmain 与 shell 主循环
command.c                 shell 命令系统
syscall.c                 int 0x30 系统调用
task.c                    PIT 抢占式任务
mem.c                     内核堆
fs.c                      FAT12/16/32 文件系统
dev.c / dev.h             块设备与自动挂载
fdc.c / atapi.c / ahci.c  软驱 / 光驱 / SATA
iso9660.c                 光盘只读文件系统
elf.c                     ELF32 加载
fb.c / vga.c              VBE framebuffer 与文字渲染
gui.c                     内核 GUI 窗口服务器
kbd.c / mouse.c           PS/2 输入
serial.c                  串口控制台
```

这一层是 AMUNOS 当前真正的系统本体。

### 2.2 用户态与运行时

```text
libc/                     minilibc + syscall 封装
hello.c / inp.c           示例程序
gui/gui-demo.c            GUI 控件演示程序
edit-fdos/                FreeDOS EDIT 移植
tcc.elf / edit.elf        已构建用户程序
```

这里说明 AMUNOS 已经有“用户态程序生态”的雏形，尤其是 TCC 和 EDIT 的存在，让系统从演示内核变成了可编程环境。

### 2.3 构建与测试工具

```text
Makefile
mka_img.py / mkbimg.py / mkcimg.py / mkd32.py / mkiso.py
validate_gui.py
validate_zh.py
validate_editzh.py
validate_storage2.py
qtest.py / qmon.py
```

这里非常重要。像 `validate_gui.py` 这种 QEMU 像素级回归测试，对自制 OS 来说含金量很高。很多 hobby OS 卡在“只能人工截图判断是否正常”，AMUNOS 已经开始走自动化验证路线，这是后续能否持续迭代的关键。

### 2.4 参考项目与历史资产

```text
flash-4th-os/
mikanos/
kernel-master/
FreeBASIC-1.10.1-source/
makar/
知乎文章离线文件
```

这些更像参考、移植、学习材料，不宜和当前 AMUNOS 主线混在一起评价。建议长期保留，但最好在文档里明确哪些是“当前系统”，哪些是“参考资料”，否则新读者会迷路。

---

## 3. 已实现能力评估

### 3.1 引导与内核

当前 README 描述的是 FAT12 引导扇区启动，加载内核后进入 x86 32 位保护模式。内存布局清晰，内核栈、ELF 装载、用户堆、内核堆、VBE LFB 都有明确位置。

优点：

- 引导链路足够直接，利于调试。
- 内核体积仍在约束内。
- 已经拥有抢占式轮转任务，具备并发基础。
- 串口控制台让无 GUI / 无显示调试更可靠。

局限：

- Ring 0 单地址空间，无分页，用户程序和内核隔离很弱。
- 用户态程序虽然通过 syscall 使用系统服务，但地址空间仍缺乏保护边界。
- 当前更接近“单机开发工作站内核”，还不是安全隔离型通用 OS。

### 3.2 文件系统与设备

当前项目已经从早期 FAT12/16 扩展到 FAT32、ISO9660、IDE、FDC、ATAPI、AHCI，并有统一块设备层和自动挂载概念。

这对 AMUNOS 的未来应用生态非常有意义：

- `A:/` 可以作为系统盘。
- `B:/ C:/ D:/` 可以成为数据盘。
- ISO9660 可以用于只读发行介质。
- AHCI 支持让系统不局限于最早期 IDE 模拟。

但目前文件系统仍有明显的“简单 OS 阶段”特征：

- 文件写入路径有容量限制。
- 缺少事务/回滚。
- 长文件名不是主能力。
- 多任务下文件系统一致性仍需更严格保护。

### 3.3 中文能力

AMUNOS 的中文链路是项目的一个强识别点：

```text
HZK16
U2GB.BIN
GB2312 / UTF-8 映射
中文文件名显示
中文文本编辑
CJK 整字删除与选中
```

这非常难得。很多自制 OS 即使有 GUI，也只停留在 ASCII。AMUNOS 的中文支持会直接影响未来 WRITE、数据库、表格、文件管理器的真实可用性。

下一步最自然的目标是输入法，而不是继续证明“能显示中文”。

### 3.4 GUI 0.3

当前 GUI 已经采用 retained-mode 内核窗口服务器：

```text
用户程序
  -> GUI syscall
  -> gui.c 窗口/控件状态
  -> 每窗口离屏 RGB565 buffer
  -> Z-order 合成
  -> VBE framebuffer
```

已具备：

- 多窗口叠放
- 活跃窗口
- 窗口拖动
- 最小化 / 最大化 / 关闭
- Button / Label / Edit / Textarea / List / Checkbox / Radio / Menu / StatusBar
- 菜单栏与下拉弹层
- 中文显示
- 文本选中
- 基本事件轮询

这已经远超“画几个按钮”的 GUI demo。现在最重要的不是增加控件数量，而是规范 GUI ABI、事件模型、控件生命周期、资源限制和主题语言。

---

## 4. 主要漏洞与工程风险

这里的“漏洞”包含安全漏洞、稳定性风险、数据损坏风险和未来扩展风险。由于 AMUNOS 目前是学习/个人工作站型 OS，不必用现代桌面 OS 的安全标准苛责它，但这些风险需要被清楚标记。

### 4.1 缺少内存保护：最高优先级的长期安全风险

当前系统运行在 32 位保护模式，但没有分页隔离，整体是 VA==PA。用户程序可以通过错误指针、越界写、恶意 syscall 参数直接破坏内核数据或其他程序状态。

影响：

- 任意用户态程序都可能导致内核崩溃。
- `sys_puts(char*)`、`sys_read(fd, buf, len)`、GUI 文本参数等都信任用户指针。
- 未来如果出现第三方程序生态，这会成为根本性安全问题。

建议：

- 短期：承认“可信用户程序模型”，在 README 中说明。
- 中期：给 syscall 加用户地址范围检查，至少限制用户指针在 `0x100000..0x400000` 或约定用户区。
- 长期：引入分页、用户/内核页权限、每进程地址空间。

### 4.2 syscall ABI 正在膨胀

当前 GUI syscall 已经扩展到 55 左右。继续按控件逐个新增，会变成：

```text
GUI_BTN
GUI_LABEL
GUI_EDIT
GUI_LIST
GUI_CHECK
GUI_RADIO
GUI_MENU
GUI_SCROLL
GUI_TAB
GUI_TREE
...
```

这会让 ABI 越来越难维护。

建议：

- 保留现有 syscall，保证兼容。
- 新增一层通用 GUI 对象接口：

```text
gui_create(type, rect, flags)
gui_set_prop(id, key, value)
gui_get_prop(id, key, out)
gui_send(id, message, arg)
```

早期可以不完全替换旧 API，但内部结构要先向“对象 + 属性 + 事件”靠拢。

### 4.3 GUI 固定资源上限

当前代码中可见：

```text
GW_MAXWIN   = 8
GW_MAXWID   = 24
GW_MAXITEMS = 24
GW_TXPOOL   = 4
TX_SIZE     = 2048
MAX_FD      = 16
```

这些限制对 demo 很合理，但对真实应用会很快到顶。

影响：

- WRITE 文档超过 2KB 会被截断。
- SHEET 单元格与控件数量不能太多。
- 文件管理器列表超过 24 项需要滚动和虚拟列表。
- 多个 GUI 程序并存时窗口/文本池会紧张。

建议：

- GUI 0.3/0.4 保留固定池，便于稳定。
- 从 GUI 0.5 开始做“固定池 + 动态扩展槽”。
- Textarea 改成分块 buffer 或用户态持有文本，内核只做视图控件。

### 4.4 文件写入仍有数据损坏风险

历史文档已经指出“删除 + 重建”式覆盖写中途失败会丢旧文件。当前 `fs_write_file_in_dir` 仍值得重点关注。

影响：

- 盘满、写失败、FAT 更新失败时，原文件可能已经被破坏。
- WRITE / SHEET 等生产软件会非常依赖保存可靠性。

建议：

- 实现安全保存流程：

```text
写入 .TMP
  -> flush FAT / directory
  -> rename old -> .BAK
  -> rename tmp -> target
  -> 成功后删除 .BAK
```

- 对 `.AWD/.ASH/.ADR` 文件默认保留一个 `.BAK`。
- `fs_write_file_in_dir` 返回值必须被所有上层检查，保存失败要反馈给用户。

### 4.5 目录与文件容量限制

当前可见 `MAX_DIR_SECTORS=256`、`MAX_FILE_CLUSTERS=256`。这对早期软盘环境够用，但对 Studio 应用不够。

影响：

- 大文档、大表格、图片、位图文件容易被截断。
- FAT32 虽然支持较大卷，但上层写入限制仍偏小。

建议：

- 文件写入从固定数组 `clusters[256]` 改成分段分配。
- 大文件读写支持流式 I/O，不再整文件进内存。
- PAINT 的 `.AGR` 不要一次性裸存 640×480 RGB565 到 A:，优先放 C:/D: 或做 RLE。

### 4.6 设备驱动仍是 poll/PIO 风格，性能和鲁棒性有限

IDE PIO、AHCI poll 只读、ATAPI PIO 这些足够当前阶段使用，但不是长期稳定存储栈。

影响：

- 大文件读写体验慢。
- 错误恢复能力弱。
- 多任务并发 I/O 需要更清晰的锁与队列。

建议：

- 保持现在简单可靠，不急着做复杂 DMA。
- 先统一 block cache 和错误码。
- 再考虑异步 I/O、DMA、AHCI 写入。

### 4.7 许可与第三方组件边界

项目包含 FreeDOS EDIT、TinyCC、HZK16 等第三方内容。README 已说明许可，但未来如果发布镜像，需要更严谨。

建议：

- `LICENSES/` 目录集中存第三方许可。
- README 明确哪些组件是 GPL/LGPL/BSD/未知来源。
- 若发布二进制镜像，应附源码/修改说明，尤其是 FreeDOS EDIT 和 TinyCC。

---

## 5. GUI 与视觉方向

此前讨论里有一个重要判断：AMUNOS 不必成为“又一个 Win3.x 克隆”。当前 Classic GUI 很像早期 Windows/中文 PC 软件，这是很好的起点，但最终可以形成更独立的风格：

> TUI 的秩序感 + GUI 的可用性。

### 5.1 保留 Classic GUI 的现实优势

Classic GUI 适合：

- WRITE
- SHEET
- 文件管理器
- 配置工具
- 编程 IDE
- 数据库

它的优点是：

- 鼠标友好。
- 中文输入/编辑更自然。
- 菜单、对话框、列表、状态栏都有成熟范式。
- 低分辨率下信息密度高。

当前推荐继续保留：

```text
灰色桌面
白色窗口
深蓝活跃标题栏
灰色非活跃标题栏
黑色正文
3D 凸起/凹陷控件
宋体/点阵中文
```

### 5.2 引入 AMUNOS Terminal Theme

为了避免落入“普通 Win3.x 仿制”的拥挤赛道，可以在 Classic 之外设计一个 Terminal/Industrial 主题。

视觉语言：

```text
黑/深灰背景
青色、白色、琥珀色少量强调
等宽字体
线框分组
> 作为选择箭头
< OK > / < RUN > 作为命令按钮
状态栏显示 CPU / MEM / DISK / MODE
```

适合应用：

- SYSTEM MONITOR
- PROCESS
- DEVICE
- NETWORK
- DEBUG
- BUILD
- SYNTH / TRACKER

这不是在图形模式里硬做 TUI，而是用 GUI 系统绘制 TUI 风格界面。鼠标、窗口、中文、文件对话框仍然保留。

### 5.3 GUI 控件优先级

当前控件已很多，后续优先补“系统级公共组件”：

```text
ScrollBar
File Dialog
Clipboard
MessageBox / ConfirmBox
Toolbar
GroupBox / Panel
Canvas
```

尤其是：

- ScrollBar：List / Textarea / Sheet 必需。
- File Dialog：所有生产软件必需。
- Clipboard：WRITE / EDIT / SHEET 必需。
- Canvas：DRAW / PAINT / PLOT 必需。

### 5.4 GUI API 建议

短期继续使用现有 syscall，保证 GUI-DEMO 和 EDIT 不受影响。

中期建议新增统一控件模型：

```c
typedef struct {
    int id;
    int type;
    int x, y, w, h;
    int flags;
    int state;
    char text[...];
} gui_control_t;
```

事件也建议扩展为：

```text
WINDOW_CLOSE
WINDOW_RESIZE
MOUSE_DOWN
MOUSE_UP
MOUSE_MOVE
KEY_DOWN
TEXT_CHANGE
FOCUS_IN
FOCUS_OUT
MENU_SELECT
LIST_SELECT
```

当前 `GEV_CLICK / GEV_KEY / GEV_ENTER / GEV_CLOSE` 足够 demo，但真实生产软件需要更细事件。

---

## 6. 功能性体验分析

### 6.1 Shell 体验

AMUNOS 的 Shell 应继续保留 DOS 式路径逻辑：

```text
A:/
B:/DOC/
C:/SRC/
```

这是项目的重要气质。此前讨论里的 `< >` 可以用于 UI 选择和命令按钮，但路径逻辑不要改得太激进。

建议：

- 保留 `DIR / TYPE / COPY / DEL / REN / CD / TCC / EDIT / GUI`。
- 增加 `VIEW` 作为分页查看。
- 增加 `WHERE` 或 `WHICH` 查找命令。
- 增加 `PATH` 但不复杂化，默认 `A:/BIN;A:/SYSTEM;C:/BIN` 即可。
- 增加 `HELP <cmd>` 的分命令帮助。

### 6.2 开发体验

当前 TCC + minilibc + ELF 已形成闭环，这是 AMUNOS 的核心卖点。

推荐强化为：

```text
EDIT HELLO.C
BUILD HELLO.C
RUN HELLO
SIZE HELLO.ELF
HEX HELLO.ELF
```

`BUILD.ELF` 应成为第一批工具之一，用来包装 TCC 参数，降低用户记忆负担。

### 6.3 中文体验

显示已经好，下一步必须是输入：

```text
Ctrl+Space 切换拼音
输入 zhong
候选栏：1中 2种 3重 ...
数字选字 / 空格首选 / Esc 取消
```

建议先做“用户态 IME 模块”，再集成到 EDIT / TAREA / WRITE。

### 6.4 保存体验

生产软件最怕“写了东西保存失败”。AMUNOS 一旦做 WRITE / SHEET，就要把保存反馈做扎实：

```text
保存中...
保存成功 A:/DOC/REPORT.AWD
保存失败: DISK FULL
已保留 REPORT.BAK
```

这比新增一个漂亮控件更重要。

---

## 7. 应用场景设想

AMUNOS 最合适的发展方向不是浏览器/现代桌面，而是早期个人电脑式的“工作室应用集合”。

### 7.1 AMUN WRITE

定位：不是普通文本编辑器，而是 AMUNOS 的 WPS 1.0。

功能：

- `.AWD` 文档格式
- 类 Markdown 标记
- 标题 / 正文 / 列表 / 引用 / 分隔线
- 搜索 / 替换
- 中文输入
- 打印预留
- 文件对话框

`.AWD` 可以保持人类可读：

```text
# 标题

这是正文。

@ 第一项
@ 第二项

> 引用内容
```

这样即使 WRITE 不运行，也能用 `TYPE` 或 `VIEW` 看内容。

### 7.2 AMUN SHEET

定位：早期电子表格。

MVP：

- 26 列 × 100 行
- `=A1+B1`
- `=SUM(A1:A10)`
- `.ASH` 文件
- 单元格选择、编辑、复制

它会推动 GUI 增加：

- 网格绘制
- 滚动条
- 公式栏
- 状态栏
- 剪贴板

### 7.3 AMUN DRAW / PAINT

DRAW 是矢量工具，PAINT 是位图工具。

DRAW 文件 `.ADR` 可以存：

```text
LINE 10 10 100 100 7
RECT 40 40 120 80 2
TEXT 50 50 "AMUNOS"
```

PAINT 文件 `.AGR` 可以先做 RLE 压缩的 RGB565 位图，避免 640×480×2 直接吃掉太多空间。

### 7.4 SYNTH / TRACKER

这是 AMUNOS 最有辨识度的方向之一。早期计算机的声音、音乐、创作工具很符合项目气质。

路线：

```text
PC Speaker SYNTH
  -> 简单音阶 / ADSR 参数
  -> TRACKER pattern 编辑
  -> SoundBlaster / AC97 / MIDI
```

界面可以使用 Terminal Theme，比普通 Classic GUI 更有工业感。

### 7.5 SYSINFO / DEVICES / PROCESS

这些是“系统自我展示”工具，建议优先实现：

- `SYSINFO.ELF`：内核版本、内存、磁盘、GUI、TCC 状态。
- `DEVICES.ELF`：IDE/FDC/ATAPI/AHCI/PCI 列表。
- `PROCESS.ELF`：任务状态。
- `MONITOR.ELF`：CPU tick、内存、水位、磁盘 I/O。

它们能让 AMUNOS 看起来像一台真实机器，而不是只靠 README 证明能力。

---

## 8. 未来发展路线

### P0：稳定当前基线

目标：把 v6.5.6 / GUI 0.3 固化成稳定版本。

任务：

- README、Makefile 注释、docs 版本号统一。
- validate 全部跑通并记录结果。
- 明确主线目录与参考目录。
- 给 syscall 表生成一份 `docs/SYSCALL_ABI.md`。

### P1：图形 syscall 与 Canvas

目标：解锁 SHEET / DRAW / PAINT / PLOT / 游戏。

建议新增：

```text
SYS_FB_PIXEL
SYS_FB_RECT
SYS_FB_LINE
SYS_FB_TEXT_AT
SYS_FB_BLIT
```

同时提供用户态封装：

```c
gfx_pixel()
gfx_rect()
gfx_line()
gfx_text()
gfx_blit()
```

### P2：小工具生态

第一批推荐：

```text
SYSINFO
VIEW
HEX
SIZE
CALC
BUILD
CLOCK
```

这些工具体量小，但会显著提升系统“能用”的感觉。

### P3：输入法

目标：中文显示 + 中文输入闭环。

建议：

- `PYRANGE.TAB` 做最小拼音候选。
- EDIT 和 GUI TAREA 支持候选栏。
- 后续再加入词语候选。

### P4：WRITE MVP

目标：第一个真正的 AMUNOS 生产力应用。

WRITE 能跑起来之后，GUI 的缺口会自然暴露：

- ScrollBar
- Clipboard
- FileDialog
- Undo/Redo
- Search/Replace
- IME

这是很好的开发驱动力。

### P5：SHEET / DRAW / PAINT

目标：AMUNOS Studio 初具规模。

目录建议：

```text
A:/BIN/
A:/SYSTEM/
A:/STUDIO/
A:/DOC/
C:/WORK/
```

### P6：系统级重构

长期才做：

- 分页与用户态隔离
- 更完整的进程模型
- 多窗口多应用并行
- 网络
- 声卡 DMA
- FAT LFN / 更强文件系统
- 安装器与包格式 `.AMN`

---

## 9. 优先级建议

最推荐的近期顺序：

```text
1. 统一文档/版本/ABI 说明
2. 修保存可靠性：TMP + BAK
3. 增加图形 syscall
4. 做 VIEW / HEX / SIZE / BUILD
5. 做 ScrollBar / FileDialog / Clipboard
6. 做拼音 IME MVP
7. 做 WRITE v1
8. 做 SHEET / DRAW / PAINT
9. 做 SYNTH / TRACKER
10. 最后再考虑分页、网络、多窗口完整桌面
```

这个顺序的核心思想是：

> 先让 AMUNOS 更可靠，再让 AMUNOS 更好用，最后再让 AMUNOS 更强大。

---

## 10. 总结

AMUNOS 当前最强的地方，是它已经同时拥有：

- 自举 C 编译能力
- 中文显示能力
- FAT 多盘文件系统
- ELF 用户程序
- 真实 GUI syscall
- 自动化 QEMU 回归测试

这几个能力组合在一起，已经足够支撑一个很有个人风格的 OS 项目。

未来最有价值的方向不是追求现代桌面，而是做成：

```text
AMUNOS Classic
  = DOS 式路径和命令秩序
  + 中文个人计算机体验
  + 复古 GUI
  + 工业/TUI 气质的系统工具
  + WRITE / SHEET / DRAW / PAINT / SYNTH 生产创作生态
```

如果 AMUNOS 能在这个方向上继续推进，它会从“自制操作系统项目”变成“一台有自己性格的小型中文工作站”。这比单纯模仿 Linux、Windows 或再做一个通用桌面环境更有辨识度，也更适合当前代码基础。
