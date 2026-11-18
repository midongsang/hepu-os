/* ============================================================================
 * 赫普OS - 第一个C语言用户态应用
 * 使用 libhepu C库，通过系统调用与内核交互
 * ========================================================================== */

#include <hepu/syscall.h>
#include <hepu/stdio.h>
#include <hepu/string.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("=== 赫普OS C语言应用 ===\n");
    printf("你好！这是用C语言编写的用户态程序。\n");
    printf("PID: %d\n", hepu_getpid());
    /* sys_hepu_info 返回编码版本号 (major<<24|minor<<16|patch<<8)，不是字符串 */
    { unsigned int _v = (unsigned int)hepu_info();
      printf("内核版本: %u.%u.%u\n", (_v>>24)&0xFFu, (_v>>16)&0xFFu, (_v>>8)&0xFFu); }
    printf("printf测试: %d + %d = %d, 十六进制=0x%x\n",
           3, 4, 3 + 4, 255);
    printf("字符串: %s, 长度=%d\n", "Hello HEPU", strlen("Hello HEPU"));

    /* 测试内存分配 */
    char *buf = hepu_malloc(64);
    if (buf) {
        strcpy(buf, "动态内存分配成功！");
        printf("%s\n", buf);
        hepu_free(buf);
    }

    printf("C语言应用执行完毕，即将退出。\n");

    return 0;
}
