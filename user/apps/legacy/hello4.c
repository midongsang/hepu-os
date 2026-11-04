/* ============================================================================
 * hello4 —— 历史测试程序（SEC-12 补源码）
 *
 * 逆向自 user/build/hello4.elf：
 *   符号 _start / path / buf / msg，字符串 "/bin/hello3.elf"、"read+close ok"，
 *   原链接地址 0x401000。
 *   用途：验证 **Linux open(2) + read(0) + close(3)** 一整条文件读路径，
 *         并且读回来的内容是真实文件数据（不是垃圾）。
 *
 * V0.33 变更：链接地址从窗口外的 0x401000 改到窗口内的 0x40005000（SEC-13）。
 * ========================================================================== */
#include "common_linux_abi.h"

HEPU_LINUX_ABI_NOTE

void _start(void)
{
    static const char path[] = "/bin/hello3.elf";
    static const char ok[]   = "read+close ok\n";
    static char buf[64];
    long fd, n;

    fd = hepu_lsc(HEPU_LSYS_open, (long)path, 0 /* O_RDONLY */, 0, 0, 0, 0);
    if (fd < 0) {
        hepu_puts("open failed: ");
        hepu_putnum(fd);
        hepu_puts("\n");
        hepu_lsc(HEPU_LSYS_exit, 1, 0, 0, 0, 0, 0);
        for (;;) { }
    }

    n = hepu_lsc(HEPU_LSYS_read, fd, (long)buf, (long)sizeof(buf), 0, 0, 0);
    if (n <= 0) {
        hepu_puts("read failed: ");
        hepu_putnum(n);
        hepu_puts("\n");
        hepu_lsc(HEPU_LSYS_exit, 1, 0, 0, 0, 0, 0);
        for (;;) { }
    }

    hepu_puts(ok);
    hepu_lsc(HEPU_LSYS_close, fd, 0, 0, 0, 0, 0);
    hepu_lsc(HEPU_LSYS_exit, 0, 0, 0, 0, 0, 0);
    for (;;) { }
}
