/* ============================================================================
 * 赫普OS - fcntl.h 头文件
 * 文件控制相关定义和函数声明
 * V8.20: 初始实现
 * ========================================================================== */

#ifndef _FCNTL_H
#define _FCNTL_H

#include <sys/types.h>

/* 文件访问模式 */
#define O_RDONLY    00000000  /* 只读 */
#define O_WRONLY    00000001  /* 只写 */
#define O_RDWR      00000002  /* 读写 */
#define O_ACCMODE   00000003  /* 访问模式掩码 */

/* 文件打开标志 */
#define O_CREAT     00000100  /* 创建文件（如果不存在） */
#define O_EXCL      00000200  /* 独占创建（与O_CREAT一起使用） */
#define O_NOCTTY    00000400  /* 不分配控制终端 */
#define O_TRUNC     00001000  /* 截断文件（如果存在且可写） */
#define O_APPEND    00002000  /* 追加模式 */
#define O_NONBLOCK  00004000  /* 非阻塞模式 */
#define O_NDELAY    O_NONBLOCK /* 非阻塞（兼容） */
#define O_SYNC      00010000  /* 同步写入 */
#define O_FSYNC     O_SYNC     /* 同步（兼容） */
#define O_ASYNC     00020000  /* 异步I/O */
#define O_DIRECT    00040000  /* 直接I/O */
#define O_LARGEFILE 00100000  /* 大文件 */
#define O_DIRECTORY 00200000  /* 必须是目录 */
#define O_NOFOLLOW  00400000  /* 不跟随符号链接 */
#define O_NOATIME   01000000  /* 不更新访问时间 */
#define O_CLOEXEC   02000000  /* 执行时关闭 */
#define O_PATH      010000000 /* 路径文件描述符 */
#define O_TMPFILE   020000000 /* 临时文件 */

/* fcntl命令 */
#define F_DUPFD     0  /* 复制文件描述符 */
#define F_GETFD     1  /* 获取文件描述符标志 */
#define F_SETFD     2  /* 设置文件描述符标志 */
#define F_GETFL     3  /* 获取文件状态标志 */
#define F_SETFL     4  /* 设置文件状态标志 */
#define F_GETLK     5  /* 获取记录锁 */
#define F_SETLK     6  /* 设置记录锁（非阻塞） */
#define F_SETLKW    7  /* 设置记录锁（阻塞） */
#define F_SETOWN    8  /* 设置进程/进程组ID（用于SIGIO） */
#define F_GETOWN    9  /* 获取进程/进程组ID */
#define F_SETSIG    10 /* 设置信号 */
#define F_GETSIG    11 /* 获取信号 */
#define F_SETPIPE_SZ 1031 /* 设置管道大小 */
#define F_GETPIPE_SZ 1032 /* 获取管道大小 */

/* 文件描述符标志 */
#define FD_CLOEXEC  1  /* 执行时关闭 */

/* 记录锁类型 */
#define F_RDLCK     0  /* 读锁（共享锁） */
#define F_WRLCK     1  /* 写锁（独占锁） */
#define F_UNLCK     2  /* 解锁 */

/* 记录锁结构 */
struct flock {
    short l_type;    /* 锁类型：F_RDLCK, F_WRLCK, F_UNLCK */
    short l_whence;  /* 锁起始位置的基准：SEEK_SET, SEEK_CUR, SEEK_END */
    off_t l_start;   /* 锁起始位置（相对于l_whence） */
    off_t l_len;     /* 锁长度（0表示到文件末尾） */
    pid_t l_pid;     /* 持有锁的进程ID（F_GETLK时返回） */
};

/* 函数声明 */
int open(const char *pathname, int flags, ...);
int creat(const char *pathname, mode_t mode);
int fcntl(int fd, int cmd, ...);
int openat(int dirfd, const char *pathname, int flags, ...);
int renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath);
int mkdirat(int dirfd, const char *pathname, mode_t mode);
int mknodat(int dirfd, const char *pathname, mode_t mode, dev_t dev);
int fchmodat(int dirfd, const char *pathname, mode_t mode, int flags);
int fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group, int flags);
int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, int flags);
int symlinkat(const char *target, int newdirfd, const char *linkpath);
int unlinkat(int dirfd, const char *pathname, int flags);
int readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);
int faccessat(int dirfd, const char *pathname, int mode, int flags);
int utimensat(int dirfd, const char *pathname, const struct timespec times[2], int flags);
int fstatat(int dirfd, const char *pathname, struct stat *statbuf, int flags);

/* AT_* 标志（用于*at函数） */
#define AT_FDCWD            -100   /* 当前工作目录 */
#define AT_SYMLINK_NOFOLLOW 0x100  /* 不跟随符号链接 */
#define AT_REMOVEDIR        0x200  /* 删除目录（用于unlinkat） */
#define AT_SYMLINK_FOLLOW   0x400  /* 跟随符号链接（用于linkat） */
#define AT_EMPTY_PATH       0x1000 /* 空路径 */
#define AT_NO_AUTOMOUNT     0x800  /* 不自动挂载 */
#define AT_STATX_SYNC_TYPE  0x6000 /* statx同步类型 */
#define AT_STATX_SYNC_AS_STAT 0x0000 /* 与stat相同 */
#define AT_STATX_FORCE_SYNC 0x2000 /* 强制同步 */
#define AT_STATX_DONT_SYNC  0x4000 /* 不同步 */
#define AT_RECURSIVE        0x8000 /* 递归 */

#endif /* _FCNTL_H */
