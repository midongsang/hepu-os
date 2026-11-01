/* ============================================================================
 * 赫普OS - 内核启动入口 (GAS / AT&T 语法)
 * 基于 Multiboot 规范，可被 GRUB 等引导器加载
 * ========================================================================== */

.set MBOOT_MAGIC,      0x1BADB002
.set MBOOT_PAGE_ALIGN, 1<<0
.set MBOOT_MEM_INFO,   1<<1
.set MBOOT_VIDEO_MODE, 1<<2    /* V6.50: 请求bootloader设置视频模式 */
.set MBOOT_FLAGS,      MBOOT_PAGE_ALIGN | MBOOT_MEM_INFO | MBOOT_VIDEO_MODE
.set MBOOT_CHECKSUM,   -(MBOOT_MAGIC + MBOOT_FLAGS)

/* 内核只读版权指纹常量 */
.set HEPU_FP_MAGIC,    0x48455055   /* "HEPU" */
.set HEPU_FP_VERSION,  0x00010003   /* V1.3 */

/* ----------------------------------------------------------------------------
 * Multiboot 头（必须在文件最前面）
 * -------------------------------------------------------------------------- */
.section .multiboot, "a"
.align 4
    .long MBOOT_MAGIC
    .long MBOOT_FLAGS
    .long MBOOT_CHECKSUM
    /* V6.50: 视频模式请求 (flags bit2 要求的4个字段) */
    .long 0          /* mode_type: 0=线性图形模式 */
    .long 640        /* width */
    .long 480        /* height */
    .long 32         /* depth (bpp) */

/* ----------------------------------------------------------------------------
 * 内核栈
 * V3.83: 旧 .bss 内栈(0x1A8F68)与 QEMU multiboot cmdline(0x1A8F50)重叠,
 * 绝对路径 cmdline 覆盖栈顶→返回地址被覆盖→早期跳飞.
 * 实际栈改到 linker.ld .stack 段(独立 16KB, 位于 .bss 之后).
 * 下面保留占位(不占多余空间, .skip 由 .stack 段承担职责).
 * -------------------------------------------------------------------------- */
.section .bss
.align 16
stack_bottom_legacy:
    .skip 0
stack_top_legacy:
.extern __stack_top

/* ----------------------------------------------------------------------------
 * 内核只读版权指纹 (HEPU Kernel Fingerprint)
 * 此结构位于 .data 段，审计工具通过魔数扫描定位
 * -------------------------------------------------------------------------- */
.section .data
.global hepu_fingerprint
hepu_fingerprint:
    .long HEPU_FP_MAGIC          /* magic */
    .long HEPU_FP_VERSION        /* fp_version */
    .long 0x00000001             /* kernel_version V0.0.1 */
    .long 0x00000001             /* flags: bit0=GPLv3 */
    .long 0x20260821             /* build_date */
    .asciz "HEPU Hyper Evolve OS"
    .asciz "GPLv3 + HEPU Additional Terms"
    .asciz "https://github.com/hepu-os/hepu-os"
    .fill 64, 1, 0               /* padding */

#ifdef HEPU_BOOT_DEBUG
/* 内核启动日志字符串(V2.05): KLOG宏显示到VGA指定行, 屏幕日志主通道 */
.section .rodata
.global klog0
klog0: .asciz "K0 ENTRY OK"
.global klog1
klog1: .asciz "K1 ESP SET"
.global klog2
klog2: .asciz "K2 BSS CLR"
.global klog3
klog3: .asciz "K3 STACK OK"
.global klog4
klog4: .asciz "K4 MAIN CALL"
#endif

/* ----------------------------------------------------------------------------
 * 入口点
 * -------------------------------------------------------------------------- */
#ifdef HEPU_BOOT_DEBUG
/* 内核启动诊断: 写 "K<num>" 到 0xB8000 的 行<row>
 * 用 flat 段(0x10) 直写VGA, 不依赖vga_init(还没初始化).
 * 破坏 %ax/%cx/%di(pushal保护). V2.05: KDIAG升级为KLOG显示完整日志.
 * 与KBEEP互补: 有屏幕看日志(主通道, 无蜂鸣器设备也能定位). */
.macro KLOG row, msg
    pushal
    movw $0x10, %ax
    movw %ax, %es
    movl $0xB8000, %edi
    movl $\row, %ecx
    imull $160, %ecx, %ecx
    addl %ecx, %edi
    movl $\msg, %esi
1:  lodsb
    testb %al, %al
    jz 2f
    movb %al, %es:(%edi)
    movb $0x0F, %es:1(%edi)
    addl $2, %edi
    jmp 1b
2:  popal
.endm
/* 内核蜂鸣诊断: V2.17已禁用(用户要求静音启动), 保留KLOG屏幕日志作主诊断通道.
   beep次数含义(历史): 引导器P0=1,P1=2,P2=3; 内核K0=2,K1=4,K2=5,K3=6,K4=7 */
