# AMUNOS 常见问题（FAQ）

## Q1. 现在能编写"输入/输出"的 C 程序吗？

**能写一半——输出已完整可用，输入还差一个关键缺口。**

### ✅ 输出（完整可用）
- `printf` / `fprintf` / `sprintf` / `snprintf`、`puts` / `putchar` / `fputs` / `fputc` / `fwrite`
  全部可用，输出到控制台（fd 1/2）或写入 FAT12 文件。
- 格式：`%s %c %d %i %u %x %X %o %p %%` + 宽度/精度 + `l`/`ll`/`z`。**无 `%f`**（无 FPU）。

### ✅ 文件 I/O（完整可用）
`fopen` / `fread` / `fwrite` / `fclose` / `fseek` / `ftell` 全通。

### ⚠️ 输入（有缺口）
- `fgetc(stdin)` 能逐字读到**可打印字符**，但：
  1. **回车键读不到**：`kbd.c` 里回车是 `key_pressed == 2`，而 `sys_read`(fd 0)/`sys_getchar`
     只等 `key_pressed == 1`（可打印字符）。程序永远收不到 `'\n'`——`sys_read` 里那句
     `if (c=='\n'||c=='\r')` 是**死代码**，行式输入会卡死在等一个永不来的 `'\n'`。
  2. **无回显**：程序运行期读键盘时按键不显示（回显只在 shell REPL 里），用户"盲打"。
- 所以 `fread(stdin,...)`、`while((c=fgetc(stdin))!='\n')` 这种行式输入**跑不通**。
  `sscanf`（`%d %u %x %s %c`）本身可用，但只作用于内存字符串；无 `scanf`/`fgets`/`gets`。

### 结论
写"只输出"或"读写文件"的程序没问题；写"从键盘读一行"还不行，卡在回车键没被 syscall
层翻译成换行符。

### 修复方案（P0-①，约 15 行）
在 `syscall.c` 的 `sys_getchar`/`sys_read`(fd 0) 里：
- 把 `key_pressed == 2`（回车）当作 `'\n'` 返回；
- 读字符时顺手回显（`put_char`）；
- 处理退格（`key_pressed == 3`）。

改完即可用 `fgetc(stdin)` + `sscanf`/`atoi` 写标准交互程序：

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    char line[64]; int i = 0, c;
    printf("What is your name? ");
    while (i < 62 && (c = fgetc(stdin)) != '\n' && c != '\r') line[i++] = c;
    line[i] = 0;
    printf("Hello, %s!\n", line);

    printf("How old? ");
    i = 0;
    while (i < 62 && (c = fgetc(stdin)) != '\n' && c != '\r') line[i++] = c;
    line[i] = 0;
    printf("You are %d years old.\n", atoi(line));
    return 0;
}
```

编译运行：`TCC HELLO.C -o HELLO.EXE` → `ELF HELLO.EXE`。

---

## Q2. 之前"运行一些命令后再编译会遇到内存/其它问题"——修好了吗？

**已修复（2026-08-14）。**

- **根因**：`fs.c` 的 `fat12_alloc_cluster` 有两处 FAT12 解码错误，会把**使用中的簇**
  误判为"空闲"，导致第一次编译写输出时覆盖了磁盘上的 `LIBC.A`；第二次编译读到被
  破坏的 libc.a → TCC 返回 `0x2003FF` 或触发异常。
- **修复**：重写分配器为"用 `fat12_get_next_cluster` 逐簇扫描，找到真正空闲簇再分配"，
  并新增 `fs_max_data_cluster` 上限。
- **验证**：连续 3 次编译均返回 `exit=0`，运行编译产物输出正确。

---

## Q3. 程序崩溃时看到 `*** FAULT ***` 是什么？

IDT 异常桩（`fault.c`）捕获 CPU 异常（UD/GPF/PF/DF 等），显示向量号、名称、异常类型后
停机。这在无分页的 Ring 0 模型下是"坏程序崩内核"的兜底手段——真正的隔离要等 P6
（Ring 3 + 分页）。

---

## Q4. 程序死循环了怎么中断？

**Ctrl+C** 全局强制终止。机制：键盘 ISR 在 Ctrl+C 时置 `force_kill=1`；前台程序运行时
任何 syscall 都是中止点（`syscall_handler` 入口检查），CPU 密集死循环由定时器 ISR 把
返回 EIP 重定向到 `force_terminate`，最终标记任务 EXITED 并让出 CPU，shell 的
`task_wait` 唤醒。
