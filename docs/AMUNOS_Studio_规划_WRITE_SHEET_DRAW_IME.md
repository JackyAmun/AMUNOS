# AMUNOS Studio 规划 — WRITE / SHEET / DRAW / PAINT / IME

> 编制时间：2026-08-31，对应内核 v6.5.6（GUI 0.3、FatFs FAT12/16/32、ISO9660、AHCI）。
> 上一份路线报告（docs/AMUNOS_未来路线报告.md）基于旧 v6.5.5 状态盘点，
> 本文件按 **v6.5.6 真实代码现状** 重写 §2 缺口表，并把 Studio 三大件
> （WRITE / SHEET / DRAW / PAINT）+ IME 的独立应用规划单列出来。

---

## 1. 范围与原则

本文件覆盖：

- **Studio 系列**：`WRITE.ELF`（文档）/ `SHEET.ELF`（表格）/ `DRAW.ELF`（矢量绘图）/ `PAINT.ELF`（位图绘图）
- **输入法**（IME）：拼音 → GB2312，全用户态，挂到 EDIT/各 Studio 的文本输入
- **底层补齐**：原生图形 syscall、键盘/鼠标事件统一
- **小工具集**：`SYSINFO / CALC / VIEW / HEX / SIZE / BUILD / CLOCK`

**原则（继承《未来路线报告 §5》）**：

1. **新功能优先放用户态 ELF**——v6.5.6 内核已 112,604 B（<192 KB 但余量紧张），新增能力尽量不进 `kernel.c / gui.c`，而是打成 `A:/BIN/*.ELF`。
2. **图形 API 是 Studio 的命门**——没有 `fb_pixel / fb_line / fb_rect / fb_text_at`，一切"画图""表格线""鼠标命中"都退回到 80×25 字符格，体验低劣。
3. **不追求多窗口 GUI**——单窗口 + 全屏应用能撑起 Studio 工具集合，多窗口重排推迟。
4. **IME 复用现有 U2GB 反向表**——已有 UTF-8→GB2312 映射，反过来查 GB→py 或 GB→unicode 即可。

---

## 2. 现状盘点（v6.5.6 实测）

| 维度 | 状态 | 备注 |
|---|---|---|
| 内核 | v6.5.6 / 112,604 B / 上限 192 KB | 余量 ≈ 80 KB；v6.5.6 已扩 96KB 段（rsvd 193 扇） |
| 存储 | FAT12/16/32(FatFs R0.16b) / ISO9660 / IDE×4 / FDC / ATAPI / AHCI | 7 槽统一块设备层 |
| 图形 | VBE 640×480×16bpp + 软渲染（fb.c） + GUI 0.3 内核窗服 | 字符/汉字粒度，**无通用像素/线/矩形 syscall** |
| 输入 | PS/2 kbd + mouse（IRQ6/IRQ12 中断门，排干 + 原子 kbd_poll） | 鼠标坐标双形式：字符格(0-79/0-24) + 像素(0-639/0-479) |
| GUI | 8 类控件（BTN/LBL/EDIT/LIST/TAREA/CHECK/RADIO/MENU）+ 状态栏 | 含 chrome（▁▢✕）、拖动快路径、菜单弹层、文本选中 |
| 文本 | UTF-8 + GB2312 + HZK16 16×16 + U2GB(28KB) + 替换框 | 已有 utf8togb syscall (26) + cjkwchar (27) |
| 用户程序 | TCC 0.9.27 + minilibc，ELF 链入 0x100000 | 现有 ELF：TCC、EDIT、GUI-DEMO、INP、HELLO |
| 命令 | cd/copy/dir/echo/elf/help/install/mov/ren/ser/tcc/time/type/ver/zh/cls/lpt/cmds | CMDS.BIN 调度，大小写不敏感 |
| 任务 | 单前台 + 后台多任务，10ms tick | 多任务协作机制齐全，缺音频/网络驱动 |

### 2.1 与 Studio 直接相关的缺口

