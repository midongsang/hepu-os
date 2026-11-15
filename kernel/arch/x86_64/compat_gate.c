/* ============================================================================
 * HepuOS 64 位 - 兼容层网关实现
 * 见 compat_gate.h 的说明：这里只放"数据 + 判定"，为将来整体搬到用户态服务准备。
 * ========================================================================== */

#include <stdint.h>
#include "compat_gate.h"
#include "syscall.h"   /* V0.54.6: 统一 syscall 号（唯一真源） */
#include "elf64.h"     /* ENV_LINUX_COMPAT + ELF 解析原语 */

/* Hepu 原生程序区：原生 ELF 链接在这里。链接基址落在区外 = 外来 ABI。 */
#define HEPU_NATIVE_PROG_BASE 0x40000000ULL
#define HEPU_NATIVE_PROG_END  0x50000000ULL

int compat_gate_detect_elf(void *elf_data, uint64_t size)
{
    extern int elf64_verify(void *elf_data);
    extern const char *elf64_find_interp(void *elf_data, uint64_t size);
    extern int elf64_has_gnu_abi_tag(void *elf_data, uint64_t size);
    extern uint64_t elf64_link_base(void *elf_data, uint64_t size);

    if (!elf_data || size < sizeof(elf64_header_t)) return 0;
    if (elf64_verify(elf_data) != 0) return 0;
    elf64_header_t *ehdr = (elf64_header_t*)elf_data;

    /* 1) 带动态链接器 = Linux 风格可执行文件 */
    if (elf64_find_interp(elf_data, size)) return 1;
    /* 2) GNU/Linux ABI 标记（PT_NOTE 里的 NT_GNU_ABI_TAG） */
    if (elf64_has_gnu_abi_tag(elf_data, size)) return 1;
    /* 3) PIE：要加载偏置才能落进用户窗口，走兼容路径 */
    if (ehdr->e_type == ET_DYN) return 1;
    /* 4) 链接基址在原生程序区之外 */
    {
        uint64_t base = elf64_link_base(elf_data, size);
        if (base && (base < HEPU_NATIVE_PROG_BASE || base >= HEPU_NATIVE_PROG_END)) return 1;
    }
    return 0;
}

/* Linux x86_64 → Hepu 原生号。
 * V0.54.6: 32/64 位 syscall 号统一之后，旧表仍指向统一前的 Hepu 号
 * （如 [1]=1 = FP_VERIFY），Linux 的 read/write/open 全部翻错，
 * 静态 glibc 与动态链接器都跑不起来。这里按 kernel/include/syscall.h 的新编号重写。
 * 未列出的号 = 不支持，返回 0 由调用方保留原号。 */