.macro KBEEP
.endm
#endif
/* ----------------------------------------------------------------------------
 * V2.10 早期IDT (HEPU_BOOT_DEBUG): 覆盖 K0→idt_init 之间.
 * 真机黑屏回BIOS的本质是"三重故障"(异常→IDT未加载→#GP→双故障→复位).
 * 此宏在K0后安装256项最小IDT(全部指向early_isr), 期间任何异常
 * 显示 "EARLY EXC eip=xxxxxxxx" 并停机, 而非静默复位.
 * IDT放0x6000(引导器GDT 0x8E00之下、BSS 0x16C000+之上, 无冲突).
 * 内核后续idt_init()会重新安装正式IDT, 早期IDT仅作兜底诊断. V2.10新增.
 * -------------------------------------------------------------------------- */
#ifdef HEPU_BOOT_DEBUG
.macro SETUP_EARLY_IDT
    pushal
    movl $0x6000, %edi          /* IDT基址 */
    movl $early_isr, %esi
    movl $256, %ecx
1:
    movw %si, 0(%edi)           /* offset_low */
    movw $0x08, 2(%edi)         /* selector = GDT_KERNEL_CODE */
    movb $0x00, 4(%edi)
    movb $0x8E, 5(%edi)         /* 32位中断门, present, DPL0 */
    movl %esi, %eax
    shrl $16, %eax
    movw %ax, 6(%edi)           /* offset_high */
    addl $8, %edi
    loop 1b
    movw $0x7FF, 0x6100         /* limit = 2047 (256*8-1) */
    movl $0x6000, 0x6102        /* base */
    lidtl 0x6100
    popal
.endm
#endif
.section .text
.global _start
_start:
    /* V6.60: 在最开头保存multiboot参数(eax=magic, ebx=info_ptr)到固定地址,
     * 因为后续K0调试/串口初始化/BSS清零都会破坏eax/ebx.
     * 0x8000是低内存安全区(引导器MBINFO在0x90000, 不会冲突). */
    movl %eax, 0x8000
    movl %ebx, 0x8004
#ifdef HEPU_BOOT_DEBUG
    /* K0: 内核入口(引导器跳转成功). V2.05改2声:
       真机用户报"三声"无法区分是引导器P2(3声)还是K0(3声).
       改为K0=2声(与P2的3声、K1的4声都不同), 用户一听便知K0是否执行:
         - 3声(P2)后无声音 → K0未执行(跳转/执行侧问题)
         - 3声(P2)后2声(K0)  → K0已执行(崩点推后到movl esp/K1)
       V2.06: 增加KLOG屏幕日志(主通道), 蜂鸣兜底
       V2.10: K0后安装早期IDT, 早期异常可见而非三重故障复位 */
    KBEEP
    KBEEP
    KLOG 0, klog0
    SETUP_EARLY_IDT
    /* V3.82 K0探针: 搬移后(内核入口)读表区0x1A08A0, 串口打印hex.
       16550 COM1初始化+打印, 若串口显示 0010E295=搬移正确; 若000003C4=搬移/读盘错. */
    /* 16550 COM1 初始化 (115200 8N1): LCR(0x3FB)DLAB=1, 除数12, 8N1, FIFO */
    movl $0x3FB, %edx
    movb $0x80, %al
    outb %al, %dx               /* LCR DLAB=1 */
    movl $0x3F8, %edx
    movb $0x0C, %al
    outb %al, %dx               /* 除数低字节 12 → 115200 */
    movl $0x3F9, %edx
    movb $0x00, %al
    outb %al, %dx               /* 除数高字节 0 */
    movl $0x3FB, %edx
    movb $0x03, %al
    outb %al, %dx               /* 8N1, DLAB=0 */
    movl $0x3FA, %edx
    movb $0xC7, %al
    outb %al, %dx               /* FIFO enable */
    /* 打印 "T0=" + 表区hex, "D0=" + .data开头(指纹)hex */
    movb $'T', %al
    call serial_putc_boot
    movb $'0', %al
    call serial_putc_boot
    movb $'=', %al
    call serial_putc_boot
    movl $0x1498A0, %esi
    movl (%esi), %eax
    movl $8, %ecx
30: movl %eax, %edx
    shrl $28, %edx
    andl $0x0F, %edx
    cmpb $10, %dl
    jb 31f
    addb $7, %dl
31: addb $'0', %dl
    movb %dl, %al
    call serial_putc_boot
    shll $4, %eax
    decl %ecx
    jnz 30b
    movb $' ', %al
    call serial_putc_boot
    movb $'D', %al
    call serial_putc_boot
    movb $'0', %al
    call serial_putc_boot
    movb $'=', %al
    call serial_putc_boot
    movl $0x147000, %esi
    movl (%esi), %eax
    movl $8, %ecx
40: movl %eax, %edx
    shrl $28, %edx
    andl $0x0F, %edx
    cmpb $10, %dl
    jb 41f
    addb $7, %dl
41: addb $'0', %dl
    movb %dl, %al
    call serial_putc_boot
    shll $4, %eax
    decl %ecx
    jnz 40b
    movb $0x0A, %al
    call serial_putc_boot
    jmp 32f
serial_putc_boot:
    pushl %eax              /* 保存字符(AL) */
    movl $0x3FD, %edx
35: inb %dx, %al            /* LSR=0x3FD */
    testb $0x20, %al        /* THR空? */
    jz 35b
    popl %eax               /* 恢复字符 */
    movl $0x3F8, %edx
    outb %al, %dx           /* 输出 */
    ret
32:
#endif
    /* 修复: 自研引导器只加载raw bin, 不负责清零BSS段.
     * BSS(含stack)未清零会让104MB运行时状态(调度器/网络栈缓冲等)
     * 读到垃圾初始值导致真机崩溃. 内核必须自行清零.
     * 注意: 临时栈用低内存0x7B00(引导器栈区, 与引导器STACK_ADDR一致),
     * 避免清零时破坏自身栈. V2.01: 原0x9000距引导器MBINFO(0x8F00)仅256B,
     * 若此处栈深>256B会覆盖Multiboot信息, 统一移到低地址. */
    movl $0x7B00, %esp
#ifdef HEPU_BOOT_DEBUG
    /* K1: 临时栈已设, BSS清零前 */
    KLOG 1, klog1
    KBEEP
    KBEEP
    KBEEP
    KBEEP
#endif
    /* 清零BSS: [__bss_start, __bss_end) */
    movl $__bss_start, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    shrl $2, %ecx           /* 除以4, dword数 */
    xorl %eax, %eax
    cld
    rep stosl
#ifdef HEPU_BOOT_DEBUG
    /* K2: BSS清零完成 */
    KLOG 2, klog2
    KBEEP
    KBEEP
    KBEEP
    KBEEP
    KBEEP
    /* V3.82 T0探针: BSS清零后读表区(0x1498A0), 8位hex显示到VGA行16 */
    movl $0xB8000, %edi
    addl $2560, %edi            /* 行16 */
    movl $0x1A08A0, %esi
    movl (%esi), %eax
    movl $8, %ecx
10: movl %eax, %edx
    shrl $28, %edx
    andl $0x0F, %edx
    cmpb $10, %dl
    jb 11f
    addb $7, %dl
11: addb $'0', %dl
    movb %dl, %es:(%edi)
    movb $0x0F, %es:1(%edi)
    addl $2, %edi
    shll $4, %eax
    decl %ecx
    jnz 10b
#endif
    /* 设置正式内核栈 (V3.83: .stack 段, 远离 QEMU cmdline 区) */
    movl $__stack_top, %esp
#ifdef HEPU_BOOT_DEBUG
    /* K3: 正式栈已设, 即将调用主函数 */
    KLOG 3, klog3
    KBEEP
    KBEEP
    KBEEP
    KBEEP
    KBEEP
    KBEEP
#endif
    /* 保存 multiboot 信息 (eax=magic, ebx=info_ptr)
     * V6.60: 从_start开头保存的固定地址0x8000恢复, 因为中间代码破坏了寄存器 */
    movl 0x8000, %eax
    movl 0x8004, %ebx
    push %ebx
    push %eax
    /* 调用内核主函数 */
    call hepu_kernel_main

    /* 主函数返回后停机 */
    cli
1:
    hlt
    jmp 1b

#ifdef HEPU_BOOT_DEBUG
/* ----------------------------------------------------------------------------
 * V2.10 early_isr: 早期IDT兜底入口. K0→idt_init之间异常时显示
 *   "EARLY EXC eip=xxxxxxxx" 然后 cli+hlt 死循环.
 * 中断帧经pushal后: 32(%esp)=原eip(无error-code), 36(%esp)=原eip(有error-code).
 * 这里取36(%esp)打印(有error-code覆盖, 无error-code时是CS/EFLAGS, 仍可区分).
 * V2.10新增.
 * -------------------------------------------------------------------------- */
.global early_isr
early_isr:
    pushal
    movw $0x10, %ax
    movw %ax, %es
    movl $0xB8000, %edi
    addl $2400, %edi            /* 行15 */
    movl $early_msg, %esi
1:  lodsb
    testb %al, %al
    jz 2f
    movb %al, %es:(%edi)
    movb $0x0F, %es:1(%edi)
    addl $2, %edi
    jmp 1b
2:  movl 36(%esp), %eax         /* 原eip(中断帧) */
    movl $8, %ecx               /* 8个十六进制位 */
3:  movl %eax, %edx
    shrl $28, %edx              /* 取最高4位 */
    andl $0x0F, %edx
    movl %edx, %ebx
    cmpb $10, %bl
    jb 4f
    addb $7, %bl
4:  addb $'0', %bl
    movb %bl, %es:(%edi)
    movb $0x0F, %es:1(%edi)
    addl $2, %edi
    shll $4, %eax               /* 下一组4位 */
    decl %ecx
    jnz 3b
    cli
5:  hlt
    jmp 5b

.section .rodata
.global early_msg
early_msg: .asciz "EARLY EXC eip="
#endif
