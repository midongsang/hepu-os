/* ============================================================================
 * hello_linux —— 历史测试程序（SEC-12 补源码）
 *
 * 逆向自 user/build/hello_linux.elf：
 *   符号 _start / msg，原链接地址 0x401000，反汇编：
 *       mov $0x1,%rax ; mov $0x1,%rdi ; lea msg(%rip),%rsi ; mov $0xd,%rdx ; syscall
 *       mov $0x3c,%rax ; mov $0x0,%rdi ; syscall
 *   即 write(1, "Hello Linux!\n", 13) 然后 exit(0)。
 *
 * V0.33 变更：原链接地址 0x401000 落在 Hepu 用户窗口
 *   [0x40000000,0x80000000) 之外，内核按设计直接拒绝（SEC-13）。
 *   现改链到窗口内的 0x40002000 —— ET_EXEC 的绝对地址是烧进代码里的，
 *   只要落在窗口内就能按链接地址原样映射，本程序因此真正跑得起来，
 *   而不是每次都被 exec 拒掉。
 * ========================================================================== */
#include "common_linux_abi.h"

HEPU_LINUX_ABI_NOTE

void _start(void)
{
    static const char msg[] = "Hello Linux!\n";
    hepu_lwrite(msg, 13);
    hepu_lsc(HEPU_LSYS_exit, 0, 0, 0, 0, 0, 0);   /* exit(0) */
    for (;;) { }
}
