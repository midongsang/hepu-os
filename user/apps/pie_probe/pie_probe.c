/*
 * pie_probe.c —— 无 libc 的 freestanding static-PIE 自检程序
 *
 * 目的（V0.33）：
 *   glibc 的 static-PIE 在 Hepu 上启动失败，原因纠缠在 glibc 自身的
 *   TLS/自重定位逻辑里，没法一眼看清。本程序只依赖 Linux ABI 的
 *   write/exit 两个系统调用，把「内核到底把什么交给了用户态」
 *   原原本本打印出来：
 *     - 初始栈上的 argc / argv / envp / auxv
 *     - AT_PHDR / AT_ENTRY / AT_BASE / AT_PHNUM / AT_PHENT / AT_EXECFN
 *     - 自己的运行时地址（验证加载偏置是否生效）
 *
 *   如果这些都对，说明内核侧的 PIE 加载（偏置 + RELATIVE 重定位 + 初始栈）
 *   已经是正确的，剩下的问题就确定在 glibc 兼容层，而不是内核加载器。
 *
 * 构建：gcc -static-pie -nostdlib -fno-stack-protector -O2 \
 *            -Wl,-e,_start -o pie_probe.elf pie_probe.c
 */

typedef unsigned long u64;
typedef long i64;

#define AT_NULL   0
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_BASE   7
#define AT_ENTRY  9
#define AT_EXECFN 31

static inline i64 sys_write(int fd, const void *buf, u64 n)
{
    i64 ret;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "a"(1L), "D"((i64)fd), "S"(buf), "d"(n)
                      : "rcx", "r11", "memory");
    return ret;
}

static inline void sys_exit(int code)
{
    __asm__ volatile ("syscall" :: "a"(60L), "D"((i64)code));
    __builtin_unreachable();
}

static u64 slen(const char *s)
{
    u64 n = 0;
    while (s[n]) n++;
    return n;
}

static void puts_(const char *s) { sys_write(1, s, slen(s)); }

static void puthex(u64 v)
{
    static const char hexd[] = "0123456789ABCDEF";
    char b[19];
    b[0] = '0'; b[1] = 'x';
    for (int i = 0; i < 16; i++) b[2 + i] = hexd[(v >> (60 - 4 * i)) & 0xF];
    b[18] = '\n';
    sys_write(1, b, 19);
}

static void putdec(u64 v)
{
    char b[24];
    int i = 0;
    if (!v) b[i++] = '0';
    while (v) { b[i++] = (char)('0' + (v % 10)); v /= 10; }
    char o[24];
    int j = 0;
    while (i) o[j++] = b[--i];
    o[j] = '\n';
    sys_write(1, o, (u64)j + 1);
}

/* 用来验证加载偏置：这是一个带初值的静态数据，它的地址应该是 偏置+链接地址 */
static u64 marker = 0x1234567890ABCDEFULL;

/* _start 必须拿到内核给的那份 RSP 的**原始值**。
 * 普通 C 入口会先生成序言（push rbp / sub rsp），
 * 于是 rsp 已经偏移，读到的 argc/auxv 全是错位的垃圾。
 * 所以用 naked 入口 + 汇编把原始 RSP 传进 C 函数。 */
void probe_main(u64 *sp);

__attribute__((naked)) void _start(void)
{
    __asm__ volatile(
        "mov %rsp, %rdi\n\t"
        "call probe_main\n\t"
        "hlt\n");
}

void probe_main(u64 *sp)
{
    puts_("[PIEPROBE] ---- start ----\n");

    u64 argc = sp[0];
    puts_("[PIEPROBE] argc=");
    putdec(argc);

    /* argv[] 之后是 NULL，再之后是 envp[]，再之后是 auxv[] */
    u64 *p = &sp[1];
    puts_("[PIEPROBE] argv[0]=");
    puthex((u64)(unsigned long)p[0]);
    while (*p) p++;          /* 跳过 argv[] */
    p++;                     /* 跳过 argv 的 NULL */
    puts_("[PIEPROBE] envp[0]=");
    puthex((u64)(unsigned long)p[0]);
    while (*p) p++;          /* 跳过 envp[] */
    p++;                     /* 跳过 envp 的 NULL */

    /* auxv */
    u64 at_phdr = 0, at_entry = 0, at_base = 0, at_phnum = 0, at_phent = 0, at_execfn = 0;
    int naux = 0;
    while (p[0] != AT_NULL) {
        u64 tag = p[0], val = p[1];
        switch (tag) {
        case AT_PHDR:   at_phdr = val;   break;
        case AT_ENTRY:  at_entry = val;  break;
        case AT_BASE:   at_base = val;   break;
        case AT_PHNUM:  at_phnum = val;  break;
        case AT_PHENT:  at_phent = val;  break;
        case AT_EXECFN: at_execfn = val; break;
        default: break;
        }
        naux++;
        p += 2;
    }
    puts_("[PIEPROBE] auxv count=");
    putdec((u64)naux);
    puts_("[PIEPROBE] AT_PHDR=");   puthex(at_phdr);
    puts_("[PIEPROBE] AT_ENTRY=");  puthex(at_entry);
    puts_("[PIEPROBE] AT_BASE=");   puthex(at_base);
    puts_("[PIEPROBE] AT_PHNUM=");  puthex(at_phnum);
    puts_("[PIEPROBE] AT_PHENT=");  puthex(at_phent);
    puts_("[PIEPROBE] AT_EXECFN="); puthex(at_execfn);

    /* 运行时地址：静态数据的地址 = 加载偏置 + 链接地址 */
    puts_("[PIEPROBE] &marker=");    puthex((u64)(unsigned long)&marker);
    puts_("[PIEPROBE] marker value=");puthex(marker);

    /* 代码地址：用 PC 相对寻址拿到 _start 的运行时地址 */
    {
        u64 here = (u64)(unsigned long)&&here_label;
    here_label:
        puts_("[PIEPROBE] code addr=");
        puthex(here);
    }

    puts_("[PIEPROBE] ---- done ----\n");
    sys_exit(0);
}
