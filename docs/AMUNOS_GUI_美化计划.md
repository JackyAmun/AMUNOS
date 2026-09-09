# AMUNOS GUI 美化计划

## 目标定位

AMUNOS 的 GUI 不适合直接追逐现代桌面系统的玻璃、模糊、圆角和重动画。它更适合走一条清晰的路线：

1. **Classic 统一化**：以 Win3.x/Win9x、早期中文 PC 软件、DOS 图形工具为基础，强化窗口、菜单、按钮、输入框、状态栏的统一感。
2. **Terminal/Industrial 可选主题**：保留用户喜欢的 TUI、工业终端、EVA MAGI 式信息面板气质，用在系统监视、设备信息、调试工具等场景。
3. **功能优先**：美化服务于可读性、状态反馈和应用体验，不把宝贵的系统复杂度消耗在装饰上。
4. **低成本演进**：优先在 `gui.c` 内部渲染层完成改进，暂不大幅扩展 syscall 和用户态 ABI。

当前 `gui.c` 已经具备窗口边框、标题栏、菜单弹窗、状态栏、按钮、编辑框、列表、文本区等基础控件，也有活动/非活动窗口区别。这是一个很好的起点，下一步重点不是“加更多颜色”，而是建立视觉规范和控件状态。

## 当前问题

1. **颜色是零散宏**  
   当前有 `C_DESKTOP`、`C_WINBG`、`C_TITLEFX`、`C_BTNBG` 等宏，但缺少统一的 light/shadow/face/accent/warn/error 语义层。后续加主题或统一 3D 边框时会比较吃力。

2. **控件状态不够完整**  
   按钮已经有凸起效果，但缺少明确的 pressed、focused、disabled、hover/active 视觉层次。对于未来 WRITE、SHEET、DRAW 这类交互应用，状态反馈会影响体验。

3. **布局节奏未固化**  
   窗口内边距、标题栏高度、菜单高度、状态栏高度、控件间距没有被抽象成统一常量。现在能用，但应用一多就容易变成每个程序各画各的。

4. **信息密集型界面还缺少容器层**  
   系统信息、设备信息、文件工具、编译工具都需要 GroupBox、Panel、分隔线、小标题等组织方式。否则内容会堆在窗口里，读起来像调试输出而不是应用。

5. **Classic 和终端风格没有分工**  
   Classic 适合通用桌面和办公应用，Terminal/Industrial 适合监控、诊断、编译、设备管理。两者都值得保留，但需要建立使用边界。

## 阶段一：立即可做的视觉修复

这一阶段不改变 GUI ABI，主要修改 `gui.c` 内部绘制逻辑。

### 1. 建立颜色语义

建议先把颜色宏扩展为语义名称：

```c
#define C_FACE      0xC618
#define C_LIGHT     0xFFFF
#define C_SHADOW    0x8410
#define C_DARK      0x4A49
#define C_ACCENT    0x0010
#define C_WARN      0xFD20
#define C_ERROR     0xF800
```

然后逐步把窗口边框、按钮、输入框里的裸色替换为语义色。这样后续切换主题时不需要逐行追颜色值。

### 2. 固化 GUI 尺寸常量

建议增加：

```c
#define GUI_TITLE_H   18
#define GUI_MENU_H    18
#define GUI_STATUS_H  16
#define GUI_PAD       8
#define GUI_GAP       6
#define GUI_BORDER    2
```

现有窗口视觉可以保持不变，但绘制代码会更容易维护。后续控件布局也可以统一使用这些常量。

### 3. 增强按钮状态

按钮建议支持：

1. normal：经典凸起。
2. pressed：内容整体右下偏移 1px，边框改为内凹。
3. focused：按钮内侧增加虚线或深色焦点框。
4. disabled：文字使用阴影色，不响应点击。

这会直接改善 GUI 的“可点击感”，也是 WRITE、SHEET、DRAW 工具栏体验的基础。

### 4. 加入 GroupBox / Panel

优先做内部绘制函数，不急着开放 syscall：

```c
static void gui_groupbox(int x, int y, int w, int h, const char *title);
static void gui_panel(int x, int y, int w, int h, int sunken);
```

用途：

1. SYSINFO：CPU / Memory / Storage / Display 分组。
2. DEVICE：磁盘、PCI、显示、输入设备分组。
3. TCC/BUILD：源码、输出、编译状态分组。
4. WRITE/SHEET/DRAW：工具区、工作区、状态区分组。

### 5. 统一状态栏

状态栏建议固定三段：

1. 左侧：当前工具或状态。
2. 中间：路径、选区、光标等上下文。
3. 右侧：时间、内存、驱动器、输入法状态。

这会让 AMUNOS 应用立刻更像一个系统，而不是一组孤立 demo。

## 阶段二：主题系统

建议先做编译期主题，再做运行时主题。

### Classic 主题

适用场景：

1. 桌面、文件管理、设置。
2. WRITE、SHEET、DRAW 等生产力应用。
3. 普通对话框、菜单、属性窗口。

视觉要点：

1. 灰色桌面和窗口面板。
2. 深蓝活动标题栏。
3. 白色输入框和文本区。
4. 明确的 3D 凸起/凹陷边框。
5. 小字号、紧凑控件、高信息密度。

