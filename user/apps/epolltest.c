/* Hepu OS - epoll / eventfd2 / pselect6 最小语义探针（B 段验收用例）
 *
 * 覆盖（原生 ABI，号表来自 kernel/include/syscall.h）：
 *   1. epoll_create 返回合法 fd（>=3）；
 *   2. ADD 管道读端；空管道 epoll_wait(超时) 返回 0；
 *   3. 写入管道后 epoll_wait 返回 1，events 含 EPOLLIN，data 回传正确；
 *   4. DEL 之后不再返回就绪；
 *   5. eventfd2 write 累加 / read 取走并清零；
 *   6. eventfd 注册进 epoll 后可被判为可读；
 *   7. 误用 read/write 打到 epfd 必须返回负值（不得当文件读写）；
 *   8. pselect6 最小读就绪判定（有数据返回 1，空管道超时返回 0）。
 *
 * 输出全部带 [EPOLL] 前缀，便于串口日志抓取。
 */
#include "../ulib.h"

/* 原生号（SYS_LNX64_* 扩展段） */
#define NR_EPOLL_CREATE  SYS_LNX64_EPOLL_CREATE
#define NR_EPOLL_CTL     SYS_LNX64_EPOLL_CTL
#define NR_EPOLL_WAIT    SYS_LNX64_EPOLL_WAIT
#define NR_EVENTFD2      SYS_LNX64_EVENTFD2
#define NR_PSELECT6      SYS_LNX64_PSELECT6

#define EPOLLIN        0x00000001u
#define EPOLL_CTL_ADD  1
#define EPOLL_CTL_DEL  2

struct epoll_event {
    uint32_t events;
    uint32_t pad;
    uint64_t data;
};

