/* ============================================================================
 * 赫普OS - 64位汇编入口
 * V8.88: 初始实现
 * 长模式入口、系统调用入口、中断入口、上下文切换
 * ========================================================================== */

.section .multiboot, "a", @progbits
.code32

/* ============================================================================
 * Multiboot1 头（用于QEMU -kernel直接加载）
 * ========================================================================== */
.align 4
multiboot_header:
    /* magic */
    .long 0x1BADB002
    /* flags: 0 = 基本支持 */
    .long 0x00000000
    /* checksum */
    .long -(0x1BADB002 + 0x00000000) & 0xFFFFFFFF
multiboot_header_end:

.section .text
.code64

/* ============================================================================
 * 64位内核入口（从UEFI引导器或Multiboot跳转过来）
 * 约定：EAX = Multiboot magic (0x2BADB002 或 0x36D76289), EBX = Multiboot info指针
 * ============================================================================ */
.global x86_64_entry
x86_64_entry:
    /* 此时已经在长模式下，CS=0x08, EFER.LME=1, CR0.PG=1 */

    /* 保存Multiboot信息（在调用C函数前保存） */
    movl %eax, %esi          /* ESI = magic */
    movq %rbx, %rdx          /* RDX = mb_info */

    /* 设置段寄存器 */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /* 设置内核栈 */
    movq $kernel_stack_top, %rsp
    movq %rsp, %rbp

    /* 清空RFLAGS */
    pushq $0
    popfq

    /* 传递Multiboot信息给C函数：x86_64_set_boot_info(magic, mb_info) */
    movl %esi, %edi          /* 第一个参数：magic */
    /* RDX已经是第二个参数：mb_info */
    call x86_64_set_boot_info

    /* 调用C语言内核主函数 */
    call x86_64_kernel_main

    /* 如果内核返回，停机 */
1:
    hlt
    jmp 1b

/* ============================================================================
 * 系统调用入口（SYSCALL指令）
 * ============================================================================ */
.global syscall_entry_64
syscall_entry_64:
    /* 保存用户态上下文 */
    /* RCX = 返回地址, R11 = RFLAGS */
    swapgs

    /* 保存用户栈，切换到内核栈 */
    movq %rsp, %gs:0x08  /* 保存用户栈 */
    movq %gs:0x00, %rsp  /* 加载内核栈 */

    /* 保存寄存器 */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* 保存syscall返回上下文（用于fork）：
     * 此时栈上：[rsp]=r15, [rsp+8]=r14, ..., [rsp+96]=rcx(返回地址), [rsp+104]=r11(rflags)
     */
    movq 96(%rsp), %rax
    movq %rax, g_syscall_ret_rip
    movq 104(%rsp), %rax
    movq %rax, g_syscall_ret_rflags
    movq %rsp, g_syscall_kernel_rsp

    /* 转换系统调用约定：
     * 用户态: rax=nr, rdi=arg1, rsi=arg2, rdx=arg3, r10=arg4, r8=arg5
     * C函数:  rdi=nr, rsi=arg1, rdx=arg2, rcx=arg3, r8=arg4, r9=arg5
     */
    pushq %rax        /* 保存nr */
    movq %r8, %r9     /* arg5 */
    movq %r10, %r8    /* arg4 */
    movq %rdx, %rcx   /* arg3 */
    movq %rsi, %rdx   /* arg2 */
    movq %rdi, %rsi   /* arg1 */
    popq %rdi         /* nr */

    /* 调用系统调用处理函数 */
    call syscall_handler_64

    /* 恢复寄存器 */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    /* RAX保存返回值，不恢复 */
    addq $8, %rsp  /* 跳过保存的RAX */

    /* 恢复用户栈 */
    movq %gs:0x08, %rsp
    swapgs

    /* 返回用户态 */
    sysretq

/* ============================================================================
 * fork_trampoline: fork子进程的返回路径
 * 子进程从这里开始，恢复syscall返回上下文，rax=0，然后sysretq返回用户态
 * ============================================================================ */
.global fork_trampoline_64
fork_trampoline_64:
    /* 恢复用户栈 */
    movq %gs:0x08, %rsp
    swapgs

    /* 设置返回上下文 */
    movq g_syscall_ret_rip, %rcx
    movq g_syscall_ret_rflags, %r11
    xorq %rax, %rax  /* 子进程fork返回0 */

    /* 返回用户态 */
    sysretq

/* ============================================================================
 * 中断入口（通用）
 * ============================================================================ */
.macro IRQ_ENTRY num
.global irq_entry_\num
irq_entry_\num:
    /* 此时CPU已压入SS, RSP, RFLAGS, CS, RIP */
    /* 如果没有错误码，压入0 */
    pushq $0
    pushq $\num

    /* 保存通用寄存器 */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* 保存段寄存器 */
    movw %ds, %ax
    pushq %rax
    movw %es, %ax
    pushq %rax

    /* 设置内核段寄存器 */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es

    /* FPU状态保存/恢复在C函数irq_handler_64内部用per-CPU数组完成 */

    /* 调用中断处理函数，参数指向中断帧 */
    movq %rsp, %rdi
    call irq_handler_64

    /* 恢复段寄存器 */
    popq %rax
    movw %ax, %es
    popq %rax
    movw %ax, %ds

    /* 恢复通用寄存器 */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    /* 跳过错误码和中断号 */
    addq $16, %rsp

    /* 从中断返回 */
    iretq
.endm

/* 带错误码的异常入口（CPU已自动压入错误码，不再pushq $0） */
.macro IRQ_ENTRY_ERR num
.global irq_entry_\num
irq_entry_\num:
    /* 此时CPU已压入SS, RSP, RFLAGS, CS, RIP, Error Code */
    pushq $\num

    /* 保存通用寄存器 */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* 保存段寄存器 */
    movw %ds, %ax
    pushq %rax
    movw %es, %ax
    pushq %rax

    /* 设置内核段寄存器 */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es

    /* FPU状态保存/恢复在C函数irq_handler_64内部用per-CPU数组完成 */

    /* 调用中断处理函数，参数指向中断帧 */
    movq %rsp, %rdi
    call irq_handler_64

    /* 恢复段寄存器 */
    popq %rax
    movw %ax, %es
    popq %rax
    movw %ax, %ds

    /* 恢复通用寄存器 */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    /* 跳过中断号和错误码（CPU压入的错误码也需要跳过） */
    addq $16, %rsp

    /* 从中断返回 */
    iretq
.endm

/* 生成32个异常入口 */
IRQ_ENTRY 0
IRQ_ENTRY 1
IRQ_ENTRY 2
IRQ_ENTRY 3
IRQ_ENTRY 4
IRQ_ENTRY 5
IRQ_ENTRY 6
IRQ_ENTRY 7
IRQ_ENTRY_ERR 8    /* Double Fault（有错误码） */
IRQ_ENTRY 9
IRQ_ENTRY_ERR 10   /* Invalid TSS（有错误码） */
IRQ_ENTRY_ERR 11   /* Segment Not Present（有错误码） */
IRQ_ENTRY_ERR 12   /* Stack Fault（有错误码） */
IRQ_ENTRY_ERR 13   /* General Protection（有错误码） */
IRQ_ENTRY_ERR 14   /* Page Fault（有错误码） */
IRQ_ENTRY 15
IRQ_ENTRY 16
IRQ_ENTRY_ERR 17   /* Alignment Check（有错误码） */
IRQ_ENTRY 18
IRQ_ENTRY 19
IRQ_ENTRY 20
IRQ_ENTRY_ERR 21   /* Control Flow Protection（有错误码） */
IRQ_ENTRY 22
IRQ_ENTRY 23
IRQ_ENTRY 24
IRQ_ENTRY 25
IRQ_ENTRY 26
IRQ_ENTRY 27
IRQ_ENTRY 28
IRQ_ENTRY 29
IRQ_ENTRY 30
IRQ_ENTRY 31

/* 生成16个硬件IRQ入口（32-47） */
IRQ_ENTRY 32
IRQ_ENTRY 33
IRQ_ENTRY 34
IRQ_ENTRY 35
IRQ_ENTRY 36
IRQ_ENTRY 37
IRQ_ENTRY 38
IRQ_ENTRY 39
IRQ_ENTRY 40
IRQ_ENTRY 41
IRQ_ENTRY 42
IRQ_ENTRY 43
IRQ_ENTRY 44
IRQ_ENTRY 45
IRQ_ENTRY 46
IRQ_ENTRY 47

/* APIC定时器中断入口（向量0x40=64） */
IRQ_ENTRY 64

/* IPI核间中断入口（向量0x50=80） */
IRQ_ENTRY 80

/* ============================================================================
 * Spurious中断专用入口（向量0xFF）
 * CPU不压入错误码，不需要发送EOI
 * 最简形式：直接iretq，不保存寄存器，不调用函数
 * ============================================================================ */
.global irq_entry_spurious
irq_entry_spurious:
    /* spurious中断：直接返回，不做任何处理 */
    iretq

/* 最简IRQ测试入口：只iretq，不保存寄存器，不调用函数，不发送EOI */
.global irq_entry_null
irq_entry_null:
    iretq

/* ============================================================================
 * 上下文切换
 * ============================================================================ */

