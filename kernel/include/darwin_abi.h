/* ============================================================================
 * 赫普OS - Darwin (macOS) i386 syscall ABI 翻译层
 * macOS 用户态 int 0x80 调用号 = Mach trap 号 (UNIX 号 + 0x2000000 基址,
 * 来源: XNU 公开 syscalls.master) → HEPU syscall 号 (kernel/include/syscall.h)
 * 阶段 C 配套: Mach-O 兼容进程 (compat_abi=3) 的 syscall 翻译入口.
 * ========================================================================== */

#ifndef HEPU_DARWIN_ABI_H
#define HEPU_DARWIN_ABI_H

#include <stdint.h>

/* macOS i386 Mach trap 号 (公开 syscalls.master, 32 位基址 0x2000000) */
#define DARWIN_SYS_exit    0x20000001u
#define DARWIN_SYS_fork    0x20000002u
#define DARWIN_SYS_read    0x20000003u
#define DARWIN_SYS_write   0x20000004u
#define DARWIN_SYS_open    0x20000005u
#define DARWIN_SYS_close   0x20000006u
#define DARWIN_SYS_wait4   0x20000007u
#define DARWIN_SYS_unlink  0x2000000Au
#define DARWIN_SYS_chdir   0x2000000Cu
#define DARWIN_SYS_chmod   0x2000000Fu
#define DARWIN_SYS_getpid  0x20000014u
#define DARWIN_SYS_getuid  0x20000018u
#define DARWIN_SYS_access  0x20000021u
#define DARWIN_SYS_kill    0x20000025u
#define DARWIN_SYS_getgid  0x2000002Fu
#define DARWIN_SYS_munmap  0x20000049u
#define DARWIN_SYS_mmap    0x200000C5u

/* 翻译结果 */
#define DARWIN_ABI_UNSUPPORTED 0xFFFFFFFFu

/* 翻译一个 Darwin (Mach trap) syscall 号 → HEPU syscall 号.
 * 返回 DARWIN_ABI_UNSUPPORTED 表示不支持. */
uint32_t darwin_abi_translate(uint32_t darwin_nr);

#endif /* HEPU_DARWIN_ABI_H */
