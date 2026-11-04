.code16
.global ap_trampoline_start
.global ap_trampoline_end

ap_trampoline_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7000, %sp
    movl $0x1, 0x9100
    .byte 0x0F, 0x01, 0x16
    .word 0x8120
    movl $0x2, 0x9104
    movl %cr0, %eax
    orl $0x01, %eax
    movl %eax, %cr0
    movl $0x3, 0x9108
    .byte 0xEA
    .word 0x803B
    .word 0x0008

.code32
ap_32bit:
    movl $0x10, %eax
    movl %eax, %ds
    movl %eax, %es
    movl %eax, %ss
    movl $0x7000, %esp
    movl $0x4, 0x910C
    movl %cr4, %eax
    orl $0x20, %eax
    movl %eax, %cr4
    movl $0x5, 0x9110
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr
    movl $0x6, 0x9114
    movl 0x9000, %eax
    movl %eax, %cr3
    movl $0x7, 0x9118
    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0
    movl $0x8, 0x911C
    .byte 0xEA
    .long 0x80AE
    .word 0x0018

.code64
ap_64bit:
    movq $0x9, 0x9120
    movq 0x9008, %rsp
    movq $0xA, 0x9124
    movq 0x9010, %rax
    call *%rax
    movq $0xB, 0x9128
    hlt
    jmp .

.org 0x100
ap_gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
    .quad 0x00AF9A000000FFFF
ap_gdt_desc:
    .word 0x1F
    .long 0x8100

ap_trampoline_end:
