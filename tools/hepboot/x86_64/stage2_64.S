/* ============================================================================
 * 赫普OS - 64位内核专用Stage2引导器（带串口调试输出）
 *
 * 功能：
 *   1. 从磁盘加载64位内核到0x100000
 *   2. 切换到保护模式
 *   3. 构建4级页表
 *   4. 切换到长模式
 *   5. 跳转到64位内核入口（multiboot_entry @ 0x10000c）
 *
 * 版本: v0.02 (添加串口输出)
 * 日期: 2026-10-10
 * ========================================================================== */

.code16
.section .text
.global _start

/* 串口输出宏（实模式和保护模式通用，直接写0x3F8端口） */
.macro SERIAL_PUTC char
    movb $\char, %al
    call serial_putc
.endm

.macro SERIAL_PUTS str
    movw $\str, %si
    call serial_puts
.endm

.macro SERIAL_PUTS32 str
    movl $\str, %esi
    call serial_puts32
.endm

_start:
    /* 远跳转，确保CS=0, IP=0x8000（兼容CD-ROM和硬盘引导）
     * CD-ROM引导时BIOS可能设置CS=0x0800,IP=0，硬盘引导时CS=0,IP=0x8000
     * 统一跳转到0:0x8000确保代码地址引用正确 */
    ljmp $0, $stage2_entry
stage2_entry:
    /* 此时在实模式，CS=0, IP=0x8000 */
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp

    /* 保存引导驱动器号 */
    movb %dl, boot_drive

    /* 初始化串口 */
    call serial_init

    /* 串口输出：Stage2启动 */
    SERIAL_PUTS msg_start

    /* V0.55 P1: 检测CPU长模式能力（菜单灰显用）+ 读取持久化启动配置 */
    call detect_lm_cap
    call load_boot_config

    /* VGA输出 "64" */
    movw $0xB800, %ax
    movw %ax, %es
    movw $160*2, %di
    movw $0x1F36, %ax
    stosw
    movw $0x1F34, %ax
    stosw

    /* ===== 启动菜单 ===== */
    call show_boot_menu
    movb %al, boot_choice

    /* 设置VBE图形模式（32位和64位共用） */
    call vbe_setup_common

    /* 根据选择加载内核 */
after_vbe_setup:
    cmpb $2, boot_choice
    je load_32bit_kernel
    jmp load_64bit_kernel

load_32bit_kernel:
    SERIAL_PUTS msg_load_32bit
    /* 进入unreal mode（实模式下访问4GB内存） */
    call enter_unreal_mode
    SERIAL_PUTS msg_unreal_ok
    /* 检测引导设备类型：CD-ROM使用2048字节扇区，硬盘使用512字节 */
    movb boot_drive, %al
    cmpb $0xE0, %al
    jl 1f
    /* CD-ROM模式：LBA=1100(4400/4)，每次32扇区，总2058扇区(4.1MB) */
    movl $850, %ebx          /* 当前LBA */
    movl $2058, %ecx         /* 剩余扇区数 */
    movl $11, %ebp           /* 扇区->字节移位：2^11=2048 */
    jmp 2f
1:
    /* 硬盘模式：LBA=3400，每次128扇区，总8231扇区(4.1MB)
     * V0.54.6: 64位内核已增长到 ~586KB，占 LBA2000-3172；原 3000 会重叠，
     * 故 32 位内核整体后移到 4400（V0.57.0: 64位内核加载区扩到 1MB,
     * 上界 LBA4048, 32位内核相应从 3400 后移到 4400）。 */
    movl $4400, %ebx         /* 当前LBA（V0.57.0: 64位内核区扩到1MB后移） */
    movl $8231, %ecx         /* 剩余扇区数 */
    movl $9, %ebp            /* 扇区->字节移位：2^9=512 */
2:
    movl $0x100000, %edi     /* 目标高内存地址 */
load_32bit_loop:
    cmpl $0, %ecx
    jle load_32bit_done
    /* 本次读取扇区数：CD-ROM用32，硬盘用128 */
    movl $128, %eax
    cmpl $0xE0, boot_drive
    jl 3f
    movl $32, %eax           /* CD-ROM每次32扇区(64KB) */
3:
    cmpl %eax, %ecx
    jg load_32bit_read
    movl %ecx, %eax
load_32bit_read:
    pushl %ebx
    pushl %ecx
    pushl %edx
    pushl %edi
    pushl %ebp
    /* 读取到低内存0x10000 */
    movw $0x7B00, %si
    movb $0x10, %ds:(%si)
    movb $0x00, %ds:1(%si)
    movw %ax, %ds:2(%si)
    movw $0x0000, %ds:4(%si)
    movw $0x1000, %ds:6(%si)  /* 段0x1000 -> 0x10000 */
    movl %ebx, %ds:8(%si)
    movl $0, %ds:12(%si)
    movb boot_drive, %dl
    movb $0x42, %ah
    int $0x13
    popl %ebp
    popl %edi
    popl %edx
    popl %ecx
    popl %ebx
    jc read_error_32
    /* 从低内存0x10000复制到高内存edi（使用FS:32位偏移） */
    pushl %ecx
    pushl %esi
    pushl %eax              /* 保存扇区数 */
    pushl %edi
    pushw %ds               /* 保存DS */
    movw $0x1000, %ax       /* DS=0x1000 -> 物理地址0x10000 */
    movw %ax, %ds
    xorl %esi, %esi         /* esi=0，DS:0 = 0x10000 */
    movl %ebp, %ecx         /* 移位值 */
    shll %cl, %eax          /* 字节数 = 扇区 * 2^shift */
    shrl $2, %eax           /* 双字数 */
    cld
copy_high_loop:
    movl (%esi), %edx
    movl %edx, %fs:(%edi)
    addl $4, %esi
    addl $4, %edi
    decl %eax
    jnz copy_high_loop
    popw %ds               /* 恢复DS */
    popl %edi
    popl %eax              /* 恢复扇区数 */
    popl %esi
    popl %ecx
    /* 更新 */
    pushl %eax              /* 保存扇区数 */
    pushl %ecx
    movl %ebp, %ecx
    shll %cl, %eax          /* 字节数 = 扇区 * 2^shift */
    addl %eax, %edi
    popl %ecx
    popl %eax              /* 恢复扇区数 */
    addl %eax, %ebx
    subl %eax, %ecx
    jmp load_32bit_loop
read_error_32:
    SERIAL_PUTS msg_read_fail
    jmp load_64bit_kernel
load_32bit_done:
    SERIAL_PUTS msg_32bit_loaded
    /* 搜索multiboot头（前8192字节，使用FS访问高内存） */
    movl $0x100000, %esi
    movl $2048, %ecx
search_multiboot:
    cmpl $0x1BADB002, %fs:(%esi)
    je multiboot_found
    addl $4, %esi
    loop search_multiboot
    SERIAL_PUTS msg_no_multiboot
    jmp load_64bit_kernel
