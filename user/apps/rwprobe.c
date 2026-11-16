/* Hepu OS - rwprobe（read/write 语义用例，TECH_DEBT_ROUTE §5-5 第 5 批前置）
 *
 * 这批要把 sys_read / sys_write 从 syscall64_full.c 切进 fs_rw_sys.c。
 * 它们和前三批不一样: 前三批切的是"分发"(拿 fd -> 查表 -> 调 ext2), 这批
 * 牵着三样不属于分发层的东西 ——
 *
 *   1. **fd offset 的读改回写**: read 之后 offset 要 += len, 且必须在 g_fd_lock
 *      内完成。切出去以后如果只搬业务、忘了把这步也通过边界 API 走, 表现是
 *      "连续读两次拿到同一段内容", 而单次读的回归完全看不出来。
 *   2. **管道 IPC**: is_pipe 分支里是 PIPE_BUF_SIZE 环形缓冲 + g_pipe_lock。
 *      管道是内核子系统, 不是文件分发。
 *   3. **stdin/stdout 特判**: fd 0 读键盘(with hlt 等中断), fd 1/2 写串口逐字节。
 *
 * 所以先有这份用例。判据全部来自现有实现的行为, 不是自创:
 *   - count <= 0        -> 0
 *   - count > 4096      -> 截断到 4096
 *   - 只读打开(fmode=0) -> write 返回 -1
 *   - 只写打开(fmode=1) -> read 返回 -1
 *   - offset < 0        -> read 返回 -1（V0.59 前的越界读修复, 现在是显式拒绝）
 *   - EOF               -> read 返回 0
 *   - 管道写满/读空     -> 不覆盖、不越界
 *
 * 为什么这么测 offset:
 *   只测"读一次有数据"证明不了 offset 回写。这里用 seek(0) -> 读 64 -> 再读 64
 *   -> 断言两段内容不同(offset 真的前进了); 再 seek(0) 重读 -> 断言与第一次
 *   逐字节相同(seek 真的生效)。这三步合起来才能卡住"搬家时把 += len 丢了"。
 */
#include "../ulib.h"

/* 注意: 这里是 Hepu **原生**号, 不是 Linux 号。
 * SYS_READ=9 / SYS_WRITE=10（Linux 的 0/1 在这套表里不是 read/write, 踩过一次:
 * 用 0/1 调用会命中空表项, 内核打 "Unimplemented syscall: 0" 并返回 0 ——
 * 看起来像系统坏了, 其实是探针自己传错了号）。 */
#define SYS_READ_L   9
#define SYS_WRITE_L  10
#define SYS_LSEEK_L  14
#define SYS_PIPE_L   16
#define SYS_OPEN_L   8
#define SYS_CLOSE_L  11

#define O_RDONLY 0
#define O_WRONLY 1

#define SEEK_SET 0

/* 目标文件: 镜像里一定存在且足够大, 前 128 字节不会重复 */
#define TARGET "/usr/share/wallpapers/wallpaper.png"

#define SZ 64

