/* idt.c - 中断描述符表 (IDT) 初始化 + 8259A PIC 重映射
 *
 * 参考 flash-4th-os/kernel/trap.c + asm/system.h
 * 简化版：直接用 C 结构体操作 IDT */

#include "common.h"

// ── IDT 门描述符 (8 字节) ──
struct idt_gate {
    unsigned short base_low;   // 处理函数地址低 16 位
    unsigned short selector;   // 代码段选择子 (0x08)
    unsigned char  zero;       // 保留，必须为 0
    unsigned char  flags;      // P|DPL|0|Type = 0x8E (中断门) 或 0x8F (陷阱门)
    unsigned short base_high;  // 处理函数地址高 16 位
} __attribute__((packed));

// ── IDT 指针 (6 字节，给 lidt 指令用) ──
struct idt_ptr {
    unsigned short limit;
    unsigned int   base;
} __attribute__((packed));

// ── IDT 表 (256 个门) ──
static struct idt_gate idt[256];

// ── 来自 head.asm 的中断/异常桩 ──
extern void asm_keyboard_handler();
extern void asm_syscall_handler();
extern void asm_timer_handler();
extern void asm_fault_ud(), asm_fault_df(), asm_fault_gp(), asm_fault_pf();

/* 设置一个 IDT 门 */
static void set_gate(int vector, unsigned int handler, unsigned char flags) {
    idt[vector].base_low  = handler & 0xFFFF;
    idt[vector].selector  = 0x08;           // 内核代码段
    idt[vector].zero      = 0;
    idt[vector].flags     = flags;
    idt[vector].base_high = (handler >> 16) & 0xFFFF;
}

/* 初始化 8259A PIC：将 IRQ0-15 重映射到中断向量 0x20-0x2F */
static void pic_remap() {
    // ICW1: 初始化命令
    io_out8(0x20, 0x11);   // 主片
    io_out8(0xA0, 0x11);   // 从片

    // ICW2: 中断向量偏移
    io_out8(0x21, 0x20);   // 主片 IRQ0-7   → 向量 0x20-0x27
    io_out8(0xA1, 0x28);   // 从片 IRQ8-15  → 向量 0x28-0x2F

    // ICW3: 级联配置
    io_out8(0x21, 0x04);   // 主片 IRQ2 连接从片
    io_out8(0xA1, 0x02);   // 从片级联标识

    // ICW4: 8086/88 模式
    io_out8(0x21, 0x01);
    io_out8(0xA1, 0x01);

    // 中断屏蔽：开启键盘 (IRQ1) + 定时器 (IRQ0)
    io_out8(0x21, 0xFC);   // 主片: 1111 1100 (IRQ0+IRQ1 开放)
    io_out8(0xA1, 0xFF);   // 从片: 1111 1111 (全部屏蔽)
}

/* 初始化 IDT — 由 kernel.c 的 kmain() 调用 */
void init_idt() {
    // 1. 先重映射 PIC
    pic_remap();

    // 2. CPU 异常处理 (陷阱门)
    set_gate(6,  (unsigned int)asm_fault_ud, 0x8F);   // 无效操作码
    set_gate(8,  (unsigned int)asm_fault_df, 0x8F);   // 双重故障
    set_gate(13, (unsigned int)asm_fault_gp, 0x8F);   // 通用保护
    set_gate(14, (unsigned int)asm_fault_pf, 0x8F);   // 页故障

    // 3. 键盘中断 (IRQ1 → 向量 0x21)
    set_gate(0x21, (unsigned int)asm_keyboard_handler, 0x8F);

    // 4. 系统调用 (int 0x30, 陷阱门 — 允许应用层调用)
    set_gate(0x30, (unsigned int)asm_syscall_handler, 0xEF);

    // 5. 定时器 (IRQ0 → 向量 0x20)
    set_gate(0x20, (unsigned int)asm_timer_handler, 0x8F);

    // 6. 加载 IDT
    struct idt_ptr ptr;
    ptr.limit = sizeof(idt) - 1;
    ptr.base  = (unsigned int)&idt;

    __asm__ volatile ("lidt %0" : : "m"(ptr));

    // 7. 初始化键盘控制器
    keyboard_init();
}
