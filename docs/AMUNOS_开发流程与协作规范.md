# AMUNOS 开发流程与协作规范

> 本文档总结 AMUNOS 项目的开发流程、构建/测试习惯、版本约定与协作要求，
> 供后续迭代（无论人工还是 AI 协作）遵循。随项目演进更新。

（2026-08-30 首次成文）

---

## 1. 项目定位与约束（不可违反的底线）

- **架构**：x86 32 位保护模式，Ring 0，无分页（VA==PA），单内核 + ELF 用户程序。
- **引导**：自写 FAT12 引导扇区 + 双段加载（SeaBIOS AH=02h 单次读 >128 扇会
  Disk Error 挂死，第 2 段必须走 AH=42h LBA 扩展读）。
- **内核体积**：硬上限 **96KB**（rsvd 193 扇 = 1 boot + 192 kernel）。
  每次改动后必须确认 `kernel.bin < 96KB`，接近上限时优先裁剪而非硬塞。
- **GUI 是内核驻留**的（`gui.c` 窗口服务器），用户态只经 syscall（28–55）使用，
  永不直写显存。新增 GUI 能力优先复用控件/事件模型，不新增并行渲染路径。
- **中文全链路**是核心特性：UTF-8/GB2312 双解码、HZK16 点阵、中文文件名
  （8.3 短名塞 GB 双字节）、选区/退格按"整字形"对齐不劈 CJK。任何文本处理
  改动必须考虑多字节边界。
- 版本独立双轨：**内核 `vX.Y.Z`**（kernel.c 启动横幅 + 串口串），**GUI 独立版本**
  （`GUI_VERSION`，当前 0.3）。两者不同步增长。

## 2. 构建与运行（全部走 WSL）

```bash
cd /mnt/c/Users/XU/Desktop/OSdev
make kernel.bin                 # 编内核
rm -f A.img && python3 mka_img.py A.img   # 改内核后必须重打镜像（缓存陷阱）
make gui-demo.elf               # 用户态 GUI 演示（进 A:/BIN/GUI.ELF）
make B.img C.img                # 数据盘
make run-trio-gui               # QEMU 三盘图形运行
```

- **铁律：改内核后必须 `rm -f A.img` 重打**，否则跑的是旧内核，浪费一轮"排查"。
- Windows 侧 `python3` 是 Store 桩，**一切文件写入/构建/测试必须在 WSL 内执行**
  （Windows 侧 python 替换文件曾静默不落盘）。
- `gui.c` 的 Edit 工具常匹配失败（1 空格缩进 + 中文注释），惯例用 **WSL python
  补丁脚本**（`rep(old,new)` 带匹配数断言），脚本用完即删。
- 永远不要对已入库文件做行尾转换（dos2unix 类）；common.h 曾因 CRLF↔LF 产生
  234 行假 diff。git 警告 LF→CRLF 属正常。
- push 走代理：`git -c http.proxy=http://127.0.0.1:7892 push`（HTTPS 直连被重置）。

## 3. 测试习惯（像素级回归）

- 每轮 GUI 改动后跑 `python3 validate_gui.py`（16 项，QEMU headless + monitor
  `sendkey`/`mouse_move` 注入 + `pmemsave` 抓帧断言），要求 16/16 OVERALL PASS。
- 其他回归：`validate_zh.py` / `validate_editzh.py` / `validate_box.py` /
  `validate_menupopup.py`。
- 诊断铁律：
  - 抓"实际显示表面"用 monitor `screendump`（比 pmemsave 更真实）；文本模式用
    `pmemsave 0xb8000 0xfa0`（文件名必须带双引号）。
  - 区分"缓冲错" vs "渲染错"：串口 dump 实际字节（hex），别只看颜色。
  - QEMU `sendkey` 大写字母不触发键事件（要 `shift+x`）；`mouse_move` 大跨度
    会拆多个 ±127 包；PS/2 包同步位只在 idx==0 接受。
- 已知不阻塞的既有容忍项要如实记录（如 T2 焦环残差类），**测试脆弱 ≠ 内核 bug**，
  先用隔离探针复现再定性。

## 4. 协作习惯（用户要求）

- 用中文交流；每轮修复**先讲根因再动手**，修复后给出验证证据（测试输出/像素计数）。
- 用户偏好：修复 → 本地验证 → commit → push，一轮一提交；commit message 标版本号。
- 提交范围最小化：只 add 真实改动的文件；发现与本次无关的脏 diff（行尾噪音等）
  先询问用户，不擅自回滚。
- 遇到"测试 FAIL 但属既有问题/可容忍"时，向用户明示并让其决策，不隐藏不扩大。
- 每完成一个里程碑，把关键事实（根因、坑、验证方法）沉淀进 memory，供后续会话复用。

## 5. 代码风格

- 内核 C：`gnu99 -m32 -fno-builtin -ffreestanding`；注释中文、讲"为什么"
  （尤其记录历史 bug 根因），风格与现文件一致（gui.c 为 1 空格缩进的历史风格，
  新文件可用常规 4 空格，但同一文件内保持一致）。
- 固定池而非堆：GUI 窗口/控件/文本槽都是静态数组上限（GW_MAXWIN=8 等），
  新增资源沿用该模式，分配/释放必须成对（教训：txpool 曾泄漏）。
- syscall 纪律：int 0x30 只有 3 个参数寄存器（ebx/ecx/edx），多参数用 packed
  （如 `win|ctl<<8`）；libc 包装顺序必须与内核 case 读法一致。
- 中断安全：硬件 IRQ 用中断门（0x8E）；共享 8042 的键盘/鼠标路径的检查+读取
  必须 cli/restore 原子化。

## 6. 当前技术债 / 已知未修项

- 内核已 96KB 临界（95~96.5KB），加存储/网络栈前需先做扩容方案
  （ boot 第 2 段还有余量，或考虑模块加载到 0x100000+）。
- GUI docs 缺项：SCROLLBAR、BUTTON hover/pressed/disabled 态、窗口 Resize、
  GUI 0.3+ 路线控件（COMBOBOX/TREEVIEW/TAB/PROGRESSBAR/SLIDER/LISTVIEW）、Tooltip/IME。
- 存储栈：仅 INT 13h BIOS 路径 + 硬盘假设（详见《存储与设备升级分析》）。
