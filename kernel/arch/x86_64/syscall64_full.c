/* ============================================================================
 * 赫普OS 64位 - 完整系统调用实现
 * 支持：write, read, open, close, exit, fork, execve, getpid, brk
 * V0.10.17: 接入copy_from_user/copy_to_user，修复用户指针安全漏洞
 * V0.54.6: syscall 号统一（以 32 位 syscall.h 为唯一真源）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "hepu_smap.h"
#include "elf64.h"
#include "ext2_shared.h"
#include "seccomp64.h"
#include "syscall.h"   /* V0.54.6: 统一 syscall 号（唯一真源） */

int g_running_linux_elf = 0;  /* V0.20.70: 当前是否在运行Linux ABI ELF */

/* 用户空间访问安全 */
extern int copy_from_user(void *kernel_dst, const void *user_src, size_t size);
extern int copy_to_user(void *user_dst, const void *kernel_src, size_t size);
extern int copy_str_from_user(char *kernel_dst, const char *user_src, size_t max_size);
/* V0.54.12(R4): 用户地址窗口校验（含溢出检查），用于危险 syscall 入口 */
extern int hepu_is_user_address(uintptr_t addr, size_t size);

/* 安全修复：从用户空间复制路径字符串
 * 旧实现固定拷贝 255/256 字节且不保证 NUL 结尾（超长路径会让后续 strlen/strcpy
 * 越过栈缓冲区），并在字符串靠近映射页末尾时误报 EFAULT。
 * 这里按字符串长度复制、强制 NUL 结尾，超长路径直接拒绝（不静默截断）。 */
static void print_syscall_number(int64_t v);

static int copy_user_path(char *dst, size_t dstsz, const char *user_path)
{
    if (!dst || dstsz < 2 || !user_path) return -1;
    int n = copy_str_from_user(dst, user_path, dstsz);
    if (n < 0 || (size_t)n >= dstsz - 1) {
        dst[0] = 0;
        return -1;
    }
    return n;
}

/* ============================================================================
 * V0.54.6: SYSCALL_xxx → SYS_xxx 值映射
 * 保留 SYSCALL_xxx 名字（syscall_table 不动），但值改成 syscall.h 统一号。
 * Linux glibc 专用号用 SYS_LNX64_xxx 前缀。
 * ========================================================================== */
#define SYSCALL_MAX     SYS_MAX

/* ---- 赫普 OS 核心 syscall ---- */
#define SYSCALL_WRITE   SYS_WRITE
#define SYSCALL_READ    SYS_READ
#define SYSCALL_OPEN    SYS_OPEN
#define SYSCALL_CLOSE   SYS_CLOSE
#define SYSCALL_EXIT    SYS_EXIT
#define SYSCALL_FORK    SYS_FORK
#define SYSCALL_EXECVE  SYS_EXECVE
#define SYSCALL_GETPID  SYS_GETPID
#define SYSCALL_BRK     SYS_BRK
#define SYSCALL_TIME    SYS_TIME
#define SYSCALL_SLEEP   SYS_SLEEP
#define SYSCALL_GETPPID SYS_GETPPID
#define SYSCALL_UNLINK  SYS_UNLINK
#define SYSCALL_MKDIR   SYS_MKDIR
#define SYSCALL_RMDIR   SYS_RMDIR
#define SYSCALL_LSEEK   SYS_LSEEK
#define SYSCALL_ACCESS  SYS_ACCESS
#define SYSCALL_RENAME  SYS_RENAME
#define SYSCALL_FSTAT   SYS_FSTAT
#define SYSCALL_GETCWD  SYS_GETCWD
#define SYSCALL_KILL    SYS_KILL
#define SYSCALL_READDIR SYS_READDIR
#define SYSCALL_STAT    SYS_STAT
#define SYSCALL_MMAP    SYS_MMAP
#define SYSCALL_MUNMAP  SYS_MUNMAP
#define SYSCALL_MPROTECT SYS_MPROTECT
#define SYSCALL_CHDIR   SYS_CHDIR
#define SYSCALL_DUP     SYS_DUP
#define SYSCALL_DUP2    SYS_DUP2
#define SYSCALL_GETTIMEOFDAY SYS_GETTIMEOFDAY
#define SYSCALL_WAITPID SYS_WAITPID
#define SYSCALL_PIPE    SYS_PIPE
#define SYSCALL_GETUID  SYS_GETUID
#define SYSCALL_GETGID  SYS_GETGID
#define SYSCALL_GETEUID SYS_GETEUID
#define SYSCALL_GETEGID SYS_GETEGID
#define SYSCALL_SOCKET  SYS_SOCKET
#define SYSCALL_CONNECT SYS_CONNECT
#define SYSCALL_ACCEPT  SYS_ACCEPT
#define SYSCALL_BIND    SYS_BIND
#define SYSCALL_LISTEN  SYS_LISTEN
#define SYSCALL_IOCTL   SYS_IOCTL
#define SYSCALL_CHMOD   SYS_CHMOD
#define SYSCALL_CHOWN   SYS_CHOWN
#define SYSCALL_FCNTL   SYS_FCNTL
/* SYSCALL_SEND/SYSCALL_RECV: syscall.h 已有 SYS_SEND=52/SYS_RECV=53，直接用 */

/* ---- 赫普 OS 硬件层（两边共用） ---- */
#define SYSCALL_DISK_READ   SYS_DISK_READ
#define SYSCALL_DISK_WRITE  SYS_DISK_WRITE
#define SYSCALL_REBOOT      SYS_REBOOT
#define SYSCALL_NET_STATS   SYS_NET_STATS
#define SYSCALL_MKFS        SYS_MKFS
/* V0.55.1 修复: 原值 226 与 SYS_MKFS(kernel/include/syscall.h) 撞号,
 * 表内 [SYS_HEPU_PS] 之前的 [SYS_MKFS]=wrap_mkfs 被 [SYSCALL_UMASK]=wrap_umask
 * 静默覆盖 —— 即 mkfs 系统调用实际执行的是 umask。改到空闲号 209。 */
#define SYSCALL_UMASK       209

/* ---- wait4/creations ---- */
#define SYSCALL_WAIT4       SYS_WAIT4
#define SYSCALL_FCHMOD      SYS_FCHMOD
#define SYSCALL_FCHOWN      SYS_FCHOWN
#define SYSCALL_LCHOWN      SYS_LCHOWN
#define SYSCALL_GETPGRP     SYS_GETPGRP
#define SYSCALL_VFORK       SYS_VFORK
#define SYSCALL_SETUID      SYS_SETUID
#define SYSCALL_SETGID      SYS_SETGID
#define SYSCALL_GETRUSAGE   SYS_GETRUSAGE
#define SYSCALL_RT_SIGRETURN SYS_RT_SIGRETURN
#define SYSCALL_CREAT       SYS_CREAT
#define SYSCALL_LINK        SYS_LINK

/* ---- Linux glibc 专用号（暂不统一，走翻译层） ---- */
#define SYSCALL_SCHED_YIELD   SYS_LNX64_SCHED_YIELD
#define SYSCALL_SETSOCKOPT    SYS_LNX64_SETSOCKOPT
#define SYSCALL_GETSOCKOPT    SYS_LNX64_GETSOCKOPT
#define SYSCALL_GETSOCKNAME   140  /* Linux x86_64 号 */
#define SYSCALL_GETPEERNAME   141
#define SYSCALL_SOCKETPAIR    SYS_LNX64_SOCKETPAIR
#define SYSCALL_CLONE         SYS_CLONE
#define SYSCALL_SENDTO        143
#define SYSCALL_RECVFROM      144
#define SYSCALL_SHUTDOWN      145
#define SYSCALL_SECCOMP       SYS_LNX64_SECCOMP
#define SYSCALL_DUP3          SYS_LNX64_DUP3
#define SYSCALL_SELECT        SYS_LNX64_SELECT
#define SYSCALL_MLOCK         SYS_LNX64_MLOCK
#define SYSCALL_MADVISE       SYS_LNX64_MADVISE
#define SYSCALL_SYSINFO       SYS_LNX64_SYSINFO
#define SYSCALL_ARCH_PRCTL    SYS_LNX64_ARCH_PRCTL
#define SYSCALL_GETDENTS      SYS_LNX64_GETDENTS
#define SYSCALL_SET_TID       SYS_LNX64_SET_TID
#define SYSCALL_SET_ROBUST    SYS_LNX64_SET_ROBUST
#define SYSCALL_RSEQ          SYS_LNX64_RSEQ
#define SYSCALL_READV         SYS_LNX64_READV
#define SYSCALL_WRITEV        SYS_LNX64_WRITEV
#define SYSCALL_PSELECT6      SYS_LNX64_PSELECT6
#define SYSCALL_EPOLL_CREATE  SYS_LNX64_EPOLL_CREATE
#define SYSCALL_EPOLL_CTL     SYS_LNX64_EPOLL_CTL
#define SYSCALL_EPOLL_WAIT    SYS_LNX64_EPOLL_WAIT
#define SYSCALL_EVENTFD2      SYS_LNX64_EVENTFD2
#define SYSCALL_CLOCK_GETTIME SYS_LNX64_CLOCK_GETTIME
#define SYSCALL_NEWUNAME      SYS_LNX64_NEWUNAME
#define SYSCALL_FUTEX         SYS_LNX64_FUTEX
#define SYSCALL_GETRANDOM     SYS_LNX64_GETRANDOM
#define SYSCALL_SIGACTION     SYS_LNX64_SIGACTION
#define SYSCALL_SIGPROCMASK   SYS_LNX64_SIGPROCMASK
#define SYSCALL_TGKILL        SYS_LNX64_TGKILL
#define SYSCALL_MREMAP        SYS_LNX64_MREMAP
#define SYSCALL_PRLIMIT       SYS_LNX64_PRLIMIT
#define SYSCALL_OPENAT        SYS_LNX64_OPENAT
#define SYSCALL_READLINK      SYS_LNX64_READLINK
#define SYSCALL_CLOCK_GETRES  SYS_LNX64_CLOCK_GETRES
#define SYSCALL_CLOCK_NANOSLEEP SYS_LNX64_CLOCK_NANOSLEEP
#define SYSCALL_NEWFSTATAT    SYS_LNX64_NEWFSTATAT
#define SYSCALL_PREAD         SYS_LNX64_PREAD
#define SYSCALL_PIPE2         159


/* V0.20.45: Linux 兼容 errno 常量 (集中定义, 不散落) */
#define HEPU_EPERM    (-1)
#define HEPU_ENOENT   (-2)
#define HEPU_ESRCH    (-3)
#define HEPU_EINTR    (-4)
#define HEPU_EIO      (-5)
#define HEPU_EBADF    (-9)
#define HEPU_ECHILD   (-10)
#define HEPU_EAGAIN   (-11)
#define HEPU_ENOMEM   (-12)
#define HEPU_EACCES   (-13)
#define HEPU_EFAULT   (-14)
#define HEPU_EBUSY    (-16)
#define HEPU_EEXIST   (-17)
#define HEPU_ENOTDIR  (-20)
#define HEPU_EISDIR   (-21)
#define HEPU_EINVAL   (-22)
#define HEPU_ENFILE   (-23)
#define HEPU_EMFILE   (-24)
#define HEPU_ENOSPC   (-28)
#define HEPU_EPIPE    (-32)
#define HEPU_ENOSYS   (-38)

#define MAX_FILES 16

extern void vga_puts(const char *s);
extern void vga_putchar(char c);
extern void vga_put_hex(uint64_t val);
extern int ext2_64_read_file(const char *path, uint8_t *buf, int max_size);
extern int ext2_64_write_file(const char *path, const uint8_t *data, int size);
extern int ext2_64_create_file(const char *path);
extern void rtc64_get_time(int *year, int *month, int *day,
                            int *hour, int *minute, int *second);

/* 文件描述符表 */
typedef struct {
    int used;
    char path[256];
    int offset;
    int mode;  /* 0=read, 1=write, 2=read/write */
    int is_pipe;       /* V0.20.18: 是否管道 */
    int pipe_buf;      /* pipe buffer id */
} sys_file_t;

/* V0.20.18: 简单管道缓冲 */
#define MAX_PIPES 4
#define PIPE_BUF_SIZE 4096
typedef struct {
    int used;
    uint8_t data[PIPE_BUF_SIZE];
    int head, tail;
    int readers, writers;
} pipe_t;
static pipe_t g_pipes[MAX_PIPES];

/* 安全修复：文件描述符表按用户进程槽位隔离。
 * 旧实现是全局单表，任意进程可对其他进程打开的 fd 做 read/write/close/lseek。
 * 下标 = 当前用户进程槽位（user_process.c 的 MAX_USER_PROCS=8），最后一张表给
 * "无当前用户进程"（内核上下文）。fork 子线程共享父进程的槽位，行为与之前一致。 */
#define MAX_FD_TABLES 9
extern int user_process_get_current_slot(void);
static sys_file_t sys_files_tbl[MAX_FD_TABLES][MAX_FILES];

static inline int fd_table_index(void)
{
    int slot = user_process_get_current_slot();
    return (slot >= 0 && slot < MAX_FD_TABLES - 1) ? slot : MAX_FD_TABLES - 1;
}
#define sys_files (sys_files_tbl[fd_table_index()])

/* 进程退出/被终止时清空该槽位的 fd 表（由 user_process_release 调用） */
void syscall_fd_table_reset(int slot)
{
    if (slot < 0 || slot >= MAX_FD_TABLES - 1) return;
    for (int i = 0; i < MAX_FILES; i++) sys_files_tbl[slot][i].used = 0;
}

static int syscall_initialized = 0;

static void sys_memcpy(void *dst, const void *src, int n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (int i = 0; i < n; i++) d[i] = s[i];
}

static int sys_strlen(const char *s) {
    int i = 0;
    while (s[i]) i++;
    return i;
}

/* 初始化系统调用 */
void syscall64_full_init(void)
{
    for (int t = 0; t < MAX_FD_TABLES; t++)
        for (int i = 0; i < MAX_FILES; i++)
            sys_files_tbl[t][i].used = 0;
    syscall_initialized = 1;
    vga_puts("[SYSCALL64] Full syscall interface initialized\n");
}

/* sys_write */
static int64_t sys_write(int fd, const char *buf, int count)
{
    if (count <= 0) return 0;
    if (count > 4096) count = 4096;  /* 限制单次写入大小 */

    /* SEC-05: 动态分配，消除静态缓冲区重入风险 */
    extern void *kmalloc(uint32_t size);
    extern void kfree(void *ptr);
    uint8_t *kernel_buf = (uint8_t *)kmalloc(count);
    if (!kernel_buf) return -1;
    if (copy_from_user(kernel_buf, buf, count) != 0) {
        kfree(kernel_buf);
        return -1;
    }

    if (fd == 1 || fd == 2) {  /* stdout/stderr */
        /* V0.13.43: 用户态 stdout 同时镜像到串口 (图形模式下 VGA 文本缓冲不可见) */
        for (int i = 0; i < count; i++) {
            uint8_t ch = kernel_buf[i];
            vga_putchar(ch);
            if (ch == '\n') {
                /* 换行转义 \r\n */
                extern void syscall_serial_out(uint8_t c);
                syscall_serial_out('\r');
            }
            extern void syscall_serial_out(uint8_t c);
            syscall_serial_out(ch);
        }
        kfree(kernel_buf);
        return count;
    }

    /* 文件写入 */
    if (fd >= 0 && fd < MAX_FILES && sys_files[fd].used) {
        /* V0.20.18: 管道写 */
        if (sys_files[fd].is_pipe) {
            int pid = sys_files[fd].pipe_buf;
            if (pid < 0 || pid >= MAX_PIPES) { kfree(kernel_buf); return -1; }
            pipe_t *p = &g_pipes[pid];
            int written = 0;
            for (int i = 0; i < count; i++) {
                int next = (p->tail + 1) % PIPE_BUF_SIZE;
                if (next == p->head) break;  /* 满 */
                p->data[p->tail] = kernel_buf[i];
                p->tail = next;
                written++;
            }
            kfree(kernel_buf);
            return written;
        }
        if (sys_files[fd].mode == 0) { kfree(kernel_buf); return -1; }  /* 只读打开，禁止写 */
        ext2_64_write_file(sys_files[fd].path, kernel_buf, count);
        sys_files[fd].offset += count;
        kfree(kernel_buf);
        return count;
    }

    kfree(kernel_buf);
    return -1;
}

