/* ============================================================================
 * 赫普OS - 计算器应用
 * 支持加减乘除、取模、幂运算
 * ========================================================================== */

#include <hepu/syscall.h>
#include <hepu/stdio.h>
#include <hepu/string.h>

static int calc_power(int base, int exp)
{
    int result = 1;
    for (int i = 0; i < exp; i++) result *= base;
    return result;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("========================================\n");
    printf("      赫普OS 计算器\n");
    printf("========================================\n\n");

    printf("用法: 输入表达式如 \"12 + 5\"\n");
    printf("支持运算符: + - * / %% ^(幂)\n");
    printf("输入 'q' 退出\n\n");

    /* 预设计算演示 */
    int a = 15, b = 4;
    printf("--- 演示计算 ---\n");
    printf("  %d + %d = %d\n", a, b, a + b);
    printf("  %d - %d = %d\n", a, b, a - b);
    printf("  %d * %d = %d\n", a, b, a * b);
    printf("  %d / %d = %d (余%d)\n", a, b, a / b, a % b);
    printf("  %d ^ %d = %d\n", a, 2, calc_power(a, 2));
    printf("  2 ^ 10 = %d\n", calc_power(2, 10));

    printf("\n--- 实用计算 ---\n");
    printf("  圆面积(半径5): %d (PI=3近似)\n", 3 * 5 * 5);
    printf("  温度转换 100C = %dF\n", 100 * 9 / 5 + 32);
    printf("  温度转换 77F = %dC\n", (77 - 32) * 5 / 9);
    printf("  百分比 37/50 = %d%%\n", 37 * 100 / 50);
    printf("  折扣 原价999 打8折 = %d\n", 999 * 8 / 10);

    printf("\n========================================\n");
    printf("  计算器运行完毕\n");
    printf("========================================\n");

    return 0;
}