| 缺口 | 现状 | 影响 |
|---|---|---|
| **fb_pixel / fb_line / fb_rect / fb_text_at** | 无 | DRAW / PAINT / SHEET 表格线 / PONG 都画不出 |
| **整窗位图（sys 端 buf 双向拷贝）** | 无 | 工具间无法共享/截屏，PAINT 缺位图缓冲 |
| **鼠标绝对坐标 + 命中测试** | 已有 mouse_px_x/y | GUI 已有，但非 GUI 程序要自己读 |
| **拼音 → GB2312 反向表** | 仅 U2GB(unicode→gb) | IME 需要 拼音串 → 候选汉字表 |
| **全屏应用切换 / 退出** | 已有 `gui_leave` 回到文本 | OK，但单窗口切多应用需要 shell `run` 重启 |
| **音频** | 仅 PC 喇叭 beep (kernel 实现) | SYNTH/MUSIC 必须有音频驱动，先 PC 喇叭起步 |
| **大量 syscall 留作扩展** | 56-63 空白 | 补 4-5 个图 syscall 不冲突 |

---

## 3. 底层补齐（前置，必须先做）

### 3.1 原生图形 syscall（4-5 条）

新增编号 56-60：

| 号 | 名称 | 入参 | 出参 | 备注 |
|---|---|---|---|---|
| 56 | `SYS_FB_PIXEL` | `(x, y, color_rgb565)` | 0/-1 | 写一个像素；GUI 活跃时写窗内缓冲，否则直写 LFB |
| 57 | `SYS_FB_RECT` | `(x, y, w, h, color)` | 0 | 填充矩形（实现可走 1+ 行 `fb_pixel` 或 SIMD 优化） |
| 58 | `SYS_FB_LINE` | `(x1, y1, x2, y2, color)` | 0 | Bresenham 直线；非 GUI 时直写 LFB |
| 59 | `SYS_FB_TEXT_AT` | `(x, y, str, color)` | 0 | 在任意像素坐标画字符串（ASCII + GB 双字节），GUI 模式写窗内 |
| 60 | `SYS_FB_BLIT` | `(dst_x, dst_y, src_buf_phys, w, h)` | 0 | 整块位图拷贝；PAINT/SHEET 大量需求 |

**实现策略**：

- `fb.c` 已有 `vga_rgb565[16]` 和 `fb_draw_box`，扩展不复杂
- GUI 模式：路由到当前活跃窗的离屏缓冲（与现有 `gui_fill` 思路一致）
- 非 GUI 模式：直写 `0xFD000000` LFB
- 字号 8×16 拉丁 + 16×16 GB；用现成 `fb_draw_glyph` / `fb_draw_cjk`

**预估工作量**：内核 0.5-1 KB，1-2 天。

### 3.2 文本选中/光标 API 复用

Studio 需要"富文本"，但 v6.11 已经实现了 TAREA（多行文本区）+ 选中 + 拖动，Studio 工具应当：

- 文本编辑全部走 `TAREA`，不要自己造轮子
- WRITE 内部用 1 个 TAREA + 富文本排版（标题 / 列表 / 段落）
- SHEET 单元格编辑也用 EDIT + List 复合

**结论**：Studio 不必"再发明文本框"。

### 3.3 文件格式

Studio 文件全部用 **UTF-8 + 少量行首标记**，人眼可读，TCC 可生成。

| 工具 | 扩展名 | 格式概要 |
|---|---|---|
| WRITE | `.AWD` | `= 标题\n@ 列表项\n  正文\n[IMG:FILE.BMP]\n` 扩展标记 |
| SHEET | `.ASH` | 头部元数据 + 行式 CSV-like：`A1=NAME\|QTY\|PRICE\|TOTAL\n1=APPLE\|10\|2.5\|=A2*C2\n` |
| DRAW | `.ADR` | 头部 `AMUN DRAW 1\n`，每对象一行 `LINE x1 y1 x2 y2 color\n` |
| PAINT | `.AGR` | 头部 `AMUN RASTER 1\nW=640 H=480\n`，裸 RGB565 数据 + 调色板 |

