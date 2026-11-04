/* ============================================================================
 * 赫普OS - Rust用户态应用链接脚本
 * 基址 0x08048000（与Linux ELF兼容，赫普OS ELF加载器使用此地址）
 * 关键：所有段连续排列在一个PT_LOAD中，确保加载器能完整加载
 * ========================================================================== */

ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(7);  /* R+W+E = 7，全部一个段 */
    stack PT_GNU_STACK FLAGS(6);
}

SECTIONS
{
    /* 从0x08048000开始 */
    . = 0x08048000;

    /* 所有段都放在一个PT_LOAD中，连续排列不4K对齐 */
    .text : {
        *(.text._start)
        *(.text*)
        *(.rodata*)
        *(.rodata)
        *(.gnu.linkonce.t*)
        *(.gnu.linkonce.r*)
        *(.gcc_except_table)
    } :text

    /* 数据段（紧跟.text，不4K对齐确保在LOAD段内） */
    .data : {
        __data_start = .;
        *(.data*)
        *(.gnu.linkonce.d*)
        *(.sdata*)
        __data_end = .;
    } :text

    /* BSS段 */
    .bss : {
        __bss_start = .;
        *(.bss*)
        *(.sbss*)
        *(COMMON)
        __bss_end = .;
    } :text

    /* 堆起始（BSS之后） */
    . = ALIGN(8);
    __heap_start = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.note.*)
        *(.eh_frame*)
        *(.eh_frame_hdr)
        *(.debug*)
        *(.debug_*)
    }
}
