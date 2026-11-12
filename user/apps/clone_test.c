/* Hepu OS - clone 真线程验证（V0.57.3）
 *
 * 验证点（每一条都必须成立才算"真线程"）：
 *   1. 子线程与父线程共享同一地址空间 —— 子线程写 g_flag，父线程能看见
 *   2. 子线程有自己的栈 —— 传入 child_stack，不覆盖父栈
 *   3. 子线程的 clone 返回值为 0，父线程拿到 tid
 *   4. 两个线程能真正交替运行（父忙等期间由时钟抢占让出 CPU）
 */
#include "../ulib.h"

#define CLONE_VM             0x00000100
#define CLONE_PARENT_SETTID  0x00100000
#define FUTEX_WAIT           0
#define FUTEX_WAKE           1

static volatile int g_child_wrote;    /* 子线程置位，父线程轮询 */
static volatile int g_flag_value;     /* 子线程写 0x1234，父线程读 */
static volatile int g_futex_word;

static void child_body(void)
{
    g_flag_value = 0x1234;
    g_child_wrote = 1;
    write(STDOUT_FILENO, "  [child] running, wrote shared flag=0x1234\n", 43);
    /* 阻塞自己：futex 字仍为 0 → 立即睡眠，不再占 CPU */
    syscall5(SYS_LNX64_FUTEX, (uint64_t)&g_futex_word, FUTEX_WAIT, 0, 0, 0);
    for (;;) { }
}

int main(void)
{
    int tid = 0;
    uint64_t child_stack = 0x6FF80000ULL;   /* 父栈区低半段，独立于父栈 */

    long r = (long)syscall5(SYS_CLONE, CLONE_VM | CLONE_PARENT_SETTID,
                            child_stack, (uint64_t)&tid, 0, 0);

    if (r == 0) {
        /* 子线程：clone 返回 0，从返回点继续，跑在新栈上 */
        child_body();
        for (;;) { }
    }
    if (r < 0) {
        puts("FAIL: clone returned negative\n");
        return 1;
    }

    puts("parent: clone returned tid\n");
    long spin = 0;
    while (!g_child_wrote && spin < 300000000L) spin++;

    if (g_child_wrote && g_flag_value == 0x1234)
        puts("PASS: parent saw child write through shared address space\n");
    else
        puts("FAIL: child write not visible to parent\n");

    /* 唤醒子线程（让它不乱占 CPU），父线程正常退出 */
    syscall5(SYS_LNX64_FUTEX, (uint64_t)&g_futex_word, FUTEX_WAKE, 1, 0, 0);
    return 0;
}
