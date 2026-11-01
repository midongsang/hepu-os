/* ============================================================================
 * 赫普OS - 64位系统调用兼容层
 * V8.97: 将64位系统调用转发到32位处理函数，复用现有系统调用实现
 * P5-1 64位完整支持：系统调用真实接线
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "x86_64.h"
#include "../../include/syscall.h"
#include "../../include/vga.h"

/* 32位系统调用处理函数声明 */
extern int32_t sys_write(int32_t fd, const void *buf, int32_t count);
extern int32_t sys_read(int32_t fd, void *buf, int32_t count);
extern int32_t sys_open(const char *path, int32_t flags, int32_t mode);
extern int32_t sys_close(int32_t fd);
extern int32_t sys_exit(int32_t status);
extern int32_t sys_getpid(void);
extern int32_t sys_fork(void);
extern int32_t sys_execve(const char *path, char *const argv[], char *const envp[]);
extern int32_t sys_brk(uint32_t addr);
extern int32_t sys_mmap(uint32_t addr, uint32_t length, int32_t prot,
                         int32_t flags, int32_t fd, int32_t offset);
extern int32_t sys_munmap(uint32_t addr, uint32_t length);
extern int32_t sys_gettimeofday(void *tv, void *tz);
extern int32_t sys_ioctl(int32_t fd, int32_t request, uint32_t arg);
extern int32_t sys_lseek(int32_t fd, int32_t offset, int32_t whence);
extern int32_t sys_unlink(const char *path);
extern int32_t sys_rename(const char *oldpath, const char *newpath);
extern int32_t sys_mkdir(const char *path, int32_t mode);
extern int32_t sys_rmdir(const char *path);
extern int32_t sys_readdir(int32_t fd, void *dirp, int32_t count);
extern int32_t sys_stat(const char *path, void *statbuf);
extern int32_t sys_fstat(int32_t fd, void *statbuf);
extern int32_t sys_dup(int32_t fd);
extern int32_t sys_dup2(int32_t oldfd, int32_t newfd);
extern int32_t sys_pipe(int32_t *fds);
extern int32_t sys_kill(int32_t pid, int32_t sig);
extern int32_t sys_signal(int32_t sig, void *handler);
extern int32_t sys_sched_yield(void);
extern int32_t sys_nanosleep(const void *req, void *rem);
extern int32_t sys_socket(int32_t domain, int32_t type, int32_t protocol);
extern int32_t sys_connect(int32_t sockfd, const void *addr, int32_t addrlen);
extern int32_t sys_send(int32_t sockfd, const void *buf, int32_t len, int32_t flags);
extern int32_t sys_recv(int32_t sockfd, void *buf, int32_t len, int32_t flags);
extern int32_t sys_bind(int32_t sockfd, const void *addr, int32_t addrlen);
extern int32_t sys_listen(int32_t sockfd, int32_t backlog);
extern int32_t sys_accept(int32_t sockfd, void *addr, int32_t *addrlen);
extern int32_t sys_malloc(uint32_t size);
extern int32_t sys_free(uint32_t addr);
extern int32_t sys_hepu_info(void *buf, uint32_t size);
extern int32_t sys_print(const char *str);
extern int32_t sys_sleep(uint32_t ms);

/* 兼容层统计 */
typedef struct {
    uint64_t total_calls;
    uint64_t forwarded;
    uint64_t native64;
    uint64_t unknown;
    uint64_t errors;
} syscall64_compat_stats_t;

static syscall64_compat_stats_t g_stats;
static int g_compat_initialized = 0;

/* ============================================================================
 * 通用64位 -> 32位 转发
 * ============================================================================ */