设计要点：**行式文本 + 显式关键字**，便于手写与机器读写，可被 `TYPE` 直接查看。

---

## 4. Studio 应用规划

### 4.1 WRITE — 文档编辑器

**目标**：从 EDIT 扩展，增加文档型排版与富文本标记。

**功能（MVP）**：

- TAREA 为底，加排版层：标题 / 段落 / 列表 / 引用
- 标记解析：`# 标题 / ## 副标题 / @ 列表项 / `**bold**` / `*italic*` / `[IMG:FILE]`
- 左右分栏：左边大纲（TAREA-单列），右边正文（TAREA-多行）
- 工具栏按钮：粗体 / 斜体 / 插入图片占位 / 列表 / 引用
- 文件操作：新建 / 打开 / 保存 / 另存为（`.AWD`）
- 打印/PDF：v1 不做，留到 v2

**实现**：

- 用户态 ELF（约 200-400 KB），单窗口 + 2 个 TAREA + 工具栏（5 个 BTN）+ 菜单栏（文件/编辑/格式）
- 复用 EDIT 已有剪贴板（v6.11 选区基础上的复制/粘贴）
- 写盘：直接把 TAREA 文本 + 解析状态存到 `.AWD`

**预估工作量**：3-5 天（依赖图形 API 验证 + 拖入图片占位）。

### 4.2 SHEET — 表格

**目标**：AMUNOS 风格的电子表格，支持 256×256 单元格、4 基础公式。

**功能（MVP）**：

- 26 列 × 100 行（启动可见 10×20，可滚动）
- 单元格编辑：双击或 F2 进编辑态（用 EDIT 控件）
- 公式：`=A1+B2` / `=SUM(A1:A5)` / `=AVERAGE` / `=MAX/MIN`
- 选区：Shift+方向键 / 鼠标拖选（复用 TAREA 选区）
- 填充柄：右下角小方块拖动复制公式
- 保存/打开 `.ASH`
- 行列号：列 A-Z 行 1-100

**实现**：

- 用户态 ELF，2D 网格用 `fb_rect` + `fb_line` 画线，单元格文字 `fb_text_at`
- 公式解析：写一个迷你表达式 parser（数字 / 单元格 / `+ - * / ( )` / SUM/AVG/MAX/MIN）
- 公式求值：算后存结果 + 维护依赖图，循环引用报错

**预估工作量**：5-7 天（parser 是主要成本）。

### 4.3 DRAW — 矢量绘图

**目标**：流程图 / 简单示意图 / 平面构图。

**功能（MVP）**：

- 工具：选择 / 直线 / 矩形 / 椭圆 / 文本 / 多边形（点击加点 / 双击闭合）
- 颜色：8 色（黑/白/红/绿/蓝/黄/青/紫）工具栏
- 选中与移动：单对象点击选中，拖动移动
- 删除：Delete 键
- 保存/打开 `.ADR`
- 缩放：v1 不做；v2 加 25%/50%/100%/200% 切换

**实现**：

- 用户态 ELF，整窗为画布，工具栏用 BTN
- 对象存储：`obj_t { type, x, y, w, h, color, text?, points? }`
- 鼠标命中：根据对象类型做几何测试
- 重画：脏区域 → 整窗 `fb_fill` + 遍历对象重画

**预估工作量**：4-6 天。

### 4.4 PAINT — 位图绘图

**目标**：早期 PC 风格的画图板。

**功能（MVP）**：

- 工具：铅笔 / 直线 / 矩形 / 椭圆 / 填充桶 / 文本 / 橡皮
- 调色板：16 色（用现有 `vga_rgb565[16]`）
- 画布：640×480（或 320×240）
- 保存/打开 `.AGR`（裸 RGB565）
- 撤销：v1 不做；v2 加 5 步 undo

**实现**：

- 用户态 ELF，内部 `unsigned short canvas[640*480]` 一次 `mem_alloc`（用户堆 2MB 够用）
- 每次操作直接写 `canvas[]`，鼠标抬起时 `fb_blit` 一次性贴 LFB
- 调色板弹层：16 色格用 `fb_rect`

**预估工作量**：3-4 天。

---

## 5. IME 输入法

### 5.1 目标

在 EDIT / TAREA / SHEET 单元格 / WRITE 文本输入场景下，按 **`Ctrl+Space` 切到拼音模式**，键入拼音 → 候选条 → 数字/方向键选字 → 上屏。

### 5.2 状态

- 已有 `SYS_UTF8TOGB (26)`：unicode → GB2312
- 已有 U2GB.bin：约 28 KB 双向表（实测 `wc -c u2gb.bin`）
- 拼音 → unicode 反查表未做

### 5.3 实现路径

**方案 A（小数据）**：自带 `PY2UNI.BIN`（拼音串 → unicode 列表），约 100-200 KB GB2312 全字符拼音表。在 A: 启动时装入用户堆。

**方案 B（动态生成）**：写一个 `gen_py2uni.py`（主机侧）：
- 输入：GB2312 全表 6763 字
- 查 Unicode 数据库（已有 CJK Unihan 拼音）→ 输出 `PY2UNI.BIN`
- 格式：`{ n_match:u8, py_str[8]:ascii, uni0,uni1,... } × N`

**优先方案 B**：工具侧生成一次，A: 启动时按需加载。

**用户态 IME 引擎**：

- `ime.elf`？**不**——IME 是"挂件"，不应是独立进程
- **更优做法**：在 EDIT / Studio 各自嵌入一个 `ime_engine_t` 模块
- 内核只暴露一条 `SYS_IME_QUERY (61, py_str, out_buf, max) → n_match` 即可
- 编辑器：拿 `n_match` → 弹候选条 → 用户选 → 取对应 GB → 走 `SYS_CJKWCHAR (27)` 上屏

**预估工作量**：2-3 天（PY2UNI.BIN 生成 1 天 + 内核 + 编辑器集成 1-2 天）。

### 5.4 候选条 UI

复用 v6.11 弹层思路：

- 用 GUI 弹窗（或 `fb_text_at` 写一个固定位置条）
- 显示 5-9 个候选，每项前有数字 1-9
- 数字键 / Space 翻页 / Esc 取消

---

## 6. 小工具集（第一波 BIN 收尾）

全部用户态，1-2 天/个：

| 工具 | 功能 | 关键 syscall |
|---|---|---|
| `SYSINFO.ELF` | VER / CPU / 内存 / 磁盘 / 任务 | 26（读 kernel 全局）、mouse_get |
| `CALC.ELF` | `+ - * / ()` 四则 + 简单表达式 | 纯用户态，stdin/stdout |
| `VIEW.ELF` | 分页 `TYPE` 的精简版 | SYS_READ + sleep |
| `HEX.ELF` | 16 进制查看文件 | SYS_OPEN/READ |
| `SIZE.ELF` | ELF 头解析显示段大小 | SYS_OPEN/READ + 解析 |
| `BUILD.ELF` | `TCC src.c -o out.exe` 包装 | SYS_EXEC / 现有命令 |
| `CLOCK.ELF` | 实时时钟 + 日期 | SYS_SLEEP + 时间显示 |
| `PACK.ELF` | `.AMN` 容器打包/解包 | 内部用 TAR + 微型格式 |

---

## 7. 阶段路线（结合 v6.5.6 后）

### ▶ P1 — 图形 syscall 平台补齐（1 周）

1. `SYS_FB_PIXEL / RECT / LINE / TEXT_AT / BLIT`（56-60）
2. `validate_gfx.py` 像素级回归：直线、矩形、文字、blit 完整
3. GUI 模式下走窗内缓冲（与现有 `gui_fill` 同路）

### ▶ P2 — 小工具集收尾（1 周）

- SYSINFO / CALC / VIEW / HEX / SIZE 全部 .ELF 进 A:/BIN
- 每个 1-2 天
- 增加 `BIN` 帮助菜单（在 EDIT 中可见）

### ▶ P3 — IME + 拼音反查表（1 周）

- `gen_py2uni.py` 生成 PY2UNI.BIN
- `SYS_IME_QUERY (61)` 系统调用
- EDIT / TAREA 集成 IME，**默认拼音模式 Ctrl+Space 切**
- `validate_ime.py`：拼音 → 候选条 → 选字 → 写入验证

### ▶ P4 — Studio MVP（2-3 周）

- **WRITE** v1（4 天）
- **DRAW** v1（5 天）
- **SHEET** v1（6 天，含 parser）
- **PAINT** v1（4 天）

### ▶ P5 — 增强 + 互通（2 周）

- Studio 互通：WRITE 插入 [IMG:DRAW.ADR] 占位 + 渲染
- SHEET ↔ WRITE：SHEET 选区 → WRITE 表格
- 多文档：WRITE 文档切换（保持单窗口，按 `[Tab]` 切）
- 撤销/重做（各 Studio 加 5-10 步 undo）

### ▶ P6 — 生态扩展（远期）

- BASIC 解释器（或 `BASIC → TCC` 包装）
- SYNTH（PC 喇叭 + 后续 SoundBlaster）
- 网络（RTL8139 驱动 / XMODEM 串口）
- FAT 长文件名 + CP936

---

## 8. 风险与回退

| 风险 | 缓解 |
|---|---|
| 图形 syscall 写错导致 GUI 显示崩 | 单独 commit，validate_gui.py 全跑 |
| Studio 太大超过 2 MB 用户堆 | 必要时改分块加载或减少 1 个工具 |
| 拼音表太大（>500 KB）放不进 A: | 改成"按区段按需加载"，或放 C: 数据盘 |
| 用户态 PC 喇叭延迟过大 | 接受 1-5 ms 抖动；v2 上 SoundBlaster DMA |
| 多 Studio 同时开内存吃紧 | 不支持多 Studio 并存，shell `run` 切换 |

---

## 9. 不做（明确出界）

- 多窗口 GUI 重排（推迟到 v7+）
- 网络（RTL8139 是独立大块；串口 XMODEM 足够）
- NVMe（qemu 可跑，驱动量级等同 AHCI）
- FAT 长文件名 + CP936（用户实盘上中文文件名体验提升有限）
- 多任务抢占改进（当前 10ms tick + 后台任务够用）

---

## 10. 文件总览（用户态工具区）

```
A:/BIN/
├── TCC.ELF         (已有)
├── EDIT.ELF        (已有)
├── GUI.ELF         (已有，演示)
├── SYSINFO.ELF     (P2)
├── CALC.ELF        (P2)
├── VIEW.ELF        (P2)
├── HEX.ELF         (P2)
├── SIZE.ELF        (P2)
├── BUILD.ELF       (P2)
├── CLOCK.ELF       (P2)
└── PACK.ELF        (P2)

A:/STUDIO/
├── WRITE.ELF       (P4)
├── SHEET.ELF       (P4)
├── DRAW.ELF        (P4)
├── PAINT.ELF       (P4)
└── SAMPLES/
    ├── SAMPLE.AWD
    ├── SAMPLE.ASH
    ├── SAMPLE.ADR
    └── SAMPLE.AGR

A:/SYSTEM/
├── HZK16           (字库)
├── U2GB.BIN        (unicode→gb 表)
├── PY2UNI.BIN      (P3, 拼音→unicode)
└── CMDS.BIN        (命令→ELF 映射)
```

---

## 11. 一句话总结

**v6.5.6 之后最大杠杆点 = 补 5 条图形 syscall（56-60），然后 Studio 三大件 + IME 全部以用户态 ELF 形式实现。** 内核保持稳定、瘦小；用户态承担创作生态；A:/BIN 收尾一批准系统级小工具。把"在 AMUNOS 内做 AMUNOS 软件"的闭环从 EDIT 扩展到 Studio。