/* sys_read */
static int64_t sys_read(int fd, char *buf, int count)
{
    if (count <= 0) return 0;
    if (count > 4096) count = 4096;

    if (fd == 0) {  /* stdin - 从键盘缓冲区读取 */
        extern int keyboard64_haschar(void);
        extern int keyboard64_getchar(void);
        int read_count = 0;
        /* 阻塞等待至少一个字符 */
        while (!keyboard64_haschar()) {
            asm volatile("hlt");  /* 等待中断 */
        }
        /* 读取可用字符（最多count个） */
        while (read_count < count && keyboard64_haschar()) {
            int c = keyboard64_getchar();
            if (c < 0) break;
            /* 安全：复制到用户空间 */
            if (copy_to_user(buf + read_count, &c, 1) != 0) {
                return -1;
            }
            read_count++;
            /* 遇到换行停止（行缓冲） */
            if (c == '\n') break;
        }
        return read_count;
    }

    /* 文件读取 */
    if (fd >= 0 && fd < MAX_FILES && sys_files[fd].used) {
        /* V0.20.18: 管道读 */
        if (sys_files[fd].is_pipe) {
            int pid = sys_files[fd].pipe_buf;
            if (pid < 0 || pid >= MAX_PIPES) return -1;
            pipe_t *p = &g_pipes[pid];
            int n = 0;
            while (n < count && p->head != p->tail) {
                uint8_t c = p->data[p->head];
                p->head = (p->head + 1) % PIPE_BUF_SIZE;
                if (copy_to_user(buf + n, &c, 1) != 0) return -1;
                n++;
            }
            return n;
        }
        if (sys_files[fd].mode == 1) return -1;   /* 只写打开，禁止读 */
        if (sys_files[fd].offset < 0) return -1;  /* 负偏移会让 file_buf+offset 越界 */
        extern void *kmalloc(uint32_t size);
        extern void kfree(void *ptr);
        uint8_t *file_buf = (uint8_t *)kmalloc(4096);
        if (!file_buf) return -1;
        /* V0.54.6: 按文件内偏移读，不再「每次只读前 4KB」。
         * 旧实现 ext2_64_read_file(path, buf, 4096) 返回的是「文件前 4KB」，
         * 偏移一旦越过 4KB 就永远返回 0 —— 动态链接器读 2MB 的 libc 时
         * 会直接卡死在这条边界上。 */
        int len = ext2_64_read_file_offset(sys_files[fd].path, file_buf,
                                          (uint32_t)count, (uint32_t)sys_files[fd].offset);
        if (len <= 0) { kfree(file_buf); return 0; }

        /* 安全：从内核缓冲区复制到用户空间 */
        if (copy_to_user(buf, file_buf, len) != 0) {
            kfree(file_buf);
            return -1;
        }
        sys_files[fd].offset += len;
        kfree(file_buf);
        return len;
    }

    return -1;
}

/* pread64(fd, buf, count, offset)：按偏移读且不改变 fd 的读指针。
 * 动态链接器（ld.so）用它在不 mmap 的情况下读 ELF 头和 PT_DYNAMIC。 */
static int64_t sys_pread(int fd, char *buf, int count, int64_t offset)
{
    if (count <= 0) return 0;
    if (count > 4096) count = 4096;
    if (offset < 0) return -1;
    if (fd < 0 || fd >= MAX_FILES || !sys_files[fd].used) return -1;
    if (sys_files[fd].mode == 1) return -1;

    extern void *kmalloc(uint32_t size);
    extern void kfree(void *ptr);
    uint8_t *kernel_buf = (uint8_t *)kmalloc((uint32_t)count);
    if (!kernel_buf) return -1;
    int len = ext2_64_read_file_offset(sys_files[fd].path, kernel_buf,
                                      (uint32_t)count, (uint32_t)offset);
    if (len <= 0) { kfree(kernel_buf); return 0; }
    if (copy_to_user(buf, kernel_buf, len) != 0) {
        kfree(kernel_buf);
        return -1;
    }
    kfree(kernel_buf);
    return len;
}

/* sys_open */
static int64_t sys_open(const char *path, int flags, int mode)
{
    /* 安全：从用户空间复制路径（NUL结尾、拒绝超长；局部缓冲区避免静态缓冲被并发覆盖） */
    char kernel_path[256];
    if (copy_user_path(kernel_path, sizeof(kernel_path), path) < 0) {
        return -1;  /* 用户地址无效或路径过长 */
    }

    /* V0.33 SEC-16: fd 0/1/2 是**保留的 stdio 槽位**。
     *
     * Linux 里 stdin/stdout/stderr 在进程启动时就已打开，open() 永远不会
     * 分配到这三个号。旧实现从 0 开始找空槽，于是进程的**第一次** open
     * 就拿到 fd=0；而 sys_read 把 fd==0 一律当 stdin 处理（阻塞等键盘）——
     * 于是「open 一个文件再 read 它」这种最普通的用法会永久卡在 hlt。
     *
     * 现场：hello4.elf（打开 /bin/hello3.elf 再读 64 字节）打开成功拿到
     * fd=0，紧接着 read(0,...) 走进 stdin 分支 → 整机停在这个进程上，
     * 启动自检电池直接断在那里。
     *
     * 这里改成从 3 开始分配，与 Linux 语义一致。 */
    for (int i = 3; i < MAX_FILES; i++) {
        if (!sys_files[i].used) {
            sys_files[i].used = 1;
            int len = sys_strlen(kernel_path);
            if (len > 255) len = 255;
            sys_memcpy(sys_files[i].path, kernel_path, len);
            sys_files[i].path[len] = 0;
            sys_files[i].offset = 0;
            sys_files[i].mode = flags & 3;

            /* 如果是创建模式，创建文件 */
            if (flags & 0x40) {  /* O_CREAT */
                ext2_64_create_file(kernel_path);
            }

            return i;
        }
    }
    return -1;
}

/* sys_close */
static int64_t sys_close(int fd)
{
    if (fd >= 0 && fd < MAX_FILES && sys_files[fd].used) {
        sys_files[fd].used = 0;
        return 0;
    }
    return -1;
}

/* sys_mkfs: 安装器格式化分区为 ext2。a1=lba a2=sectors a3=label(ptr,可空) */
extern int ext2_64_mkfs(uint32_t part_lba, uint32_t part_sectors, const char *label);
static int64_t sys_mkfs(uint32_t lba, uint32_t sectors, const char *label, uint32_t magic)
{
    /* 安全：mkfs 是高危操作。V0.54.12(R4 SC-04) 在原魔数之外加 PID==0
       临时闸——魔数是源码内可见常量，单独不足以鉴权；完整能力模型待后续。 */
    extern int user_process_get_current_pid(void);
    if (user_process_get_current_pid() != 0) return -1;
    if (magic != 0x4D4B4653u) return -1;
    char klabel[16]; klabel[0] = 0;
    if (label) copy_str_from_user(klabel, label, sizeof(klabel));
    return ext2_64_mkfs(lba, sectors, klabel[0] ? klabel : "hps");
}

/* P0-A: 用户虚拟地址窗口与分区契约。
 * 与 user_process.c（UWIN_START/UWIN_END）、sys_brk、vDSO 映射保持一致。
 * 任何 mmap/munmap 都必须先落在 [USER_WIN_BASE, USER_WIN_END) 内，
 * 否则会改写内核共享页表（PDPT[0]/高半区）——这是跨进程的破坏性越权。 */
#define USER_WIN_BASE   0x40000000ULL
#define USER_WIN_END    0x80000000ULL
#define USER_MMAP_BASE  0x64000000ULL   /* 匿名/文件映射区起点
                                         * V0.54.6: 原 0x50000000 与兼容程序区
                                         * [0x50000000,0x60000000) 重叠 —— 动态
                                         * 链接器一 mmap 就把 PIE 主程序页覆盖掉。
                                         * 移到共享库区上方，避开：兼容程序区
                                         * 0x50000000-0x60000000、解释器/库区
                                         * 0x60000000-0x70000000、TLS 预留 0x67810000。
                                         * 上界收在 TLS 预留区之前。 */
#define USER_MMAP_LIMIT 0x67800000ULL
#define USER_BRK_LO     0x68000000ULL   /* brk 堆 [0x68000000,0x6F000000) */
#define USER_BRK_HI     0x6F000000ULL
#define USER_STACK_LO   0x6FF00000ULL   /* 1MB 栈 [0x6FF00000,0x70000000) */
#define USER_STACK_HI   0x70000000ULL
#define USER_VDSO_LO    0x7FFF0000ULL   /* vDSO 页 [0x7FFF0000,0x7FFF1000) */
#define USER_VDSO_HI    0x7FFF1000ULL

/* 区间 [start,end) 必须非空且完整落在用户窗口内 */
static int user_va_range_ok(uint64_t start, uint64_t end)
{
    return end > start && start >= USER_WIN_BASE && end <= USER_WIN_END;
}

/* 区间 [start,end) 是否与保留区 [rstart,rend) 相交 */
static int user_va_range_hits(uint64_t start, uint64_t end, uint64_t rstart, uint64_t rend)
{
    return start < rend && end > rstart;
}

/* 显式地址不得覆盖 brk / 栈 / vDSO 等内核或加载器维护的保留区 */
static int user_va_is_reserved(uint64_t start, uint64_t end)
{
    return user_va_range_hits(start, end, USER_BRK_LO, USER_BRK_HI) ||
           user_va_range_hits(start, end, USER_STACK_LO, USER_STACK_HI) ||
           user_va_range_hits(start, end, USER_VDSO_LO, USER_VDSO_HI);
}

/* sys_mmap: 匿名页分配, 尊重 prot 权限
 * prot: PROT_READ=1, PROT_WRITE=2, PROT_EXEC=4
 */
static int64_t sys_mmap(uint64_t addr, uint64_t length, int prot, int flags, int fd, int64_t off)
{
    if (length == 0) return 0;
    /* 长度上限：整个窗口最多 1GB；同时避免 length+4095 溢出 */
    if (length > USER_WIN_END - USER_WIN_BASE) return -1;
    extern uint64_t user_process_current_pml4(void);
    extern int vmm64_map_page(uint64_t pml4, uint64_t virt, uint64_t phys, uint64_t flags);
    extern uint64_t pmm64_alloc_page(void);
    extern void pmm64_free_page(uint64_t phys);
    extern uint64_t vmm64_phys_to_virt(uint64_t phys);
    extern int user_process_mem_charge(uint64_t pages);
    extern void user_process_mem_uncharge(uint64_t pages);
    static uint64_t mmap_heap = USER_MMAP_BASE;
    uint64_t pml4 = user_process_current_pml4();
    if (!pml4) return -1;

    /* 文件映射：glibc 的 ld.so 用 mmap(fd, offset, MAP_PRIVATE|MAP_FIXED)
     * 把 .so 的每个 PT_LOAD 段映射进用户空间。旧实现无条件当作匿名映射，
     * 于是 ld.so 拿到的是一整片零页 —— 加载 libc 的第一步就失败。
     * 这里按「目标页虚拟地址 - 映射起点 + 文件偏移」逐页从 ext2 拷入。 */
    int file_backed = !(flags & 0x20 /* MAP_ANONYMOUS */) &&
                      fd >= 0 && fd < MAX_FILES && sys_files[fd].used;
    uint32_t file_size = 0;
    if (file_backed) {
        uint8_t isdir = 0;
        extern int ext2_64_stat(const char *path, uint32_t *size, uint8_t *isdir);
        extern int ext2_64_read_file_offset(const char *path, uint8_t *buffer,
                                            uint32_t max_size, uint32_t offset);
        if (off < 0) return -1;
        if (ext2_64_stat(sys_files[fd].path, &file_size, &isdir) != 0 || isdir) return -1;
    }

    uint64_t bytes = (length + 0xFFFULL) & ~0xFFFULL;
    uint64_t aligned;
    uint64_t end;
    uint64_t map_addr;
    if (addr) {
        aligned = addr & ~0xFFFULL;
        map_addr = addr;               /* 保留未对齐的原始地址，文件偏移要用它换算 */
        if (!user_va_range_ok(aligned, aligned + bytes)) return -1;
        if (user_va_is_reserved(aligned, aligned + bytes)) return -1;
    } else {
        /* 内核分配的匿名区：只能从 arena 向 brk 方向增长 */
        aligned = mmap_heap;
        map_addr = aligned;
        if (!user_va_range_ok(aligned, aligned + bytes)) return -1;
        if (aligned + bytes > USER_MMAP_LIMIT) return -1;
    }
    end = aligned + bytes;
    /* P1: 先记入本域配额/水位检查，超限直接拒绝 */
    uint64_t mem_pages = bytes / 4096;
    if (user_process_mem_charge(mem_pages) != 0) return -1;
    /* PTE flags: PRESENT=1, USER=4, WRITABLE=2; NX=bit63 */
    uint64_t pte_flags = 0x001 | 0x004;  /* PRESENT | USER */
    if (prot & 2) pte_flags |= 0x002;    /* PROT_WRITE */
    /* 默认 NX=1 (不可执行); PROT_EXEC 时清 NX */
    pte_flags |= (1ULL << 63);
    if (prot & 4) pte_flags &= ~(1ULL << 63);  /* PROT_EXEC */
    uint64_t mapped = aligned;
    for (uint64_t v = aligned; v < end; v += 4096) {
        uint64_t phys = pmm64_alloc_page();
        if (!phys) goto fail;
        /* V0.20.86: 清零匿名页 (malloc需要) */
        uint8_t *kvirt = (uint8_t *)vmm64_phys_to_virt(phys);
        for (int z = 0; z < 4096; z++) kvirt[z] = 0;
        if (file_backed) {
            /* 关键：addr/off 的低 12 位相同但**不一定是 0**（glibc 映射
             * libc 的第 4 个 LOAD 时 addr=...+0xC60、off=...+0xC60）。
             * 页 v 对应的文件偏移必须是 off + (v - addr)，而不是
             * off + (v - 页对齐后的 addr)——后者会整体错位 0xC60 字节，
             * 读出来的 PT_DYNAMIC 是一堆垃圾（表现为 libc「没有版本信息」）。 */
            int64_t delta = (int64_t)v - (int64_t)map_addr;
            int64_t file_off = (int64_t)off + delta;
            if (file_off >= 0 && (uint64_t)file_off < file_size) {
                uint32_t want = 4096;
                if ((uint64_t)file_off + want > file_size)
                    want = (uint32_t)(file_size - (uint64_t)file_off);
                if (ext2_64_read_file_offset(sys_files[fd].path, kvirt, want,
                                             (uint32_t)file_off) < 0) {
                    pmm64_free_page(phys);
                    goto fail;
                }
            }
        }
        if (vmm64_map_page(pml4, v, phys, pte_flags) != 0) {
            pmm64_free_page(phys);
            goto fail;
        }
        mapped = v + 4096;
    }
    /* arena 只向前推进：显式映射落在 arena 上方时，后续匿名映射从其后开始，
     * 避免与已显式映射的区间相撞；显式低地址映射不得把 arena 拉回代码区。
     * 显式映射越过 arena 上界时，直接把 arena 标记为耗尽（保守但确定）。 */
    if (!addr) {
        mmap_heap = end;
    } else if (end > mmap_heap) {
        mmap_heap = (end <= USER_MMAP_LIMIT) ? end : USER_MMAP_LIMIT;
    }
    return (int64_t)aligned;

fail:
    /* 部分失败时回收已映射页，避免"返回成功但只映射了一半" */
    {
        extern void vmm64_unmap_page(uint64_t pml4, uint64_t virt);
        extern uint64_t vmm64_virt_to_phys(uint64_t pml4, uint64_t virt);
        for (uint64_t v = aligned; v < mapped; v += 4096) {
            uint64_t phys = vmm64_virt_to_phys(pml4, v);
            vmm64_unmap_page(pml4, v);
            if (phys) pmm64_free_page(phys & ~0xFFFULL);
        }
        user_process_mem_uncharge(mem_pages);
        __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
    }
    return -1;
}

/* V0.20.90: munmap 真实回收
 * P0-A: 先校验范围，非法调用不得触碰页表（尤其不能 unmap 内核高半区）。 */
static int64_t sys_munmap(uint64_t addr, uint64_t length)
{
    if (length == 0) return 0;
    if (length > USER_WIN_END - USER_WIN_BASE) return -1;
    extern uint64_t user_process_current_pml4(void);
    extern void vmm64_unmap_page(uint64_t pml4, uint64_t virt);
    extern uint64_t vmm64_virt_to_phys(uint64_t pml4, uint64_t virt);
    extern void pmm64_free_page(uint64_t phys);
    extern void user_process_mem_uncharge(uint64_t pages);
    uint64_t pml4 = user_process_current_pml4();
    if (!pml4) return -1;
    uint64_t start = addr & ~0xFFFULL;
    uint64_t bytes = (length + 0xFFFULL) & ~0xFFFULL;
    if (!user_va_range_ok(start, start + bytes)) return -1;
    uint64_t end = start + bytes;
    uint64_t freed = 0;
    for (uint64_t v = start; v < end; v += 4096) {
        /* P1: 真实回收物理页，并同步域内存计数 */
        uint64_t phys = vmm64_virt_to_phys(pml4, v);
        vmm64_unmap_page(pml4, v);
        if (phys) { pmm64_free_page(phys & ~0xFFFULL); freed++; }
    }
    if (freed) user_process_mem_uncharge(freed);
    /* TLB flush: reload CR3 */
    __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
    return 0;
}

/* sys_mprotect: 真正改页权限
 * prot: PROT_READ=1, PROT_WRITE=2, PROT_EXEC=4
 * P0-A 补齐：与 mmap/munmap 相同的窗口/长度/溢出校验，且只作用于当前用户页表。
 */
static int64_t sys_mprotect(uint64_t addr, uint64_t len, int prot) {
    extern int vmm64_protect_page(uint64_t pml4, uint64_t virt, uint64_t prot);
    extern uint64_t user_process_current_pml4(void);
    if (len == 0) return 0;
    /* 长度上限：整个用户窗口最多 1GB；同时避免 len+4095 溢出 */
    if (len > USER_WIN_END - USER_WIN_BASE) return -1;
    uint64_t pml4 = user_process_current_pml4();
    if (!pml4) return -1;
    uint64_t start = addr & ~0xFFFULL;
    uint64_t bytes = (len + 0xFFFULL) & ~0xFFFULL;
    /* start 向下取整后仍须落在窗口内，end 不得越出窗口 */
    if (!user_va_range_ok(start, start + bytes)) return -1;
    uint64_t end = start + bytes;
    for (uint64_t v = start; v < end; v += 0x1000) {
        if (vmm64_protect_page(pml4, v, (uint64_t)prot) != 0) return -1;
    }
    return 0;
}

/* ============================================================================
 * P0-3: 权限模型（uid/gid/umask/chmod/chown）
 * 此前这些系统调用全是桩（恒返回 0）。这里给出最小可用实现：
 *   - 全局当前凭据（ruid/euid/rgid/egid + umask），与既有单用户模型一致；
 *   - chmod/chown 落到 ext2 inode 的 i_mode / i_uid / i_gid 并写回磁盘；
 *   - setuid/setgid 按 Linux 语义：仅 root 可切换到其他用户；
 *   - 权限检查：改 mode/owner 需 root 或文件属主，非 root 不能转让属主。
 * 说明：尚未实现 per-process 凭据继承（fork 后仍共享全局凭据），留待后续。
 * ========================================================================== */
