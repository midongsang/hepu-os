/* linuxcrt.S - 入口: 复刻 musl crt1 行为——esp 指向 argc,
 * 传给 C 函数解析完整启动栈 (argc/argv/envp/auxv). */
    .section .text
    .globl _start
_start:
    movl %esp, %ebx
    pushl %ebx
    call cstart
    /* exit(0) */
    movl $1, %eax
    xorl %ebx, %ebx
    int $0x80
1:  jmp 1b
