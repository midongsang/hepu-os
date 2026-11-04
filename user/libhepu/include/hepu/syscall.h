#ifndef HEPU_SYSCALL_H
#define HEPU_SYSCALL_H

#include <stdint.h>

/* 系统调用号 (与内核 syscall.h 一致) */
#define SYS_HEPU_INFO    0
#define SYS_HEPU_FP      1
#define SYS_PRINT        2
#define SYS_MALLOC       3
#define SYS_FREE         4
#define SYS_EXIT         5
#define SYS_YIELD        6
#define SYS_GETPID       7
#define SYS_OPEN         8
#define SYS_READ         9
#define SYS_WRITE        10
#define SYS_CLOSE        11
#define SYS_IPC_SEND     12
#define SYS_IPC_RECV     13
#define SYS_LSEEK        14
#define SYS_SLEEP        15
#define SYS_PIPE         16
#define SYS_SHMGET       17
#define SYS_SHMAT        18
#define SYS_KILL         19
#define SYS_SIGNAL       20
#define SYS_READDIR      30

/* 文件操作标志 */
#define O_RDONLY  0x00
#define O_WRONLY  0x01
#define O_RDWR    0x02
#define O_APPEND  0x08
#define O_CREAT   0x40
#define O_TRUNC   0x200

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* 系统调用封装 (内联汇编) */
static inline uint32_t hepu_syscall0(uint32_t num) {
    uint32_t ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num));
    return ret;
}

static inline uint32_t hepu_syscall1(uint32_t num, uint32_t arg1) {
    uint32_t ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1));
    return ret;
}

static inline uint32_t hepu_syscall2(uint32_t num, uint32_t arg1, uint32_t arg2) {
    uint32_t ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2));
    return ret;
}

static inline uint32_t hepu_syscall3(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    uint32_t ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3));
    return ret;
}

/* 便捷函数 */
static inline const char *hepu_info(void) {
    return (const char *)hepu_syscall0(SYS_HEPU_INFO);
}

static inline void hepu_print(const char *msg) {
    hepu_syscall1(SYS_PRINT, (uint32_t)msg);
}

static inline void hepu_exit(uint32_t code) {
    hepu_syscall1(SYS_EXIT, code);
    while (1);
}

static inline uint32_t hepu_getpid(void) {
    return hepu_syscall0(SYS_GETPID);
}

static inline void hepu_yield(void) {
    hepu_syscall0(SYS_YIELD);
}

static inline void *hepu_malloc(uint32_t size) {
    return (void *)hepu_syscall1(SYS_MALLOC, size);
}

static inline void hepu_free(void *ptr) {
    hepu_syscall1(SYS_FREE, (uint32_t)ptr);
}

static inline void hepu_sleep(uint32_t ms) {
    hepu_syscall1(SYS_SLEEP, ms);
}

/* 文件操作 */
static inline int hepu_open(const char *path, int flags) {
    return (int)hepu_syscall2(SYS_OPEN, (uint32_t)path, flags);
}

static inline int hepu_read(int fd, void *buf, uint32_t size) {
    return (int)hepu_syscall3(SYS_READ, fd, (uint32_t)buf, size);
}

static inline int hepu_write(int fd, const void *buf, uint32_t size) {
    return (int)hepu_syscall3(SYS_WRITE, fd, (uint32_t)buf, size);
}

/* 列目录：每项 "name\0type\0"，返回字节数，-1=失败 */
static inline int hepu_readdir(const char *path, char *buf, uint32_t bufsz) {
    return (int)hepu_syscall3(SYS_READDIR, (uint32_t)path, (uint32_t)buf, bufsz);
}

static inline int hepu_close(int fd) {
    return (int)hepu_syscall1(SYS_CLOSE, fd);
}

static inline int hepu_lseek(int fd, int32_t offset, int whence) {
    return (int)hepu_syscall3(SYS_LSEEK, fd, (uint32_t)offset, whence);
}

/* IPC 消息队列 */
static inline int hepu_ipc_send(uint32_t target_pid, uint32_t type,
                                 const void *data, uint32_t length) {
    return (int)hepu_syscall3(SYS_IPC_SEND, target_pid, type, (uint32_t)data);
}
static inline int hepu_ipc_recv(void *msg_buf) {
    return (int)hepu_syscall1(SYS_IPC_RECV, (uint32_t)msg_buf);
}

/* 管道 */
static inline int hepu_pipe(int *fds) {
    return (int)hepu_syscall1(SYS_PIPE, (uint32_t)fds);
}

/* 共享内存 */
static inline int hepu_shmget(uint32_t key, uint32_t size) {
    return (int)hepu_syscall2(SYS_SHMGET, key, size);
}
static inline void *hepu_shmat(int shm_id) {
    return (void *)hepu_syscall1(SYS_SHMAT, shm_id);
}

/* 信号 */
static inline int hepu_kill(uint32_t pid, int sig) {
    return (int)hepu_syscall2(SYS_KILL, pid, sig);
}
static inline int hepu_signal(int sig, void (*handler)(int)) {
    return (int)hepu_syscall2(SYS_SIGNAL, sig, (uint32_t)handler);
}

#endif /* HEPU_SYSCALL_H */