static int g_cred_uid   = 0;
static int g_cred_gid   = 0;
static int g_cred_euid  = 0;
static int g_cred_egid  = 0;
static int g_cred_umask = 022;   /* 默认 022，同常见发行版 */

extern uint32_t ext2_64_lookup(const char *path);
extern int ext2_read_inode(uint32_t ino, ext2_inode_t *inode);
extern int ext2_write_inode(uint32_t ino, const ext2_inode_t *inode);
extern int ext2_64_is_mounted(void);

/* 内核路径版 chmod（kpath 为内核字符串，非用户指针） */
static int64_t do_chmod_path(const char *kpath, int mode)
{
    if (!ext2_64_is_mounted()) return HEPU_EIO;
    uint32_t ino = ext2_64_lookup(kpath);
    if (ino == 0) return HEPU_ENOENT;

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) return HEPU_EIO;

    /* root 或属主可修改权限位 */
    if (g_cred_euid != 0 && inode.i_uid != (uint16_t)g_cred_euid) return HEPU_EPERM;

    /* 保留文件类型位，替换权限位（含 setuid/setgid/sticky） */
    inode.i_mode = (uint16_t)((inode.i_mode & EXT2_S_IFMT) | (mode & 07777));
    if (ext2_write_inode(ino, &inode) != 0) return HEPU_EIO;
    return 0;
}

/* 内核路径版 chown（owner/group 为 -1 表示不修改） */
static int64_t do_chown_path(const char *kpath, int owner, int group)
{
    if (!ext2_64_is_mounted()) return HEPU_EIO;
    uint32_t ino = ext2_64_lookup(kpath);
    if (ino == 0) return HEPU_ENOENT;

    ext2_inode_t inode;
    if (ext2_read_inode(ino, &inode) != 0) return HEPU_EIO;

    if (g_cred_euid != 0) {
        /* 非 root：只能操作自己拥有的文件，不能转让属主，属组须为自己所属组 */
        if (inode.i_uid != (uint16_t)g_cred_euid) return HEPU_EPERM;
        if (owner != -1 && (uint16_t)owner != inode.i_uid) return HEPU_EPERM;
        if (group != -1 && (uint16_t)group != (uint16_t)g_cred_gid) return HEPU_EPERM;
    }

    if (owner != -1) inode.i_uid = (uint16_t)owner;
    if (group != -1) inode.i_gid = (uint16_t)group;
    if (ext2_write_inode(ino, &inode) != 0) return HEPU_EIO;
    return 0;
}

/* sys_chdir: 简化 no-op */
static int64_t sys_chdir(const char *path) {
    (void)path; return 0;
}

/* sys_dup: 简化返回原fd */
static int64_t sys_dup(int fd) { return fd; }

/* sys_umask: 保存并返回旧的 umask */
static int64_t sys_umask(int mask) {
    int old = g_cred_umask;
    g_cred_umask = mask & 0777;
    return old;
}

/* sys_gettimeofday: 返回真实时间 (timeval: sec, usec) */
static int64_t sys_gettimeofday(uint64_t tv) {
    extern uint64_t pit64_get_ticks(void);
    if (tv < 0x40000000ULL || tv >= 0x80000000ULL) return HEPU_EFAULT;
    /* Linux timeval: tv_sec (8 bytes), tv_usec (8 bytes) */
    uint64_t ticks = pit64_get_ticks();
    uint64_t sec = ticks / 1000;
    uint64_t usec = (ticks % 1000) * 1000;
    copy_to_user((void*)tv, &sec, 8);
    copy_to_user((void*)(tv + 8), &usec, 8);
    return 0;
}

/* sys_waitpid: 等子进程退出 */
static int64_t sys_waitpid(int pid, int *status, int options) {
    extern int process64_waitpid(int pid, int *status);
    extern int copy_to_user(void *user_dst, const void *kernel_src, size_t size);
    (void)options;
    int kstatus = 0;
    int ret = process64_waitpid(pid, &kstatus);
    if (ret > 0 && status) {
        copy_to_user(status, &kstatus, sizeof(int));
    }
    return ret;
}

/* V0.34.5: hep ps - 列出进程 */
struct hep_ps_entry {
    int pid;
    int ppid;
    int state;
    int priority;
    uint64_t mem_pages;
    char name[32];
};
static int64_t sys_ps(struct hep_ps_entry *entries, int max) {
    extern int copy_to_user(void *dst, const void *src, size_t size);
    if (!entries || max <= 0) return -1;
    typedef struct { int pid; int state; int priority; int cpu_id; uint64_t runtime; char name[32]; int env_type; int parent_pid; uint64_t mem_pages; } process_t;
    extern process_t *process64_get_table(void);
    process_t *pt = process64_get_table();
    struct hep_ps_entry buf[8];
    int n = 0;
    for (int i = 0; i < 64 && n < 8 && n < max; i++) {
        if (pt[i].state == 0) continue;
        buf[n].pid = pt[i].pid;
        buf[n].ppid = pt[i].parent_pid;
        buf[n].state = pt[i].state;
        buf[n].priority = pt[i].priority;
        buf[n].mem_pages = pt[i].runtime;
        for (int j = 0; j < 32; j++) buf[n].name[j] = pt[i].name[j];
        buf[n].name[31] = 0;   /* V0.57.2: 强制终止符，源码 name 满 32 字节时
                                * 用户态 strlen 会越过数组读到栈顶之外 → #PF */
        n++;
    }
    copy_to_user(entries, buf, n * sizeof(struct hep_ps_entry));
    return n;
}

/* V0.20.23: 返回当前凭据 */
static int64_t sys_getuid(void) { return g_cred_uid; }
static int64_t sys_getgid(void) { return g_cred_gid; }
static int64_t sys_geteuid(void) { return g_cred_euid; }
static int64_t sys_getegid(void) { return g_cred_egid; }

/* V0.20.55: sched_yield */
static int64_t sys_sched_yield(void) {
    extern void process64_yield(void);
    process64_yield();
    return 0;
}

/* V0.20.56: socket syscall 桩 (返回假 fd) */
static int64_t sys_socket(int domain, int type, int protocol) {
    (void)domain;(void)type;(void)protocol;
    return 10; /* 假 socket fd */
}
static int64_t sys_connect(int fd, uint64_t addr, int addrlen) {
    (void)fd;(void)addr;(void)addrlen;
    return 0;
}
static int64_t sys_sendto(int fd, uint64_t buf, int len, int flags, uint64_t dest, int destlen) {
    (void)fd;(void)buf;(void)flags;(void)dest;(void)destlen;
    return len;
}
static int64_t sys_recvfrom(int fd, uint64_t buf, int len, int flags, uint64_t src, int *srclen) {
    (void)fd;(void)buf;(void)len;(void)flags;(void)src;(void)srclen;
    return 0;
}

/* V0.20.57: socket 选项桩 */
static int64_t sys_shutdown(int fd, int how) { (void)fd;(void)how; return 0; }
static int64_t sys_setsockopt(int fd, int level, int optname, uint64_t optval, int optlen) {
    (void)fd;(void)level;(void)optname;(void)optval;(void)optlen; return 0;
}
static int64_t sys_getsockopt(int fd, int level, int optname, uint64_t optval, uint64_t optlen) {
    (void)fd;(void)level;(void)optname;(void)optval;(void)optlen; return 0;
}

/* V0.20.61: getrusage / wait4 桩 */
static int64_t sys_getrusage(int who, uint64_t usage) {
    (void)who;
    if (usage < 0x40000000ULL || usage >= 0x80000000ULL) return HEPU_EFAULT;
    uint8_t buf[144];
    for (int i = 0; i < 144; i++) buf[i] = 0;
    for (int i = 0; i < 144; i++) copy_to_user((void*)(usage+i), &buf[i], 1);
    return 0;
}
static int64_t sys_wait4(int pid, uint64_t status, int options, uint64_t rusage) {
    (void)pid;(void)status;(void)options;(void)rusage;
    return 0;
}

/* V0.20.64: chmod/fchmod - 落到 ext2 inode 的 i_mode */
static int64_t sys_chmod(const char *path, int mode) {
    char kpath[256];
    if (copy_user_path(kpath, sizeof(kpath), path) < 0) return HEPU_EFAULT;
    return do_chmod_path(kpath, mode);
}
static int64_t sys_fchmod(int fd, int mode) {
    if (fd < 0 || fd >= MAX_FILES || !sys_files[fd].used || sys_files[fd].is_pipe)
        return HEPU_EBADF;
    return do_chmod_path(sys_files[fd].path, mode);
}

/* V0.20.72: 批量补充桩函数 */
static int64_t sys_rt_sigreturn(void) { return 0; }
static int64_t sys_accept(int s, uint64_t addr, uint64_t addrlen) { (void)s;(void)addr;(void)addrlen; return -1; }
static int64_t sys_bind(int s, uint64_t addr, int addrlen) { (void)s;(void)addr;(void)addrlen; return 0; }
static int64_t sys_listen(int s, int backlog) { (void)s;(void)backlog; return 0; }
static int64_t sys_getsockname(int s, uint64_t addr, uint64_t addrlen) { (void)s;(void)addr;(void)addrlen; return 0; }
static int64_t sys_getpeername(int s, uint64_t addr, uint64_t addrlen) { (void)s;(void)addr;(void)addrlen; return 0; }
static int64_t sys_socketpair(int domain, int type, int proto, int fds) { (void)domain;(void)type;(void)proto;(void)fds; return -1; }
/* V0.57.1: clone 诚实化（原实现从未生效，且偏移全错）
 *
 * 原实现用一组魔数偏移写 user_process_t：
 *   写 active(+8) / rflags(+24) / page_table(+24) / blocked(+88) / futex(+96)。
 * 但 user_process.c 中真实布局是：
 *   pid(0) active(4) rip(8) rsp(16) rflags(24) page_table(32) ...
 * 于是：active 被写到 rip 位（+8），真正的 active(+4) 永远是 0 →
 *   **那个槽位永远不会被调度**，clone 只是返回了一个 pid，等于空实现；
 * 同时 page_table 被写成 rflags（+24），真正的 page_table(+32) 从未赋值 →
 *   一旦有人改成"置 active=1"，子线程会带着脏页表跑，属更危险的坏状态。
 *
 * 真正的线程创建还需要：子线程入口 trampoline（Linux 语义是子方返回 0 并跳到
 * stack 上的 start_routine）、每线程独立内核栈、子 rax 清零、以及共享页表的
 * 引用计数。这些都没有实现，所以这里明确返回 ENOSYS(-38)，让调用方（glibc
 * pthread_create 等）立刻得到错误，而不是拿到一个永远不运行的假线程。
 *
 * 参考：fork 路径是完整可用的（sys_fork → uaspace_fork_copy，真 COW）。
 * 需要并发时请用 fork + 共享内存/管道，不要依赖 clone。
 */
/* clone(2) —— V0.57.3：真线程（共享地址空间）
 *
 * 与 fork 的根本区别：**不复制页表**。子线程和父线程是同一个地址空间里的
 * 两条执行流，子线程通过调用方传入的 stack 换栈，从 clone 的返回点继续跑，
 * 内核返回值 rax=0；父线程拿到子线程 pid。
 *
 * 四块此前缺失的东西，这里逐条补上：
 *   1) 子线程入口 trampoline —— entry_64.S 的 clone_trampoline_64，负责恢复
 *      GPR、装用户栈、写 FS_BASE、rax 清零，再 sysretq。
 *   2) 独立内核栈 —— 每次 clone 分配一页，trampoline 把它的栈顶装进
 *      g_syscall_kstack_slot，父子各用各的，不会把同一块内核栈写花。
 *   3) 子 rax 清零 —— 由 trampoline 的 `xorq %rax,%rax` 保证。
 *   4) 共享页表引用计数 —— user_process_clone_track()/untrack() 维护，
 *      父线程退出时若还有子线程活着，先撤销线程再释放页表（见 user_process.c）。
 *
 * 不支持的情形一律返回 ENOSYS，不做半吊子实现：
 *   - 没有 CLONE_VM（那应该用 fork 的复制语义）
 *   - 当前不是隔离地址空间的 ELF 进程（没有可共享的页表）
 */
/* 见 sys_clone 内的说明：用户线程还不是调度单元，默认关闭 */
static int g_clone_threads_enabled = 0;

#define CLONE_VM             0x00000100
#define CLONE_FS             0x00000200
#define CLONE_FILES          0x00000400
#define CLONE_SIGHAND        0x00000800
#define CLONE_THREAD         0x00010000
#define CLONE_SETTLS         0x00080000
#define CLONE_PARENT_SETTID  0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000
#define CLONE_CHILD_SETTID   0x01000000

static int64_t sys_clone(int flags, uint64_t stack, uint64_t parent_tid,
                         uint64_t child_tid, uint64_t tls) {
    extern int copy_to_user(void *dst, const void *src, size_t size);
    extern uint64_t user_process_current_pml4(void);
    extern int user_process_clone_track(int thread_pid);
    extern uint64_t pmm64_alloc_page(void);
    extern void pmm64_free_page(uint64_t addr);

    /* V0.57.3 总开关：默认关闭，返回 ENOSYS。
     *
     * 下面这套实现本身是完整的（共享页表 + 独立内核栈 + 子 rax=0 +
     * trampoline + 页表引用计数），QEMU 实测能把线程建出来：
     *   [PROC64] Created process: clone_thr (pid=4, prio=2)
     *   [CLONE] created thread pid=0x4 (shared pml4, stack=0x6FF80000)
     * 但**子线程拿不到 CPU** —— 这不是 clone 自身的问题，而是调度模型的
     * 缺口：process64_tick 只轮转 process64 的内核线程，用户进程
     * （user_process_switch 进去的那条执行流）根本不在调度表里；
     * smp.c::smp_timer_tick 里 AP 调度还被 `&& 0` 硬关掉。
     * 结果就是父线程一跑到底、子线程永远 READY，父线程忙等超时后
     * 还会因为被 context_switch 抽走而没有恢复路径（实测 rip=0 / CR2=0）。
     *
     * 要真正可用，前置条件是"用户线程成为调度单元"：为每条用户执行流
     * 保存 rip/rsp/rflags/GPR/fsbase，并由中断里的调度器轮转。
     * 在那之前打开这个开关只会让用户程序卡死或崩，故默认 0。 */
    if (!g_clone_threads_enabled) {
        return HEPU_ENOSYS;
    }

    if (!(flags & CLONE_VM)) {
        /* 没有共享地址空间就不是线程，交给 fork */
        return HEPU_ENOSYS;
    }
    uint64_t cur_pml4 = user_process_current_pml4();
    if (!cur_pml4) {
        return HEPU_ENOSYS;   /* 非 ELF 隔离进程，没有可共享的页表 */
    }
    /* 用户给的栈顶必须落在用户窗口内，且 16 字节对齐（SysV 入口约定） */
    if (!stack || stack < 0x40000000ULL || stack >= 0x80000000ULL) {
        return HEPU_EINVAL;
    }

    extern uint64_t g_syscall_ret_rip;
    extern uint64_t g_syscall_ret_rflags;
    extern uint64_t g_syscall_kernel_rsp;
    extern uint64_t g_clone_child_rip;
    extern uint64_t g_clone_child_rsp;
    extern uint64_t g_clone_child_rflags;
    extern uint64_t g_clone_child_tls;
    extern uint64_t g_clone_child_kstack;
    extern uint64_t g_clone_child_gprs[15];

    uint64_t kstack = pmm64_alloc_page();
    if (!kstack) return HEPU_EAGAIN;

    g_clone_child_rip = g_syscall_ret_rip;          /* 子线程从返回点继续 */
    g_clone_child_rsp = stack;
    g_clone_child_rflags = g_syscall_ret_rflags | 0x200;  /* IF 必须开，否则子线程收不到时钟 */
    g_clone_child_tls = (flags & CLONE_SETTLS) ? tls : 0;
    g_clone_child_kstack = kstack + 4096;           /* 栈顶 */

    /* 父线程 GPR 快照，布局与 entry_64.S 的 push 顺序一致：
     * [0]=r15 [1]=r14 [2]=r13 [3]=r12 [4]=r11 [5]=r10 [6]=r9 [7]=r8
     * [8]=rbp [9]=rdi [10]=rsi [11]=rdx [12]=rcx [13]=rbx [14]=rax */
    for (int i = 0; i < 15; i++)
        g_clone_child_gprs[i] = ((uint64_t *)g_syscall_kernel_rsp)[i];

    extern void clone_trampoline_64(void);
    extern int process64_create(const char *name, void (*entry)(void), int priority);
    int child_pid = process64_create("clone_thr", clone_trampoline_64, 2);
    if (child_pid < 0) {
        pmm64_free_page(kstack);
        return HEPU_EAGAIN;
    }

    user_process_clone_track(child_pid);

    if ((flags & CLONE_PARENT_SETTID) && parent_tid) {
        int v = child_pid;
        copy_to_user((void *)parent_tid, &v, sizeof(int));
    }
    if ((flags & CLONE_CHILD_SETTID) && child_tid) {
        int v = child_pid;
        copy_to_user((void *)child_tid, &v, sizeof(int));
    } else if ((flags & CLONE_CHILD_CLEARTID) && child_tid) {
        int zero = 0;
        copy_to_user((void *)child_tid, &zero, sizeof(int));
    }

    vga_puts("[CLONE] created thread pid=");
    vga_put_hex((uint64_t)child_pid);
    vga_puts(" (shared pml4, stack=0x");
    vga_put_hex(stack);
    vga_puts(")\n");
    return child_pid;
}
static int64_t sys_creat(const char *path, int mode) { (void)path;(void)mode; return 3; }
static int64_t sys_link(const char *oldp, const char *newp) { (void)oldp;(void)newp; return 0; }
static int64_t sys_chown(const char *path, int owner, int group) {
    char kpath[256];
    if (copy_user_path(kpath, sizeof(kpath), path) < 0) return HEPU_EFAULT;
    return do_chown_path(kpath, owner, group);
}
static int64_t sys_fchown(int fd, int owner, int group) {
    if (fd < 0 || fd >= MAX_FILES || !sys_files[fd].used || sys_files[fd].is_pipe)
        return HEPU_EBADF;
    return do_chown_path(sys_files[fd].path, owner, group);
}
static int64_t sys_lchown(const char *path, int owner, int group) {
    /* 简化文件系统不跟踪符号链接，与 chown 一致 */
    char kpath[256];
    if (copy_user_path(kpath, sizeof(kpath), path) < 0) return HEPU_EFAULT;
    return do_chown_path(kpath, owner, group);
}
static int64_t sys_getpgrp(void) { return 1; }
static int64_t sys_vfork(void) { return 0; }
/* setuid/setgid：root 可切换任意用户；非 root 只能切换到当前 uid/gid */
static int64_t sys_setuid(int uid) {
    if (uid < 0) return HEPU_EINVAL;
    if (g_cred_euid != 0 && uid != g_cred_uid && uid != g_cred_euid) return HEPU_EPERM;
    g_cred_uid = g_cred_euid = uid;
    return 0;
}
static int64_t sys_setgid(int gid) {
    if (gid < 0) return HEPU_EINVAL;
    if (g_cred_euid != 0 && gid != g_cred_gid && gid != g_cred_egid) return HEPU_EPERM;
    g_cred_gid = g_cred_egid = gid;
    return 0;
}

