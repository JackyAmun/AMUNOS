# AMUNOS 操作逻辑、应用生态与改进规划

> 本文用于作为 AMUNOS 后续 Shell、用户态工具、生产力软件、UI 与文档格式设计的基础规范。
>
> 核心方向：**保留 DOS 的秩序感与易用性，但不追求 DOS 兼容；吸收 Unix 中优秀的路径与程序组织思想，同时形成 AMUNOS 自己的操作体验。**

---

## 1. 总体设计哲学

AMUNOS 不需要刻意“反 Unix、反 DOS”。

更合适的目标是：

- **视觉上**：参考 DOS / 早期 PC，简洁、规整、信息密度适中。
- **操作上**：简单、直接、容易记忆。
- **命令上**：大小写不敏感。
- **输出上**：默认统一使用大写，形成整齐的系统视觉。
- **路径上**：保留盘符体系，同时统一使用 `/` 作为路径分隔符。
- **程序上**：用户程序以 ELF 为核心，TCC 是官方 C 开发工具。
- **语言上**：未来可加入 BASIC 等更适合快速创作的语言。
- **应用上**：重点发展生产力、创作、工程和工作室类软件，而不只是小游戏。
- **中文上**：逐步加入中文显示与输入能力，但不牺牲英文命令体系。

目标不是“做一个小 Linux”，而是形成一种独立的 **AMUNOS Personal Computer Experience**。

---

# 2. Shell 操作逻辑

## 2.1 大小写不敏感

以下命令完全等价：

```text
DIR
dir
Dir
DiR
```

文件名与路径也可以在 Shell 层保持大小写不敏感。

系统输出建议统一大写：

```text
AMUNOS 1.0

A:/>

LIST

SYSTEM       <DIR>
USR          <DIR>
BIN          <DIR>
HELLO.C          284

4 ITEMS
```

这样可以保留 DOS 那种整齐、机械、明确的视觉风格。

---

## 2.2 Prompt

建议使用：

```text
A:/>
```

或者：

```text
A:/USR/SRC>
```

其中：

- `A:` 表示当前驱动器。
- `/` 表示路径层级。
- `>` 表示等待用户输入。

`>` 可以作为 AMUNOS 的视觉标志。

例如：

```text
AMUNOS 1.0

A:/>

LIST

A:/>
```

如果以后需要显示更多状态，可以提供可选的状态栏，而不是强迫 Prompt 过长。

---

# 3. 路径规范

路径系统建议保持明确、稳定，不要为了追求“独特”而改变已经比较成熟的设计。

## 推荐格式

```text
A:/BIN
A:/USR
A:/USR/SRC
A:/SYSTEM
A:/GAMES
```

绝对路径：

```text
A:/USR/SRC/HELLO.C
```

相对路径：

```text
./HELLO.C
../LIB
```

当前目录：

```text
.
```

上级目录：

```text
..
```

盘符：

```text
A:
B:
C:
```

完整路径：

```text
C:/USR/INCLUDE/STDIO.H
```

---

## 3.1 为什么保留盘符

盘符是 AMUNOS 的重要个人电脑属性。

可以形成：

```text
A:  FLOPPY / SYSTEM
B:  FLOPPY / DATA
C:  HARD DISK
D:  RAM DISK
```

未来甚至可以：

```text
E:  CD-ROM
```

这样 AMUNOS 会拥有很明显的“个人电脑”特征。

---

# 4. 命令设计

不需要完全复制 DOS，也不需要强行采用 Unix 风格。

推荐使用**自然、短、容易记忆的英文单词**。

## 文件操作

```text
LIST
OPEN
COPY
MOVE
DELETE
RENAME
TYPE
EDIT
```

可以保留 DOS 用户熟悉的命令，也可以逐步加入 AMUNOS 自己的命令。

例如：

```text
LIST
```

比强制使用：

```text
LS
```

更符合 AMUNOS 的整体气质。

---

## 4.1 参数

推荐保留简单参数：

```text
LIST
LIST ALL
LIST PAGE
COPY A.TXT B.TXT
DELETE TEST.TXT
RUN HELLO.ELF
```

不要过度设计成 Unix 那种大量短参数：

```text
ls -lah
grep -rin
```

但也不需要禁止参数。

可以使用：

```text
LIST /PAGE
LIST /ALL
```

或者：

```text
LIST PAGE
LIST ALL
```

最终选择一种统一规范即可。

---

# 5. Shell 可以加入的功能

## 第一阶段

