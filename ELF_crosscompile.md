# AMUNOS 交叉编译 + ELF 执行方案分析

> 版本: v6.3.1 更新
> 主题: 用"真实编译器交叉编译 → 加载运行 ELF 可执行文件"替代自研玩具编译器

---

## 目录

1. [背景与目标](#一背景与目标)
2. [现状与瓶颈](#二现状与瓶颈)
3. [方案总览](#三方案总览)
4. [交叉编译器选型](#四交叉编译器选型)
5. [ELF 格式与加载器](#五elf-格式与加载器)
6. [libc 与系统调用对接](#六libc-与系统调用对接)
7. [进程 / 内存 / 调度模型](#七进程--内存--调度模型)
8. [三种落地路径与分阶段计划](#八三种落地路径与分阶段计划)
9. [风险与权衡](#九风险与权衡)
10. [结论与建议](#十结论与建议)

---

## 一、背景与目标

v6.3.1 已完成三大基础设施：**内存分配器（mem.c）**、**系统调用表（int 0x30）**、**协作式任务调度（task.c）**。这三块加在一起，本质上已经具备了"加载并运行外部程序"的条件。

当前 AMUNOS 的编程能力靠自研编译器：

```
模式 A (字节码):  CC file.c  →  RUN        (C4 39 条字节码, VM 解释)
模式 B (原生):    CC -x file.c →  LOAD .COM (手写 x86 代码生成器)
```

两条路都是**玩具级**，只能跑一个很小的 C 子集。本方案的目标是用一个**真实、完整、可交叉编译的 C 编译器**（首选 TinyCC/tcc），生成标准的 **ELF 可执行文件**，让 AMUNOS 像真正的操作系统一样加载并运行它。

**核心命题**：把"在系统里写代码"从"自研解释器"升级为"标准编译器 + 标准二进制格式"。

---

## 二、现状与瓶颈

### 2.1 CC -x（原生）的硬伤

看 `x86gen.c` 的实现，问题一目了然：

| 维度 | 现状 | 问题 |
|------|------|------|
| 语法 | 仅 `main` + `printf/input/赋值/if/while/return` | 无函数定义、无 struct、无指针、无 for/switch |
| 变量 | 固定 4 个槽 `a/b/c/d` @ 0x2000-0x200C | 无局部变量、无栈帧、无作用域 |
| 传参 | 固定内存槽 + `CALL 0x1000/0x1010` 蹦床 | 无调用约定、无 ABI |
| 输出 | 裸机器码 `.COM`（无头、无重定位、无符号表） | 无法链接库、无法 `#include`、无法跨文件 |
| 地址 | 写死 `0x100000` 加载、`0x2000` 数据 | 无法与内存管理/多任务协同 |

`x86gen.c` 本质是一个"手写的、单遍的、只认一种程序的"代码生成器，每加一个语法特性都要手写指令编码——这条路越走越窄。

### 2.2 C4（字节码）的天花板

C4 虽然支持函数/指针/数组/递归，但它：
- 是**解释执行**（慢一个数量级）；
- 无预处理器（不能 `#include`/`#define`）；
- 无 struct/float/for/switch；
- 语言完整度和标准 C 差距巨大，无法运行"正经"程序。

### 2.3 结论

继续堆自研编译器是死胡同。**标准答案**是拥抱成熟工具链（tcc / gcc）与标准格式（ELF），把自研精力集中在**加载器**和**运行时库**这两块"操作系统该做的事"上。

---

## 三、方案总览

```
            [交叉编译器]
        ┌─────────────────┐
        │  tcc (cross-i386)│  在 WSL 主机上, 或 AMUNOS 内部
        │  或 i386-elf-gcc │  编译 C 源文件
        └────────┬────────┘
                 │ 输出
                 ▼
           ┌───────────┐
           │  ELF 可执行 │  (ET_EXEC, 静态链接, i386)
           └─────┬─────┘
                 │ 复制到 B.img (FAT12 已有)
                 ▼
        ┌──────────────────┐
        │  AMUNOS ELF 加载器 │  (elf.c, 新模块)
        │  解析头 → 映射段   │
        └────────┬─────────┘
                 │ 跳转 e_entry
                 ▼
        ┌──────────────────┐
        │  程序运行 (作为任务) │  task.c 调度, 通过 int 0x30 调内核
        └────────┬─────────┘
                 │ 系统调用
                 ▼
        ┌──────────────────┐
        │  libc (minilibc) │  printf/malloc/... → syscall 1-7
        └──────────────────┘
```

三条流水线，每一条都已有基础：

| 环节 | 新东西 | 6.3.1 已有基础 |
|------|--------|----------------|
| 编译 | 交叉工具链 (tcc/gcc) | —（主机侧） |
| 加载 | **ELF 加载器** | fs.c 读文件、mem.c 分配 |
| 运行 | libc + 进程模型 | syscall.c (int 0x30)、task.c、mem.c |

---

## 四、交叉编译器选型

### 4.1 候选对比

| 编译器 | 二进制大小 | 输出 | 交叉编译难度 | 自举(跑在 OS 内)难度 |
|--------|-----------|------|-------------|---------------------|
| **TinyCC (tcc)** | ~100-200 KB | ELF (i386) | 低，`make cross-i386` | **低**（这是它的核心卖点） |
| i386-elf-gcc | 大 (MB 级) | ELF | 需自建 binutils/gcc | 几乎不可能自举 |
| clang | 巨大 | ELF | 需 LLVM 全套 | 不可行 |

### 4.2 为什么选 TinyCC

1. **体积小**：编译产物约 100-200 KB，能放进我们的 FAT12 盘（1.44 MB），甚至能装进内存常驻。
2. **单遍、快**：编译速度是 gcc 的数倍，适合在弱硬件/模拟器里跑。
3. **天生支持交叉**：官方提供 `make cross-i386` 目标；可用 `--config-`/`--sysincludepaths=`/`--libpaths=` 指定非宿主库与头文件。
4. **能自举**：tcc 本身就是用 C 写的、依赖很薄，是极少数能被"移植进自制 OS 内部运行"的完整 C 编译器。
5. **输出标准**：默认产 ELF；也能 `-Wl,--oformat=binary` 产裸二进制（作为中间过渡）。

### 4.3 交叉编译的关键参数

在 WSL 主机上（或未来在 AMUNOS 内）：

```bash
# 面向裸机/自制 OS 的静态 ELF
tcc -m32 -nostdlib -static \
    -Wl,-Ttext=0x100000 \          # 加载地址 (物理)
    -o prog.elf prog.c libamunos.a # 链接我们的 minilibc
```

要点：
- `-m32`：目标 i386（AMUNOS 是 32 位保护模式）。
- `-nostdlib`：不链宿主 libc，改用我们的 `libamunos.a`。
- `-static`：静态链接（见 §七 动态链接的取舍）。
- `-Ttext=`：把代码段放到我们内存映射中的物理地址（AMUNOS 无分页，VA == PA）。

> 参考：TinyCC 官方邮件列表讨论了 bare-metal/cross-i386 的用法与 `--oformat,binary`，见文末链接。

---

## 五、ELF 格式与加载器

### 5.1 ELF-32 关键结构（只读这些就够加载静态 ET_EXEC）

ELF 头（52 字节）：

| 偏移 | 字段 | 含义 |
|------|------|------|
| 0x00 | e_ident | `0x7F 'E' 'L' 'F'` 魔数；[4]=1(32位) [5]=1(小端) |
| 0x10 | e_type | 2 = ET_EXEC |
| 0x12 | e_machine | 3 = EM_386 |
| 0x18 | e_entry | 入口虚拟地址 |
| 0x1C | e_phoff | 程序头表偏移 |
| 0x2A | e_phentsize | 每个程序头大小 (32) |
| 0x2C | e_phnum | 程序头数量 |

程序头（Program Header，每个 32 字节），只关心 `PT_LOAD`（p_type == 1）：

| 偏移 | 字段 | 含义 |
|------|------|------|
| 0x00 | p_type | 1 = PT_LOAD |
| 0x04 | p_offset | 段在文件内的偏移 |
| 0x08 | p_vaddr | 段要加载到的虚拟地址 |
| 0x10 | p_filesz | 文件内字节数 |
| 0x14 | p_memsz | 内存占用字节数 (≥ filesz) |
| 0x18 | p_flags | 4=R 2=W 1=X |

### 5.2 最小加载器算法（`elf.c`）

```c
// 1. 读入整个 ELF 到缓冲区 (fs_read_file 已有)
// 2. 校验魔数与 e_type==ET_EXEC, e_machine==EM_386
// 3. 遍历每个 PT_LOAD 段:
for (i = 0; i < e_phnum; i++) {
    ph = &phdr[i];
    if (ph->p_type != PT_LOAD) continue;
    memcpy((void*)ph->p_vaddr, buf + ph->p_offset, ph->p_filesz);
    memset((void*)(ph->p_vaddr + ph->p_filesz), 0, ph->p_memsz - ph->p_filesz);
}
// 4. 跳转入口: entry = (void(*)())elf->e_entry; entry();
```

**没有分页时，`p_vaddr` 就是物理地址**。tcc 默认 Linux 链接地址是 `0x08048000`（约 128 MB），在 QEMU 默认 128 MB RAM 里是可行的，但我们更倾向用 `-Ttext=0x100000` 或自定义链接脚本，把程序放到与 `.COM` 一致的 `0x100000` 区域，避开内核（0x8000）与堆（0x400000）。

### 5.3 重定位与动态链接（暂缓）

- **静态 ELF**：段已含最终地址，`p_vaddr` 即最终地址，加载器无需处理重定位（`.rel.text` 为空）。**首选**。
- **动态 ELF**：需要 PLT/GOT、`ld.so`（ELF 解释器）、符号解析——工作量巨大，**推迟到后期**。

---

## 六、libc 与系统调用对接

### 6.1 直接复用 6.3.1 的 int 0x30 表

`syscall.c` 已经定义好了 ABI：

```c
// 调用: mov eax, 调用号; mov ebx, arg1; mov ecx, arg2; mov edx, arg3; int 0x30
//   1 putchar   2 getchar   3 puts   4 putnum
//   5 malloc    6 free      7 sleep
```

minilibc 就是这 7 个系统调用的 C 封装，再加上用户态的 `printf`/`sprintf`：

```c
// libamunos/crt0.c — 程序入口, 调 main 后调 exit
// libamunos/syscall.c — 内联汇编 int 0x30 封装
// libamunos/printf.c — vsnprintf 格式化引擎 (%d %s %c %x)
// libamunos/string.c — memcpy/memset/strlen/strcmp
```

### 6.2 系统调用返回路径的细节

当前 `asm_syscall_handler`（int 0x30）用的是**陷阱门**（flags 0xEF，允许 ring 3 调用）——这一点已经为未来 ring 3 用户态预留了。返回值写回 `frame[11]`（EAX 槽），程序从 `int 0x30` 返回后直接从 EAX 拿结果。

### 6.3 当前 syscall 表的缺口（需补）

| 缺口 | 原因 | 建议 |
|------|------|------|
| `open/read/write/close` | 程序要读写文件 | 新增 syscall 8-11，封装 fs.c |
| `exit` | 程序正常退出回 shell | 新增 syscall 12，让任务结束而非死循环 |
| `sbrk` | 让 malloc 能增长堆 | 或让 malloc 直接走 syscall 5 预分配池 |
| `getchar` 阻塞问题 | 当前是忙等 | 结合 task.c 改成 sleep 等待，让出 CPU |

> 关键点：**libc 的 printf/malloc 是"用户态库"，内核只提供最薄的原语**。这正是 6.3.1 syscall 表的价值——内核不膨胀，功能在 libc 层补。

---

## 七、进程 / 内存 / 调度模型

### 7.1 两条路线：Ring 0 快速跑通 vs Ring 3 正规化

| 维度 | Ring 0（先做） | Ring 3（后做） |
|------|----------------|----------------|
| 隔离 | 无，坏程序可崩内核 | 有，坏程序只崩自己 |
| 需要 | 无（复用现有 GDT/IDT） | TSS(ss0/esp0)、用户段(0x1B/0x23)、分页 |
| 与 6.3.1 关系 | ELF 程序就是 task.c 里的一个 task | 需改造任务栈为"内核栈+用户栈"双栈 |
| 工作量 | 小 | 大（一个完整里程碑） |

**建议**：先做 Ring 0——把 ELF 程序当作一个**协作式任务**跑起来（复用 task.c），快速验证整条链路；隔离性之后再用分页 + TSS 补。

### 7.2 与 6.3.1 三模块的衔接

- **mem.c**：加载器用 `mem_alloc` 给 ELF 程序分配栈和堆；程序内 malloc 走 syscall 5。
- **task.c**：`task_create` 一个任务，入口指向 `e_entry`；`sleep(7)`/`getchar` 让程序阻塞时让出 CPU。**多程序 = 多任务**，天然支持并发。
- **syscall.c**：程序与内核唯一的交互面，边界清晰。

### 7.3 栈与堆的布局（单程序，Ring 0）

```
0x100000  ┌─────────────┐ ← ELF .text/.data/.bss (p_vaddr 加载)
          │   ...       │
0x400000  ├─────────────┤ ← 内核堆 (mem.c HEAP_START)
          │  程序堆      │ ← 程序 malloc 池
          ├─────────────┤
0x8xxxxx  │  程序栈      │ ← task_create 分配的栈 (向下增长)
          └─────────────┘
```

---

## 八、三种落地路径与分阶段计划

### 路径 A — 主机交叉编译（最快见效）

在 WSL 上交叉编译 → 静态 ELF → 放进 B.img → AMUNOS 加载运行。

- 优点：编译器在主机上跑，功能最全、迭代最快，先证明"能跑真 ELF"。
- 缺点：不是自举，用户还得回到主机编译。

### 路径 B — tcc 自举进 AMUNOS（最终目标）

把 tcc 交叉编译成 AMUNOS 能跑的 ELF（鸡生蛋：用路径 A 的产物来编译 tcc 自己），之后用户就能**在 AMUNOS shell 里直接 `TCC foo.c -o foo.elf`**。

- 优点：真正的"系统内编程"，完整 C 语言，取代 CC 字节码。
- 缺点：tcc 运行需要较完整的 libc（malloc/stdio/文件），是最大的工作量。

### 路径 C — 改造现有 CC 输出 ELF（过渡）

让 `x86gen.c` 不再产裸 `.COM`，而是产最小 ELF——为加载器提供最早的测试样本，无需先搞定交叉工具链。

### 分阶段路线图（对齐已知的 v6.6-6.8）

| 阶段 | 版本 | 内容 |
|------|------|------|
| 1 | v6.6 | **ELF 加载器 (elf.c)** + 路径 C 的最小 ELF 生成（让加载器可测） |
| 2 | v6.7 | **minilibc**（crt0 + syscall 封装 + printf + malloc）+ 路径 A（tcc 主机交叉编译） |
| 3 | v6.8 | **tcc 自举**（路径 B）+ 补齐文件 syscall，取代 CC 字节码/`.COM` |
| 4 | 后续 | Ring 3 用户态 + 分页 + 动态链接 + newlib 移植 |

---

## 九、风险与权衡

1. **tcc 的 bare-metal 支持相对小众**：官方更常测的是 native/Linux 目标，交叉到裸机 i386 需要手动配 `config-extra.mak` 或 `TCC_LIBRARY_PATH`。缓解：先用路径 A 在主机验证，必要时退到 i386-elf-gcc。
2. **libc 是隐形大头**：printf/malloc 看似简单，做"完整"很难。缓解：minilibc 只覆盖 tcc 自举所需的最小集合，不做 newlib 级别的完整度。
3. **无分页的隔离缺失**：Ring 0 下坏程序会崩内核。缓解：先用 fault.c 已有的异常处理兜底（显示 `*** FAULT ***`），隔离性后置。
4. **`p_vaddr` 物理地址假设**：未来引入分页后，加载器要做"虚拟地址 → 物理页"映射，而非现在的 `memcpy`。缓解：加载器从一开始就把"映射"抽成函数，便于替换。
5. **存量 CC 的去留**：CC 字节码 / `.COM` 可保留作兼容层，但新程序统一走 ELF，避免两套并存。

---

## 十、结论与建议

**结论**：交叉编译 + ELF 执行是 AMUNOS 从"玩具系统"迈向"真操作系统"的关键一跃，且 **6.3.1 已经把最难的地基打好了**——内存分配、系统调用表、任务调度正是 ELF 执行的三块拼图。剩下的核心工作量只有两块：**一个 ELF 加载器**（几十行）和**一个 minilibc**（几百行），外加交叉工具链的配置。

**建议顺序**：
1. **先写 elf.c + 让 x86gen 输出最小 ELF**（本周就能验证加载器）——见路径 C。
2. **主机侧 tcc cross-i386 出静态 ELF**——见路径 A，跑通"真编译器"。
3. **minilibc 对接 int 0x30**——见 §六，让 printf/malloc 可用。
4. **tcc 自举进 OS**——见路径 B，这是"交叉编译运行 ELF 编译器"命题的完整体现。

---

## 参考链接

- TinyCC 官方仓库：https://repo.or.cz/tinycc.git （`make cross-i386` / `--oformat=binary` / `--sysincludepaths`）
- TinyCC 邮件列表 · bare-metal 与交叉编译讨论：https://lists.libreplanet.org/archive/html/tinycc-devel/2019-03/msg00028.html
- TinyCC 邮件列表 · crossbuild patches / roadmap：https://lists.gnu.org/archive/html/tinycc-devel/2026-01/msg00004.html
- TinyCC 构建系统与目标配置（TCC_TARGET_I386 等）：https://deepwiki.com/TinyCC/tinycc/5.1-build-system-and-configuration

---

*AMUNOS v6.3.1 更新 — ELF 交叉编译方案分析*
