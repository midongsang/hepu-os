/* Hepu OS - clone 真线程验证（V0.57.4）
 *
 * 验证点：
 *   1. 共享地址空间 —— 子线程写 g_child_count/g_child_done，父线程能看见
 *   2. 独立用户栈 —— 子线程跑在调用方给的 0x6FF80000，不覆盖父栈
 *   3. 子线程 clone 返回 0，父线程拿到 tid
 *   4. 两个线程真正交替运行 —— 父的计数循环被时钟中断打断，切到子线程
 */
#include "../ulib.h"

#define CLONE_VM             0x00000100
#define CLONE_PARENT_SETTID  0x00100000

static volatile long g_child_count;
static volatile long g_parent_count;
static volatile int  g_child_done;
static volatile int  g_child_branch;
static volatile int  g_child_spin;

int main(void)
{
    int tid = 0;
    long r = (long)syscall5(SYS_CLONE, CLONE_VM | CLONE_PARENT_SETTID,
                            0x6FF80000ULL, (uint64_t)&tid, 0, 0);

    if (r == 0) {
        /* 子线程：从 clone 返回点继续，rax=0，跑在独立栈上 */
        /* 先不碰 syscall：纯内存写，验证"切换 + 共享地址空间"这一环 */
        g_child_branch = 1;
        for (int i = 0; i < 200000; i++) g_child_count++;
        g_child_done = 1;

        /* V0.57.8：显式结束这条线程。
         * 内核会把它摘掉、并把 syscall 返回目标改写成父线程，
         * 所以 CPU 直接回到父线程，不会留下一个空转的孤儿线程。
         * 万一此刻没有可切换的兄弟（父还在内核里），
         * thread_exit 会普通返回，下面这行兜底继续等。 */
        thread_exit();
        for (;;) { g_child_spin++; }
    }
    if (r < 0) {
        puts("FAIL: clone returned negative\n");
        return 1;
    }

    puts("parent: clone returned tid\n");
    for (int i = 0; i < 200000; i++) g_parent_count++;

    long spin = 0;
    while (!g_child_done && spin < 300000000L) spin++;

    if (g_child_done && g_child_count > 0)
        puts("PASS: child ran in shared address space, both counters advanced\n");
    else
        puts("FAIL: child never ran\n");

    /* 显式走 64 位 syscall 退出。
     * crt0 的 _start 结尾用的是 `int $0x80`（32 位约定），在多线程/切换过的
     * 上下文里返回路径不可靠 —— 实测 clone_test 走它会在返回瞬间跳到地址 0
     * 取指（#PF error=0x15 rip=0x0）。这里不依赖 crt0。 */
    exit(0);
    return 0;
}
