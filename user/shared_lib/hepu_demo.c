/* ============================================================================
 * 赫普OS - 共享库示例
 * libhepu_demo.so - 演示动态链接的共享库
 * V8.37
 * ========================================================================== */

#include <stdint.h>

/* 导出函数：计算阶乘 */
int hepu_demo_factorial(int n)
{
    if (n <= 1) return 1;
    int result = 1;
    for (int i = 2; i <= n; i++) {
        result *= i;
    }
    return result;
}

/* 导出函数：计算斐波那契数列 */
int hepu_demo_fibonacci(int n)
{
    if (n <= 0) return 0;
    if (n == 1) return 1;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int temp = a + b;
        a = b;
        b = temp;
    }
    return b;
}

/* 导出函数：字符串长度 */
int hepu_demo_strlen(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    return len;
}

/* 导出函数：内存比较 */
int hepu_demo_memcmp(const void *s1, const void *s2, uint32_t n)
{
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    for (uint32_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) return p1[i] - p2[i];
    }
    return 0;
}

/* 导出函数：获取库版本 */
const char *hepu_demo_version(void)
{
    return "libhepu_demo.so v1.0 (HepuOS)";
}

/* 库初始化函数（DT_INIT） */
void hepu_demo_init(void)
{
    /* 库初始化代码 */
}

/* 库析构函数（DT_FINI） */
void hepu_demo_fini(void)
{
    /* 库清理代码 */
}
