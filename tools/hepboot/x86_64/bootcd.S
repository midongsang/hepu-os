/*
 * HepuOS CD-ROM引导程序（El Torito无模拟模式）
 * 被BIOS加载到0x7C00，负责加载完整的stage2到0x8000，然后跳转
 *
 * 编译：as --32 -o bootcd.o bootcd.S && ld -m elf_i386 -Ttext 0x7C00 --oformat binary -o bootcd.bin bootcd.o
 */
.code16
.global _start

_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp
    sti

    /* 保存启动驱动器号 */
    movb %dl, boot_drive

    /* 串口初始化 */
    call serial_init

    /* 输出启动信息 */
    movw $msg_bootcd, %si
    call serial_puts

    /* 检查INT 13扩展是否支持 */
    movb boot_drive, %dl
    movb $0x41, %ah
    movw $0x55AA, %bx
    int $0x13
    jc no_extensions
    cmpw $0xAA55, %bx
    jne no_extensions

    /* 获取驱动器参数，确定扇区大小 */
    movb boot_drive, %dl
    movb $0x48, %ah
    movw $0x7000, %ax
    movw %ax, %es
    xorw %di, %di
    movw $0x1E, %es:(%di)   /* 缓冲区大小 */
    int $0x13
    jc read_error

    /* 从DAP读取扇区大小（偏移0x18） */
    movw %es:0x18(%di), %ax    /* 字节每扇区 */
    movw %ax, sector_size

    /* 计算需要读取的扇区数：stage2大小16384字节 / 扇区大小 */
    movw $16384, %ax
    xorw %dx, %dx
    divw sector_size
    movw %ax, sectors_to_read

    /* 输出调试信息 */
    movw $msg_loading, %si
    call serial_puts

    /* 使用INT 13扩展读取stage2到0x8000 */
    /* DAP结构在0x7000 */
    movw $0x7000, %ax
    movw %ax, %es
    xorw %di, %di
    movb $0x10, %es:(%di)          /* DAP大小 */
    movb $0, %es:1(%di)            /* 保留 */
    movw sectors_to_read, %ax
    movw %ax, %es:2(%di)           /* 扇区数 */
    movw $0x0000, %es:4(%di)       /* 目标偏移 */
    movw $0x0800, %es:6(%di)       /* 目标段0x0800 -> 物理0x8000 */
    movl $16, %es:8(%di)           /* 起始LBA（stage2在ISO中的位置，需要调整） */
    movl $0, %es:12(%di)

    movb boot_drive, %dl
    movb $0x42, %ah
    int $0x13
    jc read_error

    /* 输出成功信息 */
    movw $msg_jump, %si
    call serial_puts

    /* 跳转到stage2 */
    ljmp $0x0000, $0x8000

no_extensions:
    movw $msg_no_ext, %si
    call serial_puts
    jmp halt

read_error:
    movw $msg_read_err, %si
    call serial_puts

halt:
    hlt
    jmp halt

/* 串口初始化 */
serial_init:
    movw $0x3F8, %dx
    addw $3, %dx
    movb $0x80, %al
    outb %al, %dx
    subw $3, %dx
    movb $0x01, %al
    outb %al, %dx
    incw %dx
    movb $0x00, %al
    outb %al, %dx
    addw $2, %dx
    movb $0x03, %al
    outb %al, %dx
    ret

/* 串口输出字符串（DS:SI = 字符串） */
serial_puts:
    lodsb
    testb %al, %al
    jz 1f
    movb %al, %ah
    movw $0x3F8+5, %dx
2:  inb %dx, %al
    testb $0x20, %al
    jz 2b
    movb %ah, %al
    movw $0x3F8, %dx
    outb %al, %dx
    jmp serial_puts
1:  ret

/* 数据 */
boot_drive:     .byte 0x80
sector_size:    .word 2048    /* 默认CD-ROM扇区大小 */
sectors_to_read: .word 8      /* 默认8个2048字节扇区=16KB */

msg_bootcd:     .asciz "[BootCD] HepuOS CD-ROM bootloader\r\n"
msg_loading:    .asciz "[BootCD] Loading stage2...\r\n"
msg_jump:       .asciz "[BootCD] Jumping to stage2...\r\n"
msg_no_ext:     .asciz "[BootCD] INT 13 extensions not supported\r\n"
msg_read_err:   .asciz "[BootCD] Read error\r\n"

/* 填充到512字节，添加引导签名 */
.fill 510 - (. - _start), 1, 0
.word 0xAA55
