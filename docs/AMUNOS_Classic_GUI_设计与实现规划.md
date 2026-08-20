# AMUNOS Classic GUI 设计与实现规划

> 本文根据当前 AMUNOS GUI syscall 演示程序整理。
>
> 当前已实现的基础能力包括：窗口创建、窗口拖动、标题栏、按钮、文本输入/编辑框、列表框、中文显示，以及基本的多窗口叠放。
>
> **窗口拖动已经实现，因此后续重点不再放在窗口移动，而是放在 Window Manager、控件、事件、文本系统和生产力软件所需的基础能力上。**

---

# 1. GUI 总体定位

AMUNOS 不需要追求现代 GUI。

推荐形成：

> **VGA + Windows 3.x / Win9x + 早期中文 PC 软件**

的视觉与交互风格。

关键词：

- 简单
- 规整
- 高信息密度
- 键盘友好
- 鼠标友好
- 低资源消耗
- 中文友好
- 不依赖复杂图形加速
- 控件具有明显的 3D 凸起 / 凹陷效果

目标不是复制 Windows，而是建立 **AMUNOS Classic GUI**。

---

# 2. 当前 GUI 基础

当前演示已经具备：

```text
WINDOW
TITLE BAR
BUTTON
TEXT / LABEL
EDIT / TEXTBOX
LISTBOX
CHINESE TEXT
MULTI-WINDOW
WINDOW DRAG
```

当前已经足以作为 GUI syscall 的原型。

下一阶段不应该单纯继续增加按钮，而应该完善：

```text
Window Manager
      ↓
Event System
      ↓
Focus
      ↓
Control System
      ↓
Text / Input
      ↓
Menu / Dialog
      ↓
生产力软件
```

---

# 3. 推荐控件体系

建议把控件分成几个等级。

## 3.1 第一层：基础控件

这些应该最先完善：

```text
LABEL
BUTTON
EDIT
LISTBOX
CHECKBOX
RADIOBUTTON
```

---

## 3.2 第二层：容器与导航

```text
WINDOW
DIALOG
PANEL
MENU
MENUBAR
SCROLLBAR
STATUSBAR
```

---

## 3.3 第三层：高级控件

以后根据应用需要增加：

```text
COMBOBOX
LISTVIEW
TAB
TREEVIEW
PROGRESSBAR
SLIDER
SPINBOX
```

不要为了“控件数量”而开发。

应该遵循：

> **有实际应用需要，再加入控件。**

---

# 4. LABEL

最简单的控件。

用途：

```text
输入：
姓名：
地址：
文件：
```

数据：

```c
text
x
y
font
color
```

绘制：

```text
draw_text(x, y, text);
```

Label 不需要复杂事件。

---

# 5. BUTTON

当前已经存在。

建议增加：

```text
NORMAL
HOVER
PRESSED
DISABLED
FOCUS
```

状态：

```text
BUTTON_NORMAL
BUTTON_HOVER
BUTTON_DOWN
BUTTON_DISABLED
```

最简单实现方式：

```text
鼠标 DOWN
    ↓
判断坐标是否在 Button
    ↓
设置 PRESSED
    ↓
鼠标 UP
    ↓
触发 BUTTON_CLICK
```

键盘：

```text
ENTER
SPACE
```

也应该可以触发按钮。

---

# 6. EDIT / TEXTBOX

这是 AMUNOS 最重要的控件之一。

因为未来：

```text
WRITE
BASIC
SHELL
DATABASE
CALC
```

都会需要它。

## 单行 Edit

支持：

```text
← →
HOME
END
BACKSPACE
DELETE
```

以后：

```text
SHIFT + ←
SHIFT + →
CTRL + A
CTRL + C
CTRL + X
CTRL + V
```

---

## 多行 Edit

用于：

```text
AMUN WRITE
SOURCE CODE
README
TEXT VIEWER
```

需要维护：

```text
text buffer
cursor position
selection
scroll position
line number
```

