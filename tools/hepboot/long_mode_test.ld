/* 64位长模式测试链接脚本 */
OUTPUT_FORMAT("elf32-i386")
ENTRY(_start)

SECTIONS
{
    /* 代码从0x100000开始，mb_header在最前面，_start在0x101000 */
    . = 0x100000;
    .text : {
        *(.text)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
    }
}
