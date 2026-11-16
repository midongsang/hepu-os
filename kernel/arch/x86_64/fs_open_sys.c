/* ============================================================================
 * Hepu OS - open / close 业务实现
 *
 * 从 syscall64_full.c 切出的第三批（KERNEL_SCOPE §5-5 第 3 步）。
 *
 * 这一批和第 2 批（stat）不同: stat 只**读** fd 表, 这批**改**它。所以边界不是
 * "加一个查询函数"就够, 而要两个写操作:
 *   fs_fd_alloc()  —— 锁内找空槽 + 写路径/mode/offset, 返回号
 *   fs_fd_release()—— 锁内置 used=0
 * 两个都在 syscall 层实现(它们拥有表、锁, 以及 g_fd_lock_holder 那套锁内记账)。
 * 本文件只负责 open/close 的**语义**: 路径拷入、O_CREAT 要不要造文件、
 * 分配失败报什么、close 传非法号报什么。
 *
 * 命名: 这两个函数叫 fs_do_* 而不是 sys_* —— 内核里已经有一组 sys_open /
 * sys_close(user_lib.c 的**用户侧** wrapper, 把调用转成 syscall), 同名会在
 * 链接期撞车(multiple definition)。内置用户库是"内核里跑的用户代码", 它的
 * sys_* 是 syscall 的发起方, 而本文件是 syscall 的受理方, 是相反的两端 ——
 * 名字必须能区分, 否则下次看到 sys_open 根本不知道是发还是收。
 *
 * 验收是 user/apps/fdstress.c(先补测试后切): 两条 CLONE_VM 线程各 400 轮
 * open→close, 查 "号落在 [3,FD_MAX)"、"同 fd 不被两线程同时持有"、
 * "结束后全释放(再开一个必须拿到 3)"。跑出 open_fail=0 conflict=0
 * leak_probe_fd=3 之后才动这段代码。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "fs_sys.h"

/* 与 syscall64_full.c 同一定义（不另起一套） */
#define FS_MAX_FILES   64

/* O_CREAT: 老代码用的是 0x40 这个字面量，这里沿用同一个数值，不改语义。 */
#define FS_O_CREAT     0x40

/* stdio 保留槽位: Linux 里 stdin/stdout/stderr 在进程启动时就已打开，
 * open() 永远不会分配到 0/1/2。旧实现从 0 开始找空槽，于是进程的第一次
 * open 就拿到 fd=0，而 sys_read 把 fd==0 一律当 stdin 处理（阻塞等键盘）——
 * 「打开一个文件再 read 它」会永久卡在 hlt。分配起点由 fs_fd_alloc 决定,
 * 这里只是把这条约定写下来, 免得下次有人"顺手改成从 0 开始"。 */
#define FS_FD_STDIO_RESERVED  3

int64_t fs_do_open(const char *path, int flags, int mode)
{
    (void)mode;
    if (!path) return -1;

    /* 路径拷入属 syscall 层的边界动作, 实现留在 syscall64_full.c。
     * 用栈上缓冲区: 旧实现的静态缓冲会被并发 open 互相覆盖。 */
    char kpath[256];
    if (copy_user_path(kpath, sizeof(kpath), path) < 0) return -1;

    /* 找空槽 + 写字段整段在锁内（由 fs_fd_alloc 保证），
     * 所以两核同时 open 不会拿到同一个号。 */
    int fd = fs_fd_alloc(kpath, flags & 3);
    if (fd < 0) return -1;

    /* O_CREAT 的 ext2 调用放在锁外: 文件系统层自己串行化, 而把一次可能睡
     * 的 IO 塞进自旋锁里会拖长临界区。**这是原有行为, 不是这批改的** ——
     * 也就是说"open(O_CREAT) 先占 fd 再建文件"的顺序保留了: 建文件失败时
     * fd 仍然被占着。这一点在 fdstress 里测不到(它不建文件), 属于已知缺口。 */
    if (flags & FS_O_CREAT) {
        extern int ext2_64_create_file(const char *path);
        ext2_64_create_file(kpath);
    }
    return fd;
}

int64_t fs_do_close(int fd)
{
    if (fd < 0 || fd >= FS_MAX_FILES) return -1;
    /* used=0 在锁内, 避免与并发 read/write 的快照撞车 */
    return fs_fd_release(fd);
}