---

# 7. Edit 的简单实现模型

不需要一开始做复杂文本引擎。

可以使用：

```text
char **lines
```

或者：

```text
一块连续文本缓冲区
+
行偏移表
```

例如：

```text
TEXT BUFFER

Hello AMUNOS
你好，世界
This is a test
```

建立：

```text
line[0] = 0
line[1] = 13
line[2] = 27
```

这样可以快速定位某一行。

---

# 8. 光标

光标应该由 Edit 自己维护：

```text
cursor_x
cursor_y
```

或者：

```text
cursor_offset
```

绘制：

```text
draw_cursor()
```

可以先使用简单的闪烁矩形：

```text
█
```

不需要复杂动画。

以后可以：

```text
CURSOR ON
CURSOR OFF
```

使用 timer 控制闪烁。

---

# 9. LISTBOX

当前已经有 ListBox。

建议完善：

```text
SELECT
MULTI_SELECT
SCROLL
KEYBOARD NAVIGATION
DOUBLE CLICK
```

键盘：

```text
↑
↓
HOME
END
PAGEUP
PAGEDOWN
ENTER
```

鼠标：

```text
CLICK
DOUBLE CLICK
```

---

# 10. ScrollBar

这是 ListBox 和多行 Edit 必需的。

最简单结构：

```text
┌──────┐
│  ▲   │
├──────┤
│      │
│ ████ │
│      │
├──────┤
│  ▼   │
└──────┘
```

内部只需要：

```text
min
max
position
page_size
```

绘制：

```text
arrow up
track
thumb
arrow down
```

核心逻辑：

```text
position / (max - min)
```

计算 thumb 位置即可。

---

# 11. CHECKBOX

用途：

```text
[✓] 中文显示正常
[ ] 自动保存
```

状态：

```text
checked
unchecked
disabled
```

点击：

```text
checked = !checked
```

即可。

---

# 12. RADIOBUTTON

用途：

```text
(●) VGA
( ) CGA
( ) MONO
```

实现时给每个 RadioButton 设置：

```text
group_id
```

同组只能有一个：

```text
selected = true
```

点击新的 RadioButton：

```text
清除同组
↓
选择当前
```

---

# 13. MENU / MENUBAR

这是生产软件必须加入的。

例如：

```text
文件(F)  编辑(E)  查看(V)  工具(T)  帮助(H)
```

最简单的数据结构：

```c
MENU {
    title;
    items[];
}
```

MenuItem：

```c
MENU_ITEM {
    text;
    shortcut;
    enabled;
    checked;
    submenu;
}
```

---

## 菜单示例

```text
文件(F)
├── 新建
├── 打开
├── 保存
├── 另存为
├────────
├── 打印
├────────
└── 退出
```

不用一开始做复杂菜单。

只要实现：

```text
鼠标点击
+
键盘 ALT
+
方向键
+
ENTER
+
ESC
```

即可。

---

# 14. DIALOG

Dialog 是所有生产软件都会使用的东西。

例如：

```text
┌───────────────────────────────┐
│ AMUNOS WRITE                  │
├───────────────────────────────┤
│                               │
│ 文件尚未保存。                 │
│ 是否保存？                     │
│                               │
│      [保存] [不保存] [取消]    │
└───────────────────────────────┘
```

建议提供：

```text
MESSAGEBOX
INPUTBOX
FILEOPEN
FILESAVE
CONFIRM
ERROR
```

最简单实现：

```text
创建窗口
+
设置 modal = true
+
阻止父窗口处理输入
```

---

# 15. Focus 系统

这是目前 GUI 最值得优先实现的基础设施之一。

每个 Window：

```text
focused_control
```

例如：

```text
WINDOW
├── BUTTON
├── LISTBOX
└── EDIT ← FOCUS
```

按：

```text
TAB
```

切换：

```text
BUTTON
 ↓
LISTBOX
 ↓
EDIT
 ↓
BUTTON
```