### Terminal/Industrial 主题

适用场景：

1. SYSINFO、DEVICE、MONITOR。
2. TCC 编译器前端、日志查看器。
3. 磁盘工具、内存查看器、调试控制台。

视觉要点：

1. 暗灰或黑色工作区。
2. 青色、绿色、琥珀色作为状态色。
3. 使用 `>` 作为选择和当前行提示。
4. 命令按钮可使用 `< RUN >`、`< STOP >`、`< SAVE >` 这类终端式文案。
5. 错误和危险状态使用红色，但必须同时配合文字或图标，不能只靠颜色。

注意：`< >` 视觉语言适合终端主题和命令按钮，不应影响 DOS/FAT 路径显示。路径仍应保持 `A:/USR/`、`B:/DOCS/` 这样的系统一致性。

## 阶段三：应用场景化美化

### SYSINFO / DEVICE

目标是从“命令输出”升级为“系统仪表盘”。

建议布局：

```text
+--------------------------------------------------+
| SYSINFO                                      [_]X |
+--------------------------------------------------+
| System        | Memory        | Storage          |
| AMUNOS 0.x    | Used / Free   | A: FAT12         |
| GUI: ON       | Heap Blocks   | B: FAT16/32      |
| Ticks: ...    | Tasks: ...    | IDE/AHCI: ...    |
+--------------------------------------------------+
| Display       | Input         | Runtime          |
| 1024x768x16   | KB / Mouse    | TCC / ELF        |
+--------------------------------------------------+
| Ready                         A:/>        12:30  |
+--------------------------------------------------+
```

### TCC / BUILD

目标是让“在系统内编译程序”变成可展示能力。

建议功能：

1. 源码路径输入框。
2. 输出 ELF 名称输入框。
3. `< COMPILE >` 按钮。
4. 编译日志文本区。
5. 成功后 `< RUN >` 按钮。

这能把 AMUNOS TCC 从命令能力提升为应用体验。

### WRITE

目标是早期 Windows 记事本/写字板风格。

建议控件：

1. 菜单栏：File / Edit / Format / Help。
2. 工具栏：New、Open、Save、Bold、Italic、Align。
3. 大文本区。
4. 状态栏显示文件名、行列、编码、修改状态。

### SHEET

目标是 Lotus/Excel 早期风格。

建议控件：

1. 行列标题。
2. 单元格选区。
3. 公式输入栏。
4. 底部 Sheet 标签。
5. 状态栏显示 SUM/AVG/COUNT。

### DRAW

目标是 Paintbrush/早期画图工具。

建议控件：

1. 左侧工具栏：铅笔、线、矩形、填充、文字。
2. 顶部颜色条。
3. 中间画布。
4. 状态栏显示坐标、画布尺寸、当前工具。

## 建议实现顺序

1. **第一批：基础视觉整理**
   - 添加颜色语义宏。
   - 添加尺寸常量。
   - 替换 GUI 绘制中的裸色。
   - 统一状态栏和菜单阴影。

2. **第二批：控件状态**
   - 添加 pressed button。
   - 添加 focused button/edit/list。
   - 添加 disabled 文本色。
   - 增加控件内边距一致性。

3. **第三批：容器控件**
   - 实现 GroupBox。
   - 实现 Panel。
   - 用在 GUI demo 或 SYSINFO GUI 原型中。

4. **第四批：主题**
   - 抽象 `gui_theme_t`。
   - 保留 Classic 为默认主题。
   - 增加 Terminal/Industrial 主题。
   - 后续再考虑 `gui_set_theme(int id)`。

5. **第五批：应用原型**
   - SYSINFO GUI。
   - TCC BUILD GUI。
   - WRITE 基础版。
   - DRAW 基础版。
   - SHEET 只先做网格和选区。

## 验证标准

每一轮 GUI 修改后至少检查：

1. 标题栏、按钮、菜单、输入框、列表、文本区是否风格一致。
2. 活动窗口和非活动窗口是否能一眼区分。
3. 所有文字是否在控件内完整显示。
4. 选中、按下、禁用、焦点状态是否清楚。
5. 640x480 和 1024x768 下是否都能正常布局。
6. 鼠标指针、菜单弹窗、状态栏是否没有遮挡关键内容。

## 不建议做的方向

1. 不建议加入透明窗口、模糊背景、渐变大面积铺底。
2. 不建议追求现代网页式大圆角卡片。
3. 不建议为了美化频繁增加 syscall。
4. 不建议让 Terminal/Industrial 主题覆盖所有应用。
5. 不建议只做颜色替换而不处理控件状态和布局节奏。

## 近期可落地版本

下一步最适合做一个 `GUI 0.4 Polish` 小版本：

1. 保持现有 ABI。
2. 在 `gui.c` 中加入颜色语义和尺寸常量。
3. 改进按钮 pressed/focus 视觉。
4. 增加内部 GroupBox 绘制。
5. 将 GUI demo 改成“System / Build / Documents”分组界面。
6. 为后续 SYSINFO GUI 和 TCC BUILD GUI 留出控件模型。

这个版本的目标不是惊艳，而是让 AMUNOS GUI 从“能画窗口”进入“有桌面系统气质”的阶段。
