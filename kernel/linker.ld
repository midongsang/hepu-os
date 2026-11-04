/* 赫普OS 内核链接脚本 */
ENTRY(_start)

SECTIONS
{
    /* 内核加载地址 1MB */
    . = 0x100000;

    /* Multiboot 头必须在最前面 */
    .multiboot ALIGN(4) : {
        KEEP(*(.multiboot))
    }

    /* 代码段 */
    .text ALIGN(4K) : {
        *(.text)
        *(.text.*)
    }

    /* V3.82修复: .data提前到.rodata前——读盘缓冲(0x20000起)上限必须<0xC0000
       (ISA空洞/VGA ROM区, 写入被固件丢弃→表区读到垃圾).
       原顺序 .text+.rodata+.data 使缓冲达0xC2800>0xC0000, 表区(源0xC08A0)在空洞内.
       .data先放→总文件≤0x9E42D→缓冲上限0xBE42D<0xC0000安全. */
    /* 数据段 - 包含内核版权指纹 */
    .data ALIGN(4K) : {
        *(.data)
        *(.data.*)
    }

    /* 只读数据 */
    .rodata ALIGN(4K) : {
        *(.rodata)
        *(.rodata.*)
    }

    /* BSS */
    .bss ALIGN(4K) : {
        __bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        __bss_end = .;
    }

    /* V3.83: 独立内核栈段(.bss 之后, 16KB).
     * 原因: 原栈在 .bss 开头(0x1A4F68-0x1A8F68), 与 QEMU multiboot
     * 加载器放在 .data 文件末(0x1A8F50)的 cmdline 字符串重叠,
     * 绝对路径 cmdline 覆盖栈顶→返回地址被覆盖→早期跳飞。
     * 栈移到这里, 远离低址 cmdline 区与堆区(堆在 __kernel_end 之后). */
    .stack ALIGN(4K) : {
        __stack_bottom = .;
        . += 16384;
        __stack_top = .;
    }

    /* 内核结束标记 */
    _end = .;
    __kernel_end = .;

    /* V2.25: 丢弃动态重定位段——静态链接的freestanding内核不需要,
     * 诊断代码(HEPU_BOOT_DEBUG)引用了字符串地址会生成.rel.dyn, 必须丢弃. */
    /DISCARD/ : {
        *(.rel.dyn)
        *(.rela.dyn)
        *(.rel.plt)
        *(.rela.plt)
        *(.rel.text)
        *(.rela.text)
        *(.rel.data)
        *(.rela.data)
    }
}