/* 切换到新进程（保存旧上下文，加载新上下文） */
.global context_switch_64
context_switch_64:
    /* RDI = 旧进程上下文指针, RSI = 新进程上下文指针 */
    /* process_context_t布局:
     * [0]=rax, [8]=rbx, [16]=rcx, [24]=rdx,
     * [32]=rsi, [40]=rdi, [48]=rbp, [56]=rsp,
     * [64]=r8, [72]=r9, [80]=r10, [88]=r11,
     * [96]=r12, [104]=r13, [112]=r14, [120]=r15,
     * [128]=rip, [136]=rflags,
     * [144]=cs, [152]=ss, [160]=ds, [168]=es, [176]=fs, [184]=gs,
     * [192]=cr3
     */

    /* 保存旧进程寄存器 */
    movq %rsp, 56(%rdi)     /* 保存栈指针 */
    movq %rbp, 48(%rdi)     /* 保存基址指针 */
    movq %rbx, 8(%rdi)      /* 保存RBX */
    movq %r12, 96(%rdi)     /* 保存R12 */
    movq %r13, 104(%rdi)    /* 保存R13 */
    movq %r14, 112(%rdi)    /* 保存R14 */
    movq %r15, 120(%rdi)    /* 保存R15 */

    /* 切换页表（CR3）- 实现用户空间隔离 */
    movq 192(%rsi), %rax    /* 加载新进程CR3 */
    testq %rax, %rax        /* 检查是否为0（内核线程无独立页表） */
    jz 1f
    movq %rax, %cr3         /* 切换CR3 */
1:

    /* 加载新进程寄存器 */
    movq 56(%rsi), %rsp     /* 加载栈指针 */
    movq 48(%rsi), %rbp     /* 加载基址指针 */
    movq 8(%rsi), %rbx      /* 加载RBX */
    movq 96(%rsi), %r12     /* 加载R12 */
    movq 104(%rsi), %r13    /* 加载R13 */
    movq 112(%rsi), %r14    /* 加载R14 */
    movq 120(%rsi), %r15    /* 加载R15 */

    ret

/* ============================================================================
 * 进程启动（新进程第一次运行）
 * ============================================================================ */
.global process_start_64
process_start_64:
    /* 新进程的入口点在RDI，参数在RSI */
    movq %rsi, %rdi  /* 设置参数 */
    call *%rax       /* 调用进程入口（RAX中） */

    /* 进程退出 */
    call process_exit_64

/* ============================================================================
 * 内核栈
 * ============================================================================ */
.section .bss
.align 16
kernel_stack_bottom:
    .skip 65536  /* 64KB内核栈 */
kernel_stack_top:

/* syscall返回上下文（用于fork完整实现） */
.global g_syscall_ret_rip
g_syscall_ret_rip:
    .quad 0
.global g_syscall_ret_rflags
g_syscall_ret_rflags:
    .quad 0
.global g_syscall_kernel_rsp
g_syscall_kernel_rsp:
    .quad 0

/* ============================================================================
 * 中断描述符表（64位）
 * ============================================================================ */
.section .data
.global idt_64
idt_64:
    .skip 256 * 16  /* 256个门描述符，每个16字节 */

.global idt_64_desc
idt_64_desc:
    .word 256 * 16 - 1  /* 限长 */
    .quad idt_64         /* 基地址 */

/* ============================================================================
 * 全局描述符表（64位）
 * ============================================================================ */
.section .data
.global gdt_64
gdt_64:
    /* 0x00: 空描述符 */
    .quad 0
    /* 0x08: 内核代码段 */
    .quad 0x00AF9A000000FFFF  /* 代码段，DPL=0，长模式 */
    /* 0x10: 内核数据段 */
    .quad 0x00CF92000000FFFF  /* 数据段，DPL=0 */
    /* 0x18: 用户代码段 */
    .quad 0x00AFFA000000FFFF  /* 代码段，DPL=3，长模式 */
    /* 0x20: 用户数据段 */
    .quad 0x00CFF2000000FFFF  /* 数据段，DPL=3 */
    /* 0x28: TSS段（16字节，需要两个描述符） */
    .quad 0
    .quad 0

.global gdt_64_desc
gdt_64_desc:
    .word 7 * 8 - 1  /* 限长 */
    .quad gdt_64     /* 基地址 */

/* ============================================================================
 * 32位模式远跳转到64位模式
 * 调用前必须：PAE已启用、EFER.LME已设置、CR3已加载PML4、CR0.PG已启用
 * 调用后：CS=0x08（64位内核代码段），进入真正的64位模式
 * ============================================================================ */
.section .text
.code32
.global x86_64_far_jump
x86_64_far_jump:
    /* 加载64位GDT */
    lgdt gdt_64_desc

    /* 远跳转到64位代码段：CS=0x08, EIP=64位入口 */
    ljmp $0x08, $x86_64_entry_64

/* 64位入口点（远跳转目标） - 放在固定地址0x100000 */
.section .text.entry64, "ax"
.code64
.global x86_64_entry_64
x86_64_entry_64:
    /* 跳过GDT重新加载（使用stage2的GDT，后续在C中重新加载） */
    jmp kernel_gdt64_flush
