/* ============================================================================
 * hello_static —— 历史测试程序（SEC-12 补源码）
 *
 * 逆向自 user/build/hello_static.elf（原 obj 名为 hello_static.o）：
 *   链接地址 0x40001000，符号 _start / msg / __bss_start / _edata / _end，
 *   反汇编逐字节还原：
 *       mov $0x1,%rax ; mov $0x1,%rdi ; lea msg(%rip),%rsi ; mov $0x19,%rdx ; syscall
 *       mov $0x3c,%rax ; mov $0x2a,%rdi ; syscall
 *       msg: "Hello from static!\n"
 *   即 write(1, msg, 25) 然后 exit(42)。
 *
 * V0.33 重要更正：旧版 kernel_main_64.c 的注释把本程序描述成
 * 「故意跳到坏地址的异常测试」—— **那是错的**。
 * 本程序根本没有跳坏地址，它只是 exit(60)（Linux 号）没生效：
 *   它没有 .note.ABI-tag ⇒ 被内核判成 Hepu 原生 ABI ⇒
 *   原生号表里没有 60 号（原生 exit 是 6）⇒ 进程不肯退出 ⇒
 *   执行流一路落到后面的 msg 字符串数据上 ⇒
 *   "He" 被解成 rex.W gs insb ⇒ CPL=3 执行 insb ⇒ #GP(0)。
 *   于是产生了那条 [EXCEPTION] vector=0xD error=0x0 rip=0x4000102E。
 *
 * 现在补上 .note.ABI-tag ⇒ exit(42) 正常生效 ⇒ 进程干净退出。
 * 真正需要「故意坏地址」的测试改由 crash_jump.elf 承担（见 crash_jump.c）。
 * ========================================================================== */
#include "common_linux_abi.h"

HEPU_LINUX_ABI_NOTE

void _start(void)
{
    static const char msg[] = "Hello from static!\n";
    hepu_lwrite(msg, 25);
    hepu_lsc(HEPU_LSYS_exit, 42, 0, 0, 0, 0, 0);   /* exit(42) */
    for (;;) { }
}
