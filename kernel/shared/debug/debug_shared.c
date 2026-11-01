/* ============================================================================
 * 赫普OS - 共享错误处理与调试工具实现
 * 统一源码树：32位和64位共用
 * V0.10.152: 初始版本
 * ========================================================================== */

#include "debug_shared.h"

/* 断言失败处理（弱符号，可被架构相关代码覆盖） */
__attribute__((weak)) void hepu_debug_assert_fail(const char *expr, const char *file, int line)
{
    hepu_debug_puts("\n[ASSERT FAIL] ");
    hepu_debug_puts(expr);
    hepu_debug_puts("\n  at ");
    hepu_debug_puts(file);
    hepu_debug_puts(":");
    hepu_debug_put_dec(line);
    hepu_debug_puts("\n");
    /* 停机 */
    while (1) {
        __asm__ volatile("hlt");
    }
}

/* 弱符号：调试输出函数（由架构相关代码实现） */
__attribute__((weak)) void hepu_debug_puts(const char *str)
{
    (void)str;
    /* 默认实现：不输出 */
}

__attribute__((weak)) void hepu_debug_put_hex(uint64_t val)
{
    (void)val;
    /* 默认实现：不输出 */
}

__attribute__((weak)) void hepu_debug_put_dec(uint64_t val)
{
    (void)val;
    /* 默认实现：不输出 */
}
