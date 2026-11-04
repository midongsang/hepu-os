/* ============================================================================
 * 赫普OS - 极简引导扇区 (MBR) - GAS 语法
 * 从磁盘读取内核到 0x100000，进入保护模式，跳转执行
 * ========================================================================== */

.code16

    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp

    /* 保存启动驱动器号 */
    movb %dl, boot_drive

    /* 打印启动信息 */
    movw $msg_loading, %si
    call print_string

    /* 读取内核到 0x1000:0x0000 = 0x10000 */
    movw $0x1000, %ax
    movw %ax, %es
    xorw %bx, %bx

    movb $0x02, %ah        /* BIOS 读扇区 */
    movb $127, %al         /* 读 127 扇区 (~64KB) */
    movb $0, %ch           /* 柱面 0 */
    movb $2, %cl           /* 扇区 2 */
    movb $0, %dh           /* 磁头 0 */
    movb boot_drive, %dl
    int $0x13
    jc disk_error

    /* 进入保护模式 */
    cli
    lgdt gdt_descriptor

    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0

    ljmp $0x08, $protected_mode

.code32
protected_mode:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $0x90000, %esp

    /* 把内核从 0x10000 复制到 0x100000 */
    movl $0x10000, %esi
    movl $0x100000, %edi
    movl $16384, %ecx      /* 64KB / 4 = 16384 双字 */
    rep movsl

    /* Multiboot 魔数 */
    movl $0x2BADB002, %eax
    xorl %ebx, %ebx

    /* 跳转到内核 */
    call *0x100000

    cli
hang:
    hlt
    jmp hang

.code16
print_string:
    lodsb
    orb %al, %al
    jz 1f
    movb $0x0E, %ah
    int $0x10
    jmp print_string
1:
    ret

disk_error:
    movw $msg_disk_error, %si
    call print_string
    cli
    hlt

/* 数据 */
boot_drive:     .byte 0
msg_loading:    .asciz "HEPU OS Boot... "
msg_disk_error: .asciz "DISK ERROR!"

/* GDT */
gdt_start:
    .quad 0x0000000000000000
gdt_code:
    .word 0xFFFF, 0x0000
    .byte 0x00, 0x9A, 0xCF, 0x00
gdt_data:
    .word 0xFFFF, 0x0000
    .byte 0x00, 0x92, 0xCF, 0x00
gdt_end:

gdt_descriptor:
    .word gdt_end - gdt_start - 1
    .long gdt_start

/* 填充到 510 字节 + 启动签名 */
. = 510
.word 0xAA55
