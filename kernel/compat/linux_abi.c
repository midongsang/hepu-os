/* ============================================================================
 * 赫普OS - Linux i386 syscall ABI 翻译层
 * Linux x86 32位 syscall 号 → HEPU syscall 号
 * (Linux 号来源: 公开 ABI asm/unistd_32.h; HEPU 号见 syscall.h)
 * ========================================================================== */

#include "linux_abi.h"
#include "syscall.h"

/* 一对一的 syscall 号映射表.
 * 结构: { linux_nr, hepu_nr }
 * 保持数组有序(按 linux_nr), 二分/线性均可(表小, 线性即可). */
typedef struct {
    uint32_t linux_nr;
    uint32_t hepu_nr;
} abi_map_t;

static const abi_map_t abi_map[] = {
    { LINUX_SYS_exit,         SYS_EXIT },
    { LINUX_SYS_fork,         SYS_FORK },
    { LINUX_SYS_read,         SYS_READ },
    { LINUX_SYS_write,        SYS_WRITE },
    { LINUX_SYS_open,         SYS_OPEN },
    { LINUX_SYS_close,        SYS_CLOSE },
    { LINUX_SYS_waitpid,      SYS_WAITPID },
    { LINUX_SYS_unlink,       SYS_UNLINK },
    { LINUX_SYS_execve,       SYS_EXECVE },
    { LINUX_SYS_chdir,        SYS_CHDIR },
    { LINUX_SYS_time,         SYS_TIME },
    { LINUX_SYS_brk,          SYS_BRK },
    { LINUX_SYS_chmod,        SYS_CHMOD },
    { LINUX_SYS_lseek,        SYS_LSEEK },
    { LINUX_SYS_getpid,       SYS_GETPID },
    { LINUX_SYS_mount,        SYS_MOUNT },
    { LINUX_SYS_getuid,       SYS_GETUID },
    { LINUX_SYS_geteuid,      SYS_GETEUID },
    { LINUX_SYS_getgid,       SYS_GETGID },
    { LINUX_SYS_getegid,      SYS_GETEGID },
    { LINUX_SYS_access,       SYS_ACCESS },
    { LINUX_SYS_kill,         SYS_KILL },
    { LINUX_SYS_rename,       SYS_RENAME },
    { LINUX_SYS_mkdir,        SYS_MKDIR },
    { LINUX_SYS_rmdir,        SYS_RMDIR },
    { LINUX_SYS_dup,          SYS_DUP },
    { LINUX_SYS_pipe,         SYS_PIPE },
    { LINUX_SYS_ioctl,        SYS_IOCTL },
    { LINUX_SYS_fcntl,        SYS_FCNTL },
    { LINUX_SYS_dup2,         SYS_DUP2 },
    { LINUX_SYS_gettimeofday, SYS_GETTIMEOFDAY },
    { LINUX_SYS_mmap,         SYS_MMAP },
    { LINUX_SYS_munmap,       SYS_MUNMAP },
    { LINUX_SYS_clone,        SYS_CLONE },
    { LINUX_SYS_mprotect,     SYS_MPROTECT },
    { LINUX_SYS_select,       SYS_SELECT },
    { LINUX_SYS_nanosleep,    SYS_NANOSLEEP },
    { LINUX_SYS_poll,         SYS_POLL },
    { 158,                    SYS_YIELD },   /* V6.70: sched_yield → sys_yield (BUG-23根因) */
    { LINUX_SYS_openat,       SYS_OPEN },
    { LINUX_SYS_mmap2,        SYS_MMAP },   /* 页单位偏移, mmap 已含 */
    { LINUX_SYS_exit_group,   SYS_EXIT },   /* 进程组退出 → 单进程 exit */
    { LINUX_SYS_sysinfo,      SYS_HEPU_INFO },
    /* V4.02: 赫普OS扩展系统调用（Linux兼容进程直接透传） */
    { 200,                    SYS_GFX_INIT },
    { 201,                    SYS_GFX_TEXT },
    /* V6.62: Rust用户态服务IPC支持 */
    { 202,                    SYS_IPC_SEND },
    { 203,                    SYS_IPC_RECV },
    /* V6.64: 微内核服务发现 */
    { 204,                    SYS_SERVICE_REGISTER },
    { 205,                    SYS_SERVICE_LOOKUP },
    /* V6.69: BSD Socket API - Linux socketcall多路复用 */
    { 102,                    SYS_SOCKETCALL },
};

#define ABI_MAP_COUNT  (sizeof(abi_map) / sizeof(abi_map[0]))

uint32_t linux_abi_translate(uint32_t linux_nr)
{
    for (uint32_t i = 0; i < ABI_MAP_COUNT; i++) {
        if (abi_map[i].linux_nr == linux_nr) {
            return abi_map[i].hepu_nr | LINUX_ABI_TRANSLATED;
        }
    }
    return LINUX_ABI_UNSUPPORTED;
}

/* Linux 程序地址空间范围: 0x08048000 (ELF基址) 起至用户空间顶端.
 * 用户态指针在该范围内即视为 Linux 程序自身地址, 无需翻译. */
int linux_abi_valid_addr(uint32_t addr)
{
    return (addr >= 0x08048000u && addr < 0xC0000000u);
}
