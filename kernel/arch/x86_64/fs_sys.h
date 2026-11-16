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

/* 向用户态写回（同样属 syscall 层的边界动作，实现留在 syscall64_full.c）。 */
extern int copy_to_user(void *user_dst, const void *kernel_src, size_t size);

/* 第 2 批新增：把某个 fd 的路径读出来。
 *
 * 为什么要包一层而不是让 fs_stat_sys.c 直接摸 fd 表：fd 表是
 * sys_files_tbl[9][MAX_FILES]，下标由"当前用户进程槽位"决定，还有 g_fd_lock
 * 和 SB-11 的锁持有者诊断变量（g_fd_lock_holder）。这些都属于 syscall 层
 * 的内部状态 —— 一旦把表本身 extern 出去，切出去的每个文件都得知道"用哪张
 * 表、要不要加锁、锁里怎么记账"，那就等于没切。
 * 这里只给"fd → 路径"这一条最小查询：锁在内部取，返回后调用方手里是一份
 * 拷贝，不再触碰表。0 = 成功。-1 = fd 越界或未使用。
 *
 * fd 表锁的必要性在这一批里是**已被满足**的前提：SB-11 已经把它做成表级
 * irqsave 锁（fd_snapshot 就是锁内快照），所以这批不必先补锁。 */
int fs_fd_get_path(int fd, char *out, size_t outsz);

#endif