kernel_gdt64_flush:
    /* 重新加载数据段寄存器 */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /* 设置64位内核栈 */
    movq $0x90000, %rsp

    /* 清零BSS段（页表在0x600000，不在BSS范围内） */
    cld
    movq $__bss_start, %rdi
    movq $__bss_end, %rcx
    subq %rdi, %rcx
    shrq $3, %rcx
    xorq %rax, %rax
    rep stosq

    /* 调用64位C语言入口 */
    call x86_64_kernel_main

    /* 如果内核返回，停机 */
1:
    hlt
    jmp 1b

/* 内核串口输出（AL=字符） */
koutc:
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

    /* 停止 */
    cli
1:  hlt
    jmp 1b

/* 64位串口输出一个字符（AL=字符） */
serial_putc_64:
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


/* ============================================================================
 * GDT加载函数（纯汇编，避免内联汇编问题）
 * 参数: RDI = GDT指针地址
 * ============================================================================ */
.global gdt64_load_asm
gdt64_load_asm:
    lgdt (%rdi)           /* 加载GDT */

    /* 直接远跳转（64位模式下用32位offset，零扩展） */
    .byte 0xEA            # ljmp opcode
    .long gdt64_flush     # 32位offset
    .word 0x08            # CS

gdt64_flush:
    movw $0x10, %ax       /* 内核数据段选择器 */
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x0, %ax
    movw %ax, %fs
    movw %ax, %gs
    ret

/* 测试：只加载GDT，不做远跳转 */
.global gdt64_load_test
gdt64_load_test:
    lgdt (%rdi)
    ret

/* ============================================================================
 * 内核64位GDT（在汇编入口处加载，放在.data段可修改TSS）
 * ========================================================================== */
.section .data
.align 16
.global kernel_gdt64
kernel_gdt64:
    /* 0: 空描述符 */
    .quad 0
    /* 1: 内核代码段 (0x08) - 64位代码段 */
    .quad 0x00209A0000000000
    /* 2: 内核数据段 (0x10) */
    .quad 0x0000920000000000
    /* 3: 用户数据段 (0x18) - DPL3，数据段（SYSRET要求用户数据段在用户代码段之前） */
    .quad 0x0000F20000000000
    /* 4: 用户代码段 (0x20) - 64位代码段，DPL3 */
    .quad 0x0020FA0000000000
    /* 5-6: TSS (0x28) - 在C代码中设置 */
    .quad 0
    .quad 0

.global kernel_gdt64_ptr
kernel_gdt64_ptr:
    .word kernel_gdt64_ptr - kernel_gdt64 - 1  /* limit */
    .quad kernel_gdt64                          /* base */

/* ============================================================================
 * 内核TSS（任务状态段）
 * ========================================================================== */
.section .bss
.align 16
.global kernel_tss64
kernel_tss64:
    .skip 104  /* TSS大小：104字节（64位） */
    .word 0    /* io_map_base */

/* ============================================================================
 * 中断栈（IST - Interrupt Stack Table）
 * 为关键异常提供独立栈，避免栈溢出时的双重故障
 * ========================================================================== */
.section .bss
.align 16
.global irq_stack_df
.global irq_stack_df_top
irq_stack_df:
    .skip 4096  /* IST1: 双重故障栈 */
irq_stack_df_top:

.global irq_stack_nmi
.global irq_stack_nmi_top
irq_stack_nmi:
    .skip 4096  /* IST2: NMI栈 */
irq_stack_nmi_top:

.global irq_stack_mc
.global irq_stack_mc_top
irq_stack_mc:
    .skip 4096  /* IST3: 机器检查栈 */
irq_stack_mc_top:

/* 通用中断栈（用于所有其他中断） */
.global irq_stack_common
.global irq_stack_common_top
irq_stack_common:
    .skip 8192  /* 8KB通用中断栈 */
irq_stack_common_top:

/* FPU状态保存缓冲区（per-CPU，8个CPU × 512字节，解决SMP竞态） */
/* 放在.data段确保被映射，64字节对齐优化FXSAVE性能 */
.section .data
.align 64
.global fpu_save_buffer_percpu
fpu_save_buffer_percpu:
    .fill 512, 1, 0    /* CPU 0 */
    .fill 512, 1, 0    /* CPU 1 */
    .fill 512, 1, 0    /* CPU 2 */
    .fill 512, 1, 0    /* CPU 3 */
    .fill 512, 1, 0    /* CPU 4 */
    .fill 512, 1, 0    /* CPU 5 */
    .fill 512, 1, 0    /* CPU 6 */
    .fill 512, 1, 0    /* CPU 7 */

/* 兼容旧符号（指向CPU0缓冲区） */
.global fpu_save_buffer
fpu_save_buffer:
    .fill 512, 1, 0
