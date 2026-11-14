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

    /* V0.13.43 修复: 用绝对地址槽访问 percpu (QEMU 上 gs:0x00 读取不可靠)
     * V0.13.43 修复: 槽是指针变量, 必须两层解引用 (mov 槽→rax, 再 mov (rax))
     * 槽为直接值语义 (entry 直接读写, 不用指针中转, 保护 rax 返回值)
     * 注意 push 后物理栈布局 (栈向下增长, 最后 push 的在栈顶):
     *   [rsp+0x00]=r15 [rsp+0x08]=r14 [rsp+0x10]=r13 [rsp+0x18]=r12
     *   [rsp+0x20]=r11 [rsp+0x28]=r10 [rsp+0x30]=r9  [rsp+0x38]=r8
     *   [rsp+0x40]=rbp [rsp+0x48]=rdi [rsp+0x50]=rsi [rsp+0x58]=rdx
     *   [rsp+0x60]=rcx [rsp+0x68]=rbx [rsp+0x70]=rax(nr) */
    /* V0.58.5 / SB-17: swapgs 之后 GS 就指向本核的 percpu_data,
     * 于是直接按 GS 相对取栈 —— 不再走全局单值槽, 避免两个核互相覆盖。
     * (V0.13.43 当年记的"QEMU 上 gs:0x00 读取不可靠", 实际是当时
     *  MSR_KERNEL_GS_BASE 没设对, 不是段基址本身的问题) */
    movq %rsp, %gs:0x08                      /* 保存用户栈到本核槽 */
    movq %gs:0x00, %rsp                      /* 取本核内核栈顶 */
    /* V0.33 修复：这里原本是 `movq %rsp, %r13` —— 它在 pushq %r13 之前
     * 就把**用户的 r13** 覆盖成了内核栈指针，于是 pushq %r13 存进去的
     * 是内核地址，popq %r13 恢复给用户的也是内核地址。
     * 后果：任何把值放在 r13 里跨系统调用传递的用户程序（编译器
     * 普遍用 r13 做 callee-saved 寄存器）都会被内核悄悄改写 ——
     * 表现为用户态读到内核地址（探针里 AT_PHNUM 读出 &g_kernel_tick64），
     * 随后写该指针时 #PF。内核栈指针另有 g_syscall_kernel_rsp 记录，
     * 不需要占用 r13。 */

    /* 保存寄存器 (rax 槽 = 真号; 全寄存器真值) */
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

    /* 保存syscall返回上下文（用于fork）
     * rcx(返回地址)=[rsp+0x60], r11(RFLAGS)=[rsp+0x20] */
    movq 96(%rsp), %rax      /* rcx = 用户返回地址 */
    movq %rax, g_syscall_ret_rip
    movq 32(%rsp), %rax      /* r11 = 用户 RFLAGS */
    movq %rax, g_syscall_ret_rflags
    movq %rsp, g_syscall_kernel_rsp

    /* 转换系统调用约定：
     * 用户态: rax=nr, rdi=arg1, rsi=arg2, rdx=arg3, r10=arg4, r8=arg5
     * C函数:  rdi=nr, rsi=arg1, rdx=arg2, rcx=arg3, r8=arg4, r9=arg5
     * 注意: nr 从 [rsp+0x70] (rax 槽) 取 */
    movq %r8, %r9      /* arg5 */
    movq %r10, %r8     /* arg4 */
    movq %rdx, %rcx    /* arg3 */
    movq %rsi, %rdx    /* arg2 */
    movq %rdi, %rsi    /* arg1 */
    movq 112(%rsp), %rdi  /* nr = [rsp+0x70] rax 槽 */

    /* 第 6 个系统调用参数（用户 r9）在 Linux x86_64 里是 mmap 的文件偏移，
     * 必须传给内核。此时 r9 寄存器已被上面的 arg5 覆盖，用户 r9 还在
     * 栈槽里（push 顺序：r15@0 ... r9@48）；第 7 个整数参数按 SysV 走栈。 */
    pushq 48(%rsp)

    /* 调用系统调用处理函数 */
    call syscall_handler_64
    addq $8, %rsp          /* 丢弃 arg6 栈槽，恢复原 rsp */

    /* V0.57.8：线程退出重定向。
     * wrap_thread_exit 会把当前线程摘掉并把 g_thr_redirect_flag 置 1；
     * 这里在**本线程的内核栈**上重建目标线程的返回帧，
     * 于是下面 popq 恢复出来的 rcx/r11/g_syscall_ustack_slot 都是目标线程的，
     * sysretq 直接落到目标线程，当前线程不再返回。 */
    cmpq $0, g_thr_redirect_flag(%rip)
    jne  .Lthr_redirect
    jmp  .Lthr_no_redirect
