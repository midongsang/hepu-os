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
    /* 64位模式下DS/ES的RPL不影响，先不设置，只切换CS/SS */

    pushq $0x1B        /* 用户数据段选择子 (0x18 | RPL3) */
    pushq %rsi         /* 用户栈指针 */
    pushq %rdx         /* RFLAGS */
    pushq $0x23        /* 用户代码段选择子 (0x20 | RPL3) */
    pushq %rdi         /* 用户指令指针 */
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
