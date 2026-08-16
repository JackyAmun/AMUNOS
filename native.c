/* native.c — .COM 运行时支持
 *
 * 调用约定 (无参数 C 函数, 通过固定内存槽传值):
 *   0x1000 → amunos_printnum()  — 读取 [0x2000] 输出数字
 *   0x1010 → amunos_getnum()    — 读键盘返回数字 (EAX)
 *   变量槽: 0x2000, 0x2004, 0x2008, 0x200C (a,b,c,d)
 */

#include "common.h"

void native_init() {}

/* 输出 [0x2000] 的数字 */
void amunos_printnum() {
    put_num(*(int*)0x2000);
    put_char(' ', 0x07);
}

/* 从键盘读一个数字，返回 EAX */
int amunos_getnum() {
    int num = 0;
    int have = 0;
    // 清掉残留按键状态
    key_pressed = 0;
    put_str("[in] ");
    while (1) {
        int kp = 0;
        while (kp == 0) { kbd_poll(); kp = key_pressed; }

        if (kp == 1) {   // 字符
            char c = current_char;
            key_pressed = 0;
            if (c >= '0' && c <= '9') {
                num = num * 10 + (c - '0');
                have = 1;
                put_char(c, 0x0F);
            } else if (c == ' ' || c == ',' || c == '\n') {
                put_char('\n', 0x07);
                if (have) return num;
            }
        } else if (kp == 3) {  // 退格
            if (have) {
                num /= 10;
                if (cur_x > 6) { cur_x--; put_char(' ', 0x07); cur_x--; update_cursor(); }
                have = (num > 0);
            }
            key_pressed = 0;
        } else if (kp == 2) {  // Enter
            put_char('\n', 0x07);
            key_pressed = 0;
            if (have) return num;
        } else {
            key_pressed = 0;
        }
    }
}