static uint64_t compat_forward_to_32(uint64_t nr, uint64_t arg1, uint64_t arg2,
                                      uint64_t arg3, uint64_t arg4,
                                      uint64_t arg5, uint64_t arg6)
{
    int32_t ret = -1;
    int32_t a1 = (int32_t)arg1;
    int32_t a2 = (int32_t)arg2;
    int32_t a3 = (int32_t)arg3;

    switch (nr) {
        case SYS_READ:
            ret = sys_read(a1, (void*)(uintptr_t)a2, a3); break;
        case SYS_WRITE:
            ret = sys_write(a1, (const void*)(uintptr_t)a2, a3); break;
        case SYS_OPEN:
            ret = sys_open((const char*)(uintptr_t)a1, a2, a3); break;
        case SYS_CLOSE:
            ret = sys_close(a1); break;
        case SYS_LSEEK:
            ret = sys_lseek(a1, a2, a3); break;
        case SYS_UNLINK:
            ret = sys_unlink((const char*)(uintptr_t)a1); break;
        case SYS_RENAME:
            ret = sys_rename((const char*)(uintptr_t)a1,
                             (const char*)(uintptr_t)a2); break;
        case SYS_MKDIR:
            ret = sys_mkdir((const char*)(uintptr_t)a1, a2); break;
        case SYS_RMDIR:
            ret = sys_rmdir((const char*)(uintptr_t)a1); break;
        case SYS_READDIR:
            ret = sys_readdir(a1, (void*)(uintptr_t)a2, a3); break;
        case SYS_STAT:
            ret = sys_stat((const char*)(uintptr_t)a1, (void*)(uintptr_t)a2); break;
        case SYS_FSTAT:
            ret = sys_fstat(a1, (void*)(uintptr_t)a2); break;
        case SYS_DUP:
            ret = sys_dup(a1); break;
        case SYS_DUP2:
            ret = sys_dup2(a1, a2); break;
        case SYS_PIPE:
            ret = sys_pipe((int32_t*)(uintptr_t)a1); break;
        case SYS_EXIT:
            ret = sys_exit(a1); break;
        case SYS_GETPID:
            ret = sys_getpid(); break;
        case SYS_FORK:
            ret = sys_fork(); break;
        case SYS_EXECVE:
            ret = sys_execve((const char*)(uintptr_t)a1,
                             (char*const*)(uintptr_t)a2,
                             (char*const*)(uintptr_t)a3); break;
        case SYS_KILL:
            ret = sys_kill(a1, a2); break;
        case SYS_SIGNAL:
            ret = sys_signal(a1, (void*)(uintptr_t)a2); break;
        case SYS_YIELD:
            ret = sys_sched_yield(); break;
        case SYS_NANOSLEEP:
            ret = sys_nanosleep((const void*)(uintptr_t)a1,
                                (void*)(uintptr_t)a2); break;
        case SYS_SLEEP:
            ret = sys_sleep((uint32_t)arg1); break;
        case SYS_BRK:
            ret = sys_brk((uint32_t)arg1); break;
        case SYS_MMAP:
            ret = sys_mmap((uint32_t)arg1, (uint32_t)arg2, (int32_t)arg3,
                           (int32_t)arg4, (int32_t)arg5, (int32_t)arg6); break;
        case SYS_MUNMAP:
            ret = sys_munmap((uint32_t)arg1, (uint32_t)arg2); break;
        case SYS_MALLOC:
            ret = sys_malloc((uint32_t)arg1); break;
        case SYS_FREE:
            ret = sys_free((uint32_t)arg1); break;
        case SYS_GETTIMEOFDAY:
            ret = sys_gettimeofday((void*)(uintptr_t)a1, (void*)(uintptr_t)a2); break;
        case SYS_IOCTL:
            ret = sys_ioctl(a1, a2, (uint32_t)arg3); break;
        case SYS_SOCKET:
            ret = sys_socket(a1, a2, a3); break;
        case SYS_CONNECT:
            ret = sys_connect(a1, (const void*)(uintptr_t)a2, a3); break;
        case SYS_SEND:
            ret = sys_send(a1, (const void*)(uintptr_t)a2, a3, (int32_t)arg4); break;
        case SYS_RECV:
            ret = sys_recv(a1, (void*)(uintptr_t)a2, a3, (int32_t)arg4); break;
        case SYS_BIND:
            ret = sys_bind(a1, (const void*)(uintptr_t)a2, a3); break;
        case SYS_LISTEN:
            ret = sys_listen(a1, a2); break;
        case SYS_ACCEPT:
            ret = sys_accept(a1, (void*)(uintptr_t)a2, (int32_t*)(uintptr_t)a3); break;
        case SYS_HEPU_INFO:
            ret = sys_hepu_info((void*)(uintptr_t)a1, (uint32_t)a2); break;
        case SYS_PRINT:
            ret = sys_print((const char*)(uintptr_t)a1); break;
        default:
            g_stats.unknown++;
            return -38;  /* ENOSYS */
    }

    g_stats.forwarded++;
    if (ret < 0) g_stats.errors++;
    return (uint64_t)(int64_t)ret;
}