/* P0-4: seccomp(2) —— 仅实现 STRICT 模式与动作查询；BPF 过滤器暂不支持 */
static int64_t sys_seccomp(unsigned int op, unsigned int flags, uint64_t args)
{
    extern int user_process_get_current_slot(void);
    int slot = user_process_get_current_slot();

    if (flags != 0) return HEPU_EINVAL;

    switch (op) {
    case SECCOMP_SET_MODE_STRICT:
        /* 已启用后不可重复设置（Linux 亦如此） */
        if (seccomp64_get_mode(slot) != SECCOMP_MODE_DISABLED) return HEPU_EINVAL;
        /* 暂以 root 作为启用条件（尚无 no_new_privs 跟踪） */
        if (g_cred_euid != 0) return HEPU_EPERM;
        seccomp64_enable_strict(slot);
        vga_puts("[SECCOMP] strict mode enabled for slot ");
        print_syscall_number(slot);
        vga_puts("\n");
        return 0;

    case SECCOMP_SET_MODE_FILTER: {
        /* P0-5: 安装 cBPF 过滤器。与 strict 一致，已启用后不可重复设置。 */
        if (seccomp64_get_mode(slot) != SECCOMP_MODE_DISABLED) return HEPU_EINVAL;
        if (g_cred_euid != 0) return HEPU_EPERM;
        if (!args) return HEPU_EFAULT;

        seccomp_fprog_t prog;
        if (copy_from_user(&prog, (const void *)args, sizeof(prog)) != 0)
            return HEPU_EFAULT;
        if (prog.len == 0 || prog.len > SECCOMP_FILTER_MAX_INSNS || !prog.filter)
            return HEPU_EINVAL;

        /* 逐条拷贝到内核缓冲区后再校验，避免把用户指针留存到分发路径 */
        static seccomp_insn_t filter_tmp[SECCOMP_FILTER_MAX_INSNS];
        for (unsigned int i = 0; i < prog.len; i++) {
            uint64_t addr = prog.filter + (uint64_t)i * sizeof(seccomp_insn_t);
            if (copy_from_user(&filter_tmp[i], (const void *)addr,
                               sizeof(seccomp_insn_t)) != 0)
                return HEPU_EFAULT;
        }

        if (seccomp64_set_filter(slot, filter_tmp, prog.len) != 0) {
            vga_puts("[SECCOMP] invalid BPF filter rejected\n");
            return HEPU_EINVAL;
        }
        vga_puts("[SECCOMP] filter mode enabled for slot ");
        print_syscall_number(slot);
        vga_puts(" (");
        print_syscall_number((int64_t)prog.len);
        vga_puts(" insns)\n");
        return 0;
    }

    case SECCOMP_GET_ACTION_AVAIL: {
        if (!args) return HEPU_EFAULT;
        uint32_t action = 0;
        if (copy_from_user(&action, (const void *)args, 4) != 0) return HEPU_EFAULT;
        /* 已实现的动作：ALLOW / LOG（放行）、ERRNO、KILL_THREAD / KILL_PROCESS。
         * TRAP / TRACE / USER_NOTIF 未实现，明确报告不可用。 */
        if (action == SECCOMP_RET_ALLOW || action == SECCOMP_RET_LOG ||
            action == SECCOMP_RET_ERRNO || action == SECCOMP_RET_KILL_THREAD ||
            action == SECCOMP_RET_KILL_PROCESS)
            return 0;
        return HEPU_EINVAL;
    }

    default:
        return HEPU_EINVAL;
    }
}
static int64_t sys_dup3(int oldfd, int newfd, int flags) { (void)flags; return newfd; }
static int64_t sys_select(int nfds, uint64_t readfds, uint64_t writefds, uint64_t exceptfds, uint64_t timeout) {
    (void)nfds;(void)readfds;(void)writefds;(void)exceptfds;(void)timeout;
    return 0;
}

/* V0.20.73: ppoll 真实桩 — glibc 启动会调 */
/* V0.55.1: ppoll 实现已就绪但尚未挂入 syscall 表(预留) */
static __attribute__((unused)) int64_t sys_ppoll(uint64_t fds, int nfds, uint64_t timeout, uint64_t sigmask) {
    (void)fds;(void)nfds;(void)timeout;(void)sigmask; return 0;
}

/* V0.20.76: mlock/madvise 桩 */
static int64_t sys_mlock(uint64_t addr, uint64_t len) { (void)addr;(void)len; return 0; }
static int64_t sys_madvise(uint64_t addr, uint64_t len, int advice) { (void)addr;(void)len;(void)advice; return 0; }

/* V0.20.25: sysinfo - 写内存信息到用户指针 */
static int64_t sys_sysinfo(uint64_t out) {
    if (out < 0x40000000ULL || out >= 0x80000000ULL) return -1;
    uint64_t buf[8] = { 512*1024, 256*1024, 0, 0, 0, 0, 0, 0 }; /* total_kb, free_kb */
    for (int i = 0; i < 8; i++) {
        if (copy_to_user((void*)(out + i*8), &buf[i], 8) != 0) return -1;
    }
    return 0;
}

/* V0.20.26: ioctl - 永远返回 0 (很多程序需要这个桩) */
static int64_t sys_ioctl(int fd, uint64_t req, uint64_t arg) {
    (void)fd; (void)req; (void)arg;
    return 0;
}

/* V0.20.75: arch_prctl — 真实现 ARCH_SET_FS (TLS) */
static int64_t sys_arch_prctl(int code, uint64_t addr) {
#ifdef HEPU_TRACE_LINUX_SYSCALL
    vga_puts("[PRCTL] code="); print_syscall_number(code);
    vga_puts(" addr="); vga_put_hex(addr);
    vga_puts("\n");
#endif
    if (code == 0x1002 /* ARCH_SET_FS */) {
        /* 写 FS_BASE MSR (0xC0000100) */
        uint32_t lo = (uint32_t)addr;
        uint32_t hi = (uint32_t)(addr >> 32);
        __asm__ volatile("wrmsr" : : "c"(0xC0000100), "a"(lo), "d"(hi));
#ifdef HEPU_TRACE_LINUX_SYSCALL
        {
            uint32_t rlo = 0, rhi = 0;
            __asm__ volatile("rdmsr" : "=a"(rlo), "=d"(rhi) : "c"(0xC0000100));
            vga_puts("[PRCTL] FS_BASE readback="); vga_put_hex(((uint64_t)rhi << 32) | rlo);
            vga_puts("\n");
        }
#endif
        /* V0.33: 同时记到进程上 —— 切回该进程时要能恢复 FS_BASE，
         * 否则线程指针在进程切换后就丢了（glibc 的 TLS 直接崩）。 */
        extern void user_process_set_fsbase(uint64_t base);
        user_process_set_fsbase(addr);
        return 0;
    }
    if (code == 0x1003 /* ARCH_GET_FS */) {
        uint32_t lo, hi;
        __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000100));
        return (int64_t)((uint64_t)hi << 32 | lo);
    }
    /* V0.33: CPUID 相关（Linux arch_prctl 语义）
     *
     * 旧实现对 0x3001 返回 **-1**。在原始 syscall ABI 里 -1 表示
     * 「通用错误」，errno 要靠别的手段猜；而 Linux 的 arch_prctl 明确规定：
     *   - ARCH_SET_CPUID (0x3001)：硬件不支持 CPUID faulting ⇒ ENODEV (-19)
     *   - ARCH_GET_CPUID (0x3002)：返回 CPUID 是否可用（1 = 可用）
     *   - 其它 code                 ⇒ EINVAL (-22)
     * 见 man7 arch_prctl(2)。
     *
     * 为什么要改：glibc 启动时会调 arch_prctl(ARCH_SET_CPUID, 0)。
     * 旧实现返回 -1，配合「-1 = 未处理」的歧义，上层把这次正常调用
     * 误报成 "[SYSCALL] Unknown syscall: 9E"，日志凭空多一条假故障。
     * 现在返回符合 Linux 语义的 errno，语义确定、不冒充。 */
    if (code == 0x3001) return -19;   /* -ENODEV：本内核不实现 CPUID faulting */
    if (code == 0x3002) return 1;     /* ARCH_GET_CPUID：CPUID 可用 */
    /* 未知 code：Linux 规定 EINVAL */
    vga_puts("[ARCH_PRCTL unknown code=");
    print_syscall_number(code);
    vga_puts("]\n");
    return -22;  /* -EINVAL */
}

/* V0.20.84: getdents64 - 读取ext2根目录 */
static int64_t sys_getdents(int fd, uint64_t dirp, int count) {
    (void)fd;
    /* Linux dirent64: ino(8) + off(8) + reclen(2) + type(1) + namelen(1) + name[] */
    if (!ext2_64_is_mounted()) return -9;  /* -EBADF */
    /* 用户窗口校验：旧实现直接解引用 dirp 写用户地址，SMAP 下会内核 #PF */
    if (dirp < 0x40000000ULL || dirp >= 0x80000000ULL) return -1;
    if (count < 19) return 0;
    extern int ext2_read_data_block(ext2_inode_t *inode, uint32_t block_idx, uint8_t *buffer);
    extern uint32_t ext2_block_size;
    typedef struct { uint32_t inode; uint16_t rec_len; uint8_t name_len; uint8_t file_type; char name[]; } ext2_dirent_t;

    uint8_t dir_buf[4096];
    /* 读root inode */
    uint8_t root_inode[128];
    if (ext2_read_inode(2, (ext2_inode_t *)root_inode) != 0) return -9;
    uint32_t block_size = ext2_block_size;
    if (block_size == 0) block_size = 1024;

    /* 只读第一个目录块 */
    if (ext2_read_data_block((ext2_inode_t*)root_inode, 0, dir_buf) != 0) return -9;

    int total = 0;
    uint32_t offset = 0;
    /* 单条目内核缓冲：最大 reclen = (19+255) 向上 8 字节对齐 = 280 */
    uint8_t kentry[288];
    while (offset < block_size && total < count - 19) {
        ext2_dirent_t *e = (ext2_dirent_t *)(dir_buf + offset);
        if (e->rec_len == 0) break;
        if (e->name_len > 0 && e->inode != 0) {
            int name_len = e->name_len;
            if (name_len > 255) name_len = 255;
            int reclen = (19 + name_len + 7) & ~7;  /* 8字节对齐 */
            if (total + reclen > count) break;
            /* 先在内核缓冲构造条目，再 copy_to_user，不直写用户指针 */
            memset(kentry, 0, sizeof(kentry));
            uint64_t d_ino = e->inode;
            int64_t d_off = (int64_t)(offset + e->rec_len);
            uint16_t d_reclen = (uint16_t)reclen;
            memcpy(kentry + 0, &d_ino, 8);    /* d_ino  @0  */
            memcpy(kentry + 8, &d_off, 8);    /* d_off  @8  */
            memcpy(kentry + 16, &d_reclen, 2);/* d_reclen @16 */
            kentry[18] = e->file_type;        /* d_type @18 */
            memcpy(kentry + 19, e->name, name_len);  /* d_name @19 */
            kentry[19 + name_len] = 0;
            if (copy_to_user((void *)(dirp + total), kentry, (uint32_t)reclen) != 0)
                return total > 0 ? total : -1;
            total += reclen;
        }
        offset += e->rec_len;
    }
    return total;
}

/* V0.20.31: set_tid_address - glibc 启动桩 */
static int64_t sys_set_tid_address(uint64_t tidptr) { (void)tidptr; return 100; }
static int64_t sys_set_robust_list(void) { return 0; }
static int64_t sys_rseq(void) { return 0; }

/* V0.20.89: readv/writev 真实实现 */
static int64_t sys_readv(int fd, uint64_t iov, int iovcnt) {
    (void)fd;(void)iov;(void)iovcnt; return 0;
}
/* V0.54.12(R4 SC-01): writev 安全加固。
 * 旧实现 ring0 直解用户 iov/iov_base（无 copy_from_user），iovcnt 无上限、
 * per-iov len 为无界 uint64，可触发内核死循环/ring0 缺页并把用户任意地址
 * 内容外发。现在：iovcnt 上限 1024、单次总字节封顶 1MB，所有用户内存走
 * copy_from_user（窗口+映射校验），分块处理避免大栈占用。 */
#define WRITEV_MAX_IOV      1024
#define WRITEV_MAX_TOTAL    (1024*1024)
#define WRITEV_IOV_CHUNK    64   /* 一次拷 64 个 iovec = 1KB 栈 */
#define WRITEV_BUF_CHUNK    512  /* 数据缓冲 512B */
static int64_t sys_writev(int fd, uint64_t iov, int iovcnt) {
    extern void syscall_serial_out(uint8_t c);
    if (iovcnt <= 0 || iovcnt > WRITEV_MAX_IOV) return -1;
    if (!hepu_is_user_address(iov, (size_t)iovcnt * 16)) return -1;

    uint64_t k_iov[WRITEV_IOV_CHUNK * 2];  /* 每项 base+len */
    uint8_t kbuf[WRITEV_BUF_CHUNK];
    int total = 0;

    for (int base_i = 0; base_i < iovcnt; base_i += WRITEV_IOV_CHUNK) {
        int chunk = iovcnt - base_i;
        if (chunk > WRITEV_IOV_CHUNK) chunk = WRITEV_IOV_CHUNK;
        /* 拷贝本批 iovec（copy_from_user 校验窗口与映射） */
        if (copy_from_user(k_iov, (const void *)(iov + (uint64_t)base_i*16),
                           (size_t)chunk*16) != 0) {
            return -1;
        }
        for (int i = 0; i < chunk; i++) {
            uint64_t ubase = k_iov[i*2];
            uint64_t ulen  = k_iov[i*2 + 1];
            /* 窗口外的 iov_base 跳过，保持原 continue 语义 */
            if (!hepu_is_user_address(ubase, 0)) continue;
            /* 累计总长度封顶 */
            uint64_t remain = ulen;
            if ((uint64_t)total + remain > WRITEV_MAX_TOTAL) {
                remain = WRITEV_MAX_TOTAL - (uint64_t)total;
            }
            uint64_t off = 0;
            while (remain > 0) {
                uint64_t n = remain;
                if (n > WRITEV_BUF_CHUNK) n = WRITEV_BUF_CHUNK;
                if (copy_from_user(kbuf, (const void *)(ubase + off), (size_t)n) != 0) {
                    return -1;
                }
                for (uint64_t j = 0; j < n; j++) {
                    char c = (char)kbuf[j];
                    if (fd == 1 || fd == 2) {
                        vga_putchar(c);
                        if (c == '\n') syscall_serial_out('\r');
                        syscall_serial_out(c);
                    }
                }
                off += n;
                remain -= n;
                total += (int)n;
                if (total >= WRITEV_MAX_TOTAL) return total;
            }
        }
    }
    return total;
}
static int64_t sys_pselect6(void) { return 0; }

/* V0.20.37: epoll 桩 */
static int64_t sys_epoll_create(int flags) { (void)flags; return 0; }
static int64_t sys_epoll_ctl(void) { return 0; }
static int64_t sys_epoll_wait(void) { return 0; }

/* V0.20.38: eventfd/clock_gettime/uname 桩 */
static int64_t sys_eventfd2(void) { return 0; }
static int64_t sys_clock_gettime(int clockid, uint64_t ts) {
    extern uint64_t pit64_get_ticks(void);
    if (ts < 0x40000000ULL || ts >= 0x80000000ULL) return HEPU_EFAULT;
    (void)clockid;
    /* Linux timespec: tv_sec (8), tv_nsec (8) */
    uint64_t ticks = pit64_get_ticks();
    uint64_t sec = ticks / 1000;
    uint64_t nsec = (ticks % 1000) * 1000000;
    copy_to_user((void*)ts, &sec, 8);
    copy_to_user((void*)(ts + 8), &nsec, 8);
    return 0;
}
static int64_t sys_newuname(uint64_t buf) {
    vga_puts("[UNAME buf=0x");
    print_syscall_number(buf);
    vga_puts("]\n");
    if (buf < 0x40000000ULL || buf >= 0x80000000ULL) return -1;
    const char *names[6] = {"Linux", "localhost", "6.0.0", "1", "x86_64", "*"};
    for (int i = 0; i < 6; i++) {
        char field[65];
        for (int j = 0; j < 65; j++) field[j] = 0;
        int k = 0; while (names[i][k] && k < 64) { field[k] = names[i][k]; k++; }
        copy_to_user((void*)(buf + i*65), field, 65);
    }
    return 0;
}