multiboot_found:
    SERIAL_PUTS msg_multiboot_found
    movl %esi, multiboot_header_ptr
    /* 进入32位保护模式 */
    cli
    lgdt gdt32_ptr
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmp $0x08, $protected_mode_32

/* 进入unreal mode（只设置FS/GS为32位，DS/ES保持实模式） */
/* ============ V0.13.63: E820 内存探测 ============
 * 遍历 BIOS E820 内存图, 记录最大可用内存结束地址(64位), 写 0x90100
 * 调用前: 实模式, ds=0 */
mem_detect_e820:
    pushal
    pushw %es
    /* E801: 单次 int15, 无缓冲区无循环 — 返回 CX=1MB~16MB扩展(KB), DX=16MB以上(64KB单位)
     * 覆盖到 4GB; 内核 pmm 上限 2GB, 足够 */
    movw $0xE801, %ax
    xorw %bx, %bx
    int $0x15
    jc 8f
    /* total = 1MB + CX*1024 + DX*65536 (字节) */
    movzwl %cx, %eax        /* 零扩展, 无高16位残留 */
    shll $10, %eax          /* CX*1024 */
    movzwl %dx, %ecx
    shll $16, %ecx          /* DX*64KB */
    addl %ecx, %eax
    addl $0x100000, %eax    /* + 低端 1MB */
    jmp 7f
8:
    movl $0x10000000, %eax  /* 探测失败兜底 256MB */
7:
    movw $0x9000, %bx
    movw %bx, %es           /* 结果段 0x9000 (0x90100) */
    xorl %edx, %edx
    cmpl $0x80000000, %eax
    ja 5f
    cmpl $0x100000, %eax
    jb 5f
    movl %eax, %es:0x0100
    movl %edx, %es:0x0104
5:
    popw %es
    popal
    ret

msg_e820_ok:   .asciz "[E820] int15 OK, SMAP OK\r\n"
msg_wr_ok:      .asciz " WROK:90100="
msg_e801_cx:    .asciz " E801:cx="
msg_e801_dx:    .asciz " dx="
msg_es_after:   .asciz " es_ret="
msg_di_after:   .asciz " di_ret="
msg_dbg_ds:     .asciz " ds:7B00="
msg_dbg_ds2:    .asciz " ds:7B30="
msg_entry_base: .asciz " base="
msg_entry_type: .asciz " type="
msg_crlf16:     .asciz "\r\n"

/* 16位hex输出: ax -> 4位hex (只经串口, 无内存访问, 实模式安全) */
serial_puthex16:
    pusha
    movw $4, %cx
1:
    rolw $4, %ax
    movw %ax, %dx
    andw $0xF, %dx
    cmpb $10, %dl
    jae 2f
    addb $'0', %dl
    jmp 3f
2:
    subb $10, %dl
    addb $'A', %dl
3:
    movb %dl, %al
    call serial_putc
    loop 1b
    popa
    ret


msg_e820_cf:   .asciz "[E820] int15 CF=1 (BIOS不支持)\r\n"
msg_e820_smap: .asciz "[E820] SMAP mismatch\r\n"

enter_unreal_mode:
    cli
    pushw %ds
    lgdt gdt32_ptr
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0           /* 保护模式 */
    movw $0x10, %ax
    movw %ax, %fs             /* FS=32位数据段（隐藏部分） */
    movw %ax, %gs             /* GS=32位数据段（隐藏部分） */
    movl %cr0, %eax
    andl $~1, %eax
    movl %eax, %cr0           /* 回到实模式 */
    xorw %ax, %ax
    movw %ax, %fs             /* FS=0，但隐藏部分仍是32位 */
    movw %ax, %gs             /* GS=0，但隐藏部分仍是32位 */
    popw %ds                  /* 恢复DS（实模式） */
    sti
    ret

load_64bit_kernel:
    SERIAL_PUTS msg_load_kernel

    /* V0.56 真机修复: 旧方案把第 10 个 64KB 块暂存到 0xA0000(VGA 显存窗口),
     * 而内核 .data(链接地址 0x1909c0+) 恰好全部落在该块 → 真机 VGA 窗口读回
     * 不可靠 → .data 被破坏 → 黑屏/重启; 另外 E801 结果写 0x90100 也会踩到
     * 第 9 块里的内核数据。改为 unreal mode 下"读一块 → 立刻拷到 1MB 以上",
     * 全程只用 0x10000 作 64KB 缓冲, 不再占用 0x60000-0xAFFFF。 */
    call enter_unreal_mode

    /* 参数: EBX=起始LBA, ECX=每块扇区数, EDX=块数(32 x 32KB = 1MB)
     * V0.57.0: 由 20 块(640KB)扩到 32 块(1MB) —— 640KB 上限时内核已占 623KB,
     * 只余 17KB, 任何第三方库都塞不进去。内核 .bss 内的多读部分会被入口清零覆盖,
     * 分配器的空闲内存起点取自链接符号 _end, 不受影响。
     * 每次 32KB 而非 64KB: 不少真机 BIOS 的 EDD 单次传输上限为 127 扇区 */
    cmpb $0xE0, boot_drive
    jl 1f
    /* CD-ROM: 2048B/扇区, 16 扇区 = 32KB, 内核起始 LBA 500 */
    movl $500, %ebx
    movl $16, %ecx
    jmp 2f
1:
    /* HDD: 512B/扇区, 64 扇区 = 32KB, 内核起始 LBA 2000 */
    movl $2000, %ebx
    movl $64, %ecx
2:
    movl $0x100000, %edi
    movl $32, %edx

load64_live_loop:
    /* DAP: 读到 0x10000 (DS=0) */
    movw $0x7B00, %si
    movb $0x10, %ds:(%si)
    movb $0x00, %ds:1(%si)
    movw %cx, %ds:2(%si)
    movw $0x0000, %ds:4(%si)
    movw $0x1000, %ds:6(%si)
    movl %ebx, %ds:8(%si)
    movl $0, %ds:12(%si)

    /* BIOS INT13 会破坏 EDX/ECX/EBX 等寄存器 → 读盘前后保存循环状态 */
    pushl %ebx
    pushl %ecx
    pushl %edx
    pushl %edi
    movb $3, load64_retry
load64_read:
    movb boot_drive, %dl
    movb $0x42, %ah
    int $0x13
    jnc load64_read_ok
    decb load64_retry
    jnz load64_read
    jmp read_error
load64_read_ok:
    popl %edi
    popl %edx
    popl %ecx
    popl %ebx

    /* 复制 32KB: 0x10000 -> EDI (FS 隐藏段 = 4GB 平坦) */
    pushl %ebx
    pushl %ecx
    pushl %edx
    pushl %edi
    movw $0x1000, %ax
    movw %ax, %ds
    xorl %esi, %esi
    movl $8192, %ecx        /* 32KB / 4 */
    cld
