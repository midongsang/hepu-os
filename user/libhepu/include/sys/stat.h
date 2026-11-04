/* ============================================================================
 * 赫普OS - sys/stat.h 头文件
 * 文件状态相关定义和函数声明
 * V8.20: 初始实现
 * ========================================================================== */

#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <sys/types.h>

/* 文件模式（mode_t） */
typedef unsigned int mode_t;

/* 文件类型 */
#define S_IFMT     0170000  /* 文件类型掩码 */
#define S_IFSOCK   0140000  /* 套接字 */
#define S_IFLNK    0120000  /* 符号链接 */
#define S_IFREG    0100000  /* 普通文件 */
#define S_IFBLK    0060000  /* 块设备 */
#define S_IFDIR    0040000  /* 目录 */
#define S_IFCHR    0020000  /* 字符设备 */
#define S_IFIFO    0010000  /* FIFO */

/* 文件类型测试宏 */
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

/* 文件权限位 */
#define S_ISUID    04000    /* 设置用户ID */
#define S_ISGID    02000    /* 设置组ID */
#define S_ISVTX    01000    /* 粘滞位 */

#define S_IRWXU    00700    /* 用户读写执行 */
#define S_IRUSR    00400    /* 用户读 */
#define S_IWUSR    00200    /* 用户写 */
#define S_IXUSR    00100    /* 用户执行 */

#define S_IRWXG    00070    /* 组读写执行 */
#define S_IRGRP    00040    /* 组读 */
#define S_IWGRP    00020    /* 组写 */
#define S_IXGRP    00010    /* 组执行 */

#define S_IRWXO    00007    /* 其他读写执行 */
#define S_IROTH    00004    /* 其他读 */
#define S_IWOTH    00002    /* 其他写 */
#define S_IXOTH    00001    /* 其他执行 */

/* 常用权限组合 */
#define S_IRUGO    (S_IRUSR | S_IRGRP | S_IROTH)
#define S_IWUGO    (S_IWUSR | S_IWGRP | S_IWOTH)
#define S_IXUGO    (S_IXUSR | S_IXGRP | S_IXOTH)

/* 文件状态结构 */
struct stat {
    dev_t     st_dev;      /* 设备ID */
    ino_t     st_ino;      /* inode号 */
    mode_t    st_mode;     /* 文件类型和权限 */
    nlink_t   st_nlink;    /* 硬链接数 */
    uid_t     st_uid;      /* 用户ID */
    gid_t     st_gid;      /* 组ID */
    dev_t     st_rdev;     /* 设备类型（如果是字符/块设备） */
    off_t     st_size;     /* 文件大小（字节） */
    blksize_t st_blksize;  /* 块大小 */
    blkcnt_t  st_blocks;   /* 块数 */
    time_t    st_atime;    /* 最后访问时间 */
    time_t    st_mtime;    /* 最后修改时间 */
    time_t    st_ctime;    /* 最后状态改变时间 */
};

/* 函数声明 */
int stat(const char *pathname, struct stat *statbuf);
int fstat(int fd, struct stat *statbuf);
int lstat(const char *pathname, struct stat *statbuf);
int chmod(const char *pathname, mode_t mode);
int fchmod(int fd, mode_t mode);
int mkdir(const char *pathname, mode_t mode);
mode_t umask(mode_t mask);

#endif /* _SYS_STAT_H */
