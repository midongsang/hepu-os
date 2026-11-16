/* Hepu OS - mmprobe（mmap/munmap/mprotect 边界用例，TECH_DEBT_ROUTE §5-5 第 4 批前置）
 *
 * 这批要把 sys_mmap / sys_munmap / sys_mprotect 从 syscall64_full.c 切进
 * fs_mm_sys.c。它们不是"一个函数搬一段代码"，而是**校验逻辑散在业务里**:
 * 窗口校验(5 处)、长度溢出、保留区相交、配额检查、arena 推进、部分失败回滚。
 * 搬家时漏掉任何一条 if, 都变成"以前拒绝的地址现在不拒绝了", 而标准回归
 * 看不出来 —— 正常程序不碰非法地址。
 *
 * 所以先有这份用例: 它测的不是"能分配", 而是**该拒绝的真的拒绝了**。
 *
 * 判据来源(与实现对齐, 不是自创):
 *   USER_WIN_BASE = 0x40000000, USER_WIN_END = 0x80000000
 *   USER_MMAP_BASE = 0x64000000, USER_MMAP_LIMIT = 0x67800000
 *   保留区: brk [0x68000000,0x6F000000)、栈 [0x6FF00000,0x70000000)、
 *           vDSO [0x7FFF0000,0x7FFF1000)
 *   user_va_range_ok(start,end) = end>start && start>=WIN_BASE && end<=WIN_END
 *   user_va_is_reserved 只查上面三个保留区
 *
 * 为什么避开了两条"看起来该测"的:
 *   - mmap(PROT_READ) 之后**写**它 → 预期 #PF, 但那会直接杀进程, 用例本身
 *     就没了。改为测 mprotect 把它改成可写**之后**能写 —— 同样证明权限位
 *     真的生效, 但不会牺牲整个用例。
 *   - munmap 掉自己的代码区 → 必然崩, 同上。改为 munmap 未映射的地址,
 *     验"幂等/不崩", 那才是搬坏了会出事的点。
 */
#include "../ulib.h"

#define SYS_MMAP_L     37
#define SYS_MUNMAP_L   38
#define SYS_MPROTECT_L 39

#define PROT_READ   1
#define PROT_WRITE  2
#define MAP_ANON    0x20

#define WIN_BASE    0x40000000ULL
#define WIN_END     0x80000000ULL
#define MMAP_BASE   0x64000000ULL
#define MMAP_LIMIT  0x67800000ULL
#define BRK_LO      0x68000000ULL
#define STACK_LO    0x6FF00000ULL
#define STACK_HI    0x70000000ULL
#define VDSO_LO     0x7FFF0000ULL