建议控件拥有：

```text
focusable
focused
```

两个状态。

---

# 16. Event 系统

建议所有 GUI 程序使用统一事件结构。

例如：

```c
GUI_EVENT {
    type;
    window;
    control;
    x;
    y;
    key;
    button;
}
```

事件类型：

```text
WINDOW_CLOSE
WINDOW_RESIZE

MOUSE_DOWN
MOUSE_UP
MOUSE_MOVE

KEY_DOWN
KEY_UP

BUTTON_CLICK
LIST_SELECT
LIST_DBLCLICK

TEXT_CHANGE

FOCUS_IN
FOCUS_OUT
```

---

# 17. GUI 程序模型

建议：

```text
程序
 ↓
GUI syscall
 ↓
事件队列
 ↓
程序获取事件
 ↓
处理事件
 ↓
更新控件
 ↓
重绘
```

典型结构：

```c
while (gui_get_event(&event)) {

    switch (event.type) {

        case BUTTON_CLICK:
            ...

        case KEY_DOWN:
            ...

        case WINDOW_CLOSE:
            ...

    }
}
```

这样所有 GUI 应用可以共享同一套逻辑。

---

# 18. Window Manager

窗口拖动已经实现。

下一步重点是：

```text
Z-ORDER
ACTIVE WINDOW
FOCUS
MINIMIZE
MAXIMIZE
RESIZE
MODAL
```

窗口结构可以包含：

```c
WINDOW {
    x;
    y;
    width;
    height;

    title;

    visible;
    active;
    minimized;
    maximized;
    modal;

    parent;

    controls;
}
```

---

# 19. Z-Order

窗口应该形成简单链表或数组：

```text
BACK
 ↓
Window A
 ↓
Window B
 ↓
Window C ← ACTIVE
FRONT
```

点击窗口：

```text
bring_to_front(window);
```

即可。

当前截图已经展示了多窗口叠放，因此下一步只需把这个行为系统化。

---

# 20. Minimize / Maximize

不需要复杂桌面系统。

最简单：

```text
MINIMIZE
```

保存：

```text
old_x
old_y
old_width
old_height
```

然后：

```text
visible = false
```

以后恢复即可。

Maximize：

```text
x = 0
y = 0
width = SCREEN_WIDTH
height = SCREEN_HEIGHT
```

如果未来做任务栏，再进一步完善。

---

# 21. 配色

AMUNOS Classic GUI 建议固定一套基础颜色。

## 基础

```text
BLACK       #000000
WHITE       #FFFFFF
GRAY        #C0C0C0
DARK_GRAY   #808080
```

## 标题栏

```text
ACTIVE      #000080
INACTIVE    #808080
```

## 文字

```text
TEXT        #000000
TITLE TEXT  #FFFFFF
```

## 强调

```text
HIGHLIGHT       #000080
HIGHLIGHT TEXT  #FFFFFF
```

---

# 22. 经典控件 3D 效果

推荐所有控件统一使用：

```text
LIGHT
FACE
SHADOW
DARK
```

例如 Button：

```text
┌──────────────┐
│    确定      │
└──────────────┘
```

边缘：

```text
左 / 上 = LIGHT
主体    = FACE
右 / 下 = SHADOW
```

按下：

```text
边缘方向反转
```

这样所有：

```text
BUTTON
CHECKBOX
RADIO
SCROLLBAR
```

都有统一的视觉语言。

---

# 23. GUI 背景

桌面推荐：

```text
GRAY
```

而不是纯白。

窗口：

```text
WHITE
```

这样：

```text
桌面
████████████████

┌──────────────┐
│ 窗口         │
│              │
└──────────────┘
```

层次非常明显。

---

# 24. 中文字体

当前中文已经可以显示，这是 AMUNOS 的重要能力。

建议以后分：

```text
SYSTEM FONT
UI FONT
EDITOR FONT
```

