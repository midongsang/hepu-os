/* ============================================================================
 * hello_libc —— 历史测试程序（SEC-12 补源码）
 *
 * 逆向自 user/build/hello_libc.elf（819KB，glibc 静态链接）：
 *   字符串 "Hello libc!"，带 .note.ABI-tag，
 *   链接地址 0x400000（glibc 默认 -no-pie 链接地址）。
 *
 * 与 glibc_test 的分工：glibc_test 用 printf（走完整 stdio 缓冲），
 * 本程序只用 write（走最薄的路径），两者一起覆盖 glibc 初始化的两端。
 * ========================================================================== */
#include <unistd.h>

int main(void)
{
    write(1, "Hello libc!\n", 12);
    return 0;
}
