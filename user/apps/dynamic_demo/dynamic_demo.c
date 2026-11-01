/* ============================================================================
 * 赫普OS - 动态链接程序示例
 * 演示如何使用共享库
 * V8.37
 * ========================================================================== */

#include <stdint.h>

/* 共享库函数声明（实际由动态链接器解析） */
extern int hepu_demo_factorial(int n);
extern int hepu_demo_fibonacci(int n);
extern int hepu_demo_strlen(const char *s);
extern const char *hepu_demo_version(void);

/* 系统调用（通过libhepu） */
extern int hepu_write(int fd, const void *buf, int count);
extern void hepu_exit(int status);

/* 简单的字符串转换 */
static void int_to_str(int n, char *buf)
{
    int i = 0;
    int negative = 0;
    char temp[16];

    if (n < 0) {
        negative = 1;
        n = -n;
    }

    if (n == 0) {
        buf[i++] = '0';
    } else {
        while (n > 0) {
            temp[i++] = '0' + (n % 10);
            n /= 10;
        }
    }

    int j = 0;
    if (negative) buf[j++] = '-';
    while (i > 0) {
        buf[j++] = temp[--i];
    }
    buf[j] = 0;
}

/* 打印字符串 */
static void print_str(const char *s)
{
    int len = 0;
    while (s[len]) len++;
    hepu_write(1, s, len);
}

/* 打印整数 */
static void print_int(int n)
{
    char buf[16];
    int_to_str(n, buf);
    print_str(buf);
}

/* 主函数 */
int main(int argc, char **argv)
{
    print_str("=== HepuOS Dynamic Linking Demo ===\n");
    print_str("Library version: ");
    print_str(hepu_demo_version());
    print_str("\n\n");

    /* 测试阶乘 */
    print_str("Factorial:\n");
    for (int i = 0; i <= 10; i++) {
        print_str("  ");
        print_int(i);
        print_str("! = ");
        print_int(hepu_demo_factorial(i));
        print_str("\n");
    }

    /* 测试斐波那契 */
    print_str("\nFibonacci:\n");
    for (int i = 0; i <= 15; i++) {
        print_str("  fib(");
        print_int(i);
        print_str(") = ");
        print_int(hepu_demo_fibonacci(i));
        print_str("\n");
    }

    /* 测试字符串长度 */
    print_str("\nString length:\n");
    const char *test_str = "Hello, HepuOS!";
    print_str("  \"");
    print_str(test_str);
    print_str("\" length = ");
    print_int(hepu_demo_strlen(test_str));
    print_str("\n");

    print_str("\n=== Demo Complete ===\n");
    hepu_exit(0);
    return 0;
}