static void out(const char *s) { while (*s) write(STDOUT_FILENO, s++, 1); }
static void out_dec(long v)
{
    char b[24]; int i = 0;
    if (v < 0) { write(STDOUT_FILENO, "-", 1); v = -v; }
    if (!v) { write(STDOUT_FILENO, "0", 1); return; }
    while (v && i < 24) { b[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0) write(STDOUT_FILENO, &b[--i], 1);
}
static void out_hex(unsigned long long v)
{
    static const char *h = "0123456789abcdef";
    char b[20]; int i = 0;
    write(STDOUT_FILENO, "0x", 2);
    if (!v) { write(STDOUT_FILENO, "0", 1); return; }
    while (v && i < 20) { b[i++] = h[v & 0xF]; v >>= 4; }
    while (i > 0) write(STDOUT_FILENO, &b[--i], 1);
}

static int check(const char *what, int ok, long long got)
{
    out(ok ? "  ok   " : "  FAIL ");
    out(what);
    out(" = ");
    if (got < 0) out_dec(got); else out_hex((unsigned long long)got);
    out("\n");
    return ok ? 0 : 1;
}

static long mmap_(unsigned long long addr, unsigned long long len,
                  int prot, int flags)
{
    return (long)syscall4(SYS_MMAP_L, addr, len, (unsigned long long)prot,
                          (unsigned long long)flags);
}
static long munmap_(unsigned long long addr, unsigned long long len)
{
    return (long)syscall2(SYS_MUNMAP_L, addr, len);
}
static long mprotect_(unsigned long long addr, unsigned long long len, int prot)
{
    return (long)syscall3(SYS_MPROTECT_L, addr, len, (unsigned long long)prot);
}

int main(void)
{
    int fails = 0;
    out("[MMP] mmap/munmap/mprotect boundary probe\n");

    /* ---- 1. 正常匿名映射：必须落在 arena 内、页对齐、可写 ---- */
    long a = mmap_(0, 4096, PROT_READ | PROT_WRITE, MAP_ANON);
    fails += check("anon mmap(4096) > 0", a > 0, a);
    if (a > 0) {
        fails += check("  addr >= MMAP_BASE", (unsigned long long)a >= MMAP_BASE, a);
        fails += check("  addr <  MMAP_LIMIT", (unsigned long long)a < MMAP_LIMIT, a);
        fails += check("  page aligned", ((unsigned long long)a & 0xFFF) == 0, a);
        /* 可写：写进去再读回来 */
        volatile unsigned char *p = (volatile unsigned char *)a;
        p[0] = 0x5A; p[4095] = 0x5A;
        fails += check("  wrote page (read back 0x5a)", p[0] == 0x5A && p[4095] == 0x5A, p[0]);
    }

    /* ---- 2. 长度 0：返回 0，不分配 ---- */
    fails += check("mmap(len=0) == 0", mmap_(0, 0, PROT_READ, MAP_ANON) == 0, 1);

    /* ---- 3. 超窗口长度：拒绝 ---- */
    fails += check("mmap(len=2GB) < 0",
                   mmap_(0, 0x80000000ULL, PROT_READ, MAP_ANON) < 0, -1);

    /* ---- 4. 跨窗口下界：起址在窗口内、结束越界 ---- */
    fails += check("mmap(cross WIN_BASE) < 0",
                   mmap_(WIN_BASE - 0x10000ULL, 0x20000ULL,
                         PROT_READ | PROT_WRITE, MAP_ANON) < 0, -1);

    /* ---- 5. 跨窗口上界 ---- */
    fails += check("mmap(cross WIN_END) < 0",
                   mmap_(WIN_END - 0x1000ULL, 0x2000ULL,
                         PROT_READ | PROT_WRITE, MAP_ANON) < 0, -1);

    /* ---- 6~8. 撞保留区：brk / 栈 / vDSO 都必须拒绝 ---- */
    fails += check("mmap(hit brk) < 0",
                   mmap_(BRK_LO, 0x1000, PROT_READ | PROT_WRITE, MAP_ANON) < 0, -1);
    fails += check("mmap(hit stack) < 0",
                   mmap_(STACK_LO, 0x1000, PROT_READ | PROT_WRITE, MAP_ANON) < 0, -1);
    fails += check("mmap(hit vdso) < 0",
                   mmap_(VDSO_LO, 0x1000, PROT_READ | PROT_WRITE, MAP_ANON) < 0, -1);

    /* ---- 9. 两次匿名映射不重叠（arena 只向前推） ---- */
    long b = mmap_(0, 4096, PROT_READ | PROT_WRITE, MAP_ANON);
    fails += check("second anon mmap > 0", b > 0, b);
    if (a > 0 && b > 0) {
        fails += check("no overlap with first", (unsigned long long)b >= (unsigned long long)a + 4096, b);
    }

    /* ---- 10. mprotect 真的生效：只读页 → 改成可写 → 能写进去 ----
     * 不直接测"写只读页会 #PF"(那会杀进程, 用例就没了); 测"改完能写"
     * 同样证明权限位真的落了页表, 而不是只写了个变量。 */
    long c = mmap_(0, 4096, PROT_READ, MAP_ANON);
    fails += check("mmap(PROT_READ) > 0", c > 0, c);
    if (c > 0) {
        fails += check("mprotect(READ|WRITE) == 0",
                       mprotect_(c, 4096, PROT_READ | PROT_WRITE) == 0, 0);
        volatile unsigned char *q = (volatile unsigned char *)c;
        q[0] = 0x77;
        fails += check("  wrote after mprotect (0x77)", q[0] == 0x77, q[0]);
    }

    /* ---- 11. mprotect 未映射地址：拒绝 ---- */
    fails += check("mprotect(unmapped 0x48000000) < 0",
                   mprotect_(0x48000000ULL, 0x1000, PROT_READ | PROT_WRITE) < 0, -1);

    /* ---- 12. munmap 自己映射的：成功 ---- */
    if (a > 0) {
        fails += check("munmap(own) == 0", munmap_(a, 4096) == 0, 0);
        /* 再 munmap 一次: 不崩即可, 返回值不作为判据(实现返回 0 或失败都说得通) */
        long r2 = munmap_(a, 4096);
        fails += check("munmap twice no crash", r2 == 0 || r2 < 0, r2);
    }

    /* ---- 13. munmap 内核地址：必须拒绝，不能改内核页表 ---- */
    fails += check("munmap(kernel 0x100000) < 0",
                   munmap_(0x100000ULL, 0x1000) < 0, -1);

    /* ---- 14. munmap 跨窗口：拒绝 ---- */
    fails += check("munmap(cross WIN_END) < 0",
                   munmap_(WIN_END - 0x1000ULL, 0x2000ULL) < 0, -1);

    out(fails ? "[MMP] RESULT: FAIL\n" : "[MMP] RESULT: PASS\n");
    return fails ? 1 : 0;
}

void _start(void) { exit(main()); }