## SYSTEM FONT

用于：

```text
Shell
Terminal
开发工具
```

可以使用等宽点阵字体。

## UI FONT

用于：

```text
Button
Menu
Dialog
Label
```

可以使用比例字体。

## EDITOR FONT

用于：

```text
WRITE
CODE
TEXT
```

优先考虑等宽。

---

# 25. 不要让每个程序自己处理中文

建议架构：

```text
Keyboard
    ↓
Input System
    ↓
IME / Text Input
    ↓
GUI
    ↓
Focused Edit
```

这样：

```text
Shell
WRITE
DATABASE
BASIC
```

可以共享输入系统。

---

# 26. 剪贴板

这是 Edit 和 WRITE 后续必须拥有的系统服务。

最简单：

```text
CLIPBOARD BUFFER
```

API：

```c
clipboard_set(text);
clipboard_get();
clipboard_clear();
```

支持：

```text
CTRL+C
CTRL+X
CTRL+V
```

第一阶段甚至可以只支持纯文本。

---

# 27. 文件选择框

生产软件需要：

```text
OPEN
SAVE
```

因此建议做统一 File Dialog：

```text
┌───────────────────────────────────┐
│ 打开                               │
├───────────────────────────────────┤
│ A:/USR/DOC                        │
│                                   │
│ README.TXT                        │
│ TEST.AWD                          │
│ REPORT.AWD                        │
│                                   │
│ 文件名：[______________]          │
│                                   │
│        [打开]       [取消]        │
└───────────────────────────────────┘
```

这个组件可以直接被：

```text
WRITE
SHEET
DRAW
PAINT
MUSIC
```

共享。

---

# 28. 状态栏

生产软件很适合加入：

```text
┌──────────────────────────────────────┐
│ 就绪                 第 1 行  第 1 列 │
└──────────────────────────────────────┘
```

例如 WRITE：

```text
就绪                    第 2 行 第 8 列
```

SHEET：

```text
就绪                    A2
```

DRAW：

```text
选择对象                X:120 Y:80
```

---

# 29. 控件实现原则

不要把所有东西写成巨大的：

```c
if (button)
if (textbox)
if (list)
```

建议使用统一结构：

```c
CONTROL {
    type;
    x;
    y;
    width;
    height;

    visible;
    enabled;
    focused;

    draw();
    event();
}
```

例如：

```text
CONTROL
├── BUTTON
├── EDIT
├── LISTBOX
├── CHECKBOX
└── RADIOBUTTON
```

每个控件负责自己的：

```text
绘制
事件
状态
```

Window 负责：

```text
管理
布局
焦点
Z-order
```

---

# 30. 不建议现在加入的东西

暂时不要做：

```text
透明窗口
圆角窗口
渐变
玻璃效果
复杂动画
现代阴影
GPU UI
Material Design
Fluent Design
```

AMUNOS 的优势不是现代化，而是：

> **一种完整而统一的早期个人计算机体验。**

---

# 31. 推荐的 GUI 开发顺序

## GUI 0.1

当前基础：

```text
Window
Button
Label
Edit
ListBox
中文
窗口拖动
```

已经基本完成。

---

## GUI 0.2

重点：

```text
Window Manager
Z-order
Active Window
Focus
Event Queue
Keyboard Navigation
```

---

## GUI 0.3

加入：

```text
Checkbox
RadioButton
ScrollBar
Menu
MenuBar
Dialog
StatusBar
```

---

## GUI 0.4

加入：

```text
多行 Edit
Selection
Clipboard
File Dialog
中文输入
字体管理
```

---

## GUI 0.5

完善：

```text
Minimize
Maximize
Resize
Taskbar（可选）
```

---

## GUI 0.6

开始制作真实软件：

```text
AMUN WRITE
AMUN SHEET
AMUN DRAW
AMUN PAINT
```

生产软件本身将成为 GUI syscall 的测试平台。

---

# 32. 最终软件架构

