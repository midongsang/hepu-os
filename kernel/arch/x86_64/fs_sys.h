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

/* 第 3 批: fd 的**分配与释放**。和上面的查询不同, 这两个改表, 所以只在
 * syscall 层实现, 由 fs_open_sys.c 调用。
 *
 * 为什么不是"把 sys_files_tbl 暴露出去": 表下标依赖当前用户进程槽位,
 * 访问要过 g_fd_lock, 锁内还要维护 g_fd_lock_holder 诊断变量, 分配起点是
 * 3(stdio 保留)。这四件事合起来就是"怎么正确地用这张表" —— 交给切出去的
 * 文件各自复刻, 迟早有文件漏一条。
 *
 * fs_fd_alloc: 锁内找空槽并写 path/mode/offset, 返回 fd; 表满返回 -1。
 *              分配从 3 开始(0/1/2 是 stdio 保留槽位)。
 * fs_fd_release: 锁内置 used=0; 0 = 成功, -1 = 该号本来就没被占用。 */
/* stdio 保留槽位。
 *
 * 这条约定原来只写在 fs_open_sys.c 里, 结果 sys_pipe 没看见它: 管道从 0 开始
 * 找空槽, 把读端分到 fd 0、写端分到 fd 1。后果是 read(读端) 命中 sys_read 的
 * fd==0 分支 -> 阻塞等键盘 -> 进程挂死在 hlt; write(写端) 命中 fd==1 分支 ->
 * 内容直接打到串口, 管道根本没被用到。
 *
 * 提到头文件是为了让所有"分配 fd"的地方(pinp/open/dup)只有一个来源。 */
#define FS_FD_STDIO_RESERVED  3

/* 第 3 批的受理端入口(叫 do_ 不叫 sys_: 内核里的 sys_open 是用户侧发起端) */
int64_t fs_do_open(const char *path, int flags, int mode);
int64_t fs_do_close(int fd);

int fs_fd_alloc(const char *kpath, int mode);
int fs_fd_release(int fd);

#endif
