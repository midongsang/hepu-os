/* ============================================================================
 * 赫普OS - 64位内核链接脚本
 * V0.10.6: 初始64位内核链接脚本
 * ========================================================================== */

ENTRY(multiboot_entry)

PHDRS
{
    text PT_LOAD FLAGS(5);  /* R+E */
    note PT_NOTE FLAGS(4); /* R */
    data PT_LOAD FLAGS(6); /* R+W */
}

SECTIONS
{
    /* 内核加载地址：1MB */
    . = 0x100000;

    /* 64位入口代码（必须在最前面，固定地址0x100000） */
    .text.entry64 : {
        *(.text.entry64)
    } :text

    /* Multiboot头 */
    .multiboot : {
        *(.multiboot)
    } :text

    /* PVH ELF Note */
    .note : {
        *(.note)
        *(.note.*)
    } :note

    /* 32位启动代码 */
    .text.startup : {
        *(.text.startup)
    } :text

    /* 代码段 */
    .text : {
        *(.text)
        *(.text.*)
    }

    /* 只读数据段 */
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    /* 数据段 */
    .data : {
        *(.data)
        *(.data.*)
    } :data

    /* BSS段 */
    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    /* 内核结束地址 */
    _end = .;
    __kernel_end = .;

    /* 丢弃不需要的段 */
    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}
