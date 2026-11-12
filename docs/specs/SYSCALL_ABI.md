---
title: 赫普OS 系统调用 ABI（源码自动生成）
status: active
sys_version: 0.57.0
updated: 2026-11-12
generator: tools/gen_syscall_doc.py
source_of_truth: kernel/include/syscall.h + kernel/arch/x86_64/syscall64_full.c
---

# 系统调用 ABI（SYSCALL_ABI）

> 本文件由 `tools/gen_syscall_doc.py` 自动生成，**禁止手写**。
> 重新生成：`python3 tools/gen_syscall_doc.py > docs/specs/SYSCALL_ABI.md`
> 双重事实源：`SYS_xxx` 号在 `kernel/include/syscall.h`，`SYSCALL_xxx` 映射与注册表在 `syscall64_full.c`。
> 调用约定：`syscall64_dispatch(number, arg1..arg5)`；64 位入口 `syscall` 指令带 6 个参数寄存器（rdi/rsi/rdx/r10/r8/r9），32 位 `int 0x80` 入口只带 4 个。
> 「号」= 最终数值；「号来源」标明该值是原生号还是直接采用 Linux 号（后者有撞号风险，见文末自检）。

## 已注册（已实现）系统调用

| 号 | 名称 | 处理器 | 号来源 | 宏 |
|----|------|--------|--------|-----|
| 5 | EXIT | wrap_exit | 原生 SYS_EXIT | `SYSCALL_EXIT` |
| 7 | GETPID | wrap_getpid | 原生 SYS_GETPID | `SYSCALL_GETPID` |
| 8 | OPEN | wrap_open | 原生 SYS_OPEN | `SYSCALL_OPEN` |
| 9 | READ | wrap_read | 原生 SYS_READ | `SYSCALL_READ` |
| 10 | WRITE | wrap_write | 原生 SYS_WRITE | `SYSCALL_WRITE` |
| 11 | CLOSE | wrap_close | 原生 SYS_CLOSE | `SYSCALL_CLOSE` |
| 14 | LSEEK | wrap_lseek | 原生 SYS_LSEEK | `SYSCALL_LSEEK` |
| 15 | SLEEP | wrap_sleep | 原生 SYS_SLEEP | `SYSCALL_SLEEP` |
| 16 | PIPE | wrap_pipe | 原生 SYS_PIPE | `SYSCALL_PIPE` |
| 19 | KILL | wrap_kill | 原生 SYS_KILL | `SYSCALL_KILL` |
| 21 | TIME | wrap_time | 原生 SYS_TIME | `SYSCALL_TIME` |
| 22 | GETTIMEOFDAY | wrap_gettimeofday | 原生 SYS_GETTIMEOFDAY | `SYSCALL_GETTIMEOFDAY` |
| 24 | STAT | wrap_stat | 原生 SYS_STAT | `SYSCALL_STAT` |
| 25 | FSTAT | wrap_fstat | 原生 SYS_FSTAT | `SYSCALL_FSTAT` |
| 26 | ACCESS | wrap_access | 原生 SYS_ACCESS | `SYSCALL_ACCESS` |
| 27 | UNLINK | wrap_unlink | 原生 SYS_UNLINK | `SYSCALL_UNLINK` |
| 28 | RENAME | wrap_rename | 原生 SYS_RENAME | `SYSCALL_RENAME` |
| 29 | MKDIR | wrap_mkdir | 原生 SYS_MKDIR | `SYSCALL_MKDIR` |
| 30 | RMDIR | wrap_rmdir | 原生 SYS_RMDIR | `SYSCALL_RMDIR` |
| 31 | READDIR | wrap_readdir | 原生 SYS_READDIR | `SYSCALL_READDIR` |
| 32 | CHMOD | wrap_chmod | 原生 SYS_CHMOD | `SYSCALL_CHMOD` |
| 33 | CHOWN | wrap_chown | 原生 SYS_CHOWN | `SYSCALL_CHOWN` |
| 36 | IOCTL | wrap_ioctl | 原生 SYS_IOCTL | `SYSCALL_IOCTL` |
| 37 | MMAP | wrap_mmap | 原生 SYS_MMAP | `SYSCALL_MMAP` |
| 38 | MUNMAP | wrap_munmap | 原生 SYS_MUNMAP | `SYSCALL_MUNMAP` |
| 39 | MPROTECT | wrap_mprotect | 原生 SYS_MPROTECT | `SYSCALL_MPROTECT` |
| 40 | CLONE | wrap_clone | 原生 SYS_CLONE | `SYSCALL_CLONE` |
| 41 | WAITPID | wrap_waitpid | 原生 SYS_WAITPID | `SYSCALL_WAITPID` |
| 42 | EXECVE | wrap_execve | 原生 SYS_EXECVE | `SYSCALL_EXECVE` |
| 43 | FORK | wrap_fork | 原生 SYS_FORK | `SYSCALL_FORK` |
| 47 | SOCKET | wrap_socket | 原生 SYS_SOCKET | `SYSCALL_SOCKET` |
| 48 | BIND | wrap_bind | 原生 SYS_BIND | `SYSCALL_BIND` |
| 49 | LISTEN | wrap_listen | 原生 SYS_LISTEN | `SYSCALL_LISTEN` |
| 50 | ACCEPT | wrap_accept | 原生 SYS_ACCEPT | `SYSCALL_ACCEPT` |
| 51 | CONNECT | wrap_connect | 原生 SYS_CONNECT | `SYSCALL_CONNECT` |
| 54 | GETCWD | wrap_getcwd | 原生 SYS_GETCWD | `SYSCALL_GETCWD` |
| 55 | CHDIR | wrap_chdir | 原生 SYS_CHDIR | `SYSCALL_CHDIR` |
| 56 | DUP | wrap_dup | 原生 SYS_DUP | `SYSCALL_DUP` |
| 57 | DUP2 | wrap_dup2 | 原生 SYS_DUP2 | `SYSCALL_DUP2` |
| 58 | FCNTL | wrap_fcntl | 原生 SYS_FCNTL | `SYSCALL_FCNTL` |
| 59 | GETUID | wrap_getuid | 原生 SYS_GETUID | `SYSCALL_GETUID` |
| 60 | GETEUID | wrap_geteuid | 原生 SYS_GETEUID | `SYSCALL_GETEUID` |
| 61 | GETGID | wrap_getgid | 原生 SYS_GETGID | `SYSCALL_GETGID` |
| 62 | GETEGID | wrap_getegid | 原生 SYS_GETEGID | `SYSCALL_GETEGID` |
| 64 | BRK | wrap_brk | 原生 SYS_BRK | `SYSCALL_BRK` |
| 65 | GETPPID | wrap_getppid | 原生 SYS_GETPPID | `SYSCALL_GETPPID` |
| 66 | WAIT4 | wrap_wait4 | 原生 SYS_WAIT4 | `SYSCALL_WAIT4` |
| 67 | FCHMOD | wrap_fchmod | 原生 SYS_FCHMOD | `SYSCALL_FCHMOD` |
| 68 | FCHOWN | wrap_fchown | 原生 SYS_FCHOWN | `SYSCALL_FCHOWN` |
| 69 | LCHOWN | wrap_lchown | 原生 SYS_LCHOWN | `SYSCALL_LCHOWN` |
| 70 | GETPGRP | wrap_getpgrp | 原生 SYS_GETPGRP | `SYSCALL_GETPGRP` |
| 71 | VFORK | wrap_vfork | 原生 SYS_VFORK | `SYSCALL_VFORK` |
| 72 | SETUID | wrap_setuid | 原生 SYS_SETUID | `SYSCALL_SETUID` |
| 73 | SETGID | wrap_setgid | 原生 SYS_SETGID | `SYSCALL_SETGID` |
| 74 | GETRUSAGE | wrap_getrusage | 原生 SYS_GETRUSAGE | `SYSCALL_GETRUSAGE` |
| 75 | RT_SIGRETURN | wrap_rt_sigreturn | 原生 SYS_RT_SIGRETURN | `SYSCALL_RT_SIGRETURN` |
| 76 | CREAT | wrap_creat | 原生 SYS_CREAT | `SYSCALL_CREAT` |
| 77 | LINK | wrap_link | 原生 SYS_LINK | `SYSCALL_LINK` |
| 100 | SECCOMP | wrap_seccomp | 原生 SYS_LNX64_SECCOMP | `SYSCALL_SECCOMP` |
| 101 | DUP3 | wrap_dup3 | 原生 SYS_LNX64_DUP3 | `SYSCALL_DUP3` |
| 102 | SELECT | wrap_select | 原生 SYS_LNX64_SELECT | `SYSCALL_SELECT` |
| 103 | MLOCK | wrap_mlock | 原生 SYS_LNX64_MLOCK | `SYSCALL_MLOCK` |
| 104 | MADVISE | wrap_madvise | 原生 SYS_LNX64_MADVISE | `SYSCALL_MADVISE` |
| 105 | SYSINFO | wrap_sysinfo | 原生 SYS_LNX64_SYSINFO | `SYSCALL_SYSINFO` |
| 106 | ARCH_PRCTL | wrap_arch_prctl | 原生 SYS_LNX64_ARCH_PRCTL | `SYSCALL_ARCH_PRCTL` |
| 107 | GETDENTS | wrap_getdents | 原生 SYS_LNX64_GETDENTS | `SYSCALL_GETDENTS` |
| 108 | SET_TID | wrap_set_tid | 原生 SYS_LNX64_SET_TID | `SYSCALL_SET_TID` |
| 109 | SET_ROBUST | wrap_set_robust | 原生 SYS_LNX64_SET_ROBUST | `SYSCALL_SET_ROBUST` |
| 110 | RSEQ | wrap_rseq | 原生 SYS_LNX64_RSEQ | `SYSCALL_RSEQ` |
| 111 | READV | wrap_readv | 原生 SYS_LNX64_READV | `SYSCALL_READV` |
| 112 | WRITEV | wrap_writev | 原生 SYS_LNX64_WRITEV | `SYSCALL_WRITEV` |
| 113 | PSELECT6 | wrap_pselect6 | 原生 SYS_LNX64_PSELECT6 | `SYSCALL_PSELECT6` |
| 114 | EPOLL_CREATE | wrap_epoll_create | 原生 SYS_LNX64_EPOLL_CREATE | `SYSCALL_EPOLL_CREATE` |
| 115 | EPOLL_CTL | wrap_epoll_ctl | 原生 SYS_LNX64_EPOLL_CTL | `SYSCALL_EPOLL_CTL` |
| 116 | EPOLL_WAIT | wrap_epoll_wait | 原生 SYS_LNX64_EPOLL_WAIT | `SYSCALL_EPOLL_WAIT` |
| 117 | EVENTFD2 | wrap_eventfd2 | 原生 SYS_LNX64_EVENTFD2 | `SYSCALL_EVENTFD2` |
| 118 | CLOCK_GETTIME | wrap_clock_gettime | 原生 SYS_LNX64_CLOCK_GETTIME | `SYSCALL_CLOCK_GETTIME` |
| 119 | NEWUNAME | wrap_newuname | 原生 SYS_LNX64_NEWUNAME | `SYSCALL_NEWUNAME` |
| 120 | FUTEX | wrap_futex | 原生 SYS_LNX64_FUTEX | `SYSCALL_FUTEX` |
| 121 | GETRANDOM | wrap_getrandom | 原生 SYS_LNX64_GETRANDOM | `SYSCALL_GETRANDOM` |
| 122 | SIGACTION | wrap_sigaction | 原生 SYS_LNX64_SIGACTION | `SYSCALL_SIGACTION` |
| 123 | SIGPROCMASK | wrap_sigprocmask | 原生 SYS_LNX64_SIGPROCMASK | `SYSCALL_SIGPROCMASK` |
| 124 | TGKILL | wrap_tgkill | 原生 SYS_LNX64_TGKILL | `SYSCALL_TGKILL` |
| 125 | MREMAP | wrap_mremap | 原生 SYS_LNX64_MREMAP | `SYSCALL_MREMAP` |
| 126 | PRLIMIT | wrap_prlimit | 原生 SYS_LNX64_PRLIMIT | `SYSCALL_PRLIMIT` |
| 127 | OPENAT | wrap_openat | 原生 SYS_LNX64_OPENAT | `SYSCALL_OPENAT` |
| 128 | READLINK | wrap_readlink | 原生 SYS_LNX64_READLINK | `SYSCALL_READLINK` |
| 129 | CLOCK_GETRES | wrap_clock_getres | 原生 SYS_LNX64_CLOCK_GETRES | `SYSCALL_CLOCK_GETRES` |
| 130 | CLOCK_NANOSLEEP | wrap_clock_nanosleep | 原生 SYS_LNX64_CLOCK_NANOSLEEP | `SYSCALL_CLOCK_NANOSLEEP` |
| 131 | NEWFSTATAT | wrap_newfstatat | 原生 SYS_LNX64_NEWFSTATAT | `SYSCALL_NEWFSTATAT` |
| 132 | SCHED_YIELD | wrap_sched_yield | 原生 SYS_LNX64_SCHED_YIELD | `SYSCALL_SCHED_YIELD` |
| 133 | SETSOCKOPT | wrap_setsockopt | 原生 SYS_LNX64_SETSOCKOPT | `SYSCALL_SETSOCKOPT` |
| 134 | GETSOCKOPT | wrap_getsockopt | 原生 SYS_LNX64_GETSOCKOPT | `SYSCALL_GETSOCKOPT` |
| 135 | SOCKETPAIR | wrap_socketpair | 原生 SYS_LNX64_SOCKETPAIR | `SYSCALL_SOCKETPAIR` |
| 136 | PREAD | wrap_pread | 原生 SYS_LNX64_PREAD | `SYSCALL_PREAD` |
| 140 | GETSOCKNAME | wrap_getsockname | Linux 号直取 | `SYSCALL_GETSOCKNAME` |
| 141 | GETPEERNAME | wrap_getpeername | 源码数字 | `SYSCALL_GETPEERNAME` |
| 143 | SENDTO | wrap_sendto | 源码数字 | `SYSCALL_SENDTO` |
| 144 | RECVFROM | wrap_recvfrom | 源码数字 | `SYSCALL_RECVFROM` |
| 145 | SHUTDOWN | wrap_shutdown | 源码数字 | `SYSCALL_SHUTDOWN` |
| 209 | UMASK | wrap_umask | 源码数字 | `SYSCALL_UMASK` |
| 222 | DISK_READ | wrap_disk_read | 原生 SYS_DISK_READ | `SYSCALL_DISK_READ` |
| 223 | DISK_WRITE | wrap_disk_write | 原生 SYS_DISK_WRITE | `SYSCALL_DISK_WRITE` |
| 224 | REBOOT | wrap_reboot | 原生 SYS_REBOOT | `SYSCALL_REBOOT` |
| 225 | NET_STATS | wrap_net_stats | 原生 SYS_NET_STATS | `SYSCALL_NET_STATS` |
| 226 | MKFS | wrap_mkfs | 原生 SYS_MKFS | `SYSCALL_MKFS` |

