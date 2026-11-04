/* ============================================================================
 * 赫普OS 64位 - 用户态切换汇编
 * ========================================================================== */

.section .text

/*
 * void user_enter(uint64_t rip, uint64_t rsp, uint64_t rflags)
 * 切换到用户态
 */
.global user_enter
user_enter:
    /* 参数：rdi=rip, rsi=rsp, rdx=rflags */
    /* V0.13.43: GDT 布局统一为 Linux 标准:
     *   GDT 条目3 (0x18) = 用户数据段, 条目4 (0x20) = 用户代码段
     *   IRETQ: SS=0x1B(数据), CS=0x23(代码) */

    pushq $0x1B        /* SS  = 0x18|RPL3 用户数据段 */
    pushq %rsi         /* 用户栈指针 */
    pushq %rdx         /* RFLAGS */
    pushq $0x23        /* CS  = 0x20|RPL3 用户代码段 */
    pushq %rdi         /* 用户指令指针 */

    /* V0.33: 遵守 Linux 进程入口 ABI（ELF_PLAT_INIT）。
     *
     * iretq 只恢复 RIP/CS/RFLAGS/RSP/SS，其余通用寄存器会带着**内核残留值**
     * 进入用户态。Linux 规定进程入口时这些寄存器必须为 0，glibc 静态程序
     * 尤其依赖这条约定：
     *   _start:  mov %rdx, %r9      ← rdx 被当作 rtld_fini 传给 __libc_start_main
     *   __libc_start_main: __cxa_atexit(rtld_fini, ...)  ← 把它注册成退出析构函数
     * 残留的 rdx（非 0）于是被注册进 __exit_funcs，进程退出时被当成函数指针
     * 调用，跳到野地址取指失败。
     *
     * 实测证据（V0.33 定位 glibc static-PIE 退出崩溃 rip=0x202）：
     *   - 崩溃 rip 0x202 == 内核 rflags 字面量 0x202，退出链解码后正好 0x202；
     *   - 同一批测试里另一个进程退出时跳到 0x6C2FCFD45B，同样是内核残留值。
     * 两者都是同一个根因：入口 GPR 未按 ABI 清零。
     *
     * 注：pushq 已经把 rip/rsp/rflags 存进中断帧，清零不会影响它们。 */
    xorq %rax, %rax
    xorq %rbx, %rbx
    xorq %rcx, %rcx
    xorq %rdx, %rdx
    xorq %rsi, %rsi
    xorq %rdi, %rdi
    xorq %rbp, %rbp
    xorq %r8,  %r8
    xorq %r9,  %r9
    xorq %r10, %r10
    xorq %r11, %r11
    xorq %r12, %r12
    xorq %r13, %r13
    xorq %r14, %r14
    xorq %r15, %r15
    iretq              /* 中断返回，切换到用户态 */

/*
 * void context_switch(uint64_t *old_rsp, uint64_t new_rsp)
 * 保存当前上下文并切换到新上下文
 */
.global context_switch
context_switch:
    /* 保存当前寄存器 */
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* 保存当前栈指针 */
    movq %rsp, (%rdi)

    /* 切换到新栈 */
    movq %rsi, %rsp

    /* 恢复新上下文寄存器 */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret

/*
 * 系统调用入口（syscall指令调用）
 * 系统调用号在rax，参数在rdi, rsi, rdx, r10, r8, r9
 */
.global syscall_entry
syscall_entry:
    /* 保存用户态寄存器 */
    pushq %rcx         /* 返回地址 */
    pushq %r11         /* 保存RFLAGS */
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %r10
    pushq %r8
    pushq %r9

    /* 调用C语言系统调用处理函数 */
    movq %rax, %rdi
    movq %rsi, %rdx    /* 调整参数顺序 */
    /* 这里应该调用 syscall64_dispatch(number, arg1, arg2, ...) */
    /* 简化：直接返回 */

    /* 恢复寄存器 */
    popq %r9
    popq %r8
    popq %r10
    popq %rdx
    popq %rsi
    popq %rdi
    popq %r11
    popq %rcx

    /* 返回用户态 */
    sysretq

/* ============================================================================
 * V0.13.46: 未响应进程强制退出 trampoline
 * iretq 跳到此处（内核态CS=0x08, rsp=中断处理栈）：
 *  1) 调用 C 清理（释放进程、恢复全局状态）
 *  2) 直接进入桌面主循环（等价于 kernel_main 完成初始化后的主循环）
 * 避免依赖 g_user_saved_rsp（10秒后内核栈内容已失效）
 * ========================================================================== */
.global user_force_exit_trampoline
user_force_exit_trampoline:
    /* 对齐栈后调用C清理函数 */
    andq $-16, %rsp
    call user_process_force_exit_cleanup
    /* 清理完成后直接进入桌面主循环 */
    call desktop64_run
.hang:
    hlt
    jmp .hang

/* ============================================================================
 * V0.13.68: 用户进程正常退出（sys_exit）的返回 trampoline
 * 由 user_exit_to_kernel 跳入（此时 CR3 已恢复为内核页表）。
 * 作用：
 *   1) 恢复 user_process_switch 保存的栈（g_user_saved_rsp）
 *   2) 调用 C 清理 user_return_cleanup（在恢复的栈帧上，返回值=switch 返回值）
 *   3) 执行 user_process_switch 的 epilogue 并返回其调用者
 * 注意：epilogue 必须与 user_process_switch 的 prologue 对应：
 *   prologue: push r12; push rbp; push rbx; sub $0x30, rsp
 *   故此处:  add $0x30; pop rbx; pop rbp; pop r12; ret
 * ========================================================================== */
.global user_return_trampoline
user_return_trampoline:
    movq g_user_saved_rsp(%rip), %rsp   /* 恢复 switch 栈 */
    call user_return_cleanup           /* 清理（自带栈对齐，返回值在 eax） */
    addq $0x30, %rsp                   /* epilogue：跳过局部区 */
    popq %rbx
    popq %rbp
    popq %r12
    ret                                /* 返回到 user_process_switch 的调用者 */