```text
PATH
SET
ECHO
CLS
HELP
VER
TIME
DATE
MEM
DEVICE
```

例如：

```text
A:/>

MEM

TOTAL MEMORY   640 KB
FREE MEMORY    312 KB
```

```text
A:/>

DEVICE

VIDEO       VGA
KEYBOARD    AT
MOUSE       PS/2
DISK A      FAT12
SERIAL 0    COM1
```

---

## 第二阶段

增加：

```text
>
>>
<
|
```

支持：

```text
LIST > FILE.TXT
TYPE FILE.TXT
```

以及：

```text
TYPE A.TXT | MORE
```

但管道不必成为 AMUNOS 的核心操作方式。

它应该是**方便功能**，而不是 Shell 的中心哲学。

---

# 6. 建议加入统一的系统信息工具

## SYSINFO.ELF

示例：

```text
AMUNOS SYSTEM INFORMATION
--------------------------------

VERSION       1.0
CPU           I386
MEMORY        640 KB
FREE MEMORY   312 KB

DISPLAY       VGA
KEYBOARD      AT
MOUSE         PS/2

DISK A        FAT12
DISK B        FAT12

SERIAL 0      COM1
NETWORK       OFF

TASKS         3
--------------------------------
```

这是很适合 AMUNOS 的官方工具。

---

# 7. 小工具生态

建议建立：

```text
A:/BIN/
```

第一批工具：

```text
SYSINFO.ELF
CALC.ELF
CLOCK.ELF
VIEW.ELF
HEX.ELF
SIZE.ELF
BUILD.ELF
PACK.ELF
FONT.ELF
```

---

## 7.1 CALC

支持：

```text
+ - * /
()
```

未来支持：

```text
SIN()
COS()
SQRT()
LOG()
```

---

## 7.2 VIEW

用于查看文本：

```text
VIEW README.TXT
VIEW SOURCE.C
```

可以加入分页：

```text
VIEW FILE.TXT /PAGE
```

---

## 7.3 HEX

用于开发和调试：

```text
HEX PROGRAM.ELF
```

输出：

```text
00000000  7F 45 4C 46 01 01 01 00
00000008  00 00 00 00 00 00 00 00
```

---

## 7.4 SIZE

显示 ELF 程序大小：

```text
SIZE HELLO.ELF

TEXT    18240
DATA     1024
BSS      4096
TOTAL   23360
```

---

# 8. BUILD.ELF

这是非常值得开发的工具。

目标：

```text
BUILD HELLO.C
```

自动：

```text
SOURCE
  ↓
TCC
  ↓
LINK
  ↓
ELF
```

输出：

```text
AMUN C BUILD

SOURCE   HELLO.C
COMPILER TCC
TARGET   I386

[1/3] COMPILE
[2/3] LINK
[3/3] VERIFY

BUILD SUCCESSFUL

HELLO.ELF CREATED
```

进一步：

```text
BUILD HELLO.C /RUN
```

实现：

```text
COMPILE
LINK
RUN
```

形成完整的 AMUNOS 内部 C 开发循环。

---

# 9. PACK.ELF

建立自己的软件包格式。

建议扩展名：

```text
.AMN
```

例如：

```text
HELLO.AMN
```

包含：

```text
HELLO.ELF
README.TXT
```

安装：

```text
INSTALL HELLO.AMN
```

输出：

```text
INSTALLING HELLO

HELLO.ELF       OK
README.TXT      OK

INSTALL COMPLETE
```

以后可以形成：

```text
AMUNOS SOFTWARE PACKAGE
```

体系。

---

# 10. AMUNOS 的重点：生产力与创作软件

AMUNOS 不应该只有：

```text
SNAKE
TETRIS
PONG
```

这些游戏。

更重要的是：

```text
WRITE
SHEET
DRAW
PAINT
MUSIC
SYNTH
TRACKER
DATABASE
MATH
```

这更接近早期个人计算机的实际应用生态。

---

# 11. AMUN WRITE

现有 EDIT 主要用于：

```text
编辑源代码
```

建议另外开发：

```text
WRITE.ELF
```

定位：

> 文档处理软件，而不是程序编辑器。

功能：

```text
NEW
OPEN
SAVE
SAVE AS
PRINT
SEARCH
REPLACE
PAGE
FONT
ALIGN
```

---

# 12. AMUN WRITE 文档格式

可以使用一种**类似 Markdown、但更适合 AMUNOS 的纯文本标记格式**。

建议：

```text
.AWD
```

AMUNOS WRITE Document。

示例：

