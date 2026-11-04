/* HepuOS compat test: exercises geteuid/getegid/clock_gettime/getpid */
#include <stdint.h>

/* V0.33: 打上 GNU/Linux ABI 标记。
 *
 * 内核用 elf64_is_linux_abi() 判定一个 ELF 该走哪套系统调用号表，
 * 依据之一就是 PT_NOTE 里的 NT_GNU_ABI_TAG（见 kernel/arch/x86_64/elf64.c）。
 * glibc 链接出来的程序自带 .note.ABI-tag，所以 glibc_test.elf 判定正确；
 * 而本程序是手写的裸 syscall 程序，没有 libc 就没有那个 note，
 * 结果被误判成「Hepu 原生 ABI」，Linux 号全部打错（pid/uid 全 0、
 * exit(60) 变成 Unknown syscall），跑飞到野地址。
 *
 * 这里补一个最小 NT_GNU_ABI_TAG note，让 ABI 判定与实际用的号表一致。 */
__asm__(
    ".section .note.ABI-tag,\"a\",@note\n"
    ".align 4\n"
    ".long 4\n"                 /* namesz = "GNU\0" */
    ".long 4\n"                 /* descsz = 最低 glibc 版本槽 */
    ".long 1\n"                 /* type  = NT_GNU_ABI_TAG */
    ".asciz \"GNU\"\n"
    ".long 0\n"                 /* desc: 最低版本 0 */
    ".previous\n"
);

static long sc(long n, long a, long b, long c, long d, long e, long f) {
    long r;
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx","r11","memory");
    return r;
}

static void p(const char *s) { long n=0; while(s[n])n++; sc(1,1,(long)s,n,0,0,0); }
static void pi(long v) {
    char b[16]; int i=0;
    if(v<0){p("-");v=-v;}
    if(v==0){p("0");return;}
    while(v>0){b[i++]='0'+v%10;v/=10;}
    for(int j=0;j<i/2;j++){char t=b[j];b[j]=b[i-1-j];b[i-1-j]=t;}
    b[i]=0; p(b);
}

void _start(void) {
    /* Linux syscall numbers */
    long pid = sc(39,0,0,0,0,0,0);
    long uid = sc(102,0,0,0,0,0,0);
    long euid = sc(107,0,0,0,0,0,0);
    long gid = sc(104,0,0,0,0,0,0);
    long egid = sc(108,0,0,0,0,0,0);

    p("pid="); pi(pid); p(" uid="); pi(uid);
    p(" euid="); pi(euid); p(" gid="); pi(gid);
    p(" egid="); pi(egid); p("\n");

    /* clock_gettime(CLOCK_REALTIME=0) */
    long ts[2] = {0,0};
    long r = sc(228, 0, (long)ts, 0,0,0,0);
    p("clock_gettime ret="); pi(r);
    p(" sec="); pi(ts[0]); p(" nsec="); pi(ts[1]); p("\n");

    p("OK\n");
    sc(60,0,0,0,0,0,0);
}
