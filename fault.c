/* fault.c — CPU 异常处理 */

#include "common.h"

static char *names[] = {
    "DE","DB","NMI","BP","OF","BR","UD","NM","DF","CSO",
    "TS","NP","SS","GP","PF","RS","XF","AC","MC","XM"
};

void fault_handler(int vector, int eip, int errcode, int eflags) {
    unsigned int cr2 = 0;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    (void)errcode; (void)eflags;
    cls();
    put_str("*** FAULT ***  Vector #");
    put_num(vector);
    if (vector < 20) { put_str(" "); put_str(names[vector]); }
    put_str(" EIP=");
    put_num(eip);
    put_str(" CR2=");
    put_num(cr2);
    put_str("\n");

    if (vector == 13) put_str("General Protection Fault\n");
    if (vector == 6)  put_str("Invalid Opcode — bad ELF?\n");
    if (vector == 14) put_str("Page Fault\n");
    if (vector == 8)  put_str("Double Fault\n");

    put_str("System halted.\n");
    while (1) { __asm__ volatile("cli; hlt"); }
}