```text
# AMUNOS

## INTRODUCTION

AMUNOS 是一个面向个人计算机的操作系统。

这是正文。

**重点内容**

- 第一项
- 第二项
```

这种格式的优势：

1. 人可以直接阅读。
2. TCC 可以很容易生成。
3. 不依赖复杂二进制格式。
4. 可以通过转换器生成纯文本。
5. 以后可以加入打印排版。
6. 未来 GUI WRITE 可以直接解析。

---

## 12.1 不必完全兼容 Markdown

可以逐步加入自己的标记：

```text
# TITLE
## HEADING

**BOLD**
*ITALIC*

[IMAGE: LOGO.BMP]

[PAGE]

[ALIGN:CENTER]

[FONT:SONG]
```

因此它可以是：

> Markdown-like + AMUNOS extensions

而不是严格 Markdown。

---

# 13. AMUN SHEET

早期个人计算机非常重要的生产力应用。

例如：

```text
     A       B       C       D
1   NAME    QTY     PRICE   TOTAL
2   APPLE   10      2.5     =B2*C2
3   PEAR     5      3       =B3*C3
```

第一阶段支持：

```text
SUM
AVERAGE
MAX
MIN
```

文件：

```text
.ASH
```

AMUNOS Spreadsheet。

---

# 14. AMUN DRAW

用于：

- 流程图
- 简单 CAD
- 示意图
- 平面设计

基本对象：

```text
LINE
BOX
CIRCLE
TEXT
MOVE
COPY
DELETE
```

文件：

```text
.ADR
```

---

# 15. AMUN PAINT

像早期 PC 的简单绘图软件。

支持：

```text
PENCIL
LINE
BOX
CIRCLE
FILL
TEXT
ERASE
```

保存：

```text
.BMP
```

或者 AMUNOS 自己的：

```text
.AGR
```

---

# 16. AMUN MUSIC / SYNTH

这是 AMUNOS 可以形成特色的应用方向。

第一阶段：

```text
SYNTH.ELF
```

支持：

```text
WAVE
FREQUENCY
VOLUME
ATTACK
DECAY
SUSTAIN
RELEASE
```

PC 键盘可以模拟键盘乐器：

```text
A W S E D F T G Y H U J K
```

对应：

```text
C C# D D# E F F# G G# A A# B
```

以后加入：

```text
MUSIC.ELF
TRACKER.ELF
MIDI.ELF
SEQUENCER.ELF
```

最终形成：

```text
AMUNOS MUSIC STUDIO
```

---

# 17. AMUN DATABASE

适合：

- 客户管理
- 库存
- 学生名单
- 财务记录
- 小型数据管理

基本操作：

```text
CREATE
ADD
EDIT
FIND
LIST
DELETE
PRINT
```

数据格式：

```text
.ADB
```

---

# 18. AMUN MATH / PLOT

科学计算：

```text
SIN()
COS()
SQRT()
LOG()
```

并支持：

```text
PLOT SIN(X)
```

以后可用于：

- 工程计算
- 数学教学
- 数据分析

---

# 19. BASIC

TCC 是 AMUNOS 的原生 C 开发工具。

BASIC 可以作为另一种更容易创作的语言：

```text
BASIC

READY.

10 PRINT "HELLO AMUNOS"
20 GOTO 10

RUN
```

重点支持：

```text
PRINT
INPUT
LET
IF
THEN
GOTO
GOSUB
RETURN
FOR
NEXT
DIM
END
```

未来增加：

```text
SCREEN
COLOR
LOCATE
SOUND
```

这样 BASIC 可以直接制作小游戏和教学程序。

---

# 20. 游戏

游戏不是主要方向，但可以作为 API 测试和软件生态的一部分。

推荐：

```text
PONG
SNAKE
TETRIS
MINES
2048
```

这些程序可以验证：

- 键盘
- 鼠标
- 定时器
- 图形 API
- 随机数
- 文件系统

---

# 21. 中文支持

中文应该作为正式能力逐步加入。

推荐：

```text
UTF-8
+
中文点阵字体
```

第一阶段不需要完整 Unicode 字体。

可以先支持：

```text
ASCII
+
GB2312 常用中文
```

或直接内部使用 Unicode code point + AMUNOS 字库。

---

## 21.1 中文显示

VGA text mode 不适合直接显示中文。

建议逐步加入：

```text
VGA framebuffer
```

使用：

```text
8x16 英文
16x16 中文
```

这样可以实现：

```text
AMUNOS 1.0

A:/>

你好，AMUNOS。

READY>
```

---

## 21.2 中文命令

