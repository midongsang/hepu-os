/* ============================================================================
 * 赫普OS - 调度器动态时间片测试 进程B (IO/交互型)
 * V3.89-E27: 反复真睡眠(SYS_SLEEP=15) 100ms 让出 CPU, 8 秒后退出.
 * 运行在 ring3, 仅通过 int 0x80 系统调用交互.
 * ========================================================================== */
.section .text
.global schtest_b_start
schtest_b_start:
    /* V3.89-E30: start = sys_time() 存入 .data 全局变量——原存 esi,
     * 睡眠/唤醒路径若未完整保存寄存器, esi 会被破坏 → 8s 退出判断永假. */
    movl $21, %eax
    int $0x80
    movl %eax, st_b

.loop_b:
    /* 每轮打印一次消息——证明 B 被调度器切入执行 */
    movl $2, %eax          /* SYS_PRINT */
    movl $msg_b, %ebx
    int $0x80

    /* SYS_SLEEP(100ms) —— V3.89-E27 真睡眠: SLEEPING 状态让出 CPU */
    movl $15, %eax
    movl $100, %ebx
    int $0x80

    /* now - st >= 8 ? */
    movl $21, %eax
    int $0x80
    subl st_b, %eax
    cmpl $8, %eax
    jb .loop_b

    /* SYS_EXIT (5) */
    movl $5, %eax
    xorl %ebx, %ebx
    int $0x80
    jmp .
.section .data
st_b:   .long 0

.section .rodata
msg_b:
    .asciz "  [sch_b] IO process tick\n"