/* V0.20.85: futex 真实实现 */
static int64_t sys_futex(uint64_t uaddr, int op, uint32_t val, uint64_t timeout, uint64_t uaddr2, uint32_t val3) {
    (void)timeout; (void)uaddr2; (void)val3;
    extern int hepu_futex_wait(uint64_t uaddr, uint32_t val);
    extern int hepu_futex_wake(uint64_t uaddr, int n);
    if (op == 0) {  /* FUTEX_WAIT */
        int r = hepu_futex_wait(uaddr, val);
        if (r < 0) return r;
        return 0;
    } else if (op == 1) {  /* FUTEX_WAKE */
        return hepu_futex_wake(uaddr, val);
    }
    return -1;
}
static int64_t sys_getrandom(uint64_t buf, int64_t len, uint64_t flags) {
    (void)flags;
    if (buf < 0x40000000ULL || buf >= 0x80000000ULL) return -1;
    if (len <= 0) return 0;
    if (len > 1024) len = 1024;

    /* 密码学安全随机数：使用内核 ChaCha20 CSPRNG。旧实现返回可预测的
     * 确定性序列 (b=i*17+3)，无法用于密钥/nonce/栈金丝雀等安全用途。 */
    extern int csprng_get_bytes(uint8_t *buffer, uint32_t n);
    extern void *kmalloc(uint32_t size);
    extern void kfree(void *ptr);
    uint8_t *kbuf = (uint8_t *)kmalloc((uint32_t)len);
    if (!kbuf) return -1;
    if (csprng_get_bytes(kbuf, (uint32_t)len) != 0) {
        kfree(kbuf);
        return -1;
    }
    if (copy_to_user((void *)buf, kbuf, (uint32_t)len) != 0) {
        kfree(kbuf);
        return -1;
    }
    kfree(kbuf);
    return len;
}

/* V0.20.41: dup2 桩 (mkdir/rmdir 已有) */
static int64_t sys_dup2(int oldfd, int newfd) { (void)oldfd;(void)newfd; return newfd; }

/* V0.54.12(R4 SC-05): 正确解析用户 struct sigaction。
 * 布局与 glibc x86_64 一致：sa_handler@0、sa_mask@8(128B)、sa_flags@136、
 * sa_restorer@144（共 152B）。旧实现把 act 指针当 handler 存储，信号投递时
 * RIP 跳到用户栈上的 sigaction 数据地址，信号功能整体失效。 */
struct k_sigaction {
    void (*sa_handler)(int);
    uint64_t sa_mask[16];
    int sa_flags;
    void (*sa_restorer)(void);
};
static int64_t sys_sigaction(int signum, const void *act, void *oldact) {
    extern int user_process_get_current_pid(void);
    extern void process64_set_signal_handler(int pid, int signum, uint64_t handler);
    extern uint64_t process64_get_handler(int pid, int signum);
    int pid = user_process_get_current_pid();
    if (signum < 1 || signum >= 32) return -1;

    if (act) {
        struct k_sigaction kact;
        if (copy_from_user(&kact, act, sizeof(kact)) != 0) return -1;
        uint64_t handler = (uint64_t)kact.sa_handler;
        /* 0=SIG_DFL、1=SIG_IGN 为合法特殊值；其余必须落在用户窗口 */
        if (handler > 1 && !hepu_is_user_address(handler, 1)) return -1;
        process64_set_signal_handler(pid, signum, handler);
    }
    if (oldact) {
        struct k_sigaction kold;
        for (int i = 0; i < 16; i++) kold.sa_mask[i] = 0;
        kold.sa_flags = 0;
        kold.sa_restorer = 0;
        kold.sa_handler = (void (*)(int))process64_get_handler(pid, signum);
        if (copy_to_user(oldact, &kold, sizeof(kold)) != 0) return -1;
    }
    return 0;
}
static int64_t sys_sigprocmask(void) { return 0; }
static int64_t sys_tgkill(int tgid, int tid, int sig) {
    extern int user_process_kill(int pid);
    extern int user_process_get_current_pid(void);
    extern int process64_is_child(int pid, int parent);
    int self = user_process_get_current_pid();
    (void)tid;(void)sig;
    if (tgid <= 0) return -1;
    if (tgid == self) return user_process_kill(tgid);
    if (process64_is_child(tgid, self)) return user_process_kill(tgid);
    return -1;
}

/* kill(pid, sig) = tgkill(-1, pid, sig) */
static int64_t sys_kill(int pid, int sig) {
    extern int user_process_kill(int p);
    extern int user_process_get_current_pid(void);
    extern int process64_is_child(int p, int parent);
    extern void process64_send_signal(int pid, int signum);
    int self = user_process_get_current_pid();
    if (pid <= 0) return -1;
    /* SIGKILL(9) 或 SIGTERM(15) → 直接终止 */
    if (sig == 9 || sig == 15) {
        if (pid == self) return user_process_kill(pid);
        if (process64_is_child(pid, self)) return user_process_kill(pid);
        return -1;
    }
    /* 其他信号 → 发pending bitmap，等返回用户态时投递 */
    if (pid == self || process64_is_child(pid, self)) {
        process64_send_signal(pid, sig);
        return 0;
    }
    return -1;  /* EPERM */
}

/* V0.20.43: mremap/prlimit 桩 */
static int64_t sys_mremap(void) { return 0; }
static int64_t sys_prlimit64(void) { return 0; }

/* V0.20.44: openat/readlink
 * V0.54.6: openat 从「固定返回 fd 3」的桩改为真正打开。
 * glibc 的 ld.so 全部走 openat(AT_FDCWD, "/lib/.../libc.so.6", O_RDONLY)：
 * 旧桩只返回一个从未打开过的 fd，后续 fstat/pread/mmap 全部落空，
 * 动态链接在第一步就失败。 */
static int64_t sys_openat(int dirfd, const char *path, int flags, int mode) {
    (void)dirfd;
    /* 本内核没有 cwd 之外的目录上下文；相对路径按根目录解析。 */
    if (path) {
        char probe[2];
        /* 用户指针不能直接解引用：先探测首字节（copy_from_user 内部校验）。 */
        if (copy_from_user(probe, path, 1) != 0) return HEPU_EFAULT;
        if (probe[0] != '/') {
            char kpath[256];
            if (copy_user_path(kpath, sizeof(kpath), path) < 0) return HEPU_EFAULT;
            char full[257];
            full[0] = '/';
            int i = 0;
            while (kpath[i] && i < 254) { full[i + 1] = kpath[i]; i++; }
            full[i + 1] = 0;
            return sys_open(full, flags, mode);
        }
    }
    return sys_open(path, flags, mode);
}
static int64_t sys_readlink(const char *path, uint64_t buf, int64_t bufsiz) {
    (void)path; (void)buf; (void)bufsiz;
    return 0;
}

/* V0.20.49: fcntl 桩 */
static int64_t sys_fcntl(void) { return 0; }

/* V0.20.50: clock_getres / clock_nanosleep */
static int64_t sys_clock_getres(int clockid, uint64_t res) {
    (void)clockid;
    if (res < 0x40000000ULL || res >= 0x80000000ULL) return HEPU_EFAULT;
    uint64_t sec = 0, nsec = 1000000; /* 1ms */
    copy_to_user((void*)res, &sec, 8);
    copy_to_user((void*)(res + 8), &nsec, 8);
    return 0;
}
static int64_t sys_clock_nanosleep(void) { return 0; }

/* V0.20.52: newfstatat - 返回 Linux 144 字节 stat
 *
 * V0.33 修复（真实内核缺陷，--cpu max 下表现为内核态 #PF → 整机挂死）：
 *   旧实现把**用户空间指针** path 直接交给 ext2_64_stat()。
 *   ext2_64_stat → ext2_64_resolve_path() 第一件事就是解引用 path[0]/path[1]，
 *   那是纯内核态的指针解引用，没有任何 copy_from_user。
 *   - SMAP 关闭时：ring0 本来就能访问用户页 ⇒ 侥幸不炸（但仍是无校验的
 *     用户指针解引用，属于契约漏洞）；
 *   - SMAP 开启时（--cpu max）：ring0 访问用户页必须先 STAC，否则 #PF ⇒
 *     内核态缺页 ⇒ 直接 hlt 停机。
 *   现象日志：[EXCEPTION] vector=0xE error=0x1 rip=0x10BE43
 *             CR2=0x400AF35D（用户地址）
 *   修复：和其它所有路径类 syscall 一样，先 copy_user_path 到内核缓冲再传。
 */
static int64_t sys_newfstatat(int dirfd, const char *path, uint64_t statbuf, int flags) {
    (void)dirfd; (void)flags;
    extern int ext2_64_stat_ino(const char *path, uint32_t *size, uint8_t *isdir,
                                uint32_t *ino);
    if (statbuf < 0x40000000ULL || statbuf >= 0x80000000ULL) return HEPU_EFAULT;
    /* 安全：路径必须先从用户空间复制到内核缓冲（见上） */
    char kpath[256];
    if (copy_user_path(kpath, sizeof(kpath), path) < 0) return HEPU_EFAULT;
    uint32_t sz = 0; uint8_t isdir = 0; uint32_t ino = 0;
    int r = ext2_64_stat_ino(kpath, &sz, &isdir, &ino);
    if (r != 0) return HEPU_ENOENT;
    /* Linux stat: st_mode(4) st_size(8) ... 简化填关键字段 */
    uint8_t sbuf[144];
    for (int i = 0; i < 144; i++) sbuf[i] = 0;
    *(uint64_t *)(sbuf + 0) = 1;                        /* st_dev */
    *(uint64_t *)(sbuf + 8) = ino;                      /* st_ino */
    *(uint64_t *)(sbuf + 16) = 1;                       /* st_nlink */
    uint32_t mode = isdir ? 0x41ED : 0x81A4; /* 0755 dir / 0644 file */
    sbuf[24] = mode & 0xFF; sbuf[25] = (mode>>8)&0xFF;
    /* st_size at offset 48 */
    sbuf[48] = sz & 0xFF; sbuf[49] = (sz>>8)&0xFF; sbuf[50] = (sz>>16)&0xFF; sbuf[51] = (sz>>24)&0xFF;
    for (int i = 0; i < 144; i++) copy_to_user((void*)(statbuf+i), &sbuf[i], 1);
    return 0;
}

/* sys_pipe: 创建管道, fds[0]=读端 fds[1]=写端 */
static int64_t sys_pipe(int *fds) {
    /* 安全修复：fds 是用户指针，旧实现仅查地址范围就内核态直写，
     * SMAP 开启或页未映射时 ring0 写用户地址 → #PF → 整机 hlt。
     * 一律走 copy_to_user（含范围+逐页映射校验+STAC）。 */
    int pid = -1;
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!g_pipes[i].used) { pid = i; break; }
    }
    if (pid < 0) return -1;

    /* 找两个空闲fd */
    int rfd = -1, wfd = -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (!sys_files[i].used) {
            if (rfd < 0) rfd = i;
            else { wfd = i; break; }
        }
    }
    if (rfd < 0 || wfd < 0) return -1;

    g_pipes[pid].used = 1;
    g_pipes[pid].head = g_pipes[pid].tail = 0;
    g_pipes[pid].readers = 1;
    g_pipes[pid].writers = 1;

    sys_files[rfd].used = 1;
    sys_files[rfd].is_pipe = 1;
    sys_files[rfd].pipe_buf = pid;
    sys_files[rfd].mode = 0;
    sys_files[wfd].used = 1;
    sys_files[wfd].is_pipe = 1;
    sys_files[wfd].pipe_buf = pid;
    sys_files[wfd].mode = 1;

    /* 写回用户空间；失败则回滚已分配的管道与 fd，避免泄漏 */
    if (copy_to_user(&fds[0], &rfd, sizeof(int)) != 0 ||
        copy_to_user(&fds[1], &wfd, sizeof(int)) != 0) {
        g_pipes[pid].used = 0;
        sys_files[rfd].used = 0;
        sys_files[wfd].used = 0;
        return -1;
    }
    return 0;
}

/* sys_stat: 返回 Linux x86_64 兼容 stat 结构 (144字节) */
struct linux_stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int32_t  st_blksize;
    int32_t  st_blocks;
    int64_t  st_atime;
    uint64_t st_atime_nsec;
    int64_t  st_mtime;
    uint64_t st_mtime_nsec;
    int64_t  st_ctime;
    uint64_t st_ctime_nsec;
    int32_t  __unused[3];
};

#define S_IFDIR  0040000
#define S_IFREG  0100000
#define S_IRWXU  00700
#define S_IRWXG  00070
#define S_IRWXO  00007

extern int ext2_64_stat(const char *path, uint32_t *size, uint8_t *isdir);
static int64_t sys_stat(const char *path, struct linux_stat *st)
{
    char kp[256];
    if (copy_user_path(kp, sizeof(kp), path) < 0) return -1;
    uint32_t sz = 0; uint8_t id = 0;
    int r = ext2_64_stat(kp, &sz, &id);
    if (r != 0) return -1;

    struct linux_stat ks;
    for (int i = 0; i < (int)sizeof(ks); i++) ((char*)&ks)[i] = 0;
    ks.st_dev = 0;
    ks.st_ino = 0;
    ks.st_nlink = 1;
    ks.st_mode = (id ? S_IFDIR : S_IFREG) | S_IRWXU | S_IRWXG | S_IRWXO;
    ks.st_uid = 0;
    ks.st_gid = 0;
    ks.st_rdev = 0;
    ks.st_size = sz;
    ks.st_blksize = 512;
    ks.st_blocks = (sz + 511) / 512;
    if (st) copy_to_user(st, &ks, sizeof(ks));
    return 0;
}

/* sys_readdir: 列目录，写入用户缓冲 outbuf（每项 "name\0type\0"），返回字节数 */
extern int ext2_64_list_dir_entries(const char *path, char names[][256], uint8_t *types, int max);
static int64_t sys_readdir(const char *path, char *outbuf, int outsz)
{
    char kernel_path[256];
    if (copy_user_path(kernel_path, sizeof(kernel_path), path) < 0) return -1;
    if (outsz <= 2) return -1;
    static char names[64][256];
    static uint8_t types[64];
    int n = ext2_64_list_dir_entries(kernel_path, names, types, 64);
    if (n < 0) return -1;
    char kbuf[2048];
    int used = 0;
    for (int i = 0; i < n; i++) {
        int l = 0; while (names[i][l]) l++;
        int need = l + 2;
        if (used + need >= (int)sizeof(kbuf)) break;
        for (int j = 0; j < l; j++) kbuf[used++] = names[i][j];
        kbuf[used++] = 0;
        kbuf[used++] = types[i] ? 'd' : 'f';
        kbuf[used++] = 0;
    }
    if (used > outsz) used = outsz;
    if (copy_to_user(outbuf, kbuf, (size_t)used) != 0) return -1;
    return used;
}

/* V0.33.8: fork 父进程返回上下文 (子进程exit后直接回父进程) */
uint64_t g_fork_parent_rip = 0;
uint64_t g_fork_parent_rsp = 0;
uint64_t g_fork_parent_rflags = 0;
uint64_t g_fork_parent_cr3 = 0;
uint64_t g_fork_parent_kstack = 0;
uint64_t g_fork_parent_ustack = 0;
uint64_t g_fork_parent_retval = 0;
/* V0.33.10: 父进程GPR快照 (独立于内核栈, 被子进程syscall覆盖) */
uint64_t g_fork_parent_gprs[15];

/* sys_exit */
static void sys_exit(int code)
{
    vga_puts("\n[SYSCALL64] Process exited with code ");
    print_syscall_number((int64_t)code);
    vga_puts("\n");

    /* V0.33.10: 如果是 fork 子进程, 恢复父进程 */
    extern uint64_t g_fork_child_pml4;
    if (g_fork_child_pml4 && g_fork_parent_kstack) {
        vga_puts("[FORK] child exit, return to parent\n");
        /* V0.34.1: 记录子进程退出码到进程表 (zombie) */
        extern int process64_set_zombie(int code);
        process64_set_zombie(code);
        g_fork_child_pml4 = 0;
        extern void fork_return_to_parent(void);
        fork_return_to_parent();
    }

    /* 如果在用户态，返回到Shell */
    extern void user_exit_to_kernel(void);
    user_exit_to_kernel();

    while (1) { asm volatile("hlt"); }
}

/* sys_getpid */
static int64_t sys_getpid(void)
{
    return 1;  /* 简化：始终返回1 */
}

/* sys_time */
static int64_t sys_time(void)
{
    int year, month, day, hour, minute, second;
    rtc64_get_time(&year, &month, &day, &hour, &minute, &second);
    /* 简化：返回秒数（从午夜开始） */
    return hour * 3600 + minute * 60 + second;
}

/* sys_brk：真正按需映射/释放堆页。增长时分配物理页映射到用户堆区（可写、用户、NX）；
 * 收缩时取消映射并归还物理页。堆不可执行（W^X：数据页 NX，代码页不可写）。 */