load64_copy_loop:
    movl (%esi), %eax
    movl %eax, %fs:(%edi)
    addl $4, %esi
    addl $4, %edi
    decl %ecx
    jnz load64_copy_loop
    xorw %ax, %ax
    movw %ax, %ds           /* 恢复 DS=0 */
    popl %edi
    popl %edx
    popl %ecx
    popl %ebx

    addl %ecx, %ebx         /* 下一块 LBA */
    addl $32768, %edi       /* 下一块目标 */
    decl %edx
    jnz load64_live_loop

    jmp load_64bit_done

load64_retry: .byte 0

load_64bit_done:
    SERIAL_PUTS msg_load_ok

    /* VGA输出 "OK" */
    movw $0xB800, %ax
    movw %ax, %es
    movw $160*2 + 4, %di
    movw $0x2F4F, %ax
    stosw
    movw $0x2F4B, %ax
    stosw

    /* V0.13.63: 真机内存探测 — 在 VBE/内核加载(INT13) 全部完成后、进保护模式前
     * 调用 (SeaBIOS 的 VBE/磁盘服务会覆盖 0x90000 低内存区, 过早探测结果会被清)
     * 结果写 0x90100: 64位 总内存大小(字节) [0x90100=低32位, 0x90104=高32位]
     * 内核 pmm64_init 读取替代硬编码 2GB (真机<2GB 时分配不存在内存会崩溃) */
    call mem_detect_e820

    jmp enter_pm_64

    /* 1.5 设置VBE图形模式（多模式降级） */
vbe_setup_common:
    SERIAL_PUTS msg_vbe_start

    /* 确保实模式段寄存器正确（之前的VGA输出可能修改了ES） */
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es

    /* 先检查VBE是否存在 */
    movw $0x4F00, %ax
    movw $0x7000, %di
    movw %di, %es
    xor %di, %di
    int $0x10
    cmpw $0x004F, %ax
    jne vbe_failed

    /* 尝试多个VBE模式（从高到低） */
    movw $0, vbe_width  /* 标记未设置 */

    /* 模式1: 1024x768x32 (0x144) */
    movw $0x144, %cx
    call vbe_try_mode
    cmpw $0, vbe_width
    jne vbe_set_ok

    /* 模式2: 800x600x32 (0x143) */
    movw $0x143, %cx
    call vbe_try_mode
    cmpw $0, vbe_width
    jne vbe_set_ok

    /* 模式3: 640x480x32 (0x141) */
    movw $0x141, %cx
    call vbe_try_mode
    cmpw $0, vbe_width
    jne vbe_set_ok

    /* 模式4: 1024x768x16 (0x117) */
    movw $0x117, %cx
    call vbe_try_mode
    cmpw $0, vbe_width
    jne vbe_set_ok

    /* 模式5: 800x600x16 (0x114) */
    movw $0x114, %cx
    call vbe_try_mode
    cmpw $0, vbe_width
    jne vbe_set_ok

    /* 模式6: 1024x768x8 (0x105) */
    movw $0x105, %cx
    call vbe_try_mode
    cmpw $0, vbe_width
    jne vbe_set_ok

    /* 模式7: 800x600x8 (0x103) */
    movw $0x103, %cx
    call vbe_try_mode
    cmpw $0, vbe_width
    jne vbe_set_ok

    jmp vbe_failed

vbe_try_mode:
    /* 保存模式号（CX可能被INT 0x10破坏） */
    pushw %cx

    /* 获取模式信息 */
    movw $0x4F01, %ax
    movw $0x7000, %di
    movw %di, %es
    xor %di, %di
    int $0x10
    cmpw $0x004F, %ax
    jne vbe_try_fail_pop

    /* 检查是否支持线性帧缓冲（模式属性位7） */
    pushw %ds
    movw $0x7000, %ax
    movw %ax, %ds
    movb 0x00, %al           /* 模式属性 */
    testb $0x80, %al
    jz vbe_try_fail_pop2

    /* 读取模式信息（先保存到临时寄存器，恢复DS后再写入变量） */
    movw 0x12, %ax           /* X分辨率 */
    movw %ax, %bx            /* 临时保存 */
    movw 0x14, %ax           /* Y分辨率 */
    movw %ax, %cx            /* 临时保存 */
    movb 0x19, %al           /* 每像素位数 */
    movb %al, %dl            /* 临时保存 */
    movw 0x10, %ax           /* pitch */
    movw %ax, %si            /* 临时保存 */
    movw 0x28, %ax           /* 帧缓冲低16位 */
    movw %ax, %di            /* 临时保存 */
    movw 0x2A, %ax           /* 帧缓冲高16位 */
    movw %ax, %bp            /* 临时保存 */
    popw %ds                 /* 恢复DS=0 */

    /* 现在DS=0，正确写入变量 */
    movw %bx, vbe_width
    movw %cx, vbe_height
    movb %dl, vbe_bpp
    movw %si, vbe_pitch
    movw %di, vbe_fb
    movw %bp, vbe_fb+2

    /* 设置VBE模式 */
    popw %cx                 /* 恢复模式号 */
    movw %cx, vbe_current_mode  /* 保存当前模式号 */
    movw $0x4F02, %ax
    movw %cx, %bx
    orw $0x4000, %bx         /* 使用线性帧缓冲 */
    int $0x10
    cmpw $0x004F, %ax
    jne vbe_try_fail

    ret

vbe_try_fail_pop2:
    popw %ds
vbe_try_fail_pop:
    popw %cx
vbe_try_fail:
    movw $0, vbe_width
    ret

vbe_set_ok:
    SERIAL_PUTS msg_vbe_ok
    jmp vbe_done

vbe_failed:
    SERIAL_PUTS msg_vbe_fail
    movw $0, vbe_width

vbe_done:
    jmp after_vbe_setup

enter_pm_64:
    /* 2. 加载GDT并切换到保护模式 */
    SERIAL_PUTS msg_pm
    lgdt gdt32_ptr

    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0

    ljmp $0x08, $protected_mode

read_error:
    SERIAL_PUTS msg_read_err
    movw $0xB800, %ax
    movw %ax, %es
    movw $160*2 + 4, %di
    movw $0x4F45, %ax
    stosw
    movw $0x4F52, %ax
    stosw
1:
    hlt
    jmp 1b

/* 串口初始化（115200 8N1） */
serial_init:
    movw $0x3F8, %dx
    addw $3, %dx          /* LCR */
    movb $0x80, %al       /* DLAB=1 */
    outb %al, %dx
    subw $3, %dx          /* 回到数据端口 */
    movb $0x01, %al       /* 除数低字节=1 -> 115200 */
    outb %al, %dx
    incw %dx
    movb $0x00, %al       /* 除数高字节=0 */
    outb %al, %dx
    addw $2, %dx          /* LCR */
    movb $0x03, %al       /* 8N1, DLAB=0 */
    outb %al, %dx
    ret

/* 串口输出一个字符（AL=字符） */

/* 32位hex输出 (al=值? 不 — eax=32位值, 8位hex) */
serial_puthex32:
    pushal
    movl $8, %ecx