理想结构：

```text
┌──────────────────────────────────────┐
│             AMUNOS APP               │
│                                      │
│ WRITE / SHEET / DRAW / MUSIC / BASIC │
└──────────────────┬───────────────────┘
                   │
             AMUN GUI API
                   │
             GUI SYSCALL
                   │
        ┌──────────┴──────────┐
        │     WINDOW MANAGER  │
        │                     │
        │ Window              │
        │ Control             │
        │ Focus               │
        │ Event               │
        │ Menu                │
        │ Dialog              │
        └──────────┬──────────┘
                   │
             Graphics Layer
                   │
          VGA / Framebuffer
```

---

# 33. 与 AMUNOS Shell 的关系

GUI 不需要取代 Shell。

两者应该并存：

```text
AMUNOS
│
├── SHELL
│    └── 命令驱动
│
└── GUI
     └── 事件驱动
```

共享：

```text
文件系统
进程
设备
字体
输入
剪贴板
```

Shell：

```text
A:/USR>

LIST
RUN WRITE.ELF
```

GUI：

```text
┌──────────────────────────────┐
│ 文件(F) 编辑(E) 查看(V)      │
├──────────────────────────────┤
│                              │
│                              │
└──────────────────────────────┘
```

这样 AMUNOS 可以同时拥有：

> DOS 式命令行秩序 + 早期 PC GUI。

---

# 34. 最终视觉定位

AMUNOS Classic GUI：

```text
        AMUNOS CLASSIC GUI

     VGA
      +
 Windows 3.x
      +
 Win9x
      +
 中文早期 PC 软件
      +
 AMUNOS 自己的设计
```

核心视觉：

```text
灰色桌面
白色窗口
深蓝标题栏
白色标题文字
黑色正文
灰色 3D 控件
宋体 / 点阵字体
简单图标
无渐变
无透明
无圆角
```

最终效果应该让人一眼感觉：

> **这是一台属于 AMUNOS 的个人电脑，而不是 Linux/Windows 的仿制品。**

---

# 35. 最重要的开发原则

### ① 不追求控件数量

少而稳定：

```text
Window
Button
Edit
List
Menu
Dialog
ScrollBar
```

已经足够做第一批生产软件。

### ② 不追求视觉复杂

统一的 3D 控件 + 蓝色标题栏 + 灰色桌面就是 AMUNOS 的视觉语言。

### ③ 不让应用重复造轮子

系统提供：

```text
File Dialog
MessageBox
Clipboard
Menu
Font
Input
```

应用直接调用。

### ④ GUI 要服务于应用

不要为了测试 GUI 而测试 GUI。

应该尽快让：

```text
AMUN WRITE
```

成为第一个真正使用 GUI syscall 的大型程序。

### ⑤ 生产软件反向推动 OS

例如：

```text
WRITE
 ↓
需要多行 Edit
 ↓
需要 ScrollBar
 ↓
需要 Clipboard
 ↓
需要中文输入
 ↓
需要字体系统
```

这比为了“完善内核”而凭空增加功能更有效。

---

# 36. 第一批真正值得实现的控件清单

最终可以压缩成：

```text
[CORE]

WINDOW
LABEL
BUTTON
EDIT
LISTBOX

[INPUT]

CHECKBOX
RADIOBUTTON
SCROLLBAR

[WINDOW]

MENU
MENUBAR
DIALOG
STATUSBAR

[SYSTEM]

FOCUS
EVENT
CLIPBOARD
FILE DIALOG
FONT

[OPTIONAL]

COMBOBOX
TAB
LISTVIEW
TREEVIEW
PROGRESSBAR
SLIDER
```

其中最优先：

```text
FOCUS
EVENT
MENU
DIALOG
SCROLLBAR
多行 EDIT
CLIPBOARD
FILE DIALOG
```

因为这些能力会直接决定 `AMUN WRITE` 是否能够真正成为一个完整的生产力软件。
