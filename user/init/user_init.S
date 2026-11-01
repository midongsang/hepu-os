/* ============================================================================
 * 赫普OS - 第一个用户态程序 (init)
 * 运行在 ring 3，只能通过 int 0x80 系统调用与内核交互
 * 编译为位置无关代码，映射到用户空间执行
 * V2.19: 消息英文化(VGA文本模式不支持中文, 避免乱码);
 *        去掉无限循环(V2.18修复kheap后init真正进入ring3死循环,
 *        process_enter_user_mode永不返回导致Shell无法启动),
 *        改为有限演示后 SYS_EXIT, 退出时内核启动Shell.
 * ========================================================================== */
.section .text
.global user_init_start
user_init_start:
    /* 调用 SYS_PRINT (eax=2) 打印欢迎信息 */
    movl $2, %eax
    movl $msg1, %ebx
    int $0x80

    /* V2.22: 循环打印, 演示抢占式调度——运行期间 PIT 定时器
     * time_slice 到期会置 need_resched, 中断返回时切到 procb. */
    movl $3, %esi          /* 打印3次, 每次yield让出CPU给procb */
loop_tick:
    movl $2, %eax
    movl $msg_tick, %ebx
    int $0x80
    /* V2.22: SYS_YIELD (eax=6) 让出CPU, syscall 返回路径切换进程 */
    movl $6, %eax          /* SYS_YIELD */
    int $0x80
    decl %esi
    jnz loop_tick

    /* 完成信息 */
    movl $2, %eax
    movl $msg2, %ebx
    int $0x80

    /* V2.25: init 是常驻进程(PID 1), 不允许退出——无限循环演示心跳.
     * 内核 sys_exit(procb触发) 会把 init 置为 SLEEPING 常驻保留,
     * init 永不调用 SYS_EXIT, PCB/栈不销毁. */
loop_forever:
    movl $2, %eax
    movl $msg_tick, %ebx
    int $0x80
    movl $6, %eax          /* SYS_YIELD 让出CPU */
    int $0x80
    jmp loop_forever

.section .rodata
msg1:
    .asciz "  [user init] Hello! Running in ring 3\n"
    .asciz "  [user init] talking to kernel via int 0x80\n"
    .asciz "  [user init] kernel version: "
msg_tick:
    .asciz "  [user init] alive(tick), preemptive sched works\n"
msg2:
    .asciz "  [user init] user-mode program executed OK\n"
newline:
    .asciz "\n"
.global user_init_end
user_init_end:
