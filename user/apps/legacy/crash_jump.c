/* ============================================================================
 * crash_jump —— **故意**跳到坏地址的用户态异常测试（V0.33 新增）
 *
 * 为什么要新增这个程序：
 *   旧的启动自检电池拿 hello_static.elf 当「故意异常」用例，
 *   但那是个**误判** —— hello_static 只是因为缺 .note.ABI-tag 导致
 *   exit(60) 没生效、执行流落到字符串数据上才炸的，
 *   并不是它自己「故意跳坏地址」（见 hello_static.c 里的详细说明）。
 *   现在 hello_static 补了 ABI note 后会干净退出，电池里就少了一次异常，
 *   SEC-07「用户态异常只终止该进程、不整机 hlt」的验证能力必须另找载体。
 *
 * 本程序就是那个载体：显式跳到一个未映射地址，
 *   → 触发 #PF（vector 0xE）
 *   → 内核应当只终止本进程，内核继续跑完剩余自检项
 *
 * 它也带 .note.ABI-tag（用的是 Linux 号表，虽然本程序不调 syscall）。
 * ========================================================================== */
#include "common_linux_abi.h"

HEPU_LINUX_ABI_NOTE

void _start(void)
{
    static const char msg[] = "crash_jump: jumping to bad address\n";
    volatile unsigned long *bad = (volatile unsigned long *)(unsigned long)0xDEAD0000UL;

    hepu_lwrite(msg, hepu_lstrlen(msg));

    /* 故意跳到未映射地址 —— 内核应隔离本进程，而不是整机挂死 */
    *bad = 0x1234;

    /* 正常不会到这里 */
    hepu_lsc(HEPU_LSYS_exit, 0, 0, 0, 0, 0, 0);
    for (;;) { }
}
