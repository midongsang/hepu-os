/* ============================================================================
 * 赫普OS - .hug 测试应用: hello
 * 纯二进制，位置无关，通过 int 0x80 系统调用运行
 * ========================================================================== */

.section .text
.global _start
_start:
    /* SYS_PRINT (eax=2) 打印消息 */
    movl $2, %eax
    movl $msg, %ebx
    int $0x80

    /* SYS_EXIT (eax=5) */
    movl $5, %eax
    movl $0, %ebx
    int $0x80

    jmp .

.section .rodata
msg:
    .asciz "  [.hug应用] 你好！我是从 initrd 加载的 .hug 原生应用！\n"
