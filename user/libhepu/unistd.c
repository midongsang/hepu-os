/* ============================================================================
 * 赫普OS - 标准库 unistd
 * 系统调用封装：read、write、open、close、brk、sbrk
 * V8.05: 初始实现
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 基本类型定义 */
typedef int pid_t;
typedef long off_t;
typedef long ssize_t;
typedef unsigned int useconds_t;

/* 系统调用号 */
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_BRK     12
#define SYS_LSEEK   19
#define SYS_ACCESS  33
#define SYS_UNLINK  10
#define SYS_RENAME  38
#define SYS_MKDIR   39
#define SYS_RMDIR   40
#define SYS_CHDIR   12
#define SYS_GETCWD  17
#define SYS_DUP     32
#define SYS_DUP2    33
#define SYS_PIPE    42
#define SYS_IOCTL   54

/* 系统调用 */
static inline uint32_t syscall0(uint32_t num)
{
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory"
    );
    return ret;
}

static inline uint32_t syscall1(uint32_t num, uint32_t arg1)
{
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1)
        : "memory"
    );
    return ret;
}

static inline uint32_t syscall2(uint32_t num, uint32_t arg1, uint32_t arg2)
{
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2)
        : "memory"
    );
    return ret;
}

static inline uint32_t syscall3(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
    uint32_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3)
        : "memory"
    );
    return ret;
}

/* 文件操作 */
ssize_t read(int fd, void *buf, size_t count)
{
    return (ssize_t)syscall3(SYS_READ, (uint32_t)fd, (uint32_t)buf, (uint32_t)count);
}

ssize_t write(int fd, const void *buf, size_t count)
{
    return (ssize_t)syscall3(SYS_WRITE, (uint32_t)fd, (uint32_t)buf, (uint32_t)count);
}

int open(const char *pathname, int flags, int mode)
{
    return (int)syscall3(SYS_OPEN, (uint32_t)pathname, (uint32_t)flags, (uint32_t)mode);
}

int close(int fd)
{
    return (int)syscall1(SYS_CLOSE, (uint32_t)fd);
}

off_t lseek(int fd, off_t offset, int whence)
{
    return (off_t)syscall3(SYS_LSEEK, (uint32_t)fd, (uint32_t)offset, (uint32_t)whence);
}

int unlink(const char *pathname)
{
    return (int)syscall1(SYS_UNLINK, (uint32_t)pathname);
}

int rename(const char *oldpath, const char *newpath)
{
    return (int)syscall2(SYS_RENAME, (uint32_t)oldpath, (uint32_t)newpath);
}

int mkdir(const char *pathname, int mode)
{
    return (int)syscall2(SYS_MKDIR, (uint32_t)pathname, (uint32_t)mode);
}

int rmdir(const char *pathname)
{
    return (int)syscall1(SYS_RMDIR, (uint32_t)pathname);
}

int chdir(const char *path)
{
    return (int)syscall1(SYS_CHDIR, (uint32_t)path);
}

char *getcwd(char *buf, size_t size)
{
    return (char *)syscall2(SYS_GETCWD, (uint32_t)buf, (uint32_t)size);
}

int access(const char *pathname, int mode)
{
    return (int)syscall2(SYS_ACCESS, (uint32_t)pathname, (uint32_t)mode);
}

/* 内存管理 */
int brk(void *addr)
{
    return (int)syscall1(SYS_BRK, (uint32_t)addr);
}

void *sbrk(intptr_t increment)
{
    void *old_brk = (void *)syscall1(SYS_BRK, 0);
    if (increment == 0) {
        return old_brk;
    }
    void *new_brk = (void *)syscall1(SYS_BRK, (uint32_t)old_brk + increment);
    if (new_brk == old_brk) {
        return (void *)-1;
    }
    return old_brk;
}

/* 进程控制 */
int dup(int oldfd)
{
    return (int)syscall1(SYS_DUP, (uint32_t)oldfd);
}

int dup2(int oldfd, int newfd)
{
    return (int)syscall2(SYS_DUP2, (uint32_t)oldfd, (uint32_t)newfd);
}

int pipe(int pipefd[2])
{
    return (int)syscall1(SYS_PIPE, (uint32_t)pipefd);
}

int ioctl(int fd, unsigned long request, void *argp)
{
    return (int)syscall3(SYS_IOCTL, (uint32_t)fd, (uint32_t)request, (uint32_t)argp);
}

/* 睡眠（简化） */
unsigned int sleep(unsigned int seconds)
{
    /* 简化：使用PIT睡眠系统调用 */
    extern void pit_sleep(uint32_t ms);
    pit_sleep(seconds * 1000);
    return 0;
}

int usleep(useconds_t usec)
{
    extern void pit_sleep(uint32_t ms);
    pit_sleep(usec / 1000);
    return 0;
}
