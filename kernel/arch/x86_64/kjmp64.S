/* ============================================================================
 * kjmp64.S — 64 位内核的 setjmp / longjmp
 *
 * Lua 的错误处理靠 longjmp 从深层调用栈弹回保护点(ldo.c 的 luaD_rawrunprotected),
 * 内核此前没有这套机制(32 位内核有 arch/x86/setjmp.S, 64 位没有)。
 *
 * jmp_buf 布局(8 个 8 字节槽, 与 third_party/lua/include/setjmp.h 一致):
 *   [0]=rbx [8]=rbp [16]=r12 [24]=r13 [32]=r14 [40]=r15 [48]=rsp [56]=rip
 * 只保存 callee-saved 寄存器, 符合 SysV 调用约定。
 * ========================================================================== */
.text

.global setjmp
.global _setjmp
setjmp:
_setjmp:
    movq %rbx,  0(%rdi)
    movq %rbp,  8(%rdi)
    movq %r12, 16(%rdi)
    movq %r13, 24(%rdi)
    movq %r14, 32(%rdi)
    movq %r15, 40(%rdi)
    leaq 8(%rsp), %rax          /* 恢复到"调用者"的栈顶(已 pop 返回地址) */
    movq %rax, 48(%rdi)
    movq (%rsp), %rax           /* 返回地址 = setjmp 之后的下一条指令 */
    movq %rax, 56(%rdi)
    xorl %eax, %eax             /* setjmp 直接返回 0 */
    ret

.global longjmp
.global _longjmp
longjmp:
_longjmp:
    movq  0(%rdi), %rbx
    movq  8(%rdi), %rbp
    movq 16(%rdi), %r12
    movq 24(%rdi), %r13
    movq 32(%rdi), %r14
    movq 40(%rdi), %r15
    movq 48(%rdi), %rsp
    movl %esi, %eax             /* longjmp 的返回值不能是 0(C 标准保证返回非 0) */
    testl %eax, %eax
    jne 1f
    movl $1, %eax
1:
    jmp *56(%rdi)
