/* ============================================================================
 * 赫普OS - 内核版 setjmp/longjmp (GAS / AT&T)
 * V3.85: 用户进程退出后, sys_exit 用 longjmp 回到 hug_run 的主线挂起点,
 *        恢复原 Shell 循环(不再 hlt 停机/重复启动 shell_main).
 * 只保存 callee-saved 寄存器(eip/esp/ebp/ebx/esi/edi), 与 C 调用约定一致.
 * ========================================================================== */

.section .text

/* int hepu_setjmp(uint32_t *buf)
 * 保存当前上下文到 buf[6]: eip, esp, ebp, ebx, esi, edi
 * 直接调用返回 0; hepu_longjmp 跳回时返回非0 */
.global hepu_setjmp
hepu_setjmp:
    movl 4(%esp), %ecx        /* buf */
    movl 0(%esp), %eax        /* 调用点返回地址 = 恢复时的 eip */
    movl %eax, 0(%ecx)        /* buf[0] = eip */
    movl %esp, 4(%ecx)        /* buf[1] = esp */
    movl %ebp, 8(%ecx)        /* buf[2] = ebp */
    movl %ebx, 12(%ecx)       /* buf[3] = ebx */
    movl %esi, 16(%ecx)       /* buf[4] = esi */
    movl %edi, 20(%ecx)       /* buf[5] = edi */
    xorl %eax, %eax           /* setjmp 直接返回 0 */
    ret

/* void hepu_longjmp(uint32_t *buf, int val)
 * 恢复 buf 保存的上下文, 跳回 setjmp 调用点, 返回 val(非0)
 * V3.85: esp 恢复须 +4 —— setjmp 保存的是入口 esp(含返回地址槽),
 * 而调用点代码(call 返回后)期望 esp=call前esp; 直接恢复入口 esp 会
 * 让函数返回路径的 add/ret 错位 4B → 跳飞(实测 hug_run 不恢复).
 * 恢复后直接用 jmp(不再 push+ret), 避免二次压栈. */
.global hepu_longjmp
hepu_longjmp:
    movl 4(%esp), %ecx        /* buf */
    movl 8(%esp), %eax        /* val */
    movl 20(%ecx), %edi
    movl 16(%ecx), %esi
    movl 12(%ecx), %ebx
    movl 8(%ecx), %ebp
    movl 4(%ecx), %esp        /* setjmp 入口 esp */
    addl $4, %esp             /* 恢复为 call 前 esp */
    movl 0(%ecx), %edx        /* 恢复 eip */
    /* V3.99-E44: longjmp 目标处开中断——sys_exit 在 kfree 自己栈前已 cli
     * (E-44: 杜绝释放栈上中断窗口 → ISR 6), 此处 sti 恢复 IF=1, 保证
     * 跳回主线 Shell 后键盘/PIT 中断可用(E-35 显式 sti 的职责并入本处). */
    sti
    jmp *%edx