1:
    roll $4, %eax
    movl %eax, %edx
    andl $0xF, %edx
    cmpb $10, %dl
    jae 2f
    addb $'0', %dl
    jmp 3f
2:
    subb $10, %dl
    addb $'A', %dl
3:
    movb %dl, %al
    call serial_putc
    loop 1b
    popal
    ret

serial_putc:
    pushw %ax
    pushw %dx
    movb %al, %ah         /* 保存字符到AH */
    movw $0x3F8+5, %dx    /* LSR */
1:
    inb %dx, %al
    testb $0x20, %al      /* THRE */
    jz 1b
    movb %ah, %al         /* 恢复字符 */
    movw $0x3F8, %dx
    outb %al, %dx
    popw %dx
    popw %ax
    ret

/* 串口输出字符串（DS:SI=字符串） */
serial_puts:
    pushw %ax
    pushw %si
1:
    lodsb
    testb %al, %al
    jz 2f
    call serial_putc
    jmp 1b
2:
    popw %si
    popw %ax
    ret

/* 输出AL中的8位十六进制值 */
serial_put_hex8:
    pushw %ax
    pushw %cx
    movb %al, %cl
    /* 高4位 */
    shrb $4, %al
    call hex_digit
    call serial_putc
    /* 低4位 */
    movb %cl, %al
    andb $0x0F, %al
    call hex_digit
    call serial_putc
    /* 换行 */
    movb $'\r', %al
    call serial_putc
    movb $'\n', %al
    call serial_putc
    popw %cx
    popw %ax
    ret

hex_digit:
    cmpb $10, %al
    jl 1f
    addb $'A'-10, %al
    ret
1:
    addb $'0', %al
    ret

/* ============================================================
 * cdrom_read_lba: 使用AH=02h从CD-ROM读取扇区(2048字节/扇区)
 * 输入: EBX=LBA, AL=扇区数(<=32), ES:BX=目标缓冲区
 * 输出: CF=0成功, CF=1失败
 * CHS参数: H=64, S=32 (isohybrid默认)
 * ============================================================ */
cdrom_read_lba:
    pushl %ebx
    pushl %ecx
    pushl %edx
    pushw %es
    pushw %di

    movb %al, %bh            /* BH=扇区数 */

    /* LBA转CHS: C=LBA>>11, H=(LBA>>5)&0x3F, S=(LBA&0x1F)+1 */
    movl %ebx, %eax
    movb %al, %cl            /* CL = LBA低8位 */
    andb $0x1F, %cl          /* CL = LBA & 0x1F */
    incb %cl                 /* CL = S = (LBA&0x1F)+1 */

    movl %ebx, %eax
    shrl $5, %eax
    andb $0x3F, %al          /* AL = H = (LBA>>5)&0x3F */
    movb %al, %dh            /* DH = H */

    movl %ebx, %eax
    shrl $11, %eax           /* AL = C = LBA>>11 */
    movb %al, %ch            /* CH = C(低8位) */
    /* C的高2位放到CL的位6-7 */
    movb %al, %ah
    shrb $2, %ah
    andb $0xC0, %ah
    orb %ah, %cl

    movb %bh, %al            /* AL=扇区数 */
    movb boot_drive, %dl
    movb $0x02, %ah
    int $0x13
    jc 1f
    popw %di
    popw %es
    popl %edx
    popl %ecx
    popl %ebx
    ret

1:  /* 读取失败，输出错误码 */
    pushw %ax
    SERIAL_PUTS msg_cdrom_err
    popw %ax
    call serial_put_hex8
    SERIAL_PUTS msg_crlf2
    stc  /* 保持CF=1表示失败 */
    popw %di
    popw %es
    popl %edx
    popl %ecx
    popl %ebx
    ret

/* ============================================================
 * show_boot_menu: 显示启动菜单，等待选择或倒计时
 * 返回: AL=1 (64bit), AL=2 (32bit)
 * V0.55 P1: 已有持久化默认选择时跳过菜单直接引导（ESC 进菜单）；
 *           CPU 不支持 64 位时 64-bit 选项灰显、按键无效、默认 32-bit
 * ============================================================ */
show_boot_menu:
    pushw %ax
    pushw %bx
    pushw %cx
    pushw %dx

    SERIAL_PUTS msg_menu_start

    /* 已有保存的默认选择 → 快速引导路径（2秒 ESC 窗口） */
    cmpb $0, saved_choice
    je full_menu
    /* V0.55: CPU 不支持 64 位时，保存的 64-bit 选择强制改 32-bit */
    cmpb $1, saved_choice
    jne sm_cap_ok
    cmpb $0, lm_capable
    jne sm_cap_ok
    movb $2, saved_choice
sm_cap_ok:
    SERIAL_PUTS msg_saved_boot
    movb $4, %bl               /* 4 x ~0.5s ≈ 2s */
quick_countdown:
    inb $0x64, %al
    testb $0x01, %al
    jz quick_no_key
    inb $0x60, %al
    cmpb $0x01, %al            /* ESC 扫描码 → 回完整菜单 */
    je full_menu
quick_no_key:
    movl $0x10000, %ecx
quick_delay:
    nop
    loop quick_delay
    decb %bl
    jnz quick_countdown
    /* 返回保存的默认选择 */
    popw %dx
    popw %cx
    popw %bx
    popw %ax
    movb saved_choice, %al
    ret

full_menu:
    /* 清屏 */
    movw $0x0600, %ax
    movb $0x07, %bh
    xorw %cx, %cx
    movw $0x184F, %dx
    int $0x10

    /* 设置光标到(0,0) */
    movw $0x0200, %ax
    xorw %bx, %bx
    xorw %dx, %dx
    int $0x10

    /* 显示标题 */
    movw $menu_title, %si
    call vga_puts

    /* 64-bit 选项：记录起始行，CPU 不支持时该行灰显 */
    movb $0x03, %ah
    xorw %bx, %bx
    int $0x10                  /* DH=当前行 */
    movb %dh, %ch              /* CH=64-bit 选项起始行 */
    movw $menu_64, %si
    call vga_puts
    cmpb $0, lm_capable
    jne menu_show_32
    call gray_vga_rows         /* 灰显 CH 与 CH+1 两行 */
    movw $menu_64_na, %si
    call vga_puts
menu_show_32:
    movw $menu_32, %si
    call vga_puts
    movw $menu_hint, %si
    call vga_puts

    SERIAL_PUTS msg_menu_wait

    /* 倒计时5秒，支持键盘选择 */
    movb $5, %bl
menu_countdown:
    /* 显示倒计时数字（保存bl到栈） */
    pushw %bx
    movb %bl, %al
    addb $'0', %al
    movb $0x0E, %ah
    xorw %bx, %bx
    int $0x10
    movb $' ', %al
    int $0x10
    popw %bx

    /* 检查键盘输入（直接读PS/2控制器） */
    inb $0x64, %al          /* 读状态寄存器 */
    testb $0x01, %al        /* bit0=输出缓冲区满？ */
    jz no_key_pressed
    inb $0x60, %al          /* 读扫描码 */
    /* 转换扫描码到ASCII（简化版，只处理1和2） */
    cmpb $0x02, %al         /* 扫描码2 = '1'键按下 */
    je select_64bit
    cmpb $0x03, %al         /* 扫描码3 = '2'键按下 */
    je select_32bit
