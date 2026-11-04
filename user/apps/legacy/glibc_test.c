/* ============================================================================
 * glibc_test —— 历史测试程序（SEC-12 补源码）
 *
 * 逆向自 user/build/glibc_test.elf（900KB，glibc 静态链接）：
 *   自有符号 main，字符串 "Hello from glibc!"，
 *   带 .note.ABI-tag ⇒ 内核 ABI 判定一直是对的。
 *   链接地址 0x400000（glibc 默认 -no-pie 链接地址），落在用户窗口内。
 *
 * 用途：验证**真正的 glibc 静态程序**（有 TLS、有 CRT、有 atexit）
 *       在 HepuOS 上能跑完并正常退出 —— 这是 Linux 兼容层的核心用例。
 * ========================================================================== */
#include <stdio.h>

int main(void)
{
    printf("Hello from glibc!\n");
    return 0;
}
