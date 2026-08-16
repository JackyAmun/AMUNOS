# AMUNOS 开发约定与自测流程

## 一、核心约定（长期有效）

1. **内置文件尽量放 A 盘**：`TCC.ELF`、`LIBC.A`、头文件、示例源码等只读内容放 A.img
   （系统盘）；用户可写文件放 B:（数据盘）。
2. **全局识别 Ctrl+C 强制终止**：任何前台程序都要能被 Ctrl+C 中断回 shell。
3. **以后还是要自测**：每次改动都要写测试并跑通。
4. **自测完就把自测删了**：临时测试代码/测试脚本验证通过后删除，保持代码库干净。

## 二、环境

- **宿主**：Windows 11（`C:\Users\XU\Desktop\OSDev`）。
- **构建/运行**：全部在 **WSL Ubuntu**（`/mnt/c/Users/XU/Desktop/OSDev`）——
  `nasm`、`gcc-multilib`、`ld`、`python3`、`qemu-system-i386` 都在 WSL。
- **注意**：Windows 侧 `python3` 是 Store 桩（exit 49）；QEMU 只在 WSL 有。构建/测试
  一律 `wsl.exe -e bash -c "cd /mnt/c/Users/XU/Desktop/OSDev && ..."`。

## 三、构建

```bash
cd /mnt/c/Users/XU/Desktop/OSDev
make all        # 构建 A.img（boot + kernel + tcc.elf + 内置文件）
make B.img      # 构建 B.img（数据盘 + hello.elf）
make clean      # 清理 .o / .bin / .img
```

关键约束：**内核必须 <52KB**（boot 104 扇区硬限制，保留扇区 105）。新代码克制，
大缓冲一律走堆（`mem_alloc`），不要加大的静态数组。

## 四、测试

测试脚本用 WSL 里的 python3 跑 QEMU（图形界面需 `-monitor telnet:...` + sendkey）：

```bash
wsl.exe -e bash -c "cd /mnt/c/Users/XU/Desktop/OSDev && python3 test_xxx.py"
```

QEMU 交互要点：
- `-monitor telnet:127.0.0.1:PORT,server,nowait` 供脚本 sendkey 注入按键。
- WSL 输出中文会乱码，且 grep 会把二进制当 "Binary file matches"——先重定向到
  `/tmp/*.txt`，再用 `grep -aE` 匹配。

## 五、自测清单模板

改完一块后，按此模板自测，全部通过后删除临时测试：

1. **干净构建**：`make clean && make all && make B.img` 无错误。
2. **功能验证**：QEMU 内跑通目标命令/程序，断言输出正确。
3. **回归**：跑一遍已有关键流程（如 `TCC HELLO.C -o HELLO.EXE` → `ELF HELLO.EXE` 输出
   "Hello from TCC on AMUNOS!"）。
4. **清理**：删除临时测试代码/脚本，再次干净构建确认启动正常。

## 六、已交付/已验证记录

