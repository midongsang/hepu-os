/* ============================================================================
 * 赫普OS - Linux i386 syscall ABI 翻译层头文件
 * Linux x86 32位 syscall 号 (公开 ABI, 见 linux asm/unistd_32.h)
 * 翻译为 HEPU syscall 号 (kernel/include/syscall.h)
 * ========================================================================== */

#ifndef HEPU_LINUX_ABI_H
#define HEPU_LINUX_ABI_H

#include <stdint.h>

/* Linux i386 常用 syscall 号 (公开标准, 部分) */
#define LINUX_SYS_restart_syscall  0
#define LINUX_SYS_exit             1
#define LINUX_SYS_fork             2
#define LINUX_SYS_read             3
#define LINUX_SYS_write            4
#define LINUX_SYS_open             5
#define LINUX_SYS_close            6
#define LINUX_SYS_waitpid          7
#define LINUX_SYS_creat            8
#define LINUX_SYS_link             9
#define LINUX_SYS_unlink          10
#define LINUX_SYS_execve          11
#define LINUX_SYS_chdir           12
#define LINUX_SYS_time            13
#define LINUX_SYS_chmod           15
#define LINUX_SYS_lchown          16
#define LINUX_SYS_lseek           19
#define LINUX_SYS_getpid          20
#define LINUX_SYS_mount           21
#define LINUX_SYS_umount2         22
#define LINUX_SYS_setuid          23
#define LINUX_SYS_getuid          24
#define LINUX_SYS_alarm           27
#define LINUX_SYS_fstat           28
#define LINUX_SYS_pause           29
#define LINUX_SYS_access          33
#define LINUX_SYS_kill            37
#define LINUX_SYS_rename          38
#define LINUX_SYS_mkdir           39
#define LINUX_SYS_rmdir           40
#define LINUX_SYS_dup             41
#define LINUX_SYS_pipe            42
#define LINUX_SYS_brk             45
#define LINUX_SYS_getgid          47
#define LINUX_SYS_signal          48
#define LINUX_SYS_geteuid         49
#define LINUX_SYS_getegid         50
#define LINUX_SYS_ioctl           54
#define LINUX_SYS_fcntl           55
#define LINUX_SYS_dup2            63
#define LINUX_SYS_getppid         64
#define LINUX_SYS_setsid          66
#define LINUX_SYS_sigaction       67
#define LINUX_SYS_sethostname     74
#define LINUX_SYS_gettimeofday    78
#define LINUX_SYS_getrlimit       79
#define LINUX_SYS_symlink         83
#define LINUX_SYS_readlink        85
#define LINUX_SYS_mmap            90
#define LINUX_SYS_munmap          91
#define LINUX_SYS_truncate        92
#define LINUX_SYS_ftruncate       93
#define LINUX_SYS_fchmod          94
#define LINUX_SYS_getpriority    96
#define LINUX_SYS_socketcall     102
#define LINUX_SYS_sysinfo        116
#define LINUX_SYS_ipc            117
#define LINUX_SYS_fsync          118
#define LINUX_SYS_clone          120
#define LINUX_SYS_uname          122
#define LINUX_SYS_mprotect       125
#define LINUX_SYS_sigprocmask    126
#define LINUX_SYS_select         142
#define LINUX_SYS_writev         146
#define LINUX_SYS_nanosleep      162
#define LINUX_SYS_poll           168
#define LINUX_SYS_rt_sigaction   174
#define LINUX_SYS_mmap2          192
#define LINUX_SYS_stat64         195
#define LINUX_SYS_lstat64        196
#define LINUX_SYS_fstat64        197
#define LINUX_SYS_getdents64     220
#define LINUX_SYS_set_thread_area 243
#define LINUX_SYS_exit_group     252
#define LINUX_SYS_openat         295
#define LINUX_SYS_mkdirat        296
#define LINUX_SYS_fstatat64      300
#define LINUX_SYS_unlinkat       301
#define LINUX_SYS_renameat       307
#define LINUX_SYS_pipe2          353

/* 翻译结果 */
#define LINUX_ABI_TRANSLATED  0x80000000u  /* 高位置位: 已翻译 */
#define LINUX_ABI_UNSUPPORTED 0xFFFFFFFFu

/* 翻译一个 Linux i386 syscall 号 → HEPU syscall 号.
 * 返回 LINUX_ABI_UNSUPPORTED 表示不支持. */
uint32_t linux_abi_translate(uint32_t linux_nr);

/* 判断一个地址是否在 Linux 程序地址空间内 (加载器用) */
int linux_abi_valid_addr(uint32_t addr);

#endif /* HEPU_LINUX_ABI_H */
