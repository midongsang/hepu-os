/* ============================================================================
 * dynhello —— 历史测试程序（SEC-12 补源码）
 *
 * 逆向自 user/build/dynhello.elf：
 *   DT_NEEDED = /tmp/libtest.so（构建时的临时路径），.interp = /lib64/ld-linux-x86-64.so.2
 *   字符串 "dynhello: test_add OK"
 *   即：链接 libtest.so，调用 test_add，校验结果后打印这行。
 *
 * 注意：HepuOS 当前不支持 PT_INTERP 动态链接器加载，
 *       本程序在 HepuOS 上会被 exec 拒绝 —— 保留源码是为了让
 *       「历史测试资产」可从源码重建，而不是为了在内核上跑通。
 * ========================================================================== */
#include <stdio.h>

extern int test_add(int a, int b);

int main(void)
{
    int r = test_add(20, 22);

    if (r == 42) {
        printf("dynhello: test_add OK\n");
        return 0;
    }
    printf("dynhello: test_add FAILED (%d)\n", r);
    return 1;
}