static void out(const char *s) { while (*s) write(STDOUT_FILENO, s++, 1); }
static void out_dec(long v)
{
    if (v < 0) { out("-"); v = -v; }
    char b[24]; int i = 0;
    if (v == 0) b[i++] = '0';
    while (v > 0) { b[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0) write(STDOUT_FILENO, &b[--i], 1);
}

static int check(const char *what, int ok, long detail)
{
    out(ok ? "  ok   " : "  FAIL ");
    out(what);
    if (detail != -2) { out(" = "); out_dec(detail); }
    out("\n");
    return ok ? 0 : 1;
}

static int same(const unsigned char *a, const unsigned char *b, int n)
{
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

static void syscall3v(unsigned long *r, long nr, long a, long b, long c)
{
    register unsigned long ret asm("rax");
    asm volatile("syscall" : "=a"(ret) : "a"(nr), "D"(a), "S"(b), "d"(c)
                 : "rcx", "r11", "memory");
    *r = ret;
}

static long lseek_(int fd, long off, int whence)
{
    unsigned long r;
    syscall3v(&r, SYS_LSEEK_L, fd, off, whence);
    return (long)r;
}

static long open_(const char *p, int flags)
{
    unsigned long r;
    syscall3v(&r, SYS_OPEN_L, (long)p, flags, 0);
    return (long)r;
}

static long close_(int fd)
{
    unsigned long r;
    syscall3v(&r, SYS_CLOSE_L, fd, 0, 0);
    return (long)r;
}

static long pipe_(int *fds)
{
    unsigned long r;
    syscall3v(&r, SYS_PIPE_L, (long)fds, 0, 0);
    return (long)r;
}

static long read_off(int fd, void *buf, long n)
{
    unsigned long r;
    syscall3v(&r, SYS_READ_L, fd, (long)buf, n);
    return (long)r;
}

static long write_off(int fd, const void *buf, long n)
{
    unsigned long r;
    syscall3v(&r, SYS_WRITE_L, fd, (long)buf, n);
    return (long)r;
}

int main(void)
{
    int fails = 0;
    unsigned char a[SZ], b[SZ], c[SZ];
    /* 不用 static: 避免 .bss, 与其它 probe 一致 */
    long fd, r1, r2, r3;
    volatile int pfds[2];   /* volatile: 内核用指针写回, 不加会被 -O2 缓存成垃圾值 */

    out("[RW] read/write semantics probe\n");

    /* ---- 1. 打开目标文件 ---- */
    fd = open_(TARGET, O_RDONLY);
    fails += check("open(target, O_RDONLY) >= 0", fd >= 0, fd);
    if (fd < 0) { out("[RW] RESULT: FAIL (open)\n"); return 1; }

    /* ---- 2. seek 到 0 后读 64 字节 ---- */
    fails += check("lseek(fd,0,SEEK_SET) == 0", lseek_(fd, 0, SEEK_SET) == 0, -1);
    r1 = read_off(fd, a, SZ);
    fails += check("read 64 -> r1 > 0", r1 > 0, r1);

    /* ---- 3. 再读 64: 内容必须与第一段不同（证明 offset 前进） ---- */
    r2 = read_off(fd, b, SZ);
    fails += check("read again -> r2 > 0", r2 > 0, r2);
    fails += check("两段内容不同(offset 真的前进)",
                   (r1 > 0 && r2 > 0 && !same(a, b, SZ)), -2);

    /* ---- 4. seek(0) 重读: 必须与第一段逐字节相同（证明 seek 生效） ---- */
    fails += check("lseek(fd,0) again == 0", lseek_(fd, 0, SEEK_SET) == 0, -1);
    r3 = read_off(fd, c, SZ);
    fails += check("seek 后重读内容与原段相同",
                   (r3 > 0 && same(a, c, (int)(r3 < SZ ? r3 : SZ))), -2);

    /* ---- 5. 负偏移: lseek 本身必须拒绝, 且失败不污染 offset ----
     *
     * 一开始这里写的是「lseek(-1) 之后 read < 0」, 跑出来是 FAIL —— 不是系统
     * 的问题, 是判据错了: sys_lseek 在 newpos<0 时**直接返回 -1 且不改 offset**,
     * 所以负偏移根本注入不进 fd 表。sys_read 里那条 offset<0 的检查是防御性的
     * （防 fd 表被别的路径写坏）, 从用户侧构造不出来。
     *
     * 改成测真正可达、真正会出事的那一面: **失败的 seek 不能留副作用**。
     * 如果哪天 lseek 改成「先写回再校验」, offset 被写成 -1, 下一次 read 就会
     * 从 file_buf 之前的内核内存里读出来拷给用户 —— 这条判据能卡住它。 */
    fails += check("lseek(-1,SEEK_SET) < 0", lseek_(fd, -1, SEEK_SET) < 0, -1);
    {
        long r = read_off(fd, b, SZ);
        fails += check("失败的 seek 不留副作用(offset 仍为 0, read 仍满 64)", r == SZ, r);
    }
    lseek_(fd, 0, SEEK_SET);

    /* ---- 6. count 上限: > 4096 要被截断到 4096 ---- */
    {
        /* 用一个 8KB 栈缓冲; 返回必须 <= 4096 */
        unsigned char big[8192];
        long r = read_off(fd, big, 8192);
        fails += check("read 8192 -> 返回 <= 4096", (r >= 0 && r <= 4096), r);
    }

    /* ---- 7. count == 0 返回 0 ---- */
    fails += check("read count=0 -> 0", read_off(fd, b, 0) == 0, -1);

    /* ---- 8. 只读 fd 上写: 必须 -1 ---- */
    fails += check("只读 fd 上 write < 0", write_off(fd, b, 4) < 0, -1);

    close_(fd);

    /* ---- 9. 只写打开后读: 必须 -1 ---- */
    fd = open_(TARGET, O_WRONLY);
    if (fd >= 0) {
        fails += check("只写 fd 上 read < 0", read_off(fd, b, 4) < 0, -1);
        close_(fd);
    } else {
        out("  skip  只写打开失败, 跳过该项 (ext2 可能不允许 O_WRONLY)\n");
    }

    /* ---- 10. 管道: 写 3 字节再读回, 内容必须一致 ---- */
    {
        pfds[0] = -1; pfds[1] = -1;
        long pr = pipe_((int *)pfds);
        fails += check("pipe() >= 0", pr >= 0, pr);
        out("  info  pipe fds = [");
        out_dec(pfds[0]); out(" ,"); out_dec(pfds[1]); out("]\n");
        fails += check("pipe 写回读端 fd >= 3", pfds[0] >= 3, pfds[0]);
        fails += check("pipe 写回写端 fd >= 3", pfds[1] >= 3, pfds[1]);
        if (pr >= 0 && pfds[0] >= 3 && pfds[1] >= 3) {
            const char msg[3] = {'h', 'p', 'u'};
            char got[3] = {0, 0, 0};
            long w = write_off(pfds[1], msg, 3);
            fails += check("pipe write 3 -> 3", w == 3, w);
            long g = read_off(pfds[0], got, 3);
            fails += check("pipe read 3 -> 3", g == 3, g);
            fails += check("管道内容一致", (g == 3 && got[0] == 'h' && got[1] == 'p' && got[2] == 'u'), -2);
            close_(pfds[0]);
            close_(pfds[1]);
        }
    }

    out(fails ? "[RW] RESULT: FAIL\n" : "[RW] RESULT: PASS\n");
    return fails ? 1 : 0;
}

void _start(void) { exit(main()); }