.Lthr_redirect:
    movq %rsp, %rdi
    call uthread_apply_redirect
    movq %rax, 112(%rsp)
    movq $0, g_thr_redirect_flag(%rip)
    jmp  .Lthr_redirect_done
.Lthr_no_redirect:

    /* 保存返回值到 rax 栈槽（[rsp+0x70]）。
     * 下面的 process64_deliver_signals 是 C 函数，返回时 rax 会被它的返回值
     * 覆盖 —— 旧代码没有保存，导致所有普通系统调用（getpid/waitpid/...）
     * 返回给用户态的都是 0。 */
    movq %rax, 112(%rsp)

.Lthr_redirect_done:
    /* 信号投递：检查pending_signals，修改返回RIP到handler */
    movq %rsp, %rdi          /* rdi = 内核栈指针 */
    call process64_deliver_signals

    /* 恢复寄存器 (rax 保留返回值; 恢复顺序 = push 逆序) */
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
    popq %rax          /* 恢复系统调用返回值（rax 槽已被结果覆盖） */

    /* 恢复用户栈 (V0.58.5 / SB-17: 从本核 GS 槽取, 保护 rax 返回值) */
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
    /* R3-7: 从本核 per-CPU 的 %gs:0x10 取"当前进程自己的" fork 快照槽。
     * 全局单份已经废弃 —— 两个父进程并发 fork 时那份会被互相覆盖，
     * 子进程会恢复到别人的 rip/rsp。 */
    movq %gs:0x10, %rax
    testq %rax, %rax
    jz 9f
    /* 0x00 = child_pml4（0 表示沿用当前 CR3） */
    movq 0x00(%rax), %rdx
    testq %rdx, %rdx
    jz 1f
    movq %rdx, %cr3
1:
    /* 0x50.. = parent_gprs[15]
     * [0]r15 [1]r14 [2]r13 [3]r12 [4]r11 [5]r10 [6]r9 [7]r8
     * [8]rbp [9]rdi [10]rsi [11]rdx [12]rcx [13]rbx [14]rax
     * 跳过 r11/rsp/rip/rax（由专用槽单独设置） */
    movq 0x50(%rax), %r15
    movq 0x58(%rax), %r14
    movq 0x60(%rax), %r13
    movq 0x68(%rax), %r12
    movq 0x78(%rax), %r10
    movq 0x80(%rax), %r9
    movq 0x88(%rax), %r8
    movq 0x90(%rax), %rbp
    movq 0x98(%rax), %rdi
    movq 0xa0(%rax), %rsi
    movq 0xa8(%rax), %rdx
    movq 0xb8(%rax), %rbx
    /* 0x10 = child_rsp */
    movq 0x10(%rax), %rsp
    swapgs
    /* 0x08 = child_rip,  0x18 = child_rflags */
    movq 0x08(%rax), %rcx
    movq 0x18(%rax), %r11
    xorq %rax, %rax
    sysretq
9:
    /* 快照槽为空说明调度器没绑定，这时候 sysret 出去是跳野地址，宁可停机 */
    cli
    hlt
    jmp 9b

/* V0.33.10: 子进程 exit 后恢复父进程。
 * 父进程的内核栈上保存了全部GPR(和syscall_entry push的一样)。
 * 我们切到父内核栈, pop GPR, 设 rax=child_pid, sysret回父用户态。 */
.global fork_return_to_parent
fork_return_to_parent:
    movq %gs:0x10, %rax
    testq %rax, %rax
    jz 9f
    /* 0x20 = parent_cr3 */
    movq 0x20(%rax), %rdx
    testq %rdx, %rdx
    jz 1f
    movq %rdx, %cr3
1:
    /* 0x50.. = parent_gprs[15]，同上跳过 r11/rsp/rip/rax */
    movq 0x50(%rax), %r15
    movq 0x58(%rax), %r14
    movq 0x60(%rax), %r13
    movq 0x68(%rax), %r12
    movq 0x78(%rax), %r10
    movq 0x80(%rax), %r9
    movq 0x88(%rax), %r8
    movq 0x90(%rax), %rbp
    movq 0x98(%rax), %rdi
    movq 0xa0(%rax), %rsi
    movq 0xa8(%rax), %rdx
    movq 0xb8(%rax), %rbx
    /* 0x38 = parent_ustack */
    movq 0x38(%rax), %rsp
    swapgs
    /* 0x28 = parent_rip, 0x30 = parent_rflags, 0x40 = parent_retval */
    movq 0x28(%rax), %rcx
    movq 0x30(%rax), %r11
    movq 0x40(%rax), %rax
    sysretq
