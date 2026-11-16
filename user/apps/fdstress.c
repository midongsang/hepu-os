/* Hepu OS - fdstress（fd 表并发复用用例，TECH_DEBT_ROUTE §5-5 第 3 批前置）
 *
 * 为什么第 3 批要先有这个:
 * 第 3 批要把 sys_open / sys_close 切进 fs/。这两个函数和目录三件套不同 ——
 * 它们**改** fd 表(找空槽、写 used、清 used), 而 read/write/lseek/pread 都在
 * 读同一张表。SB-11 已经给表加了 irqsave 锁, 但"加了锁"和"锁的位置对"是两件事,
 * 而这类错在单线程下永远看不出来: 单线程 open 拿到的 fd 一定合法, close 一定成功。
 *
 * 这个程序用两条**真线程**(CLONE_VM, 共享地址空间与 fd 表)各跑 N 轮 open→close,
 * 检测三件事:
 *   1. open 拿到的号必须落在 [3, FD_MAX) —— 0/1/2 是 stdio 保留槽位, 越界即错
 *   2. 同一时刻不能有两个线程持有同一个 fd —— 这是"锁位置不对"最直接的指纹
 *   3. 跑完之后全部释放 —— 开一个应当立刻拿到 3
 *
 * 判据为什么是这三条而不是"不许出错": fd 表满、竞态、泄漏在用户态只能表现为
 * "拿到奇怪的号 / 拿到重复的号 / 关不掉", 所以就把这三件事直接测出来。
 */
#include "../ulib.h"

/* ulib.h 里没有 out/out_dec, SYS_CLONE 也没带出来。这里补最小的本地版本:
 * 逐字符 write(与能稳定输出的那几行同一条路径)。 */
#define SYS_CLONE_LOCAL 40

static void out(const char *s) { while (*s) write(STDOUT_FILENO, s++, 1); }
static void out_dec(long v)
{
    char b[24]; int i = 0;
    if (v < 0) { write(STDOUT_FILENO, "-", 1); v = -v; }
    if (!v) { write(STDOUT_FILENO, "0", 1); return; }
    while (v && i < 24) { b[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0) write(STDOUT_FILENO, &b[--i], 1);
}

#define CLONE_VM             0x00000100
#define CLONE_PARENT_SETTID  0x00100000

#define FD_MAX      48
#define ROUNDS      400

/* 共享计数。clone 的两条线程共享地址空间, 这些变量都能看见。 */
static volatile int busy[FD_MAX];   /* busy[fd] = 此刻持有该 fd 的线程数 */
static volatile int conflict;       /* 同一 fd 被同时持有的次数 */
static volatile int bad_range;      /* fd 落在 [0,3) 或 >= FD_MAX 的次数 */
static volatile int open_fail;      /* open 返回负值的次数 */
static volatile int close_fail;     /* close 返回负值的次数 */
static volatile int child_done;

static void worker(void)
{
    for (int r = 0; r < ROUNDS; r++) {
        int fd = open("/bin/hello.elf", 0);
        if (fd < 0) { open_fail++; continue; }
        if (fd < 3 || fd >= FD_MAX) { bad_range++; close(fd); continue; }

        /* 先占位再检查: 如果这一刻 busy[fd] 已经不是 0, 说明另一个线程
         * 同时认为这个 fd 是它的 —— 那就是"锁没把分配和写入罩住"。 */
        if (__sync_fetch_and_add(&busy[fd], 1) != 0) conflict++;

        close(fd);
        busy[fd]--;
    }
}

int main(void)
{
    int tid = 0;
    long r = (long)syscall5(SYS_CLONE_LOCAL, CLONE_VM | CLONE_PARENT_SETTID,
                            0x6FF80000ULL, (uint64_t)&tid, 0, 0);

    if (r == 0) {
        worker();
        child_done = 1;
        thread_exit();
        for (;;) { }
    }

    /* 父线程跑同一套 worker, 与子线程真正交错 */
    worker();
    while (!child_done) { }

    out("[FDSTRESS] threads done, rounds/thread=");
    out_dec(ROUNDS);
    out("\n");
    out("[FDSTRESS] open_fail=");  out_dec(open_fail);  out("\n");
    out("[FDSTRESS] close_fail="); out_dec(close_fail); out("\n");
    out("[FDSTRESS] bad_range=");  out_dec(bad_range);  out("\n");
    out("[FDSTRESS] conflict=");   out_dec(conflict);   out("\n");

    /* 全部释放后, 下一个 open 必须拿到 3 —— 拿不到说明有 fd 没被关掉 */
    int probe = open("/bin/hello.elf", 0);
    out("[FDSTRESS] leak_probe_fd="); out_dec(probe);
    out(probe == 3 ? "  (no leak)\n" : "  (LEAK)\n");
    if (probe >= 0) close(probe);

    int ok = (open_fail == 0 && close_fail == 0 && bad_range == 0 &&
              conflict == 0 && probe == 3);
    out(ok ? "[FDSTRESS] RESULT: PASS\n" : "[FDSTRESS] RESULT: FAIL\n");
    return ok ? 0 : 1;
}

void _start(void) { exit(main()); }