共 106 项已注册。

## 已定义但未注册（未实现/未接线）

| 名称 | 号 | 号来源 | 宏 |
|------|-----|--------|-----|
| PIPE2 | 159 | 源码数字 | `SYSCALL_PIPE2` |

## 生成器自检

✅ 未检测到已注册宏之间的号冲突。

## 口径说明

1. **号空间是混用的**：`SYS_xxx` 原生号（0–99）、`SYS_LNX64_xxx` 自定义扩展号（100 段）、
   以及少数直接采用 Linux x86_64 号（如 `SYSCALL_GETSOCKNAME=140`、`SYSCALL_SHUTDOWN=145`）。
   后者一旦与未来原生号扩张撞车，注册表会静默覆盖——新增号时必须先跑本生成器查冲突。
2. **本表只描述"已注册"**（即 `syscall_table[]` 里有项）。注册了但实现是桩返回 0 的项
   （当前：`epoll_create/epoll_ctl/epoll_wait/eventfd2`）**仍然算"已注册"**，
   其语义完整度见 `planning/ROADMAP.md` P1-1 与 `architecture/KERNEL.md` 4.3 备注。
3. 32 位路径（`int 0x80`）与本表共用同一号空间，但入口只压 4 个参数寄存器，
   所以 `mmap(fd/offset)`、`clone(tls)`、`futex` 在 32 位路径上参数不全。
