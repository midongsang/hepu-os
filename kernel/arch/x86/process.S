/* ============================================================================
 * 赫普OS - 进程上下文切换汇编
 * ========================================================================== */

.section .text

/* ----------------------------------------------------------------------------
 * 进入用户态进程
 * C原型: void process_enter_user_asm(process_t *proc)
 *
 * 进程内核栈帧布局 (kernel_esp指向ds):
 *   [kernel_esp]    ds
 *   [kernel_esp+4]  edi, esi, ebp, esp, ebx, edx, ecx, eax (pusha)
 *   [kernel_esp+36] irq_number
 *   [kernel_esp+40] error_code
 *   [kernel_esp+44] eip
 *   [kernel_esp+48] cs
 *   [kernel_esp+52] eflags
 *   [kernel_esp+56] user_esp (用户态)
 *   [kernel_esp+60] ss (用户态)
 *
 * process_t 字段偏移 (V2.22修正, 用gcc offsetof验证的真实布局):
 *   48 = kernel_stack
 *   52 = kernel_esp
 *   68 = page_dir (V3.86修正: PROC_NAME_LEN=32, 布局 entry@64/page_dir@68/owned@72)
 *        V4.00-E45 复核: -m32 编译下 offsetof 确认 68 正确
 *        (64位宿主编译会因指针8字节误报72, 勿用). cacheinfo 命令实时核对.
 * -------------------------------------------------------------------------- */
.global process_enter_user_asm
process_enter_user_asm:
    /* V2.22: 关中断——进入用户态过程(切CR3/设TSS/切栈/iret)必须原子完成,
     * 否则 PIT 定时器中断会打断, irq 里的调度器(schedule)切换进程,
     * 破坏本函数执行上下文(eax/esp), 导致 mov %ds #GP 或异常栈错乱. */
    cli
    mov 4(%esp), %eax          /* eax = proc指针 */

    /* 切换页目录 (如果非空) */
    mov 68(%eax), %ebx
    test %ebx, %ebx
    jz 1f
    mov %ebx, %cr3
1:

    /* 设置TSS内核栈 = proc->kernel_stack
     * V2.22: 参数顺序——先保存proc, 再压kernel_stack作为call参数(栈顶). */
    mov 48(%eax), %ebx
    pushl %eax              /* 保存proc */
    pushl %ebx              /* call参数: kernel_stack (栈顶) */
    call tss_set_kernel_stack
    addl $4, %esp           /* 弹参数 */
    popl %eax               /* 恢复proc */

    /* 切换到进程的内核栈 (kernel_esp) */
    mov 52(%eax), %esp

    /* 恢复ds */
    popl %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* 恢复通用寄存器 */
    popa

    /* 跳过irq_number和error_code */
    addl $8, %esp

    /* 中断返回，进入用户态 */
    sti
    iret

/* ----------------------------------------------------------------------------
 * 进程上下文切换 (由调度器在中断中调用)
 * 保存当前esp到current->kernel_esp，切换到next->kernel_esp
 * C原型: void context_switch(process_t *next)
 *
 * 注意: 调用时当前esp指向中断帧的ds位置
 * -------------------------------------------------------------------------- */
.global context_switch
context_switch:
    /* 保存当前esp到current->kernel_esp */
    movl current, %eax
    test %eax, %eax
    jz 1f
    movl %esp, 52(%eax)    /* current->kernel_esp = esp */
1:
    /* current = next */
    movl 4(%esp), %eax
    movl %eax, current

    /* 设置TSS内核栈 */
    movl 48(%eax), %ebx
    pushl %eax              /* 保存next */
    pushl %ebx              /* call参数: kernel_stack (栈顶) */
    call tss_set_kernel_stack
    addl $4, %esp           /* 弹参数 */
    popl %eax               /* 恢复next */

    /* 切换页目录 */
    movl 68(%eax), %ebx
    test %ebx, %ebx
    jz 2f
    movl %ebx, %cr3
2:
    /* 切换到next的内核栈 */
    movl 52(%eax), %esp

    /* 恢复ds */
    popl %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    /* 恢复通用寄存器 */
    popa

    /* 跳过irq_number和error_code */
    addl $8, %esp

    sti
    iret