no_key_pressed:

    /* 延时（QEMU TCG下约0.5秒） */
    movl $0x10000, %ecx
menu_delay_loop:
    nop
    loop menu_delay_loop

    decb %bl
    jnz menu_countdown

    /* 超时默认：CPU 支持长模式→64位，否则→32位 */
    cmpb $0, lm_capable
    je timeout_to_32
    jmp select_64bit
timeout_to_32:
    jmp select_32bit

select_64bit:
    SERIAL_PUTS msg_menu_64
    popw %dx
    popw %cx
    popw %bx
    popw %ax
    movb $1, %al
    call save_boot_config      /* V0.55 P1: 持久化默认选择 */
    ret

select_32bit:
    SERIAL_PUTS msg_menu_32
    popw %dx
    popw %cx
    popw %bx
    popw %ax
    movb $2, %al
    call save_boot_config
    ret

/* ============================================================
 * V0.55 P1: CPU 长模式能力检测 + 启动配置持久化（LBA33）
 * 配置扇区布局: [0..3]='HPCF' 魔数, [4]=选择(1/2), [5..511]=0
 * 缓冲区用低内存 0x7900（MBR 0x7C00 之下, DAP 0x7B00 之下, 互不冲突）
 * ============================================================ */

/* detect_lm_cap: CPUID 0x80000001 EDX bit29 → lm_capable */
detect_lm_cap:
    pushfl
    popl %eax
    movl %eax, %ecx
    xorl $0x200000, %eax       /* 翻转 EFLAGS.ID(bit21) 探测 CPUID */
    pushl %eax
    popfl
    pushfl
    popl %edx
    cmpl %ecx, %edx            /* 与原始 EFLAGS 比：bit21 能置位→有 CPUID */
    je dlm_done                /* 没变 → 无 CPUID → 不支持 */
    pushl %ecx
    popfl                      /* 恢复原 EFLAGS */
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb dlm_done
    movl $0x80000001, %eax
    cpuid
    testl $(1 << 29), %edx
    jz dlm_done
    movb $1, lm_capable
    SERIAL_PUTS msg_dbg_lm_yes
    ret
dlm_done:
    SERIAL_PUTS msg_dbg_lm_no
    ret

/* load_boot_config: 读 LBA33 → 校验魔数 → saved_choice（失败静默） */
load_boot_config:
    pushw %si
    pushw %ax
    movw $0x7B00, %si          /* EDD 读包 */
    movb $0x10, %ds:(%si)
    movb $0x00, %ds:1(%si)
    movw $1, %ds:2(%si)        /* 1 扇区 */
    movw $0x7900, %ds:4(%si)   /* 缓冲 0x7900 */
    movw $0, %ds:6(%si)
    movl $33, %ds:8(%si)       /* LBA 33 */
    movl $0, %ds:12(%si)
    movb boot_drive, %dl
    movb $0x42, %ah
    int $0x13
    jc lbc_fail                /* 读失败（如 CD-ROM）→ 无保存 */
    cmpl $0x46435048, %ds:0x7900   /* 'HPCF' */
    jne lbc_fail
    movb %ds:0x7904, %al
    cmpb $1, %al
    je lbc_ok
    cmpb $2, %al
    jne lbc_fail
lbc_ok:
    movb %al, saved_choice
    SERIAL_PUTS msg_dbg_cfg_found
    popw %ax
    popw %si
    ret
lbc_fail:
    SERIAL_PUTS msg_dbg_cfg_none
lbc_done:
    popw %ax
    popw %si
    ret

/* save_boot_config: AL=选择(1/2) → 写 LBA33（CD-ROM/写失败静默忽略） */
save_boot_config:
    pushw %si
    pushw %ax
    pushw %cx
    pushw %di
    movb %al, %cl              /* CL=choice */
    movb boot_drive, %al
    cmpb $0xE0, %al
    jae sbc_done               /* CD-ROM 不写 */
    /* 填充配置扇区 @0x7900（stosl 写 ES:DI，需临时置 ES=0） */
    pushw %es
    xorw %ax, %ax
    movw %ax, %es
    movw $0x7900, %di
    movl $0x46435048, %eax     /* 'HPCF' */
    stosl
    movb %cl, %al
    stosb
    movb $0, %al
    movw $507, %cx
sbc_fill:
    stosb
    loop sbc_fill
    popw %es
    /* EDD 写包 @0x7B00 */
    movw $0x7B00, %si
    movb $0x10, %ds:(%si)
    movb $0x00, %ds:1(%si)
    movw $1, %ds:2(%si)
    movw $0x7900, %ds:4(%si)
    movw $0, %ds:6(%si)
    movl $33, %ds:8(%si)
    movl $0, %ds:12(%si)
    movb boot_drive, %dl
    movb $0x43, %ah            /* EDD 写扇区 */
    int $0x13
    jnc sbc_wr_ok
    SERIAL_PUTS msg_dbg_cfg_wrfail
    jmp sbc_done
sbc_wr_ok:
    SERIAL_PUTS msg_dbg_cfg_wr
sbc_done:
    popw %di
    popw %cx
    popw %ax
    popw %si
    ret

/* gray_vga_rows: VGA 文本模式灰显 CH 行与 CH+1 行（属性 0x08 暗灰） */
gray_vga_rows:
    pushw %es
    pushw %ax
    pushw %cx
    pushw %di
    movw $0xB800, %ax
    movw %ax, %es
    movb %ch, %al
    movb $160, %dl
    mulb %dl                   /* AX = CH*160（行内字节偏移） */
    movw %ax, %di
    movb $0x08, %ah
    movb $80, %cl
gvr_row1:
    movb %ah, %es:1(%di)
    addw $2, %di
    decb %cl
    jnz gvr_row1
    movb $80, %cl              /* DI 已自然对齐到 CH+1 行首 */
gvr_row2:
    movb %ah, %es:1(%di)
    addw $2, %di
    decb %cl
    jnz gvr_row2
    popw %di
    popw %cx
    popw %ax
    popw %es
    ret

/* VGA文本输出（实模式，DS:SI=字符串） */
vga_puts:
    pushw %ax
    pushw %bx
1:
    lodsb
    testb %al, %al
    jz 2f
    movb $0x0E, %ah
    movw $0x0007, %bx
    int $0x10
    jmp 1b
2:
    popw %bx
    popw %ax
    ret

