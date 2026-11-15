/* ============================================================================
 * HepuOS 64 位 - 文件系统 syscall 业务（从 syscall64_full.c 切出）
 *
 * KERNEL_SCOPE §5-5：syscall64_full.c 只该做"分发"，业务归 fs / mm。
 * 这里放目录与文件的**语义实现**（存在性判定、错误码选择、参数校验），
 * syscall 层只保留号 → 函数的映射。
 *
 * 边界约定：用户指针拷入（copy_user_path）仍属 syscall 层的边界动作，
 * 但实现放在 syscall64_full.c 里，这里 extern 使用 —— 不复制第二份。
 * ========================================================================== */
#ifndef HEPU_FS_SYS_H
#define HEPU_FS_SYS_H

#include <stddef.h>

/* 从用户态拷路径到内核栈缓冲；返回路径长度，失败 -1。
 * 定义在 syscall64_full.c（§5-5 去 static）。 */
int copy_user_path(char *dst, size_t dstsz, const char *user_path);

#endif