| 时间 | 内容 | 状态 |
|------|------|------|
| — | DIR `-P` 分页输出 | ✅ 已验证 |
| 2026-08-14 | 修复"运行命令后编译"的内存问题（fat12_alloc_cluster） | ✅ 已验证（连续 3 次编译 exit=0） |
| — | TCC → ELF → 运行闭环（"Hello from TCC on AMUNOS!"、"libc: malloc+strcpy OK"） | ✅ 已验证 |
| — | 全局 Ctrl+C 强制终止 | ✅ 已验证 |
| v6.4 | P0-① 输入缺口修复（回车→`\n`+回显）+ 多进程重构 | ✅ 已验证 |
| v6.5 | 串口 COM1（SER）+ 并口 LPT1（LPT）轮询驱动 + QEMU `-serial`/`-parallel file:` 日志自测 + 保留扇区扩容 105 | ✅ 已验证 |
| — | 串口远程控制台：`input_poll()` 统一键盘+串口 RX → shell/编辑器/程序 stdin；全程经 `tcp:127.0.0.1:5555` socket 驱动 SER/LPT/TCC/ELF 自测通过，`./serial-console.sh` 交互连接 | ✅ 已验证 |
| v6.5 | 串口显示错乱修复（VGA→COM1 镜像 `\n`→`\r\n` 翻译 + redraw 尾空格不再进串口） | ✅ 已验证 |
| v6.5 | 删除旧自研 CC 编译器（cc.c/x86gen.c/native.c，内核 51546→34010 B） | ✅ 已验证 |
| v6.5 | 目录结构两级树 + 路径遍历（`fs_resolve_path`：`TYPE USR\SRC\X.C`、`COPY S\A D\B`、`DEL SUB\F.TXT`、CD/CD..；EDIT 按目录存取） | ✅ 已验证 |
| v6.5 | 命令辅助参数：任意命令 `CMD /?`（或 `-?`）显示用法；文件命令大小写不敏感 | ✅ 已验证 |
| v6.5 | TCC 编译进度：`Compiling ...` + `TCC done (N.Ns)` 耗时 | ✅ 已验证 |
| v6.5 | 编辑器改 FreeDOS EDIT 功能键式（F1=Help F2=Save F3=Open F4=New F5=Quit） | ✅ 已验证 |
| v6.5 | 编辑器移出内核 → 用户态 EDIT.ELF（edit.c 交叉编译，`syscall 15 getkey` 原始键码驱动；内核删 editor.o，34010→31322 B） | ✅ 已验证 |
| v6.5 | 按名运行：cwd 下 `XXX.ELF` → 输入 `XXX` 即运行；`CMDS.TXT` 命令→ELF 对照表（每盘根目录，`命令名 目标ELF`，表优先于按名运行，编辑后即时生效） | ✅ 已验证 |
| v6.5 | 串口自测 8 步全过：DIR 见 EDIT.ELF/CMDS.TXT、`EDIT` 表映射→用法、`EDIT T2.TXT` F2 保存/F5 退出、TYPE 见 abc、`B: HELLO` 按名运行、ECHO 覆盖 CMDS.TXT 后 HELLO 改跑 \EDIT.ELF | ✅ 已验证 |
| v6.5 | **目录树物理落地**（此前只有遍历代码）：A:\BOOT（BOOT.BIN/KERNEL.BIN）、A:\BIN（TCC.ELF/EDIT.ELF）、A:\USR\LIB（LIBC.A/LIBTCC1.A）、A:\USR\INCLUDE（全部头）、A:\USR\SRC（HELLO.C/INP.C）、B:\USR\SRC（C 测试源码）；CRT1.O/CRTI.O/CRTN.O 留根（TCC crt_paths="."） | ✅ 已验证 |
| v6.5 | TCC 命令改指 BIN\TCC.ELF 并注入 `-I USR\INCLUDE -L USR\LIB -B USR\LIB`；`ELF` 路径缓冲 13→64（此前 \BIN\EDIT.ELF 被截成 \BIN\EDIT.E） | ✅ 已验证 |
| v6.5 | 目录树串口自测 9 步全过：A: DIR 见 BOOT/BIN/USR <DIR>、TYPE CMDS.TXT 见 EDIT \BIN\EDIT.ELF、EDIT 表→用法、CD USR\SRC 见 HELLO.C、TCC USR\SRC\HELLO.C -o HELLO.EXE + ELF HELLO.EXE→Hello from TCC、B: HELLO 按名运行、表覆盖 | ✅ 已验证 |
| v6.5 | **EDIT 串口远程可见性修复**（`edit.c`）：编辑器直写 VRAM 0xB8000，串口控制台原本看不到编辑画面——诊断确认功能/渲染全正确（monitor `xp` 逐字节核对 + 保存后 TYPE 内容精确），唯一问题是远程不可见。现把**当前行 + 状态栏 + 消息 + 打开文件列表**镜像到 COM1（`e_ser_refresh`/`e_ser_dump`/`e_msg`/`e_read_name` 串口回显），远程控制台可完整编辑操作。串口自测：`[new file]`/`Ln 002`/`[saved]`/重开列 `[000]hello line [001]second` 全过 | ✅ 已验证 |
