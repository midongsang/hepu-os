/* 赫普OS 引导器链接脚本：固定基址 0x8000 (V4.57-E102: 原0x7C00与MBR冲突)
 * V6.53: 单段布局，保持稳定
 */
OUTPUT_FORMAT(elf32-i386)
SECTIONS
{
    . = 0x8000;
    .text : {
        *(.text)
        *(.rodata*)
        *(.data)
        *(.bss*)
        *(COMMON)
    }
}
