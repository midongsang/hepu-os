/* ============================================================================
 * 赫普OS 64位 - 用户态Hello World程序
 * 独立ELF64用户程序，通过user_process_exec从ext2加载执行
 * V0.10.149: 使用ulib用户态库
 * ========================================================================== */

#include "ulib.h"

/* 用户态主程序（ELF入口点） */
void _start(void)
{
    puts("========================================");
    puts("  HepuOS 64-bit User Mode Application");
    puts("  Hello World from ELF64 user space!");
    puts("========================================");
    puts("");

    puts("This program runs in ring 3 (user mode).");
    puts("It uses syscall instruction to request");
    puts("kernel services.");
    puts("");

    printf("PID: %d\n", getpid());
    printf("System time: %u\n", (unsigned)time());
    puts("");

    puts("System calls tested:");
    puts("  - write (fd=1, stdout)");
    puts("  - getpid");
    puts("  - time");
    puts("  - exit (code=0)");
    puts("");

    puts("User mode application completed.");

    /* 退出进程，返回码0 */
    exit(0);
}
