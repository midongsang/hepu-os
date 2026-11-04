/* ============================================================================
 * hello2 —— 历史测试程序（SEC-12 补源码）
 *
 * 逆向自 user/build/hello2.elf：
 *   符号 _start / msg，字符串 "getpid returned:"，原链接地址 0x401000。
 *   用途：验证 **Linux getpid 号(39)** 在兼容层里翻译正确。
 *
 * V0.33 变更：链接地址从窗口外的 0x401000 改到窗口内的 0x40003000（SEC-13）。
 * ========================================================================== */
#include "common_linux_abi.h"

HEPU_LINUX_ABI_NOTE

void _start(void)
{
    static const char msg[] = "getpid returned:";
    long pid;

    hepu_puts(msg);
    pid = hepu_lsc(HEPU_LSYS_getpid, 0, 0, 0, 0, 0, 0);
    hepu_putnum(pid);
    hepu_puts("\n");
    hepu_lsc(HEPU_LSYS_exit, 0, 0, 0, 0, 0, 0);
    for (;;) { }
}
