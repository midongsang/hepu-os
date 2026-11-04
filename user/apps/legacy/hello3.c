/* ============================================================================
 * hello3 —— 历史测试程序（SEC-12 补源码）
 *
 * 逆向自 user/build/hello3.elf：
 *   符号 _start / path / ok，字符串 "/bin/hello2.elf" 与 "opened file"，
 *   原链接地址 0x401000。
 *   用途：验证 **Linux open(2)** 能打开镜像里 /bin 下的真实文件。
 *
 * V0.33 变更：链接地址从窗口外的 0x401000 改到窗口内的 0x40004000（SEC-13）。
 * ========================================================================== */
#include "common_linux_abi.h"

HEPU_LINUX_ABI_NOTE

void _start(void)
{
    static const char path[] = "/bin/hello2.elf";
    static const char ok[]   = "opened file\n";
    long fd;

    fd = hepu_lsc(HEPU_LSYS_open, (long)path, 0 /* O_RDONLY */, 0, 0, 0, 0);
    if (fd < 0) {
        hepu_puts("open failed: ");
        hepu_putnum(fd);
        hepu_puts("\n");
        hepu_lsc(HEPU_LSYS_exit, 1, 0, 0, 0, 0, 0);
        for (;;) { }
    }
    hepu_puts(ok);
    hepu_lsc(HEPU_LSYS_close, fd, 0, 0, 0, 0, 0);
    hepu_lsc(HEPU_LSYS_exit, 0, 0, 0, 0, 0, 0);
    for (;;) { }
}