9:
    cli
    hlt
    jmp 9b

/* ============================================================================
 * 中断入口（通用）
 * ========================================================================== */
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
     * [192]=cr3, [200]=_fxpad, [208..719]=fx_area (FXSAVE 区)
     */

    /* 保存旧进程寄存器 */
    movq %rsp, 56(%rdi)     /* 保存栈指针 */
    movq %rbp, 48(%rdi)     /* 保存基址指针 */
    movq %rbx, 8(%rdi)      /* 保存RBX */
    movq %r12, 96(%rdi)     /* 保存R12 */
    movq %r13, 104(%rdi)    /* 保存R13 */
    movq %r14, 112(%rdi)    /* 保存R14 */
    movq %r15, 120(%rdi)    /* 保存R15 */

    /* SSE/FPU 状态: FXSAVE 要求 16 字节对齐缓冲, fx_area 在 [208]。
     * 内核主体是 -mno-sse, 但 Lua/FreeType 这类第三方库是 -msse2, 不保存 xmm
     * 就会在两个线程之间互相污染。 */
    testq %rdi, %rdi
    jz 9f
    fxsave 208(%rdi)
9:

    /* 切换页表（CR3）- 实现用户空间隔离 */
    movq 192(%rsi), %rax    /* 加载新进程CR3 */
    testq %rax, %rax        /* 检查是否为0（内核线程无独立页表） */
    jz 1f
    movq %rax, %cr3         /* 切换CR3 */
1:
    /* 恢复新进程的 SSE/FPU 状态(在加载 GPR 之前做, 不依赖任何 GPR) */
    fxrstor 208(%rsi)

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
/* V0.33: 另起一对别名符号，专供 PMM 保留内核栈用
 * （kernel_stack_top 在别处被声明成指针，名字会撞） */
.global hepu_kstack_bottom
.global hepu_kstack_top
hepu_kstack_bottom:
kernel_stack_bottom:
    .skip 65536  /* 64KB内核栈 */
kernel_stack_top:
hepu_kstack_top:

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
/* 历史符号（R3-7 后已无读取方，保留只为不破坏外部引用）。
 * fork 上下文现在按进程存在 g_fork_snaps[] 里，由 %gs:0x10 指向。 */
.global g_fork_child_pml4
g_fork_child_pml4:
    .quad 0
.global g_fork_child_rip
g_fork_child_rip:
    .quad 0
.global g_fork_child_rsp
g_fork_child_rsp:
    .quad 0
.global g_fork_child_rflags
g_fork_child_rflags:
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
    /* V0.54.6: 最简路径 — 直接用正确 magic 值，绕开 Stage2 传递问题 */
    movl $0x2BADB002, %edi        /* RDI = magic (multiboot 规范值) */
    movq $0, %rsi                 /* RSI = mb_info = 0 (VBE 在 0x6000) */
    jmp kernel_gdt64_flush

kernel_gdt64_flush:
    /* 重新加载数据段寄存器（兼容其他入口点） */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /* 设置64位内核栈 */
    movq $0x90000, %rsp

    /* V0.54.6: 保存 Multiboot 信息 (RDI=magic, RSI=mb_info)
     * 必须在 BSS zeroing 前调用, 因为它会覆盖 RDI */
    call x86_64_set_boot_info

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

    /* 用 retfq 切换CS到0x08（长模式安全）：
       旧实现的 .byte 0xEA + 32位offset 在长模式下按64位operand解析，
       AP核以CS=0x18调用时产生#UD（无效指令）。retfq 从栈弹出
       新CS:RIP，无operand歧义。 */
    leaq 1f(%rip), %rax
    pushq $0x08           /* 新CS = 0x08（内核代码段） */
    pushq %rax            /* 新RIP = 1: */
    lretq
1:
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
    /* 5-6: 核 0 的 TSS 描述符 (0x28) - 在 tss64_init() 中填充 */
    .quad 0
    .quad 0
    /* 7-36: 其余 15 个核的 TSS 描述符 (核 i 的选择子 = 0x28 + i*16)
     * V0.58.5 / SB-18: 每核一份 TSS, 让 AP 跑用户态时的中断切到自己的栈。
     * 15 个核 * 2 槽 = 30 项, 加上前面 7 项共 37 项。 */
    .fill 30, 8, 0

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
    /* V0.58.5 / SB-18: 每核一份, 共 MAX_CPUS(16) 份, 每份 104 字节 */
    .skip 1664  /* 104 * 16 */
.align 16
kernel_tss64_end:

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