.code32
protected_mode:
    /* 设置数据段 */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $0x90000, %esp

    /* 串口输出：进入保护模式 */
    movl $msg_pm_ok, %esi
    call serial_puts32

    /* VGA输出 "PM" */
    movl $0xB8000 + 160*3, %edi
    movb $'P', %al
    movb $0x1F, %ah
    stosw
    movb $'M', %al
    stosw

    jmp kernel_already_high
    /* 把内核从0x10000、0x20000、0x30000、0x40000和0x50000复制到0x100000（共320KB） */
    movl $0x10000, %esi
    movl $0x100000, %edi
    movl $16384, %ecx        /* 前64KB = 16384双字 */
    rep movsl

    movl $0x20000, %esi      /* 第二个64KB在0x20000 */
    movl $16384, %ecx
    rep movsl

    movl $0x30000, %esi      /* 第三个64KB在0x30000 */
    movl $16384, %ecx
    rep movsl

    movl $0x40000, %esi      /* 第四个64KB在0x40000 */
    movl $16384, %ecx
    rep movsl

    movl $0x50000, %esi      /* 第五个64KB在0x50000 */
    movl $16384, %ecx
    rep movsl

    movl $0x60000, %esi      /* 第六个64KB在0x60000 */
    movl $16384, %ecx
    rep movsl

    movl $0x70000, %esi      /* 第七个64KB在0x70000 */
    movl $16384, %ecx
    rep movsl

    movl $0x80000, %esi      /* 第八个64KB在0x80000 */
    movl $16384, %ecx
    rep movsl

    movl $0x90000, %esi      /* 第九个64KB在0x90000 */
    movl $16384, %ecx
    rep movsl

    movl $0xA0000, %esi      /* 第十个64KB在0xA0000 */
    movl $16384, %ecx
    rep movsl

kernel_already_high:
    /* 串口输出：内核复制完成 */
    movl $msg_copy_ok, %esi
    call serial_puts32

    /* 3. 检测CPU是否支持长模式 */
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb no_long_mode

    movl $0x80000001, %eax
    cpuid
    testl $(1 << 29), %edx
    jz no_long_mode

    movl $msg_lm_support, %esi
    call serial_puts32

    /* 4. 构建4级页表
     * V0.54.6: 从 0xC00000 移到 0x2E00000
     *   - 之前 BSS 小(10MB+)时 0xC00000 安全
     *   - kheap 扩到 32MB 后 BSS 结束于 ~44MB，覆盖了 0xC00000 区域
     *   - 新位置 0x2E00000 (46MB) 在 24×2MB=48MB 映射内，又高于 BSS 上限 */
    .equ PT_BASE, 0x2E00000
    movl $PT_BASE, %edi
    movl $0, %eax
    movl $8192, %ecx          /* 清零32KB (8页): PML4 + PDPT低 + PD低 + PDPT高 + PD高 + PD_FB */
    rep stosl

    /* PML4[0] -> PDPT (0x2E1000) */
    movl $(PT_BASE + 0x1000), %eax
    orl $0x03, %eax
    movl %eax, PT_BASE

    /* PDPT[0] -> PD (0x2E2000) */
    movl $(PT_BASE + 0x2000), %eax
    orl $0x03, %eax
    movl %eax, PT_BASE + 0x1000

    /* PD: 使用2MB大页映射前48MB（低地址 0x00000000-0x03000000）
     * V0.54.6: 从8×2MB扩到24×2MB，支持kheap从8MB→32MB (BSS约44MB) */
    movl $(PT_BASE + 0x2000), %edi
    movl $0x00000083, %eax   /* PS=1(2MB大页), R/W=1, P=1 */
    movl $24, %ecx           /* 24 × 2MB = 48MB */
1:
    movl %eax, (%edi)
    addl $0x200000, %eax     /* 下一个2MB页 */
    addl $8, %edi
    loop 1b

    /* 高地址映射：0xFFFFFF0000000000 -> 0x00000000（内核高半核） */
    /* PML4[511] -> PDPT_HI (0x2E4000) */
    movl $(PT_BASE + 0x4000), %eax
    orl $0x03, %eax
    movl %eax, PT_BASE + 511*8

    /* PDPT_HI[510] -> PD_HI (0x2E5000) */
    movl $(PT_BASE + 0x5000), %eax
    orl $0x03, %eax
    movl %eax, PT_BASE + 0x4000 + 510*8

    /* PD_HI: 使用2MB大页映射前48MB到高地址（内核高半核） */
    movl $(PT_BASE + 0x5000), %edi
    movl $0x00000083, %eax   /* PS=1(2MB大页), R/W=1, P=1 */
    movl $24, %ecx           /* 24 × 2MB = 48MB，与低地址PD对称 */
2:
    movl %eax, (%edi)
    addl $0x200000, %eax
    addl $8, %edi
    loop 2b

    /* 帧缓冲映射：如果VBE模式设置成功，映射帧缓冲到页表 */
    cmpw $0, vbe_width
    je no_fb_map
    /* 帧缓冲地址0xFD000000: PML4索引=0, PDPT索引=3, PD索引=488 */
    /* 使用现有的PML4[0]->PDPT(0x2E1000)，添加PDPT[3]->PD_FB(0x2E7000) */
    movl $(PT_BASE + 0x7000 + 0x03), PT_BASE + 0x1000 + 3*8
    /* PD_FB[488] -> fb物理地址（2MB大页） */
    movl vbe_fb, %eax
    orl $0x00000083, %eax   /* PS=1, R/W=1, P=1 */
    movl %eax, PT_BASE + 0x7000 + 488*8
    /* PD_FB[489] -> fb+2MB */
    addl $0x200000, %eax
    movl %eax, PT_BASE + 0x7000 + 489*8
no_fb_map:

    movl $msg_paging, %esi
    call serial_puts32

    /* 5. 加载PML4到CR3 */
    movl $PT_BASE, %eax
    movl %eax, %cr3

    /* 6. 启用PAE */
    movl %cr4, %eax
    orl $(1 << 5), %eax
    movl %eax, %cr4

    /* 7. 启用长模式 */
    movl $0xC0000080, %ecx
    rdmsr
    orl $(1 << 8), %eax
    wrmsr

    /* 8. 启用分页 */
    movl %cr0, %eax
    orl $(1 << 31), %eax
    movl %eax, %cr0

    /* 9. 加载64位GDT并远跳转 */
    lgdt gdt64_ptr
    ljmp $0x08, $long_mode

no_long_mode:
    movl $msg_no_lm, %esi
    call serial_puts32
    movl $0xB8000 + 160*3, %edi
    movb $'N', %al
    movb $0x4F, %ah
    stosw
    movb $'O', %al
    stosw
1:
    hlt
    jmp 1b

/* 32位串口输出字符串 */
serial_puts32:
    pushl %eax
    pushl %edx
1:
    movb (%esi), %al
    testb %al, %al
    jz 2f
    /* 等待THRE */
    movw $0x3F8+5, %dx
3:
    inb %dx, %al
    testb $0x20, %al
    jz 3b
    /* 输出字符 */
    movb (%esi), %al
    movw $0x3F8, %dx
    outb %al, %dx
    incl %esi
    jmp 1b
2:
    popl %edx
    popl %eax
    ret

