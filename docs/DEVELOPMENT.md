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

关键约束：**内核必须 <49KB**（boot 99 扇区硬限制）。新代码克制，大缓冲一律走堆
（`mem_alloc`），不要加大的静态数组。

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
| 待办 | P0-① 输入缺口修复（回车→`\n`+回显） | ⏳ 未开始 |