不建议把系统完全中文化。

推荐：

```text
LIST
EDIT
RUN
COPY
DELETE
```

仍然是官方命令。

但可以增加中文别名：

```text
列目录 → LIST
运行   → RUN
编辑   → EDIT
复制   → COPY
删除   → DELETE
```

这样：

```text
A:/> 列目录
```

也可以执行。

系统 API 和程序语言仍然以英文为主。

---

# 22. 配色规范

AMUNOS 应该保持一种非常克制的“早期计算机 + 工作站”视觉。

## 默认 Shell

推荐：

```text
背景：深蓝 / 近黑蓝
文字：浅灰 / 白
提示符：亮白
强调：青色
警告：黄色
错误：红色
```

不要大量使用鲜艳渐变。

推荐感觉：

```text
深蓝背景
白色文字
青色标题
灰色分隔线
黄色状态
红色错误
```

---

## 22.1 推荐基础调色板

```text
BLACK      #000000
DARKBLUE   #000080
BLUE       #0000AA
CYAN       #00AAAA
GREEN      #00AA00
GRAY       #AAAAAA
WHITE      #FFFFFF
YELLOW     #FFFF55
RED        #FF5555
```

整体尽量接近经典 VGA / DOS 调色板。

---

# 23. 图形应用配色

GUI / Studio 不必完全使用 Shell 的深蓝背景。

可以使用：

```text
WINDOW     #C0C0C0
TITLE      #000080
TEXT       #000000
HIGHLIGHT  #FFFFFF
SHADOW     #808080
ACCENT     #008080
```

形成经典的：

> 早期 GUI / Windows 3.x / OS/2 / 90年代工作站

感觉。

---

# 24. 软件生态目录

建议逐步形成：

```text
A:/
├── SYSTEM/
│
├── BIN/
│   ├── TCC.ELF
│   ├── EDIT.ELF
│   ├── SYSINFO.ELF
│   ├── CALC.ELF
│   ├── VIEW.ELF
│   ├── HEX.ELF
│   ├── BUILD.ELF
│   └── PACK.ELF
│
├── STUDIO/
│   ├── WRITE.ELF
│   ├── SHEET.ELF
│   ├── DRAW.ELF
│   ├── PAINT.ELF
│   ├── MUSIC.ELF
│   ├── SYNTH.ELF
│   ├── TRACKER.ELF
│   ├── DATABASE.ELF
│   └── MATH.ELF
│
├── GAMES/
│   ├── PONG.ELF
│   ├── SNAKE.ELF
│   ├── TETRIS.ELF
│   └── MINES.ELF
│
├── USR/
│   ├── INCLUDE/
│   ├── LIB/
│   └── SRC/
│
└── DOC/
```

---

# 25. 最终目标

AMUNOS 的应用生态应该形成：

```text
                 AMUNOS
                    │
       ┌────────────┼────────────┐
       │            │            │
   DEVELOPMENT   STUDIO       SYSTEM
       │            │            │
      TCC          WRITE       SYSINFO
      EDIT         SHEET       VIEW
      BUILD        DRAW        HEX
      BASIC        MUSIC       DISK
                   PAINT
                    │
                    │
                 GAMES
                    │
            PONG / SNAKE / ...
```

最终让 AMUNOS 不只是：

> “一个可以运行 C 程序的自制操作系统”

而成为：

> **一套完整的个人计算机环境。**

它可以：

- 编程
- 写文章
- 做表格
- 绘图
- 做音乐
- 管理数据
- 做工程计算
- 编辑中文
- 玩游戏
- 使用串口和网络

这才是 AMUNOS 最值得发展的方向。

---

# 26. 推荐开发优先级

## 第一优先级

```text
SYSINFO
CALC
VIEW
HEX
BUILD
```

## 第二优先级

```text
AMUN WRITE
AMUN SHEET
AMUN MATH
AMUN PAINT
```

## 第三优先级

```text
BASIC
SYNTH
MUSIC
TRACKER
DATABASE
```

## 第四优先级

```text
中文字体
Framebuffer
中文输入
GUI
```

## 第五优先级

```text
PONG
SNAKE
TETRIS
MINES
```

---

## 核心原则

**不要为了“像 DOS”而复制 DOS。**

**不要为了“现代”而复制 Unix/Linux。**

AMUNOS 应该：

> **保留 DOS 的秩序、Unix 的优秀路径思想、早期个人电脑的创作精神，以及自己的软件格式和操作体验。**

最终形成属于 AMUNOS 自己的一套计算机文化。