static const int linux_map[350] = {
    [0]=SYS_READ, [1]=SYS_WRITE, [2]=SYS_OPEN, [3]=SYS_CLOSE,
    [4]=SYS_STAT, [5]=SYS_FSTAT, [6]=SYS_STAT, [7]=SYS_POLL,
    [8]=SYS_LSEEK, [9]=SYS_MMAP, [10]=SYS_MPROTECT, [11]=SYS_MUNMAP,
    [12]=SYS_BRK, [13]=SYS_LNX64_SIGACTION, [14]=SYS_LNX64_SIGPROCMASK,
    [15]=SYS_RT_SIGRETURN, [16]=SYS_IOCTL, [17]=SYS_LNX64_PREAD,
    [19]=SYS_LNX64_READV, [20]=SYS_LNX64_WRITEV,
    [21]=SYS_ACCESS, [22]=SYS_PIPE, [23]=SYS_LNX64_SELECT,
    [24]=SYS_LNX64_SCHED_YIELD, [25]=SYS_LNX64_MREMAP,
    [28]=SYS_LNX64_MADVISE,
    [32]=SYS_DUP, [33]=SYS_DUP2, [35]=SYS_NANOSLEEP,
    [39]=SYS_GETPID,
    [41]=SYS_SOCKET, [42]=SYS_CONNECT, [43]=SYS_ACCEPT,
    [44]=SYS_SEND, [45]=SYS_RECV,
    [49]=SYS_BIND, [50]=SYS_LISTEN,
    [53]=SYS_LNX64_SOCKETPAIR, [54]=SYS_LNX64_SETSOCKOPT,
    [55]=SYS_LNX64_GETSOCKOPT,
    [56]=SYS_CLONE, [57]=SYS_FORK, [58]=SYS_VFORK, [59]=SYS_EXECVE,
    [60]=SYS_EXIT, [61]=SYS_WAIT4, [62]=SYS_KILL,
    [63]=SYS_LNX64_NEWUNAME,
    [72]=SYS_FCNTL,
    [78]=SYS_LNX64_GETDENTS, [79]=SYS_GETCWD, [80]=SYS_CHDIR,
    [82]=SYS_RENAME, [83]=SYS_MKDIR, [84]=SYS_RMDIR, [85]=SYS_CREAT,
    [86]=SYS_LINK, [87]=SYS_UNLINK, [89]=SYS_LNX64_READLINK,
    [90]=SYS_CHMOD, [91]=SYS_FCHMOD, [92]=SYS_CHOWN, [93]=SYS_FCHOWN,
    [94]=SYS_LCHOWN,
    [96]=SYS_GETTIMEOFDAY, [98]=SYS_GETRUSAGE, [99]=SYS_LNX64_SYSINFO,
    [102]=SYS_GETUID, [104]=SYS_GETGID, [105]=SYS_SETUID,
    [106]=SYS_SETGID, [107]=SYS_GETEUID, [108]=SYS_GETEGID,
    [110]=SYS_GETPPID,
    [149]=SYS_LNX64_MLOCK,
    [158]=SYS_LNX64_ARCH_PRCTL,
    [186]=SYS_GETPID,
    [202]=SYS_LNX64_FUTEX,
    [217]=SYS_LNX64_GETDENTS, [218]=SYS_LNX64_SET_TID,
    [228]=SYS_LNX64_CLOCK_GETTIME, [229]=SYS_LNX64_CLOCK_GETRES,
    [230]=SYS_LNX64_CLOCK_NANOSLEEP, [231]=SYS_EXIT_GROUP,
    [234]=SYS_LNX64_TGKILL,
    [257]=SYS_LNX64_OPENAT, [258]=SYS_MKDIR, [260]=SYS_FCHOWN,
    [262]=SYS_LNX64_NEWFSTATAT, [263]=SYS_UNLINK, [264]=SYS_RENAME,
    [265]=SYS_LINK, [267]=SYS_LNX64_READLINK, [268]=SYS_FCHMOD,
    [269]=SYS_ACCESS, [270]=SYS_LNX64_PSELECT6,
    [273]=SYS_LNX64_SET_ROBUST,
    [290]=SYS_LNX64_EVENTFD2, [291]=SYS_LNX64_EPOLL_CREATE,
    [292]=SYS_LNX64_DUP3,
    [302]=SYS_LNX64_PRLIMIT, [318]=SYS_LNX64_GETRANDOM,
    [334]=SYS_LNX64_RSEQ,
};

int compat_gate_active(void)
{
    extern int user_process_current_env_type(void);
    return user_process_current_env_type() == ENV_LINUX_COMPAT;
}

int compat_gate_translate_nr(int nr)
{
    if (nr < 0 || nr >= 350) return 0;
    return linux_map[nr];
}

int compat_gate_map_entries(void)
{
    int n = 0;
    for (int i = 0; i < 350; i++) if (linux_map[i]) n++;
    return n;
}

int compat_gate_selftest(void)
{
    extern void vga_puts(const char *);
    extern void vga_put_hex(uint64_t v);
    /* 关键映射抽查：read/write/execve/futex/getdents —— 这几个错一个，
     * 静态 glibc 或动态链接器就直接跑不起来。 */
    struct { int lnx; int expect; } cases[] = {
        {0,   SYS_READ}, {1,  SYS_WRITE}, {59, SYS_EXECVE},
        {202, SYS_LNX64_FUTEX}, {78, SYS_LNX64_GETDENTS}, {60, SYS_EXIT},
    };
    int bad = 0;
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        if (linux_map[cases[i].lnx] != cases[i].expect) bad++;
    }
    /* 未映射的号必须返回 0（调用方据此保留原号） */
    if (linux_map[499] != 0) bad++;   /* 越界由 translate 挡住，这里只验表尾 */
    if (compat_gate_translate_nr(350) != 0) bad++;
    vga_puts("[COMPATG] linux_map selftest ");
    vga_puts(bad ? "BAD " : "PASS ");
    vga_puts("entries=");
    vga_put_hex((uint64_t)compat_gate_map_entries());
    vga_puts(" bad=");
    vga_put_hex((uint64_t)bad);
    vga_puts("\n");
    return bad ? -1 : 0;
}
