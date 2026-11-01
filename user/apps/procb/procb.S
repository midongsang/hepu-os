/* ============================================================================
 * 赫普OS - 用户态测试进程 B
 * V2.19: 消息英文化(VGA文本模式不支持中文);
 *        去掉无限循环(与init一致, 有限演示后 SYS_EXIT, 避免刷屏+让Shell可启动).
 * ========================================================================== */
.section .text
.global procb_start
procb_start:
    /* SYS_PRINT (eax=2) */
    movl $2, %eax
    movl $msg_b, %ebx
    int $0x80

    /* 有限演示完成, SYS_EXIT (eax=5) 退出 */
    movl $5, %eax
    movl $0, %ebx          /* exit code = 0 */
    int $0x80

    /* 不会到达 */
    jmp .
.section .rodata
msg_b:
    .asciz "  [proc-B] user-mode process running, preemptive sched works\n"
