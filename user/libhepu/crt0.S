/* ============================================================================
 * 赫普OS 用户态C运行时启动代码 (crt0)
 * _start -> main -> exit
 * ========================================================================== */

.section .text
.global _start
_start:
    /* 调用 main(argc, argv) - 原型阶段 argc=0, argv=NULL */
    pushl $0          /* argv */
    pushl $0          /* argc */
    call main

    /* main返回值在eax中，作为退出码 */
    movl %eax, %ebx
    movl $5, %eax       /* SYS_EXIT */
    int $0x80
    /* 不会到达 */
    jmp .

/* 外部函数 */
.extern main
