/* ============================================================================
 * hello_c2 —— 历史测试程序（SEC-12 补源码）
 *
 * 逆向自 user/build/hello_c2.elf：
 *   符号 main / _start，字符串 "Hello from C!"，原链接地址 0x401000，
 *   .text 从 0x401000 起、_start 落在 0x40103f（main 之后），
 *   带 .rodata / .eh_frame —— 典型的 gcc 编译 C 程序。
 *
 * 用途：验证**同一段逻辑用 gcc 编译**（而非手写汇编）时，
 *       Linux 号表同样工作 —— 与 hello_static 形成对照。
 *
 * V0.33 变更：链接地址从窗口外的 0x401000 改到窗口内的 0x40006000（SEC-13）。
 * ========================================================================== */
#include "common_linux_abi.h"

HEPU_LINUX_ABI_NOTE

int main(void)
{
    static const char msg[] = "Hello from C!\n";
    hepu_lwrite(msg, 14);
    return 0;
}

/* gcc -nostdlib 不生成 CRT，所以自己提供 _start：
 * 调 main → exit(main 的返回值)。 */
void _start(void)
{
    int rc = main();
    hepu_lsc(HEPU_LSYS_exit, rc, 0, 0, 0, 0, 0);
    for (;;) { }
}
