/* link.ld — AMUNOS 用户程序链接脚本 (libc.a 链接, 载入 0x100000) */
ENTRY(_start)

SECTIONS
{
    . = 0x100000;

    .text   : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    .bss    : { *(.bss .bss.*) *(COMMON) }
}
