/* spin.hug - 死循环测试应用 (验证调度器超时强制回收) */
#include <hepu/stdio.h>

int main(void)
{
    printf("spin start (infinite loop)\n");
    /* 故意死循环: 若调度器超时回收生效, 2秒后被强制杀回 Shell */
    for (;;) {
        __asm__ volatile ("pause");
    }
    return 0;
}
