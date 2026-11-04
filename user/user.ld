/* ============================================================================
 * 赫普OS 64位 - 用户态链接脚本
 * 用户态地址空间：代码段从0x400000开始
 * ========================================================================== */

ENTRY(_start)

SECTIONS
{
    . = 0x40000000;

    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.comment)
        /* V0.33: 只丢 .note.GNU-stack，不要用 *(.note*) ——
         * 那会把 .note.ABI-tag 一起丢掉，
         * 导致内核的 elf64_is_linux_abi() 判不出 GNU/Linux ABI 标记，
         * 把用 Linux 号表的程序误判成 Hepu 原生程序。 */
        *(.note.GNU-stack)
        *(.note.stapsdt)
        *(.eh_frame)
    }
}
