/* ============================================================================
 * 赫普OS 用户态C运行时启动代码 (crt0)
 * _start -> main -> exit
 *
 * 这份文件被 **32 位和 64 位两套构建** 同时引用（顶层 Makefile 的
 * `$(BUILD_DIR)/user/crt0.o` 规则用 `-m32` 编译它），所以必须按架构分。
 * 旧版只有 64 位一种写法（`%rsp`/`%rax`/`syscall`），32 位构建直接编不过：
 * `Error: bad register name '%rsp'` —— 这就是 32 位内核编不出来的第一道墙。
 *
 * 两套约定：
 *   64 位（V0.58.3 / SEC-22）：`syscall` 指令，rdi 传退出码，rax 传号。
 *     旧版是 32 位约定（pushl/int $0x80），而程序是 64 位 ELF —— int $0x80 压的是
 *     64 位中断帧，内核 0x80 兼容门按 32 位帧回弹，返回地址读成 0，表现为 rip=0 的 #PF。
 *   32 位：`int $0x80`，ebx 传退出码，eax 传号（入口见 kernel/arch/x86/interrupt.S）。
 * ========================================================================== */

.section .text
.global _start

#ifdef __x86_64__

_start:
    andq $-16, %rsp        /* SysV ABI：call 前 16 字节对齐 */
    xorl %ebp, %ebp        /* 清 frame pointer，便于回溯停在 _start */
    xorl %edi, %edi        /* argc = 0 */
    xorl %esi, %esi        /* argv = NULL */
    call main              /* 返回值在 rax */
    movq %rax, %rdi        /* 退出码 -> rdi */
    movl $5, %eax          /* SYS_EXIT */
    syscall
1:  hlt
    jmp 1b

#else  /* 32 位 i386 */

_start:
    andl $-16, %esp        /* cdecl：call 前 16 字节对齐 */
    xorl %ebp, %ebp        /* 清 frame pointer */
    xorl %eax, %eax        /* 可变参数个数 = 0（SysV i386 约定） */
    xorl %ecx, %ecx
    xorl %edx, %edx
    pushl $0               /* argv = NULL */
    pushl $0               /* argc = 0 */
    call main
    addl $8, %esp          /* 平掉两个参数 */
    movl %eax, %ebx        /* 退出码 -> ebx（i386 系统调用第 1 个参数） */
    movl $5, %eax          /* SYS_EXIT */
    int $0x80
1:  hlt
    jmp 1b

#endif

/* 外部函数 */
.extern main
