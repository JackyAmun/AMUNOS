/* AMUNOS 链接脚本 — 平坦二进制，真机可启动
 * 内核加载到 0x10000 (stage2 负责从磁盘读取，避免覆盖自身)
 */

OUTPUT_FORMAT("binary")
ENTRY(kernel_entry)

SECTIONS
{
    . = 0x10000;

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
