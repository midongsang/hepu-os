/* ARM64 裸机入口（QEMU virt: 加载到 0x4000_0000） */
.global _start
.section .text.head, "ax"
_start:
    ldr x0, =__stack_top
    mov sp, x0
    bl  bare_main
1:  wfi
    b   1b