.code64
long_mode:
    cli                     /* V0.56: 进入内核前关中断 — IDT 仍是实模式 IVT,
                             * 外部中断会以非法门描述符触发 #GP → 三重故障 */
    /* 成功进入64位长模式 */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movq $0x70000, %rsp

    /* 直接输出"LM>"，不调用函数 */
    movb $'L', %al
    call outc64
    movb $'M', %al
    call outc64
    movb $'>', %al
    call outc64
    movb $'\r', %al
    call outc64
    movb $'\n', %al
    call outc64

    /* VGA输出 "LM" */
    movq $0xB8000 + 160*4, %rdi
    movb $'L', %al
    movb $0x2F, %ah
    stosw
    movb $'M', %al
    stosw

    /* 把VBE信息复制到固定地址0x6000（内核从这里读取）
     * 注: 此处用 EAX 没问题, 后面会在跳转前重新设 EAX=magic */
    movq $0x6000, %rdi
    movzwl vbe_width(%rip), %eax
    movw %ax, (%rdi)
    movzwl vbe_height(%rip), %eax
    movw %ax, 2(%rdi)
    movzwl vbe_pitch(%rip), %eax
    movw %ax, 4(%rdi)
    movzbl vbe_bpp(%rip), %eax
    movb %al, 6(%rdi)
    movl vbe_fb(%rip), %eax
    movl %eax, 8(%rdi)
    movl $0, 12(%rdi)          /* fb 高32位: VBE 帧缓冲始终 <4GB */

    /* 输出"JMP>"然后跳转 */
    movb $'J', %al
    call outc64
    movb $'M', %al
    call outc64
    movb $'P', %al
    call outc64
    movb $'>', %al
    call outc64
    movb $'\r', %al
    call outc64
    movb $'\n', %al
    call outc64

    /* 验证0x100000地址的内容（应该是0xb0 0x4b = mov $0x4b,%al） */
    movabsq $0x100000, %rbx
    movb (%rbx), %al
    call outc64
    movb $'-', %al
    call outc64
    movb 1(%rbx), %al
    call outc64
    movb $'\r', %al
    call outc64
    movb $'\n', %al
    call outc64

    /* V0.54.6: 设置 EAX=multiboot magic, EBX=0 (VBE信息在 0x6000)
     * 注：内核 entry_64.S 已硬编码 magic 值，这里设不设都可以 */
    movl $0x2BADB002, %eax
    movl $0, %ebx
    movabsq $0x100000, %rcx
    jmp *%rcx

/* 简单的64位串口输出（AL=字符） */
outc64:
    pushq %rax
    pushq %rdx
    movb %al, %ah         /* 保存字符到AH */
1:
    movw $0x3F8+5, %dx
    inb %dx, %al
    testb $0x20, %al
    jz 1b
    movb %ah, %al         /* 恢复字符 */
    movw $0x3F8, %dx
    outb %al, %dx
    popq %rdx
    popq %rax
    ret

1:
    hlt
    jmp 1b

/* 64位串口输出字符串 */
serial_puts64:
    pushq %rax
    pushq %rdx
1:
    movb (%rsi), %al
    testb %al, %al
    jz 2f
    movw $0x3F8+5, %dx
3:
    inb %dx, %al
    testb $0x20, %al
    jz 3b
    movb (%rsi), %al
    movw $0x3F8, %dx
    outb %al, %dx
    incq %rsi
    jmp 1b
2:
    popq %rdx
    popq %rax
    ret

/* 64位串口输出一个字符（AL=字符） */
serial_putc64:
    pushq %rax
    pushq %rdx
    movb %al, %ah
1:
    movw $0x3F8+5, %dx
    inb %dx, %al
    testb $0x20, %al
    jz 1b
    movb %ah, %al
    movw $0x3F8, %dx
    outb %al, %dx
    popq %rdx
    popq %rax
    ret

/* 字符串常量 */
msg_start:      .asciz "[Stage2_64] Starting...\r\n"
msg_load_kernel:.asciz "[Stage2_64] Loading kernel...\r\n"
msg_load_ok:    .asciz "[Stage2_64] Kernel loaded OK\r\n"
msg_copy_ok:    .asciz "[Stage2_64] Kernel copied to 0x100000\r\n"
msg_read_err:   .asciz "[Stage2_64] ERROR: Disk read failed!\r\n"
msg_cdrom_err:  .asciz "[Stage2_64] CD-ROM AH=02h error code: 0x"
msg_crlf2:      .asciz "\r\n"
msg_pm:         .asciz "[Stage2_64] Entering protected mode...\r\n"
msg_pm_ok:      .asciz "[Stage2_64] Protected mode OK\r\n"
msg_lm_support: .asciz "[Stage2_64] CPU supports long mode\r\n"
msg_no_lm:      .asciz "[Stage2_64] ERROR: CPU does NOT support long mode!\r\n"
msg_paging:     .asciz "[Stage2_64] Page tables built, enabling paging...\r\n"
msg_lm_ok:      .asciz "[Stage2_64] LONG MODE OK!\r\n"
msg_jump:       .asciz "[Stage2_64] Jumping to kernel @ 0x103f40...\r\n"
msg_32bit_unsupported: .asciz "[Stage2_64] 32-bit kernel not supported yet, using 64-bit\r\n"
msg_load_32bit: .asciz "[Stage2_64] Loading 32-bit kernel from LBA 500...\r\n"
msg_unreal_ok:  .asciz "[Stage2_64] Unreal mode enabled, accessing 4GB memory\r\n"
msg_debug_read: .asciz "[Stage2_64] DEBUG: About to read disk...\r\n"
msg_debug_after_read: .asciz "[Stage2_64] DEBUG: Disk read returned\r\n"
msg_debug_copy: .asciz "[Stage2_64] DEBUG: Copying to high memory...\r\n"
msg_debug_copy_done: .asciz "[Stage2_64] DEBUG: Copy done, next loop\r\n"
msg_read_fail:  .asciz "[Stage2_64] ERROR: Disk read failed, fallback to 64-bit\r\n"
msg_32bit_loaded: .asciz "[Stage2_64] 32-bit kernel loaded OK\r\n"
msg_no_multiboot: .asciz "[Stage2_64] ERROR: No multiboot header found, fallback to 64-bit\r\n"
msg_multiboot_found: .asciz "[Stage2_64] Multiboot header found, entering 32-bit mode...\r\n"
msg_jump_32bit: .asciz "[Stage2_64] Jumping to 32-bit kernel...\r\n"

