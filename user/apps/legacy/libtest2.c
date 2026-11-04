/* ============================================================================
 * libtest2 —— 历史共享库（SEC-12 补源码）
 *
 * 逆向自 user/build/libtest2.so（DYN，链在 0x60000000）：
 *   导出符号：test_value / get_value / double_value / func_table / call_func
 *   反汇编还原：
 *     int *test_value;                    ← 是个**指针**
 *     int get_value(void)                 { return *test_value; }
 *     int double_value(void)              { return *test_value * 2; }
 *     int (*func_table[2])(void);         ← 函数指针数组
 *     int call_func(int i)                { return func_table[i](); }
 *
 * 注意 get_value/double_value 都**不带参数**（从调用约定看只用了 edi 存
 * 但没读），所以这里按无参实现。
 * ========================================================================== */
#include <unistd.h>

int test_value = 21;

int get_value(void)
{
    return test_value;
}

int double_value(void)
{
    return test_value * 2;
}

/* func_table：两个简单函数，供 call_func 间接调用 */
static int func_inc(void)  { return test_value + 1; }
static int func_double(void) { return test_value * 2; }

int (*func_table[2])(void) = {
    func_inc,
    func_double,
};

int call_func(int i)
{
    if (i < 0 || i >= 2) return -1;
    return func_table[i]();
}
