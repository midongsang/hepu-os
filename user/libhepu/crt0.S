/* ============================================================================
 * 赫普OS 用户态C运行时启动代码 (crt0)
 * _start -> main -> exit
 *
 * V0.58.3 (SEC-22)：旧版是 32 位约定（pushl/call main/movl %ebx/int $0x80）。
 * 程序是 64 位 ELF，int $0x80 压的是 64 位中断帧，而内核那条 0x80 兼容门
 * 按 32 位帧回弹，返回地址被读成 0 —— 表现为 rip=0x0 的 #PF。
 * 改为 64 位：syscall 指令 + rdi 传退出码，与 ulib.h 的 syscall1 一致。
 *
 * 注意：当前各用户程序都自己定义 _start 并直接调 exit()，本文件未被任何
 * Makefile 链接。保留它只是作为"标准入口模板"，内容必须是正确的 64 位版本。
 * ========================================================================== */

.section .text
.global _start
_start:
    andq $-16, %rsp        /* SysV ABI：call 前 16 字节对齐 */
    xorl %ebp, %ebp        /* 清 frame pointer，便于回溯停在 _start */
    xorl %edi, %edi        /* argc = 0 */
    xorl %esi, %esi        /* argv = NULL */
    call main              /* 返回值在 rax */
    movq %rax, %rdi        /* 退出码 */
    movl $5, %eax          /* SYS_EXIT */
    syscall
1:  hlt
    jmp 1b

/* 外部函数 */
.extern main
