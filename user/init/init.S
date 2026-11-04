/* ============================================================================
 * 赫普OS - 第一个用户态进程 (init)
 * 通过 int 0x80 系统调用与内核交互
 * ========================================================================== */

.section .text
.global _start

_start:
    /* 调用 SYS_PRINT (eax=2) 打印欢迎信息 */
    movl $2, %eax          /* SYS_PRINT */
    movl $msg, %ebx        /* arg1 = 字符串指针 */
    int $0x80

    /* 调用 SYS_HEPU_INFO (eax=0) 获取内核版本 */
    movl $0, %eax
    int $0x80
    /* eax = 版本字符串指针，打印它 */
    movl %eax, %ebx
    movl $2, %eax
    int $0x80

    /* 打印换行 */
    movl $2, %eax
    movl $newline, %ebx
    int $0x80

    /* 调用 SYS_EXIT (eax=5) 退出 */
    movl $5, %eax
    movl $0, %ebx          /* exit code = 0 */
    int $0x80

    /* 不会到达这里 */
    jmp .

.section .rodata
msg:
    .asciz "  [user] Hello! I am init, first user process of HEPU OS.\n"
    .asciz "  [user] talking to kernel via int 0x80 syscall\n"
    .asciz "  [user] kernel version: "
newline:
    .asciz "\n"
