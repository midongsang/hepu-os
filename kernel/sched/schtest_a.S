/* ============================================================================
 * 赫普OS - 调度器动态时间片测试 进程A (CPU 密集)
 * V3.89-E27: 持续计算不主动让出, 8 秒后 SYS_EXIT 回 Shell.
 * V3.89-E30: 退出时间 8s→10s——B(8s)先退, A 后退出时系统无其他
 * 活进程(消除"B 恰在睡眠时 A 退出"的竞态: 该时序曾致 B 恢复帧
 * 损坏 GPF, shell 永失响应). 前 8 秒 A/B 轮转演示不受影响.
 * 运行在 ring3, 仅通过 int 0x80 系统调用交互.
 * ========================================================================== */
.section .text
.global schtest_a_start
schtest_a_start:
    /* 先打印一条消息(与 init 同构), 确认用户态入口正常 */
    movl $2, %eax          /* SYS_PRINT */
    movl $msg_a, %ebx
    int $0x80

    /* start = sys_time() (SYS_TIME=21) */
    movl $21, %eax
    int $0x80
    movl %eax, %esi          /* st = start 秒 */

.loop_a:
    /* CPU 密集: 空转 30000 次 */
    movl $30000, %ecx
.spin_a:
    decl %ecx
    jnz .spin_a

    /* now - st >= 10 ? (V3.89-E30: 8→10, B 先退避免重叠退出竞态) */
    movl $21, %eax
    int $0x80
    subl %esi, %eax
    cmpl $10, %eax
    jb .loop_a

    /* SYS_EXIT (5) */
    movl $5, %eax
    xorl %ebx, %ebx
    int $0x80
    jmp .
.section .rodata
msg_a:
    .asciz "  [sch_a] CPU-dense process running\n"
