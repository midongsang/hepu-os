/* ============================================================================
 * HepuOS 64位 - fork 端到端测试
 * 验证：fork 返回值区分父子、父子写互不影响、子进程独立地址空间
 * ========================================================================== */
#include "ulib.h"

static int g_shared;   /* 数据段变量：父子应各有副本 */

void _start(void)
{
    puts("");
    puts("=== fork test start ===");
    g_shared = 1111;

    int pid = fork();
    if (pid < 0) {
        puts("FORK: FAILED (returned <0)");
        exit(1);
    }

    if (pid == 0) {
        /* 子进程 */
        printf("CHILD: my pid=%d, g_shared before=%d\n", getpid(), g_shared);
        g_shared = 2222;                 /* 改自己副本 */
        printf("CHILD: wrote g_shared=%d, exiting\n", g_shared);
        exit(7);
    } else {
        /* 父进程 */
        int status = -1;
        int wpid = waitpid(pid, &status, 0);
        printf("PARENT: waitpid returned pid=%d, status=%d\n", wpid, status);
        printf("PARENT: child pid=%d, g_shared=%d (expect 1111, isolated)\n",
               pid, g_shared);
        if (g_shared == 1111)
            puts("FORK: OK - parent data untouched by child");
        else
            puts("FORK: BUG - parent g_shared corrupted!");
        if (wpid == pid && status == 7)
            puts("WAITPID: OK - child exit code 7 received");
        else
            puts("WAITPID: BUG - wrong pid or exit code");
        puts("=== fork test done ===");
        exit(0);
    }
}
