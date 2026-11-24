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
#include <stdint.h>

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
/* ---- fd 快照：一个 fd 的"当时样子"，锁内一次取全 ----
 *
 * 第 2/3/4 批各要一个字段就够了(path / mode / offset), 于是分别开了
 * fs_fd_get_path / fs_fd_get_mode 这样的窄接口。read/write 要同时用到
 * path + mode + is_pipe + pipe_buf + offset —— 如果分五次去取, 中间夹一个
 * 并发 close 就会拿到"路径来自 A、offset 来自 B"的缝合快照。
 *
 * 所以这一批改成一次锁内取全。这不是新概念, 就是把 syscall64_full.c 里
 * 原本 static 的 fd_snapshot() 提升为公开边界, 并补上 offset 字段。
 * 表本身仍然不外露: 下标依赖当前用户进程槽位、访问要过 g_fd_lock、锁内还要
 * 维护 g_fd_lock_holder 诊断变量 —— 让每个切出去的文件都复制一遍这套认知
 * 才是真正的技术债。 */
typedef struct {
    char    path[256];
    int     mode;       /* 0 = 只读, 1 = 只写, -1 = 未知 */
    int     is_pipe;
    int     pipe_buf;   /* 管道在 g_pipes[] 里的下标 */
    int64_t offset;     /* 文件内读位置 */
} fs_fd_info_t;

/* 锁内取快照; 0 = 成功, -1 = fd 越界或未占用 */
int fs_fd_snapshot(int fd, fs_fd_info_t *out);

/* 锁内写回读位置; 0 = 成功, -1 = 失败(含 fd 已关闭 / off < 0)。
 * offset 的回写必须在锁内完成: 并发读会各自基于旧值累加, 丢字节。 */
int fs_fd_set_offset(int fd, int64_t off);

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

/* §5-5 第 1 批: 目录类 syscall 实现切到 fs_dir_sys.c, 原型在此统一对外声明。
 * 原型与 fs_dir_sys.c 定义逐字一致:
 *   int64_t sys_unlink(const char *path);
 *   int64_t sys_mkdir(const char *path, int mode);
 *   int64_t sys_rmdir(const char *path); */
int64_t sys_unlink(const char *path);
int64_t sys_mkdir(const char *path, int mode);
int64_t sys_rmdir(const char *path);

/* P4: getdents64 语义实现迁到 fs_dir_sys.c。
 * fd → 路径由 fs_fd_get_path 取快照，枚举走 vfs64_iterate，因而支持任意目录，
 * 不再固定根目录。fs_do_getdents_kernel 供内核自检直接写内核缓冲；
 * fs_do_getdents 负责用户指针边界与 copy_to_user。 */
int64_t fs_do_getdents(int fd, uint64_t dirp, int count);
int fs_do_getdents_kernel(int fd, uint8_t *kbuf, int count);

/* ---- 第 5 批: 文件读写的语义实现 (fs_rw_sys.c) ----
 *
 * 只处理"真文件"。两类东西故意不在这里:
 *   - fd 0/1/2 的 stdio 特判: 那是键盘与串口, 不是文件系统;
 *   - 管道: 那是 IPC, 不是文件分发(§5-5 分层轴: 谁定义数据谁操作它)。
 * 两者由 syscall 层分流, 分流依据是调用方传进来的快照。
 *
 * 快照由调用方 (syscall 层) 取好再传进来 —— 由它取是因为分流判断
 * (is_pipe?) 和语义调用要用**同一份**快照, 各取一次会缝合。
 */
int64_t fs_do_read(int fd, const fs_fd_info_t *fi, char *ubuf, int count);
int64_t fs_do_write(int fd, const fs_fd_info_t *fi, const char *ubuf, int count);

/* pread: 按指定偏移读、**不改** fd 的读位置。
 *
 * 语义上和 fs_do_read 只差"要不要回写 offset"这一个动作, 但这两者不能合并:
 * ld.so 正是靠 pread 反复读 ELF 头和 PT_DYNAMIC 而不破坏文件游标, 如果把它
 * 并进 fs_do_read, 动态链接器的加载顺序会被悄悄改掉 —— 这种错在静态程序上
 * 完全看不出来, 只有 glibc 端到端能抓到。 */
int64_t fs_do_pread(int fd, const fs_fd_info_t *fi, char *ubuf, int count,
                    int64_t offset);

#endif
