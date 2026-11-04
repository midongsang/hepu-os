/* ============================================================================
 * libtest —— 历史共享库（SEC-12 补源码）
 *
 * 逆向自 user/build/libtest.so（DYN，链在 0x60000000，即用户窗口的「库区」）：
 *   导出符号：test_add / test_msg / my_syscall3
 *   反汇编还原：
 *     test_add(int a, int b)  → return a + b;
 *     test_msg()              → write(1, "Hello from libtest.so!", 23);
 *     my_syscall3(a,b,c,d)    → 直接执行 syscall（rax=a, rdi=b, rsi=c, rdx=d），
 *                               返回 rax —— 这是当年用来打通「库内自定义号」
 *                               的裸系统调用封装（与 SEC-14 记录的历史扩展号相关）。
 *
 * 说明：HepuOS 当前**不支持 PT_INTERP（动态链接器）加载**，
 *       所以依赖本库的 dynhello/dynhello2 在内核上跑不起来；
 *       本库的意义是让构建可重现，并保留这些历史用例的语义。
 * ========================================================================== */
#include <unistd.h>

int test_add(int a, int b)
{
    return a + b;
}

void test_msg(void)
{
    write(1, "Hello from libtest.so!", 23);
}

/* 裸 syscall 封装：把 4 个参数塞进 rax/rdi/rsi/rdx 直接进内核。
 * a = 系统调用号，其余 3 个是参数。 */
long my_syscall3(long a, long b, long c, long d)
{
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(a), "D"(b), "S"(c), "d"(d)
                     : "rcx", "r11", "memory");
    return ret;
}