/* 启动菜单字符串 */
menu_title: .asciz "========================================\r\n  HepuBoot v0.10.73 Hybrid\r\n========================================\r\n\r\n"
menu_64:    .asciz "  1. HepuOS 64-bit (default)\r\n     New arch | Large memory | TrueType\r\n\r\n"
menu_64_na: .asciz "     [CPU does not support 64-bit]\r\n\r\n"
menu_32:    .asciz "  2. HepuOS 32-bit (stable)\r\n     Mature | Full desktop | 36 apps\r\n\r\n"
menu_hint:  .asciz "  [1/2] Select | [Enter] Boot | Auto: "
msg_menu_start: .asciz "[Stage2_64] Boot menu starting...\r\n"
msg_menu_wait:  .asciz "[Stage2_64] Waiting for selection...\r\n"
msg_menu_key:   .asciz "[Stage2_64] Key pressed\r\n"
msg_menu_64:    .asciz "[Stage2_64] Selected: 64-bit\r\n"
msg_menu_32:    .asciz "[Stage2_64] Selected: 32-bit\r\n"
msg_saved_boot: .asciz "[Stage2_64] Saved boot choice found (ESC=menu, 2s)\r\n"
msg_dbg_lm_yes:  .asciz "[Stage2_64] DBG: LM capable=YES\r\n"
msg_dbg_lm_no:   .asciz "[Stage2_64] DBG: LM capable=NO\r\n"
msg_dbg_cfg_found: .asciz "[Stage2_64] DBG: cfg sector valid\r\n"
msg_dbg_cfg_none:  .asciz "[Stage2_64] DBG: cfg sector absent\r\n"
msg_dbg_cfg_wr:    .asciz "[Stage2_64] DBG: cfg sector written\r\n"
msg_dbg_cfg_wrfail:.asciz "[Stage2_64] DBG: cfg sector WRITE FAILED\r\n"

/* 变量 */
boot_drive: .byte 0x80
boot_choice: .byte 1
/* V0.55 P1: 启动配置持久化 + CPU 能力标记 */
lm_capable: .byte 0        /* 1=CPU 支持长模式 */
saved_choice: .byte 0      /* 非零=LBA33 已保存的默认选择(1/2) */
msg_vbe_start:  .asciz "[Stage2_64] Setting VBE mode 1024x768x32...\r\n"
msg_vbe_exists: .asciz "[Stage2_64] VBE controller detected\r\n"
msg_vbe_ok:     .asciz "[Stage2_64] VBE mode set OK\r\n"
msg_debug_vbe_return: .asciz "[Stage2_64] DEBUG: VBE setup returned\r\n"
msg_vbe_fail:   .asciz "[Stage2_64] VBE mode set FAILED, using text mode\r\n"

/* VBE模式信息（传递给内核） */
.align 4
.global vbe_width
vbe_width:      .word 0
.global vbe_height
vbe_height:     .word 0
.global vbe_pitch
vbe_pitch:      .word 0
.global vbe_bpp
vbe_bpp:        .byte 0
.global vbe_fb
vbe_fb:         .long 0
.global vbe_current_mode
vbe_current_mode: .word 0

/* 32位保护模式入口（multiboot启动） */
.code32
protected_mode_32:
    /* 设置数据段 */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $0x90000, %esp

    /* 设置multiboot信息结构 */
    movl $0x2BADB002, %eax    /* multiboot magic */
    movl $multiboot_info, %ebx /* multiboot info指针 */

    /* 设置VBE/framebuffer信息（如果VBE模式设置成功） */
    cmpw $0, vbe_width
    je no_vbe_info
    /* 设置VBE信息（bit 11） */
    movl $0x4030, multiboot_info + 72   /* vbe_control_info */
    movl $0x4130, multiboot_info + 76   /* vbe_mode_info */
    movw vbe_current_mode, %ax
    movw %ax, multiboot_info + 80       /* vbe_mode */
    /* 设置framebuffer信息（bit 12） */
    movl vbe_fb, %eax
    movl %eax, multiboot_info + 88      /* framebuffer_addr低32位 */
    movl $0, multiboot_info + 92        /* framebuffer_addr高32位 */
    movw vbe_pitch, %ax
    movl %eax, multiboot_info + 96      /* framebuffer_pitch */
    movw vbe_width, %ax
    movl %eax, multiboot_info + 100     /* framebuffer_width */
    movw vbe_height, %ax
    movl %eax, multiboot_info + 104     /* framebuffer_height */
    movb vbe_bpp, %al
    movb %al, multiboot_info + 108      /* framebuffer_bpp */
    movb $1, multiboot_info + 109       /* framebuffer_type=1 (RGB) */
    /* 设置flags: bit0(内存) + bit11(VBE) + bit12(framebuffer) */
    movl multiboot_info, %eax
    orl $0x1801, %eax                   /* bit0 + bit11 + bit12 */
    movl %eax, multiboot_info
    /* 设置内存信息 */
    movl $640, multiboot_info + 4       /* mem_lower */
    movl $256 * 1024, multiboot_info + 8 /* mem_upper = 256MB */
no_vbe_info:

    /* 从multiboot头获取入口地址 */
    movl multiboot_header_ptr, %esi
    movl 4(%esi), %eax       /* flags */
    testl $0x10000, %eax     /* 检查bit16（地址信息） */
    jz use_default_entry
    movl 28(%esi), %edi      /* entry_addr */
    testl %edi, %edi
    jnz jump_32bit_kernel
use_default_entry:
    movl $0x101000, %edi     /* 默认入口点（.text段，4K对齐） */
jump_32bit_kernel:
    SERIAL_PUTS32 msg_jump_32bit
    jmp *%edi                 /* 跳转到32位内核 */

/* multiboot信息结构（简化版） */
.align 4
multiboot_info:
    .long 0                    /* flags */
    .long 0                    /* mem_lower */
    .long 0                    /* mem_upper */
    .long 0                    /* boot_device */
    .long 0                    /* cmdline */
    .long 0                    /* mods_count */
    .long 0                    /* mods_addr */
    .long 0, 0, 0, 0           /* syms */
    .long 0                    /* mmap_length */
    .long 0                    /* mmap_addr */
    .long 0                    /* drives_length */
    .long 0                    /* drives_addr */
    .long 0                    /* config_table */
    .long 0                    /* boot_loader_name */
    .long 0                    /* apm_table */
    .long 0                    /* vbe_control_info */
    .long 0                    /* vbe_mode_info */
    .word 0                    /* vbe_mode */
    .word 0                    /* vbe_interface_seg */
    .word 0                    /* vbe_interface_off */
    .word 0                    /* vbe_interface_len */
    .long 0, 0                 /* framebuffer_addr (64位) */
    .long 0                    /* framebuffer_pitch */
    .long 0                    /* framebuffer_width */
    .long 0                    /* framebuffer_height */
    .byte 0                    /* framebuffer_bpp */
    .byte 1                    /* framebuffer_type (1=RGB) */
    .word 0                    /* 保留 */

multiboot_header_ptr: .long 0

/* 32位GDT */
.align 16
gdt32:
    .quad 0
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt32_ptr:
    .word gdt32_ptr - gdt32 - 1
    .long gdt32

/* 64位GDT */
.align 16
gdt64:
    .quad 0
    .quad 0x00AF9A000000FFFF
    .quad 0x00AF92000000FFFF
gdt64_ptr:
    .word gdt64_ptr - gdt64 - 1
    .long gdt64
    .long 0

/* 填充到16KB */
.org 32768
