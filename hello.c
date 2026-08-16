/* hello.c — AMUNOS ELF 测试程序 (freestanding, 直接走 int 0x30)
 *
 * 在主机上用 gcc -m32 或 tcc 交叉编译成静态 ELF, 链接到 0x100000:
 *   gcc -m32 -nostdlib -static -no-pie -fno-pie -fno-builtin \
 *       -fno-stack-protector -fno-asynchronous-unwind-tables \
 *       -Wl,-Ttext=0x100000 -Wl,--build-id=none -o hello.elf hello.c
 *
 * 运行时系统调用约定 (与 syscall.c 一致):
 *   eax=调用号, ebx=arg1, ecx=arg2, edx=arg3; int 0x30
 *   3 = puts(char*), 4 = putnum(int)
 */

static void sys_puts(const char *s) {
    __asm__ volatile("int $0x30" : : "a"(3), "b"(s) : "memory");
}
static void sys_putnum(int n) {
    __asm__ volatile("int $0x30" : : "a"(4), "b"(n) : "memory");
}
static void sys_exit(int status) {
    __asm__ volatile("int $0x30" : : "a"(13), "b"(status) : "memory");
}

void _start(void) {
    sys_puts("Hello from ELF! (AMUNOS v6.5)\n");
    int i;
    for (i = 1; i <= 5; i++) {
        sys_putnum(i * 100);
    }
    sys_puts("\n[ELF] done, returning to shell\n");
    sys_exit(0);        /* 显式退出, 防止 ret 弹出垃圾返回地址 → PF */
}
