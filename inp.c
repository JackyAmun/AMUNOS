/* inp.c — 输入功能测试程序 (libc 链接)
 *
 * 测试 AMUNOS 的输入缺口修复:
 *   1. 回车键 → '\n' (fgetc 能收到, 行式输入不再卡死)
 *   2. 程序运行期按键回显 (不再"盲打")
 *   3. 退格键 → '\b' (read_line 里回退一个字符)
 *
 * 运行: ELF INP.ELF  (或 TCC INP.C -o INP.EXE 后 ELF INP.EXE)
 */
#include <stdio.h>

/* 读一行: 直到 '\n'/'\r'/EOF; '\b' 回退一个字符 */
static void read_line(char *buf, int max) {
    int i = 0, c;
    while (i < max - 1) {
        c = fgetc(stdin);
        if (c == '\n' || c == '\r' || c < 0) break;
        if (c == '\b') { if (i > 0) i--; continue; }
        buf[i++] = (char)c;
    }
    buf[i] = 0;
}

/* 简易 atoi (stdlib 的 atoi 亦可用; 这里内联保证自包含) */
static int atoi_s(const char *s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); s++; }
    return n;
}

int main(void) {
    char line[64];

    printf("What is your name? "); fflush(stdout);
    read_line(line, sizeof(line));
    printf("Hello, %s!\n", line);

    printf("How old are you? "); fflush(stdout);
    read_line(line, sizeof(line));
    printf("You are %d years old.\n", atoi_s(line));

    printf("[inp] input test PASSED\n");
    return 0;
}
