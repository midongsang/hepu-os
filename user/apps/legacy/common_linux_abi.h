/* ============================================================================
 * HepuOS —— 历史测试程序的 Linux ABI 公共头（SEC-12 补源码）
 * ----------------------------------------------------------------------------
 * 背景：v0.33 之前，user/build/ 下有一批**没有源码**的历史测试二进制
 *       （hello_static / hello_linux / hello2 / hello3 / hello4 / hello_c2 /
 *         dynhello / dynhello2 / glibc_test / hello_libc / libtest / libtest2）。
 *       它们散落在镜像里，构建不可重现 —— 改一个字节都没法重做。
 *
 *       逆向还原后发现：这些程序**全部使用 Linux x86-64 系统调用号**
 *       （write=1 / open=2 / getpid=39 / exit=60 ...），
 *       但其中一批是手写的裸 syscall 程序，**没有 .note.ABI-tag**，
 *       于是被内核 elf64_is_linux_abi() 判成「Hepu 原生 ABI」，
 *       Linux 号全部打错 —— 典型表现是 exit(60) 落到原生表里没有的号，
 *       进程不肯退出，执行流一路跑到后面的字符串数据上去。
 *
 *       本头给这批程序提供：
 *         ① 最小 NT_GNU_ABI_TAG note —— 让 ABI 判定与实际使用的号表一致；
 *         ② 裸 syscall 封装（这些程序不链接 libc）。
 *
 *       参考：user/apps/comptest.c 里同样的 note 写法（V0.33 缺陷 6 修复）。
 * ========================================================================== */
#ifndef HEPU_LEGACY_LINUX_ABI_H
#define HEPU_LEGACY_LINUX_ABI_H

/* --- Linux x86-64 系统调用号（只用到的这几个） ------------------------- */
#define HEPU_LSYS_write        1
#define HEPU_LSYS_open         2
#define HEPU_LSYS_close        3
#define HEPU_LSYS_read         0
#define HEPU_LSYS_getpid       39
#define HEPU_LSYS_exit        60

/* --- GNU/Linux ABI 标记 ------------------------------------------------
 * 内核 elf64_is_linux_abi() 依据 PT_NOTE 里的 NT_GNU_ABI_TAG 判定
 * 一个 ELF 该走哪套系统调用号表（见 kernel/arch/x86_64/elf64.c）。
 * glibc 链接出来的程序自带这个 note；手写裸 syscall 程序没有，
 * 就会在兼容层上打错号 —— 这正是 V0.33 缺陷 6 的根因。 */
#define HEPU_LINUX_ABI_NOTE                                                   \
    __asm__(                                                                  \
        ".section .note.ABI-tag,\"a\",@note\n"                                  \
        ".align 4\n"                                                          \
        ".long 4\n"                 /* namesz = "GNU\0" */                     \
        ".long 4\n"                 /* descsz = 最低 glibc 版本槽 */            \
        ".long 1\n"                 /* type  = NT_GNU_ABI_TAG */                \
        ".asciz \"GNU\"\n"                                                    \
        ".long 0\n"                 /* desc: 最低版本 0 */                      \
        ".previous\n"                                                             \
    );

/* --- 裸 syscall 封装 --------------------------------------------------- */
static inline long hepu_lsc(long n, long a, long b, long c, long d,
                            long e, long f)
{
    long r;
    register long r10 __asm__("r10") = d;
    register long r8  __asm__("r8")  = e;
    register long r9  __asm__("r9")  = f;
    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return r;
}

static inline long hepu_lwrite(const char *s, long n)
{
    return hepu_lsc(HEPU_LSYS_write, 1, (long)s, n, 0, 0, 0);
}

static inline long hepu_lstrlen(const char *s)
{
    long n = 0;
    while (s[n]) n++;
    return n;
}

/* 打印字符串（不含换行） */
static inline void hepu_puts(const char *s)
{
    hepu_lwrite(s, hepu_lstrlen(s));
}

/* 打印十进制数 */
static inline void hepu_putnum(long v)
{
    char b[24];
    int i = 0;
    unsigned long u;

    if (v < 0) {
        hepu_puts("-");
        u = (unsigned long)(-(v + 1)) + 1UL;   /* 避免 INT64_MIN 溢出 */
    } else {
        u = (unsigned long)v;
    }
    if (u == 0) {
        hepu_puts("0");
        return;
    }
    while (u > 0) {
        b[i++] = (char)('0' + (u % 10));
        u /= 10;
    }
    while (i > 0) {
        char t = b[--i];
        hepu_lwrite(&t, 1);
    }
}

#endif /* HEPU_LEGACY_LINUX_ABI_H */
