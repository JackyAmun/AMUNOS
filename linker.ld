/* AMUNOS 链接脚本 — 平坦二进制，真机可启动
 * 内核加载到 0x8000 (boot.asm 负责从磁盘读取)
 */

OUTPUT_FORMAT("binary")
ENTRY(kernel_entry)

SECTIONS
{
    . = 0x8000;

    .text : {
        *(.text*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