/* ============================================================================
 * 原生64位系统调用（大缓冲区支持）
 * ============================================================================ */

static uint64_t native64_write(uint64_t fd, uint64_t buf, uint64_t count)
{
    uint64_t total = 0;
    const char *ptr = (const char*)(uintptr_t)buf;

    while (count > 0) {
        uint32_t chunk = (count > 0xFFFFF000) ? 0xFFFFF000 : (uint32_t)count;
        int32_t n = sys_write((int32_t)fd, ptr, (int32_t)chunk);
        if (n < 0) return (uint64_t)(int64_t)n;
        total += n;
        ptr += n;
        count -= n;
        if (n == 0) break;
    }
    return total;
}

static uint64_t native64_read(uint64_t fd, uint64_t buf, uint64_t count)
{
    uint64_t total = 0;
    char *ptr = (char*)(uintptr_t)buf;

    while (count > 0) {
        uint32_t chunk = (count > 0xFFFFF000) ? 0xFFFFF000 : (uint32_t)count;
        int32_t n = sys_read((int32_t)fd, ptr, (int32_t)chunk);
        if (n < 0) return (uint64_t)(int64_t)n;
        total += n;
        ptr += n;
        count -= n;
        if (n == 0) break;
    }
    return total;
}

/* ============================================================================
 * 兼容层主调度
 * ============================================================================ */

uint64_t syscall64_compat_dispatch(uint64_t nr, uint64_t arg1, uint64_t arg2,
                                    uint64_t arg3, uint64_t arg4,
                                    uint64_t arg5, uint64_t arg6)
{
    g_stats.total_calls++;

    /* 大缓冲区使用原生64位处理 */
    if (nr == SYS_WRITE && arg3 > 0xFFFFF000) {
        g_stats.native64++;
        return native64_write(arg1, arg2, arg3);
    }
    if (nr == SYS_READ && arg3 > 0xFFFFF000) {
        g_stats.native64++;
        return native64_read(arg1, arg2, arg3);
    }

    return compat_forward_to_32(nr, arg1, arg2, arg3, arg4, arg5, arg6);
}

/* ============================================================================
 * 初始化
 * ============================================================================ */

int syscall64_compat_init(void)
{
    if (g_compat_initialized) return 0;

    memset(&g_stats, 0, sizeof(g_stats));
    g_compat_initialized = 1;

    vga_puts("[SYSCALL64-COMPAT] 64-bit syscall compatibility layer initialized\n");
    vga_printf("[SYSCALL64-COMPAT]   Forwarding %d syscalls to 32-bit handlers\n", SYS_MAX);
    vga_puts("[SYSCALL64-COMPAT]   Native 64-bit: write/read (large buffers)\n");

    return 0;
}

void syscall64_compat_get_stats(syscall64_compat_stats_t *stats)
{
    if (stats) *stats = g_stats;
}

int syscall64_compat_is_initialized(void)
{
    return g_compat_initialized;
}
