/* ============================================================================
 * 赫普OS 64位 - 用户地址访问安全测试
 * 验证 user_access64.c 修复:
 *   1) 合法用户地址写正常
 *   2) 内核地址(0x100000)被拒绝, 不能借系统调用读内核内存
 *   3) 用户窗口内未映射地址被优雅拒绝, 不再触发缺页 hlt 死机
 * ========================================================================== */

#include "ulib.h"

void _start(void)
{
    const char ok[] = "SEC-OK valid user write\n";
    char *kernel   = (char *)0x100000;    /* 内核装载处 */
    char *unmapped = (char *)0x50000000;  /* 用户窗口内、通常未映射 */

    int r;

    r = write(1, ok, (int)sizeof(ok) - 1);
    printf("valid    write ret=%d (expect %d)\n", r, (int)sizeof(ok) - 1);

    r = write(1, kernel, 16);
    printf("kernel   write ret=%d (expect -1)\n", r);

    r = write(1, unmapped, 16);
    printf("unmapped write ret=%d (expect -1, no crash)\n", r);

    puts("SEC-TEST DONE");
    exit(0);
}