static int64_t sys_brk(uint64_t addr)
{
    /* V0.33: 堆区搬进用户窗口的私有分区。
     *
     * 缺陷（真实存在）：原来堆在 0x40100000-0x60000000 —— 那是用户窗口
     * 的**前 1GB**，在隔离模式下会落到**内核共享的 PDPT[0]** 里：
     *   - 用户堆的映射写进了内核的页表树；
     *   - brk 收缩时 vmm64_unmap_page 还会把内核低身份映射用的
     *     2MB 大页整段拆掉（见 vmm64.c 的 V0.33 修复）。
     * 结果是内核随后访问自己的物理内存会打到别的物理页上，
     * 表现为「写用户栈没写进去」「读回来是别的内容」。
     *
     * 现在按 docs 的用户窗口分区契约，堆独占 [0x68000000,0x70000000)：
     * 落在用户窗口的 PDPT[1] 私有区里，映射只影响本进程 ✓。
     * 同时 break 改为每进程独立（user_process_get_brk/set_brk）。
     */
    #define USER_BRK_BASE  0x68000000ULL  /* 堆区起点（用户窗口私有分区） */
    #define USER_BRK_LIMIT 0x6F000000ULL  /* 堆区上限（留 1MB 给栈区下方） */
    #define USER_BRK_INIT  0x68010000ULL  /* 初始 break（留一页余量） */
    /* V0.33: TLS 预留区
     *
     * glibc 的 static-PIE 在 __libc_setup_tls 里**自己**算 TLS 块的位置：
     * 它把 TLS 块放在「程序 break 往下一点点」的地方（实测约 brk 起点下方
     * 2.6MB），并直接把 TLS 初始化镜像 memcpy 进去。
     * Linux 上这段内存是内核通过 PT_TLS 预留好的；Hepu 没有这个机制，
     * 于是 glibc 往一块没人映射的地址写 → #PF（CR2 落在 brk 起点下方）。
     *
     * 处理办法（确定性优先于丰富性）：堆第一次使用时，把 break 下方
     * 这段地址一次性映射好（数据页、可写、用户、NX）。地址落在用户窗口
     * 的私有分区里，不会动内核的页表。
     */
    #define USER_TLS_RESERVE (8UL * 1024 * 1024)
    #define BRK_PAGE_SIZE  4096ULL
    #define BRK_PAGE_MASK  (~(BRK_PAGE_SIZE - 1))

    extern uint64_t user_process_get_brk(void);
    extern void user_process_set_brk(uint64_t brk);
    uint64_t program_break = user_process_get_brk();
    if (!program_break) program_break = USER_BRK_INIT;

    extern uint64_t user_process_current_pml4(void);
    extern int vmm64_map_page(uint64_t pml4, uint64_t virt, uint64_t phys, uint64_t flags);
    extern void vmm64_unmap_page(uint64_t pml4, uint64_t virt);
    extern uint64_t pmm64_alloc_page(void);
    extern void pmm64_free_page(uint64_t phys);
    /* 页标志（与 vmm64.c 一致） */
    #define BRK_P  0x001ULL
    #define BRK_W  0x002ULL
    #define BRK_U  0x004ULL
    #define BRK_NX (1ULL << 63)

    if (addr == 0) return program_break;
    if (addr < USER_BRK_BASE || addr > USER_BRK_LIMIT) return program_break;

    uint64_t pml4 = user_process_current_pml4();
    if (!pml4) { program_break = addr; user_process_set_brk(addr); return program_break; }

    if (addr > program_break) {
        /* P1: 按最坏情况（含 TLS 预留）先记入本域配额，超限则不扩堆 */
        extern int user_process_mem_charge(uint64_t pages);
        extern void user_process_mem_uncharge(uint64_t pages);
        extern int user_process_tls_reserve_done(void);
        uint64_t charge_pages = (addr - program_break + BRK_PAGE_SIZE - 1) / BRK_PAGE_SIZE;
        if (!user_process_tls_reserve_done()) {
            uint64_t tlo = (program_break > USER_TLS_RESERVE) ? (program_break - USER_TLS_RESERVE) : USER_BRK_BASE;
            tlo &= ~BRK_PAGE_MASK;
            charge_pages += (program_break - tlo) / BRK_PAGE_SIZE;
        }
        if (user_process_mem_charge(charge_pages) != 0) return program_break;
        uint64_t mapped_pages = 0;
        /* 增长：映射 [program_break 向上取整, addr] 的页 */
        /* 同时把 break 下方的 TLS 预留区一次性映射好（glibc static-PIE 需要） */
        {
            extern int user_process_tls_reserve_done(void);
            extern void user_process_tls_reserve_mark(void);
            if (!user_process_tls_reserve_done()) {
                uint64_t lo = (program_break > USER_TLS_RESERVE) ? (program_break - USER_TLS_RESERVE) : USER_BRK_BASE;
                lo &= ~BRK_PAGE_MASK;
                for (uint64_t v = lo; v < program_break; v += BRK_PAGE_SIZE) {
                    uint64_t phys = pmm64_alloc_page();
                    if (!phys) break;
                    if (vmm64_map_page(pml4, v, phys, BRK_P | BRK_W | BRK_U | BRK_NX) != 0) {
                        pmm64_free_page(phys);
                        break;
                    }
                    mapped_pages++;
                }
                user_process_tls_reserve_mark();
            }
        }
        /* V0.33 修复：起点必须**向下**取整到 break 所在的那一页。
         *
         * 原来这里是向上取整（(break + 4095) & ~4095）：break 落在页中间时
         * （glibc 的 malloc 正是这样：brk(0) 拿当前断点，再 brk(断点+N) 扩展），
         * 断点所在的那一页永远不会被映射 —— 但 glibc 会往
         * [break, break+N) 里写数据，于是 #PF。
         * 实测崩溃点 CR2=0x680103D0 = 初始 break(0x68010000) + 0x3D0，
         * 正好落在这块「没被映射的半页」里。
         *
         * V0.33 修复 2：**已经映射过的页不能重新映射。**
         * 重新映射 = 换一块新的物理页 = 页里的内容全丢。
         * 而堆的第一页里正躺着 glibc 的 TCB（线程控制块），
         * 栈保护 canary 就在 %fs:0x28（= TCB + 0x28）。
         * 第二次 brk 扩展时把这一页重新映射一遍 → canary 变成垃圾 →
         * glibc 报「stack smashing detected」。
         * 所以：断点所在页**已经映射**就从下一页开始，只补真正缺的页。
         */
        uint64_t start = program_break & BRK_PAGE_MASK;
        {
            extern uint64_t vmm64_virt_to_phys(uint64_t pml4, uint64_t virt);
            if (vmm64_virt_to_phys(pml4, start) != 0) {
                /* 这一页已经在用（里面可能有 TCB/堆数据），别动它 */
                start = (program_break + BRK_PAGE_SIZE - 1) & BRK_PAGE_MASK;
            }
        }
#ifdef HEPU_TRACE_LINUX_SYSCALL
        vga_puts("[BRK] grow ");
        print_syscall_number(program_break);
        vga_puts(" -> ");
        print_syscall_number(addr);
        vga_puts(" start=");
        vga_put_hex(start);
        vga_puts("\n");
#endif
        for (uint64_t v = start; v < addr; v += BRK_PAGE_SIZE) {
            uint64_t phys = pmm64_alloc_page();
            if (!phys) break;   /* 内存不足，停在能映射到的位置 */
#ifdef HEPU_TRACE_LINUX_SYSCALL
            vga_puts("[BRK] map v=");
            vga_put_hex(v);
            vga_puts(" phys=");
            vga_put_hex(phys);
            vga_puts("\n");
#endif
            if (vmm64_map_page(pml4, v, phys, BRK_P | BRK_W | BRK_U | BRK_NX) != 0) {
                pmm64_free_page(phys);
                break;
            }
            /* 新页清零：堆页必须是零页（glibc 的 malloc 元数据、
             * TCB 周边都假定是干净的）。跟 Linux 的匿名页语义一致。
             * V0.33 注意：只能通过**用户自己的页表**拿到这一页的物理帧，
             * 再用内核的身份映射去写 —— 不能拿内核堆缓冲区的指针乱写。 */
            {
                extern uint64_t vmm64_phys_to_virt(uint64_t phys);
                uint8_t *dst = (uint8_t *)vmm64_phys_to_virt(phys);
                if (dst) {
                    for (int z = 0; z < 4096; z++) dst[z] = 0;
                }
            }
            mapped_pages++;
        }
        /* 未能映射的页退还配额 */
        if (charge_pages > mapped_pages)
            user_process_mem_uncharge(charge_pages - mapped_pages);
        program_break = addr;
        user_process_set_brk(addr);
    } else if (addr < program_break) {
        /* 收缩：释放 [addr 向上取整, program_break) 的页
         * （断点所在的那一页保留 —— 它仍属于堆的一部分） */
        uint64_t start = (addr + BRK_PAGE_SIZE - 1) & BRK_PAGE_MASK;
        uint64_t freed = 0;
        extern void user_process_mem_uncharge(uint64_t pages);
        for (uint64_t v = start; v < program_break; v += BRK_PAGE_SIZE) {
            uint64_t phys = 0;
            extern uint64_t vmm64_virt_to_phys(uint64_t pml4, uint64_t virt);
            phys = vmm64_virt_to_phys(pml4, v);
            vmm64_unmap_page(pml4, v);
            if (phys) { pmm64_free_page(phys); freed++; }
        }
        if (freed) user_process_mem_uncharge(freed);
        program_break = addr;
        user_process_set_brk(addr);
    }
    return program_break;
}

/* 外部函数 */
extern int process64_create(const char *name, void (*entry)(void), int priority);
extern int process64_get_current_pid(void);
extern void vga_put_hex(uint64_t val);

/* sys_fork - 创建子进程（简化版：创建新内核线程） */
static int64_t sys_fork(void)
{
    /* 简化版：创建一个新的内核线程作为子进程
     * 完整实现需要：复制页表、复制文件描述符、设置返回值等
     * 当前版本：创建新进程，父进程返回子PID，子进程返回0
     */
    vga_puts("[SYSCALL] fork() called\n");

    /* 若当前是隔离模式 ELF 进程，克隆其用户地址空间作为子进程页表 */
    extern uint64_t uaspace_fork_copy(uint64_t parent_pml4);
    extern uint64_t user_process_current_pml4(void);
    extern uint64_t g_fork_child_pml4;
    g_fork_child_pml4 = 0;
    uint64_t cur_pml4 = user_process_current_pml4();
    if (cur_pml4) {
        uint64_t child_pml4 = uaspace_fork_copy(cur_pml4);
        if (child_pml4) {
            g_fork_child_pml4 = child_pml4;
            vga_puts("[SYSCALL] fork: cloned user address space\n");
        }
    }

    /* 快照父进程 syscall 返回上下文到子进程专用槽，
     * 避免父进程后续 syscall 覆盖全局槽导致子进程回不到用户态 */
    extern uint64_t g_syscall_ret_rip;
    extern uint64_t g_syscall_ret_rflags;
    extern uint64_t g_syscall_ustack_slot;
    extern uint64_t g_fork_child_rip;
    extern uint64_t g_fork_child_rsp;
    extern uint64_t g_fork_child_rflags;
    g_fork_child_rip = g_syscall_ret_rip;
    g_fork_child_rsp = g_syscall_ustack_slot;
    g_fork_child_rflags = g_syscall_ret_rflags;

    /* 创建子进程（使用fork_trampoline_64，子进程从syscall返回点继续执行） */
    extern void fork_trampoline_64(void);
    int child_pid = process64_create("fork_child", fork_trampoline_64, 2);

    if (child_pid < 0) {
        vga_puts("[SYSCALL] fork() failed: cannot create child\n");
        return -1;
    }

    /* V0.33.10: 保存父进程完整内核上下文, 子进程exit后恢复父进程 */
    extern uint64_t g_syscall_kernel_rsp;
    g_fork_parent_rip = g_syscall_ret_rip;
    g_fork_parent_rflags = g_syscall_ret_rflags;
    g_fork_parent_cr3 = cur_pml4;
    g_fork_parent_kstack = g_syscall_kernel_rsp;
    g_fork_parent_ustack = g_syscall_ustack_slot;
    g_fork_parent_retval = (uint64_t)child_pid;
    /* 拷贝父进程GPR快照到独立缓冲 (子进程syscall会覆盖内核栈) */
    for (int i = 0; i < 15; i++)
        g_fork_parent_gprs[i] = ((uint64_t *)g_syscall_kernel_rsp)[i];

    vga_puts("[SYSCALL] fork() created child pid: ");
    char buf[8];
    int n = child_pid, i = 0;
    char tmp[8];
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
    vga_puts(buf);
    vga_puts("\n");

    /* V0.20.16: 主动让出CPU, 让子进程立刻跑 */
    extern void process64_yield(void);
    process64_yield();

    /* 父进程返回子PID */
    return child_pid;
}

/* fork子进程入口 - 子进程从这里开始执行，模拟fork返回0 */
void fork_child_entry(void)
{
    vga_puts("[SYSCALL] fork child started (will return 0 to caller)\n");
    /* 子进程中fork返回0 - 通过系统调用返回机制
     * 当前架构下，子进程作为独立内核线程运行
     * 完整的fork（子进程从syscall返回点继续执行）需要在syscall入口复制用户态上下文
     * 暂用简化版：子进程执行完毕后退出
     */
    extern void process_exit_64(void);
    process_exit_64();
}

/* ELF64加载器外部声明 */
extern void elf64_init(void);
extern int elf64_verify(void *elf_data);
extern uint64_t elf64_get_entry(void *elf_data);
extern void *elf64_load(void *elf_data, uint64_t size);

/* sys_execve - 执行程序（V0.10.75: 集成ELF64加载器） */
static int64_t sys_execve(const char *path, char *const argv[], char *const envp[])
{
    (void)argv;
    (void)envp;

    /* 安全：从用户空间复制路径到内核缓冲区 */
    char kernel_path[256];
    if (copy_user_path(kernel_path, sizeof(kernel_path), path) < 0) {
        vga_puts("[SYSCALL] execve() ERROR: invalid user path\n");
        return -1;
    }

    vga_puts("[SYSCALL] execve() called: ");
    vga_puts(kernel_path);
    vga_puts("\n");

    /* 1. 从ext2文件系统读取ELF文件 - V0.20.16: 动态分配 */
    extern void *kmalloc(uint32_t size);
    extern void kfree(void *ptr);
    uint8_t *elf_buffer = (uint8_t *)kmalloc(2 * 1024 * 1024);
    if (!elf_buffer) {
        vga_puts("[SYSCALL] execve() ERROR: kmalloc failed\n");
        return -1;
    }
    int file_size = ext2_64_read_file(kernel_path, elf_buffer, 2*1024*1024);
    if (file_size < 64) {
        vga_puts("[SYSCALL] execve() ERROR: Cannot read file\n");
        kfree(elf_buffer);
        return -1;
    }

    vga_puts("[SYSCALL] execve() read ");
    char buf[16];
    int n = file_size, i = 0;
    char tmp[16];
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
    vga_puts(buf);
    vga_puts(" bytes\n");

    /* 2. 验证ELF64 */
    if (elf64_verify(elf_buffer) != 0) {
        vga_puts("[SYSCALL] execve() ERROR: Invalid ELF64 file\n");
        return -1;
    }

    /* 3. 获取入口点 */
    uint64_t entry = elf64_get_entry(elf_buffer);
    vga_puts("[SYSCALL] execve() entry point: ");
    vga_put_hex(entry);
    vga_puts("\n");

    /* 4. 加载ELF到内存 */
    void *loaded = elf64_load(elf_buffer, file_size);
    if (!loaded) {
        vga_puts("[SYSCALL] execve() ERROR: ELF load failed\n");
        kfree(elf_buffer);
        return -1;
    }

    /* V0.20.34: 检查 PT_INTERP, 加载动态链接器 */
    {
        const char *interp_path = elf64_find_interp(elf_buffer, file_size);
        if (interp_path) {
            vga_puts("[SYSCALL] PT_INTERP: ");
            vga_puts(interp_path);
            vga_puts("\n");
            extern uint8_t *kmalloc_buf(uint64_t size);
            uint8_t *interp_buf = kmalloc(2*1024*1024);
            if (interp_buf) {
                int isz = ext2_64_read_file(interp_path, interp_buf, 2*1024*1024);
                if (isz > 0) {
                    vga_puts("[SYSCALL] Loading interpreter ");
                    vga_put_hex(isz);
                    vga_puts(" bytes\n");
                    elf64_load_library("ld.so", interp_buf, isz);
                    /* 解析 interpreter 的符号后, 入口改为 interpreter 的 */
                    if (elf64_verify(interp_buf) == 0) {
                        /* e_entry 是 ELF header 第 24 字节 (64位) */
                        entry = *(uint64_t*)((uint8_t*)interp_buf + 24);
                        vga_puts("[SYSCALL] New entry (interp): ");
                        vga_put_hex(entry);
                        vga_puts("\n");
                    }
                } else {
                    vga_puts("[SYSCALL] Interpreter not found, continuing static\n");
                }
            }
        }
    }

    kfree(elf_buffer);  /* V0.20.16: 释放内核缓冲 */

    /* 5. 完整实现需要：
     * - 创建新的用户地址空间
     * - 映射ELF段到用户空间
     * - 设置用户栈（argv, envp）
     * - 切换到用户态并跳转到入口点
     *
     * 当前阶段：加载验证成功，返回入口点
     * 实际跳转待用户地址空间完善后实现
     */
    vga_puts("[SYSCALL] execve() ELF loaded successfully (jump pending)\n");
    return entry;
}

/* sys_sleep - 忙等待（syscall 内关中断，不能 hlt，否则永不返回） */
static int64_t sys_sleep(uint64_t milliseconds)
{
    volatile uint64_t count = milliseconds * 100000;
    while (count > 0) {
        count--;
        asm volatile("nop");
    }
    return 0;
}

/* sys_getppid - 获取父进程ID */
static int64_t sys_getppid(void)
{
    extern int process64_get_current_pid(void);
    extern void *process64_get_current(void);
    /* 简化：返回0（root父进程） */
    return 0;
}

