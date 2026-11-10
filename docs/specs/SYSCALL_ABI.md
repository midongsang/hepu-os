---
title: 赫普OS 系统调用 ABI（源码自动生成）
status: active
sys_version: 0.54.11
updated: 2026-11-01
generator: tools/gen_syscall_doc.py
source_of_truth: kernel/arch/x86_64/syscall64_full.c
---

# 系统调用 ABI（SYSCALL_ABI）

> 本文件由 `tools/gen_syscall_doc.py` 自动生成，禁止手写。
> 生成命令：`python3 tools/gen_syscall_doc.py > docs/specs/SYSCALL_ABI.md`
> SYSCALL_MAX=256。调用约定：`syscall64_dispatch(number, arg1..arg5)`。

## 已注册系统调用

| 号 | 名称 | 处理器 | 状态 |
|----|------|--------|------|

| 1 | WRITE | wrap_write | 已实现 |
| 2 | READ | wrap_read | 已实现 |
| 3 | OPEN | wrap_open | 已实现 |
| 4 | CLOSE | wrap_close | 已实现 |
| 5 | FSTAT | wrap_fstat | 已实现 |
| 6 | EXIT | wrap_exit | 已实现 |
| 7 | FORK | wrap_fork | 已实现 |
| 8 | LSEEK | wrap_lseek | 已实现 |
| 10 | UNLINK | wrap_unlink | 已实现 |
| 11 | EXECVE | wrap_execve | 已实现 |
| 12 | BRK | wrap_brk | 已实现 |
| 13 | TIME | wrap_time | 已实现 |
| 14 | SLEEP | wrap_sleep | 已实现 |
| 20 | GETPID | wrap_getpid | 已实现 |
| 21 | ACCESS | wrap_access | 已实现 |
| 25 | KILL | wrap_kill | 已实现 |
| 26 | DISK_READ | wrap_disk_read | 已实现 |
| 27 | DISK_WRITE | wrap_disk_write | 已实现 |
| 28 | REBOOT | wrap_reboot | 已实现 |
| 29 | NET_STATS | wrap_net_stats | 已实现 |
| 30 | READDIR | wrap_readdir | 已实现 |
| 31 | MKFS | wrap_mkfs | 已实现 |
| 32 | STAT | wrap_stat | 已实现 |
| 33 | MMAP | wrap_mmap | 已实现 |
| 34 | MUNMAP | wrap_munmap | 已实现 |
| 50 | MPROTECT | wrap_mprotect | 已实现 |
| 52 | CHDIR | wrap_chdir | 已实现 |
| 53 | DUP | wrap_dup | 已实现 |
| 54 | UMASK | wrap_umask | 已实现 |
| 55 | GETTIMEOFDAY | wrap_gettimeofday | 已实现 |
| 56 | WAITPID | wrap_waitpid | 已实现 |
| 57 | PIPE | wrap_pipe | 已实现 |
| 58 | GETUID | wrap_getuid | 已实现 |
| 59 | GETGID | wrap_getgid | 已实现 |
| 60 | SYSINFO | wrap_sysinfo | 已实现 |
| 61 | IOCTL | wrap_ioctl | 已实现 |
| 62 | ARCH_PRCTL | wrap_arch_prctl | 已实现 |
| 63 | GETDENTS | wrap_getdents | 已实现 |
| 64 | GETPPID | wrap_getppid | 已实现 |
| 65 | SET_TID | wrap_set_tid | 已实现 |
| 66 | SET_ROBUST | wrap_set_robust | 已实现 |
| 67 | RSEQ | wrap_rseq | 已实现 |
| 68 | READV | wrap_readv | 已实现 |
| 69 | WRITEV | wrap_writev | 已实现 |
| 70 | PSELECT6 | wrap_pselect6 | 已实现 |
| 71 | EPOLL_CREATE | wrap_epoll_create | 已实现 |
| 72 | EPOLL_CTL | wrap_epoll_ctl | 已实现 |
| 73 | EPOLL_WAIT | wrap_epoll_wait | 已实现 |
| 75 | CLOCK_GETTIME | wrap_clock_gettime | 已实现 |
| 76 | NEWUNAME | wrap_newuname | 已实现 |
| 77 | FUTEX | wrap_futex | 已实现 |
| 78 | GETRANDOM | wrap_getrandom | 已实现 |
| 79 | GETCWD | wrap_getcwd | 已实现 |
| 81 | DUP2 | wrap_dup2 | 已实现 |
| 82 | RENAME | wrap_rename | 已实现 |
| 83 | MKDIR | wrap_mkdir | 已实现 |
| 84 | RMDIR | wrap_rmdir | 已实现 |
| 85 | MREMAP | wrap_mremap | 已实现 |
| 86 | PRLIMIT | wrap_prlimit | 已实现 |
| 87 | OPENAT | wrap_openat | 已实现 |
| 88 | READLINK | wrap_readlink | 已实现 |
| 89 | FCNTL | wrap_fcntl | 已实现 |
| 90 | CLOCK_GETRES | wrap_clock_getres | 已实现 |
| 91 | CLOCK_NANOSLEEP | wrap_clock_nanosleep | 已实现 |
| 92 | NEWFSTATAT | wrap_newfstatat | 已实现 |
| 93 | GETEUID | wrap_geteuid | 已实现 |
| 94 | GETEGID | wrap_getegid | 已实现 |
| 95 | SCHED_YIELD | wrap_sched_yield | 已实现 |
| 96 | SOCKET | wrap_socket | 已实现 |
| 97 | CONNECT | wrap_connect | 已实现 |
| 98 | SENDTO | wrap_sendto | 已实现 |
| 99 | RECVFROM | wrap_recvfrom | 已实现 |
| 100 | SHUTDOWN | wrap_shutdown | 已实现 |
| 101 | SETSOCKOPT | wrap_setsockopt | 已实现 |
| 102 | GETSOCKOPT | wrap_getsockopt | 已实现 |
| 103 | GETRUSAGE | wrap_getrusage | 已实现 |
| 104 | WAIT4 | wrap_wait4 | 已实现 |
| 105 | CHMOD | wrap_chmod | 已实现 |
| 106 | FCHMOD | wrap_fchmod | 已实现 |
| 107 | PIPE2 | — | 未注册 |
| 108 | EVENTFD2 | wrap_eventfd2 | 已实现 |
| 109 | RT_SIGRETURN | wrap_rt_sigreturn | 已实现 |
| 110 | ACCEPT | wrap_accept | 已实现 |
| 111 | BIND | wrap_bind | 已实现 |
| 112 | LISTEN | wrap_listen | 已实现 |
| 113 | GETSOCKNAME | wrap_getsockname | 已实现 |
| 114 | GETPEERNAME | wrap_getpeername | 已实现 |
| 115 | SOCKETPAIR | wrap_socketpair | 已实现 |
| 116 | CLONE | wrap_clone | 已实现 |
| 117 | CREAT | wrap_creat | 已实现 |
| 118 | LINK | wrap_link | 已实现 |
| 119 | CHOWN | wrap_chown | 已实现 |
| 120 | FCHOWN | wrap_fchown | 已实现 |
| 121 | LCHOWN | wrap_lchown | 已实现 |
| 122 | GETPGRP | wrap_getpgrp | 已实现 |
| 123 | VFORK | wrap_vfork | 已实现 |
| 124 | SETUID | wrap_setuid | 已实现 |
| 125 | SETGID | wrap_setgid | 已实现 |
| 126 | DUP3 | wrap_dup3 | 已实现 |
| 127 | SELECT | wrap_select | 已实现 |
| 128 | MLOCK | wrap_mlock | 已实现 |
| 129 | MADVISE | wrap_madvise | 已实现 |
| 130 | SIGACTION | wrap_sigaction | 已实现 |
| 131 | SIGPROCMASK | wrap_sigprocmask | 已实现 |
| 132 | TGKILL | wrap_tgkill | 已实现 |
| 256 | MAX | — | 未注册 |