static void out(const char *s) { while (*s) write(STDOUT_FILENO, s++, 1); }
static void out_dec(long v)
{
    if (v < 0) { out("-"); v = -v; }
    char b[24]; int i = 0;
    if (v == 0) b[i++] = '0';
    while (v > 0) { b[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0) write(STDOUT_FILENO, &b[--i], 1);
}

static int g_fails = 0;
static void check(const char *what, int ok, long detail)
{
    out(ok ? "  [EPOLL] ok   " : "  [EPOLL] FAIL ");
    out(what);
    if (detail != -2) { out(" = "); out_dec(detail); }
    out("\n");
    if (!ok) g_fails++;
}

static long epoll_create_(int size)
{
    return (long)syscall1(NR_EPOLL_CREATE, size);
}
static long epoll_ctl_(int epfd, int op, int fd, struct epoll_event *ev)
{
    return (long)syscall4(NR_EPOLL_CTL, epfd, op, fd, (uint64_t)ev);
}
static long epoll_wait_(int epfd, struct epoll_event *ev, int max, int timeout)
{
    return (long)syscall4(NR_EPOLL_WAIT, epfd, (uint64_t)ev, max, timeout);
}
static long eventfd2_(int initval, int flags)
{
    return (long)syscall2(NR_EVENTFD2, initval, flags);
}
/* pselect6 有 6 个参数（ulib 只到 syscall5），这里内联 r9 传第 6 参。 */
static inline uint64_t syscall6_(uint64_t nr, uint64_t a1, uint64_t a2,
                                  uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
    register uint64_t ret asm("rax");
    register uint64_t r10 asm("r10") = a4;
    register uint64_t r8  asm("r8")  = a5;
    register uint64_t r9  asm("r9")  = a6;
    asm volatile("syscall" : "=a"(ret)
                 : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
                 : "rcx", "r11", "memory");
    return ret;
}
static long pipe_(int *fds)
{
    return (long)syscall3(SYS_PIPE, (uint64_t)fds, 0, 0);
}

int main(void)
{
    out("[EPOLL] probe start\n");

    /* ---- 1. epoll_create ---- */
    int epfd = (int)epoll_create_(1);
    check("epoll_create 返回 fd >= 3", epfd >= 3, epfd);
    if (epfd < 3) { out("[EPOLL] RESULT: FAIL (create)\n"); return 1; }

    /* ---- 2. 管道 + ADD + 空等待超时 ---- */
    volatile int pfds[2] = { -1, -1 };
    long pr = pipe_((int *)pfds);
    check("pipe() >= 0", pr >= 0, pr);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data = (uint64_t)pfds[0];
    long cr = epoll_ctl_(epfd, EPOLL_CTL_ADD, pfds[0], &ev);
    check("epoll_ctl ADD 管道读端 == 0", cr == 0, cr);

    struct epoll_event out_ev;
    long wr = epoll_wait_(epfd, &out_ev, 1, 100);
    check("空管道 wait(100ms) 超时返回 0", wr == 0, wr);

    /* ---- 3. 写管道后等待就绪 ---- */
    char msg = 'h';
    long w = write(pfds[1], &msg, 1);
    check("pipe write 1 -> 1", w == 1, w);

    wr = epoll_wait_(epfd, &out_ev, 1, 1000);
    check("写后 wait 返回 1", wr == 1, wr);
    check("events 含 EPOLLIN", (wr == 1 && (out_ev.events & EPOLLIN)) != 0,
          (long)out_ev.events);
    check("data 回传管道读端 fd", wr == 1 && out_ev.data == (uint64_t)pfds[0],
          (long)out_ev.data);

    /* 排空管道 */
    char got;
    read(pfds[0], &got, 1);

    /* ---- 4. DEL 后不再就绪 ---- */
    long dr = epoll_ctl_(epfd, EPOLL_CTL_DEL, pfds[0], 0);
    check("epoll_ctl DEL == 0", dr == 0, dr);
    wr = epoll_wait_(epfd, &out_ev, 1, 100);
    check("DEL 后 wait 超时返回 0", wr == 0, wr);

    /* ---- 7. 误用 read/write 打 epfd 必须为负 ---- */
    char x = 'x';
    long wbad = write(epfd, &x, 1);
    check("write(epfd) 返回负值", wbad < 0, wbad);
    long rbad = read(epfd, &x, 1);
    check("read(epfd) 返回负值", rbad < 0, rbad);

    /* ---- 5. eventfd 计数读写 ---- */
    int efd = (int)eventfd2_(0, 0);
    check("eventfd2 返回 fd >= 3", efd >= 3, efd);
    if (efd >= 3) {
        uint64_t val = 5;
        long ew = write(efd, &val, 8);
        check("eventfd write 8 -> 8", ew == 8, ew);
        uint64_t back = 0;
        long er = read(efd, &back, 8);
        check("eventfd read 8 -> 8", er == 8, er);
        check("读回计数 == 5", er == 8 && back == 5, (long)back);
        /* 取走并清零后再读应 < 0（v1 不阻塞读空） */
        er = read(efd, &back, 8);
        check("读空 eventfd 返回负值", er < 0, er);

        /* ---- 6. eventfd 注册进 epoll 后就绪 ---- */
        struct epoll_event ev2;
        ev2.events = EPOLLIN;
        ev2.data = (uint64_t)efd;
        cr = epoll_ctl_(epfd, EPOLL_CTL_ADD, efd, &ev2);
        check("epoll_ctl ADD eventfd == 0", cr == 0, cr);
        val = 3;
        write(efd, &val, 8);
        wr = epoll_wait_(epfd, &out_ev, 1, 1000);
        check("eventfd 就绪 wait 返回 1", wr == 1, wr);
        check("eventfd 就绪 events 含 EPOLLIN",
              wr == 1 && (out_ev.events & EPOLLIN), (long)out_ev.events);
        read(efd, &back, 8);
        close(efd);
    }

    /* ---- 8. pselect6 最小读就绪 ---- */
    {
        /* 重新建一条管道做 pselect 用例 */
        volatile int qfds[2] = { -1, -1 };
        if (pipe_((int *)qfds) >= 0) {
            struct { int64_t sec; int64_t nsec; } ts = { 0, 100000000 }; /* 100ms */
            uint64_t rfds = 1UL << qfds[0];
            /* 空管道，100ms 超时应返回 0 */
            long ps = (long)syscall6_(NR_PSELECT6, qfds[0] + 1, (uint64_t)&rfds,
                                     0, 0, (uint64_t)&ts, 0);
            check("pselect6 空管道超时返回 0", ps == 0, ps);
            char m2 = 'y';
            write(qfds[1], &m2, 1);
            rfds = 1UL << qfds[0];
            ps = (long)syscall6_(NR_PSELECT6, qfds[0] + 1, (uint64_t)&rfds,
                                0, 0, (uint64_t)&ts, 0);
            check("pselect6 有数据返回 1", ps == 1, ps);
            check("pselect6 位图保留读端", ps == 1 && (rfds & (1UL << qfds[0])),
                  (long)rfds);
            read(qfds[0], &m2, 1);
            close(qfds[0]);
            close(qfds[1]);
        }
    }

    close(pfds[0]);
    close(pfds[1]);
    close(epfd);

    out(g_fails ? "[EPOLL] RESULT: FAIL\n" : "[EPOLL] RESULT: PASS\n");
    return g_fails ? 1 : 0;
}

void _start(void) { exit(main()); }