/* sys_unlink - 删除文件 */
static int64_t sys_unlink(const char *path)
{
    if (!path) return -1;
    /* 安全：从用户空间复制路径 */
    char kpath[256];
    if (copy_user_path(kpath, sizeof(kpath), path) < 0) return -1;
    extern int ext2_64_delete_file(const char *path);
    return ext2_64_delete_file(kpath);
}

/* sys_mkdir - 创建目录 */
static int64_t sys_mkdir(const char *path, int mode)
{
    if (!path) return -1;
    (void)mode;
    char kpath[256];
    if (copy_user_path(kpath, sizeof(kpath), path) < 0) return -1;
    extern int ext2_64_create_dir(const char *path);
    return ext2_64_create_dir(kpath);
}

/* sys_rmdir - 删除目录 */
static int64_t sys_rmdir(const char *path)
{
    if (!path) return -1;
    char kpath[256];
    if (copy_user_path(kpath, sizeof(kpath), path) < 0) return -1;
    extern int ext2_64_delete_dir(const char *path);
    return ext2_64_delete_dir(kpath);
}

/* sys_lseek - 移动文件指针 */
static int64_t sys_lseek(int fd, int64_t offset, int whence)
{
    if (fd < 0 || fd >= MAX_FILES || !sys_files[fd].used) return -1;
    int64_t newpos;
    switch (whence) {
        case 0: /* SEEK_SET */
            newpos = offset;
            break;
        case 1: /* SEEK_CUR */
            if (__builtin_add_overflow((int64_t)sys_files[fd].offset, offset, &newpos)) return -1;
            break;
        case 2: /* SEEK_END */
            /* 简化：不支持文件大小查询，返回当前偏移 */
            newpos = sys_files[fd].offset;
            break;
        default:
            return -1;
    }
    /* 安全修复：旧实现允许负偏移，sys_read 会据此读取 file_buf 之前的内核内存并拷给用户 */
    if (newpos < 0 || newpos > 0x7FFFFFFF) return -1;
    sys_files[fd].offset = (int)newpos;
    return newpos;
}

/* sys_access - 检查文件访问权限 */
static int64_t sys_access(const char *path, int mode)
{
    if (!path) return -1;
    (void)mode;
    char kpath[256];
    if (copy_user_path(kpath, sizeof(kpath), path) < 0) return -1;
    /* 简化：检查文件是否存在 */
    extern int ext2_64_read_file(const char *path, uint8_t *buf, int max_size);
    uint8_t tmp[1];
    int ret = ext2_64_read_file(kpath, tmp, 1);
    return (ret >= 0) ? 0 : -1;
}

/* sys_rename - 重命名文件 */
static int64_t sys_rename(const char *oldpath, const char *newpath)
{
    if (!oldpath || !newpath) return -1;
    /* 简化：不支持重命名，返回-1 */
    (void)oldpath; (void)newpath;
    return -1;
}

/* sys_fstat - 获取文件状态 */
static int64_t sys_fstat(int fd, void *statbuf)
{
    if (fd < 0 || fd >= MAX_FILES || !sys_files[fd].used) return -1;
    if (!statbuf) return HEPU_EFAULT;
    extern int ext2_64_stat_ino(const char *path, uint32_t *size, uint8_t *isdir,
                                uint32_t *ino);
    uint32_t sz = 0; uint8_t isdir = 0; uint32_t ino = 0;
    if (ext2_64_stat_ino(sys_files[fd].path, &sz, &isdir, &ino) != 0) return HEPU_ENOENT;
    /* Linux x86_64 struct stat 关键字段：st_dev@0, st_ino@8,
     * st_nlink@16, st_mode@24, st_size@48, st_blksize@56, st_blocks@64。
     * ld.so 用 (st_dev, st_ino) 去重「已加载对象」——两者恒 0 会把
     * libc.so.6 误认成主程序，版本检查与符号查找全部失败。 */
    uint8_t sbuf[144];
    for (int i = 0; i < 144; i++) sbuf[i] = 0;
    *(uint64_t *)(sbuf + 0) = 1;                        /* st_dev */
    *(uint64_t *)(sbuf + 8) = ino;                      /* st_ino */
    *(uint64_t *)(sbuf + 16) = 1;                       /* st_nlink */
    uint32_t mode = isdir ? 0x41ED : 0x81A4;             /* 0755 dir / 0644 file */
    *(uint32_t *)(sbuf + 24) = mode;
    *(uint64_t *)(sbuf + 48) = sz;                       /* st_size */
    *(uint64_t *)(sbuf + 56) = 4096;                     /* st_blksize */
    *(uint64_t *)(sbuf + 64) = (sz + 511) / 512;         /* st_blocks */
    if (copy_to_user(statbuf, sbuf, 144) != 0) return HEPU_EFAULT;
    return 0;
}

/* sys_getcwd - 获取当前工作目录 */
static int64_t sys_getcwd(char *buf, size_t size)
{
    if (!buf || size == 0) return -1;
    /* 简化：返回根目录 */
    const char *cwd = "/";
    size_t len = 1;
    if (size < len + 1) return -1;
    if (copy_to_user(buf, cwd, len + 1) != 0) return -1;
    return (int64_t)buf;
}

/* 系统调用函数指针类型（统一6参数） */
typedef int64_t (*syscall_fn_t)(int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);

/* 系统调用表最大编号：使用 L45 已定义的 SYSCALL_MAX = SYS_MAX（统一 syscall 号） */

/* 包装函数：将具体系统调用转换为统一6参数接口 */
static int64_t wrap_write(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4; (void)a5; (void)a6;
    return sys_write((int)a1, (const char*)a2, (int)a3);
}
static int64_t wrap_read(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4; (void)a5; (void)a6;
    return sys_read((int)a1, (char*)a2, (int)a3);
}
static int64_t wrap_pread(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a5; (void)a6;
    return sys_pread((int)a1, (char*)a2, (int)a3, (int64_t)a4);
}
static int64_t wrap_open(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4; (void)a5; (void)a6;
    return sys_open((const char*)a1, (int)a2, (int)a3);
}
static int64_t wrap_close(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_close((int)a1);
}
static int64_t wrap_exit(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    sys_exit((int)a1);
    return 0;
}
/* V0.20.88: exit_group = exit (glibc调用) */
static int64_t wrap_exit_group(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    sys_exit((int)a1);
    return 0;
}
static int64_t wrap_getpid(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_getpid();
}
static int64_t wrap_time(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_time();
}
static int64_t wrap_brk(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_brk((uint64_t)a1);
}
static int64_t wrap_fork(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_fork();
}
static int64_t wrap_execve(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4; (void)a5; (void)a6;
    return sys_execve((const char*)a1, (char* const*)a2, (char* const*)a3);
}
static int64_t wrap_sleep(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_sleep((uint64_t)a1);
}
static int64_t wrap_getppid(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_getppid();
}
static int64_t wrap_unlink(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_unlink((const char*)a1);
}
static int64_t wrap_mkdir(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_mkdir((const char*)a1, (int)a2);
}
static int64_t wrap_rmdir(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_rmdir((const char*)a1);
}
static int64_t wrap_readdir(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4; (void)a5; (void)a6;
    return sys_readdir((const char*)a1, (char*)a2, (int)a3);
}
static int64_t wrap_mkfs(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a5; (void)a6;
    return sys_mkfs((uint32_t)a1, (uint32_t)a2, (const char*)a3, (uint32_t)a4);
}
static int64_t wrap_stat(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_stat((const char*)a1, (void*)a2);
}
static int64_t wrap_mmap(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    return sys_mmap((uint64_t)a1, (uint64_t)a2, (int)a3, (int)a4, (int)a5, a6);
}
static int64_t wrap_munmap(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_munmap((uint64_t)a1, (uint64_t)a2);
}
static int64_t wrap_mprotect(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4; (void)a5; (void)a6;
    return sys_mprotect((uint64_t)a1, (uint64_t)a2, (int)a3);
}
static int64_t wrap_chdir(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_chdir((const char*)a1);
}
static int64_t wrap_dup(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_dup((int)a1);
}
static int64_t wrap_umask(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_umask((int)a1);
}
static int64_t wrap_gettimeofday(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_gettimeofday((uint64_t)a1);
}
static int64_t wrap_waitpid(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4; (void)a5; (void)a6;
    return sys_waitpid((int)a1, (int*)a2, (int)a3);
}
static int64_t wrap_ps(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_ps((void*)a1, (int)a2);
}
static int64_t wrap_pipe(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_pipe((int*)a1);
}
static int64_t wrap_getuid(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getuid();
}
static int64_t wrap_getgid(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getgid();
}
static int64_t wrap_geteuid(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_geteuid();
}
static int64_t wrap_getegid(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getegid();
}
static int64_t wrap_sched_yield(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_sched_yield();
}
static int64_t wrap_socket(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6; return sys_socket((int)a1,(int)a2,(int)a3);
}
static int64_t wrap_connect(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6; return sys_connect((int)a1,(uint64_t)a2,(int)a3);
}
static int64_t wrap_sendto(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    return sys_sendto((int)a1,(uint64_t)a2,(int)a3,(int)a4,(uint64_t)a5,(int)a6);
}
static int64_t wrap_recvfrom(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    return sys_recvfrom((int)a1,(uint64_t)a2,(int)a3,(int)a4,(uint64_t)a5,(int*)a6);
}
static int64_t wrap_shutdown(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3;(void)a4;(void)a5;(void)a6; return sys_shutdown((int)a1,(int)a2);
}
static int64_t wrap_setsockopt(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    return sys_setsockopt((int)a1,(int)a2,(int)a3,(uint64_t)a4,(int)a5);
}
static int64_t wrap_getsockopt(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    return sys_getsockopt((int)a1,(int)a2,(int)a3,(uint64_t)a4,(uint64_t)a5);
}
static int64_t wrap_getrusage(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3;(void)a4;(void)a5;(void)a6; return sys_getrusage((int)a1,(uint64_t)a2);
}
static int64_t wrap_wait4(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a5;(void)a6; return sys_wait4((int)a1,(uint64_t)a2,(int)a3,(uint64_t)a4);
}
static int64_t wrap_chmod(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3;(void)a4;(void)a5;(void)a6; return sys_chmod((const char*)a1,(int)a2);
}
static int64_t wrap_fchmod(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3;(void)a4;(void)a5;(void)a6; return sys_fchmod((int)a1,(int)a2);
}
/* V0.20.72: 批量wrapper */
static int64_t wrap_rt_sigreturn(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_rt_sigreturn();
}
static int64_t wrap_accept(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6; return sys_accept((int)a1,(uint64_t)a2,(uint64_t)a3);
}
static int64_t wrap_bind(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6; return sys_bind((int)a1,(uint64_t)a2,(int)a3);
}
static int64_t wrap_listen(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3;(void)a4;(void)a5;(void)a6; return sys_listen((int)a1,(int)a2);
}
static int64_t wrap_getsockname(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6; return sys_getsockname((int)a1,(uint64_t)a2,(uint64_t)a3);
}
static int64_t wrap_getpeername(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6; return sys_getpeername((int)a1,(uint64_t)a2,(uint64_t)a3);
}
static int64_t wrap_socketpair(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a5;(void)a6; return sys_socketpair((int)a1,(int)a2,(int)a3,(int)a4);
}
static int64_t wrap_clone(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a6; return sys_clone((int)a1,(uint64_t)a2,(uint64_t)a3,(uint64_t)a4,(uint64_t)a5);
}
static int64_t wrap_creat(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3;(void)a4;(void)a5;(void)a6; return sys_creat((const char*)a1,(int)a2);
}
static int64_t wrap_link(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3;(void)a4;(void)a5;(void)a6; return sys_link((const char*)a1,(const char*)a2);
}
static int64_t wrap_chown(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6; return sys_chown((const char*)a1,(int)a2,(int)a3);
}
static int64_t wrap_fchown(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6; return sys_fchown((int)a1,(int)a2,(int)a3);
}
static int64_t wrap_lchown(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6; return sys_lchown((const char*)a1,(int)a2,(int)a3);
}
static int64_t wrap_getpgrp(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getpgrp();
}
static int64_t wrap_vfork(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_vfork();
}
static int64_t wrap_setuid(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_setuid((int)a1);
}
static int64_t wrap_setgid(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_setgid((int)a1);
}
static int64_t wrap_seccomp(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6;
    return sys_seccomp((unsigned int)a1, (unsigned int)a2, (uint64_t)a3);
}
static int64_t wrap_dup3(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6; return sys_dup3((int)a1,(int)a2,(int)a3);
}
static int64_t wrap_select(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a6; return sys_select((int)a1,(uint64_t)a2,(uint64_t)a3,(uint64_t)a4,(uint64_t)a5);
}
static int64_t wrap_mlock(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3;(void)a4;(void)a5;(void)a6; return sys_mlock((uint64_t)a1,(uint64_t)a2);
}
static int64_t wrap_madvise(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6; return sys_madvise((uint64_t)a1,(uint64_t)a2,(int)a3);
}
static int64_t wrap_sysinfo(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_sysinfo((uint64_t)a1);
}
static int64_t wrap_ioctl(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6; return sys_ioctl((int)a1, (uint64_t)a2, (uint64_t)a3);
}
static int64_t wrap_arch_prctl(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3;(void)a4;(void)a5;(void)a6; return sys_arch_prctl((int)a1, (uint64_t)a2);
}
static int64_t wrap_getdents(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6; return sys_getdents((int)a1, (uint64_t)a2, (int)a3);
}
static int64_t wrap_set_tid(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_set_tid_address((uint64_t)a1);
}
static int64_t wrap_set_robust(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_set_robust_list();
}
static int64_t wrap_rseq(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_rseq();
}
static int64_t wrap_readv(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6; return sys_readv((int)a1, (uint64_t)a2, (int)a3);
}
static int64_t wrap_writev(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6; return sys_writev((int)a1, (uint64_t)a2, (int)a3);
}
static int64_t wrap_pselect6(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_pselect6();
}
static int64_t wrap_epoll_create(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_epoll_create((int)a1);
}
static int64_t wrap_epoll_ctl(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_epoll_ctl();
}
static int64_t wrap_epoll_wait(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_epoll_wait();
}
static int64_t wrap_eventfd2(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_eventfd2();
}
static int64_t wrap_clock_gettime(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3;(void)a4;(void)a5;(void)a6; return sys_clock_gettime((int)a1, (uint64_t)a2);
}
static int64_t wrap_newuname(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_newuname((uint64_t)a1);
}
static int64_t wrap_futex(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    return sys_futex((uint64_t)a1, (int)a2, (uint32_t)a3, (uint64_t)a4, (uint64_t)a5, (uint32_t)a6);
}
static int64_t wrap_getrandom(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6; return sys_getrandom((uint64_t)a1, a2, (uint64_t)a3);
}
static int64_t wrap_dup2(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3;(void)a4;(void)a5;(void)a6; return sys_dup2((int)a1, (int)a2);
}
static int64_t wrap_sigaction(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6;
    return sys_sigaction((int)a1, (const void*)a2, (void*)a3);
}
static int64_t wrap_sigprocmask(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_sigprocmask();
}
static int64_t wrap_tgkill(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6;
    return sys_tgkill((int)a1, (int)a2, (int)a3);
}
static int64_t wrap_kill(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3;(void)a4;(void)a5;(void)a6;
    return sys_kill((int)a1, (int)a2);
}
static int64_t wrap_mremap(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_mremap();
}
static int64_t wrap_prlimit(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_prlimit64();
}
static int64_t wrap_openat(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a5;(void)a6; return sys_openat((int)a1, (const char*)a2, (int)a3, (int)a4);
}
static int64_t wrap_readlink(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4;(void)a5;(void)a6; return sys_readlink((const char*)a1, (uint64_t)a2, a3);
}
static int64_t wrap_fcntl(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_fcntl();
}
static int64_t wrap_clock_getres(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3;(void)a4;(void)a5;(void)a6; return sys_clock_getres((int)a1, (uint64_t)a2);
}
static int64_t wrap_clock_nanosleep(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_clock_nanosleep();
}
static int64_t wrap_newfstatat(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a5;(void)a6; return sys_newfstatat((int)a1, (const char*)a2, (uint64_t)a3, (int)a4);
}
static int64_t wrap_access(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3;(void)a4;(void)a5;(void)a6; return sys_access((const char*)a1, (int)a2);
}
static int64_t wrap_lseek(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a4; (void)a5; (void)a6;
    return sys_lseek((int)a1, a2, (int)a3);
}
static int64_t wrap_rename(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_rename((const char*)a1, (const char*)a2);
}
static int64_t wrap_fstat(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_fstat((int)a1, (void*)a2);
}
static int64_t wrap_getcwd(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_getcwd((char*)a1, (size_t)a2);
}

/* V0.13.46: 终止进程 syscall（PID -> 0成功/-1失败） */

/* V0.13.47: 磁盘扇区读写（MBR分区表工具/三区制地基）
 * sys_disk_read(lba, user_buf, count): 读 count 个512B扇区到用户缓冲
 * sys_disk_write(lba, user_buf, count): 从用户缓冲写 count 个扇区 */
static int64_t sys_disk_read(uint64_t lba, void *user_buf, uint64_t count, int drive)
{
    /* SEC-04: 裸盘读写需特权 */
    extern int user_process_get_current_pid(void);
    if (user_process_get_current_pid() != 0) return -1;
    if (count < 1 || count > 64 || lba > 0xFFFFFFFFULL - count) return -1;  /* lba 会被截断为32位 */
    static uint8_t kb[64 * 512];   /* 最多64扇区=32KB */
    extern int ide64_read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer);
    extern void ide64_set_drive(int drive);
    ide64_set_drive(drive ? 1 : 0);
    if (ide64_read_sectors((uint32_t)lba, (uint8_t)count, kb) != 0) return -1;
    if (copy_to_user(user_buf, kb, (size_t)(count * 512)) != 0) return -1;
    return (int64_t)(count * 512);
}

static int64_t sys_disk_write(uint64_t lba, void *user_buf, uint64_t count, int drive)
{
    /* SEC-04: 裸盘写需特权 */
    extern int user_process_get_current_pid(void);
    if (user_process_get_current_pid() != 0) return -1;
    if (count < 1 || count > 64 || lba > 0xFFFFFFFFULL - count) return -1;  /* lba 会被截断为32位 */
    static uint8_t kb[64 * 512];
    if (copy_from_user(kb, user_buf, (size_t)(count * 512)) != 0) return -1;
    extern int ide64_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buffer);
    extern void ide64_set_drive(int drive);
    ide64_set_drive(drive ? 1 : 0);
    if (ide64_write_sectors((uint32_t)lba, (uint8_t)count, kb) != 0) return -1;
    return (int64_t)(count * 512);
}

static int64_t wrap_disk_read(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a5; (void)a6;
    return sys_disk_read((uint64_t)a1, (void*)a2, (uint64_t)a3, (int)a4);
}
static int64_t wrap_disk_write(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a5; (void)a6;
    return sys_disk_write((uint64_t)a1, (void*)a2, (uint64_t)a3, (int)a4);
}

/* V0.13.49: 重启系统（安装引导流: 安装完成后重启进入新系统） */
static int64_t sys_reboot(void)
{
    /* V0.54.12(R4 SC-03): 临时权限闸，PID==0 才允许重启。
       完整 cred/capability 模型留作后续架构项；内核内安装/文本 shell 走
       直接调用不经过本分发，不受影响。 */
    extern int user_process_get_current_pid(void);
    if (user_process_get_current_pid() != 0) return -1;
    vga_puts("[SYSCALL] Rebooting system...\n");
    /* 8042 键盘控制器复位脉冲 */
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
    for (;;) { __asm__ volatile("hlt"); }
    return 0;
}

static int64_t wrap_reboot(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_reboot();
}

/* net_stats: 返回累计收发字节，供任务管理器算实时速率。
 * a1 = uint64_t* tx_bytes_out, a2 = uint64_t* rx_bytes_out（可空）。返回设备数。 */
static int64_t wrap_net_stats(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5, int64_t a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    extern void net_hal_get_stats(int dev_idx, uint64_t *tx_pkts, uint64_t *rx_pkts,
                                  uint64_t *tx_bytes, uint64_t *rx_bytes);
    uint64_t tx = 0, rx = 0;
    net_hal_get_stats(0, 0, 0, &tx, &rx);
    /* 安全修复：a1/a2 是用户指针，旧实现仅查范围就内核态直写，
     * SMAP/未映射页 → 内核 #PF 停机。走 copy_to_user（失败忽略，仅丢统计）。 */
    if (a1) copy_to_user((void *)a1, &tx, 8);
    if (a2) copy_to_user((void *)a2, &rx, 8);
    return (int64_t)tx;   /* rax 同时带 tx，rx 通过指针 */
}

/* 系统调用表（函数指针数组，参考32位实现） */
static syscall_fn_t syscall_table[SYSCALL_MAX] = {
    [SYSCALL_WRITE]  = wrap_write,
    [SYSCALL_READ]   = wrap_read,
    [SYSCALL_PREAD]  = wrap_pread,
    [SYSCALL_OPEN]   = wrap_open,
    [SYSCALL_CLOSE]  = wrap_close,
    [SYSCALL_EXIT]   = wrap_exit,
    [SYSCALL_FORK]   = wrap_fork,
    [SYSCALL_EXECVE] = wrap_execve,
    [SYSCALL_BRK]    = wrap_brk,
    [SYSCALL_TIME]   = wrap_time,
    [SYSCALL_SLEEP]  = wrap_sleep,
    [SYSCALL_GETPID] = wrap_getpid,
    /* 新增系统调用 */
    [SYSCALL_GETPPID] = wrap_getppid,
    [SYSCALL_UNLINK]  = wrap_unlink,
    [SYSCALL_MKDIR]   = wrap_mkdir,
    [SYSCALL_RMDIR]   = wrap_rmdir,
    [SYSCALL_LSEEK]   = wrap_lseek,
    [SYSCALL_ACCESS]  = wrap_access,
    [SYSCALL_RENAME]  = wrap_rename,
    [SYSCALL_FSTAT]   = wrap_fstat,
    [SYSCALL_GETCWD]  = wrap_getcwd,
    [SYSCALL_KILL]    = wrap_kill,
    [SYSCALL_DISK_READ]  = wrap_disk_read,
    [SYSCALL_DISK_WRITE] = wrap_disk_write,
    [SYSCALL_REBOOT]    = wrap_reboot,
    [SYSCALL_NET_STATS] = wrap_net_stats,
    [SYSCALL_READDIR]  = wrap_readdir,
    [SYSCALL_MKFS]     = wrap_mkfs,
    [SYSCALL_STAT]     = wrap_stat,
    [SYSCALL_MMAP]     = wrap_mmap,
    [SYSCALL_MUNMAP]   = wrap_munmap,
    [SYSCALL_MPROTECT] = wrap_mprotect,
    [SYSCALL_CHDIR]    = wrap_chdir,
    [SYSCALL_DUP]      = wrap_dup,
    [SYSCALL_UMASK]    = wrap_umask,
    [SYSCALL_GETTIMEOFDAY] = wrap_gettimeofday,
    [SYSCALL_WAITPID]  = wrap_waitpid,
    [SYS_HEPU_PS] = wrap_ps,  /* V0.34.5 → V0.54.6: 统一 syscall 号 */
    [SYSCALL_PIPE]     = wrap_pipe,
    [SYSCALL_GETUID]   = wrap_getuid,
    [SYSCALL_GETGID]   = wrap_getgid,
    [SYSCALL_GETEUID]  = wrap_geteuid,
    [SYSCALL_GETEGID]  = wrap_getegid,
    [SYSCALL_SCHED_YIELD] = wrap_sched_yield,
    [SYSCALL_SOCKET]    = wrap_socket,
    [SYSCALL_CONNECT]   = wrap_connect,
    [SYSCALL_SENDTO]    = wrap_sendto,
    [SYSCALL_RECVFROM]  = wrap_recvfrom,
    [SYSCALL_SHUTDOWN]  = wrap_shutdown,
    [SYSCALL_SETSOCKOPT] = wrap_setsockopt,
    [SYSCALL_GETSOCKOPT] = wrap_getsockopt,
    [SYSCALL_GETRUSAGE]  = wrap_getrusage,
    [SYSCALL_WAIT4]      = wrap_wait4,
    [SYSCALL_CHMOD]      = wrap_chmod,
    [SYSCALL_FCHMOD]     = wrap_fchmod,
    [SYSCALL_RT_SIGRETURN] = wrap_rt_sigreturn,
    [SYSCALL_ACCEPT]     = wrap_accept,
    [SYSCALL_BIND]       = wrap_bind,
    [SYSCALL_LISTEN]     = wrap_listen,
    [SYSCALL_GETSOCKNAME] = wrap_getsockname,
    [SYSCALL_GETPEERNAME] = wrap_getpeername,
    [SYSCALL_SOCKETPAIR] = wrap_socketpair,
    [SYSCALL_CLONE]      = wrap_clone,
    [SYSCALL_CREAT]      = wrap_creat,
    [SYSCALL_LINK]       = wrap_link,
    [SYSCALL_CHOWN]      = wrap_chown,
    [SYSCALL_FCHOWN]     = wrap_fchown,
    [SYSCALL_LCHOWN]     = wrap_lchown,
    [SYSCALL_GETPGRP]    = wrap_getpgrp,
    [SYSCALL_VFORK]      = wrap_vfork,
    [SYSCALL_SETUID]     = wrap_setuid,
    [SYSCALL_SETGID]     = wrap_setgid,
    [SYSCALL_SECCOMP]    = wrap_seccomp,
    [SYSCALL_DUP3]       = wrap_dup3,
    [SYSCALL_SELECT]     = wrap_select,
    [SYSCALL_MLOCK]      = wrap_mlock,
    [SYSCALL_MADVISE]    = wrap_madvise,
    [SYSCALL_SYSINFO]  = wrap_sysinfo,
    [SYSCALL_IOCTL]    = wrap_ioctl,
    [SYSCALL_ARCH_PRCTL] = wrap_arch_prctl,
    [SYS_EXIT_GROUP] = wrap_exit_group,  /* V0.20.88 → V0.54.6: 统一 syscall 号 */
    [SYSCALL_GETDENTS]  = wrap_getdents,
    [SYSCALL_SET_TID]   = wrap_set_tid,
    [SYSCALL_SET_ROBUST] = wrap_set_robust,
    [SYSCALL_RSEQ]      = wrap_rseq,
    [SYSCALL_READV]     = wrap_readv,
    [SYSCALL_WRITEV]    = wrap_writev,
    [SYSCALL_PSELECT6]  = wrap_pselect6,
    [SYSCALL_EPOLL_CREATE] = wrap_epoll_create,
    [SYSCALL_EPOLL_CTL] = wrap_epoll_ctl,
    [SYSCALL_EPOLL_WAIT] = wrap_epoll_wait,
    [SYSCALL_EVENTFD2]  = wrap_eventfd2,
    [SYSCALL_CLOCK_GETTIME] = wrap_clock_gettime,
    [SYSCALL_NEWUNAME]  = wrap_newuname,
    [SYSCALL_FUTEX]     = wrap_futex,
    [SYSCALL_GETRANDOM] = wrap_getrandom,
    [SYSCALL_DUP2]      = wrap_dup2,
    [SYSCALL_SIGACTION] = wrap_sigaction,
    [SYSCALL_SIGPROCMASK] = wrap_sigprocmask,
    [SYSCALL_TGKILL]    = wrap_tgkill,
    [SYSCALL_MREMAP]    = wrap_mremap,
    [SYSCALL_PRLIMIT]   = wrap_prlimit,
    [SYSCALL_OPENAT]    = wrap_openat,
    [SYSCALL_READLINK]  = wrap_readlink,
    [SYSCALL_FCNTL]     = wrap_fcntl,
    [SYSCALL_CLOCK_GETRES] = wrap_clock_getres,
    [SYSCALL_CLOCK_NANOSLEEP] = wrap_clock_nanosleep,
    [SYSCALL_NEWFSTATAT] = wrap_newfstatat,
};

/* 安全修复：旧实现用 char tmp[8]/buf[8] 打印调用号，用户传入 rax=2147483647
 * 之类的值即可写穿内核栈；这里改为足够大的缓冲区并正确处理负数 */
static void print_syscall_number(int64_t v)
{
    char tmp[24], buf[26];
    int i = 0, j = 0;
    int neg = (v < 0);
    uint64_t u = neg ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
    do { tmp[i++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (neg) buf[j++] = '-';
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
    vga_puts(buf);
}

/* V0.33: 「本次调用是否已被分发处理」标志
 *
 * 旧实现用**返回值 -1** 同时表达两件完全不同的事：
 *   1. 「这个系统调用号没人处理」（未命中 syscall_table）；
 *   2. 「命中了，但处理结果就是一个 errno=-1」。
 * 上层 syscall_handler_64 只看返回值区分不了这两者，于是把一个**合法的
 * errno=-1** 误判成「未知系统调用」，打印 "[SYSCALL] Unknown syscall: XX"
 * 并把用户的正常调用报错掉。
 *
 * 真实症状：glibc 启动时调用 arch_prctl(ARCH_SET_CPUID=0x3001)，
 * sys_arch_prctl 对它返回 -1 ⇒ dispatch 返回 -1 ⇒ 上层误判未处理 ⇒
 * 日志里凭空多出 "[SYSCALL] Unknown syscall: 9E"（0x9E=158=arch_prctl）。
 *
 * 修法（契约拆分，符合 PHILOSOPHY「确定性优先」）：
 *   - 返回值**只**表达 errno / 调用结果；
 *   - 「是否处理过」由独立标志 g_syscall64_handled 表达。
 * 两个语义彻底分开，不再互相冒充。 */
int g_syscall64_handled = 0;

/* 查询上一次 dispatch 是否处理了该调用（供 syscall64_simple.c 使用） */
int syscall64_last_handled(void) { return g_syscall64_handled; }

/* 系统调用主处理函数（查表调用） */
int64_t syscall64_dispatch(int number, int64_t arg1, int64_t arg2,
                            int64_t arg3, int64_t arg4, int64_t arg5,
                            int64_t arg6)
{
    /* 未初始化 = 内核自己还没准备好 = 这次调用没人处理 */
    g_syscall64_handled = 0;
    if (!syscall_initialized) return -1;

    /* seccomp 过滤器看到的是用户实际发出的系统调用号（翻译前），
     * 与 Linux seccomp_data.nr 语义一致；翻译只用于后续分发查表。 */
    int seccomp_nr = number;

    /* Linux x86_64 系统调用号翻译（兼容层）。
     * V0.54.6: 32/64 位 syscall 号统一之后，旧表仍指向统一前的 Hepu 号
     * （如 [1]=1 = FP_VERIFY），Linux 的 read/write/open 全部翻错，
     * 静态 glibc 与动态链接器都跑不起来。这里按 kernel/include/syscall.h
     * 的新编号重写。 */
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
    /* V0.33: Linux syscall 翻译 — **按进程**判定 ABI
     * 旧实现用的是全局开关 g_running_linux_elf（exec 时无条件置 1），
     * 于是连 Hepu 原生程序都被按 Linux 号表翻译：原生 read(2) 被当成
     * open(2)、原生 open(3) 被当成 close(4)……原生 ABI 被兼容层绑架。
     * 现在按当前进程的 env_type 判定 —— 原生进程走原生号表，
     * 只有 Linux 兼容进程才翻译（PHILOSOPHY.md 第四节：原生优先，兼容附加）。 */
    {
        extern int user_process_current_env_type(void);
        if (user_process_current_env_type() == ENV_LINUX_COMPAT &&
            number >= 0 && number < 350 && linux_map[number]) {
            number = linux_map[number];
        }
    }

    /* V0.33: Linux syscall 跟踪（默认关）
     * 旧实现对每个 Linux 兼容进程的**每一次**系统调用都打一行，
     * 启动日志被冲得没法看。跟踪是调试手段，不是系统行为 —— 收进编译开关。
     * 打开：make HEPU_TRACE_LINUX_SYSCALL=1
     * V0.33：跟踪点挪到**翻译之后、所有提前返回之前** ——
     * 原来放在查表调用旁边，write/open/arch_prctl 这些走提前返回分支的
     * 系统调用一个都看不到，排查时「这个调用到底发生没有」根本查不出来。 */
#ifdef HEPU_TRACE_LINUX_SYSCALL
    {
        extern int user_process_current_env_type(void);
        if (user_process_current_env_type() == ENV_LINUX_COMPAT) {
            vga_puts("[S");
            print_syscall_number(number);
            vga_puts(" a=");
            print_syscall_number(arg1);
            vga_puts(" b=");
            print_syscall_number(arg2);
            vga_puts("]\n");
        }
    }
#endif

    /* P0-4/P0-5: seccomp 强制点 —— strict 白名单或 cBPF 过滤器拒绝则直接返回。
     * 放在号段边界检查之前，保证过滤器对**用户实际发出的任何调用号**都可见；
     * 两种模式由 seccomp64_guard_current() 统一处理：
     *   ALLOW/LOG 放行；ERRNO 返回 -(data)；KILL_* 终止进程并返回 -EPERM。 */
    {
        uint64_t sargs[6] = {
            (uint64_t)arg1, (uint64_t)arg2, (uint64_t)arg3,
            (uint64_t)arg4, (uint64_t)arg5, (uint64_t)arg6
        };
        int64_t sret = 0;
        if (seccomp64_guard_current(seccomp_nr, 0, sargs, &sret)) {
            g_syscall64_handled = 1;
            return sret;
        }
    }

    /* 边界检查 */
    if (number < 0 || number >= SYSCALL_MAX) {
        extern int user_process_current_env_type(void);
        if (user_process_current_env_type() == ENV_LINUX_COMPAT) {
            g_syscall64_handled = 1;   /* 兼容层：已处理（按 0 返回，让 glibc 继续） */
            return 0;
        }
        vga_puts("[SYSCALL64] Unknown syscall: ");
        print_syscall_number(number);
        vga_puts("\n");
        g_syscall64_handled = 1;       /* 已判定为 ENOSYS —— 这是一次「处理过的调用」 */
        return -38;  /* -ENOSYS */
    }

    /* 检查函数指针是否存在 */
    if (!syscall_table[number]) {
        extern int user_process_current_env_type(void);
        if (user_process_current_env_type() != ENV_LINUX_COMPAT) {
            vga_puts("[SYSCALL64] Unimplemented syscall: ");
            print_syscall_number(number);
            vga_puts("\n");
        }
        g_syscall64_handled = 1;       /* 已判定为「未实现但按 0 返回」 */
        return 0;  /* V0.21.1: 返回0而非-ENOSYS, 让glibc继续 */
    }

    /* 查表调用 —— 这里命中表，返回值**就是** errno/结果，
     * 哪怕是 -1 也必须原样返回给用户态，不能被上层当成「未处理」。 */
    g_syscall64_handled = 1;
    int64_t ret = syscall_table[number](arg1, arg2, arg3, arg4, arg5, arg6);
#ifdef HEPU_TRACE_LINUX_SYSCALL
    {
        extern int user_process_current_env_type(void);
        if (user_process_current_env_type() == ENV_LINUX_COMPAT) {
            vga_puts(" ret=");
            print_syscall_number((uint64_t)ret);
            vga_puts("\n");
        }
    }
#endif
    return ret;
}
