/* ============================================================================
 * HepuOS 64 位 —— epoll / eventfd2 / pselect6 最小实现（v1，仅水平触发 LT）
 *
 * 范围与已知限制（与 docs/specs/SYSCALL_ABI.md 对齐）：
 *   - epoll_create / epoll_ctl(ADD/MOD/DEL) / epoll_wait；
 *   - eventfd2 真计数器：write 累加、read 取走并清零（Linux 非信号量语义）；
 *     读空返回 -EAGAIN（v1 不阻塞读空）；
 *   - pselect6 最小语义：nfds 内读就绪位图 + 超时；write/except 位图与
 *     sigmask v1 忽略；
 *   - 就绪判定按 fd 类型自身：eventfd 计数>0、pipe 有数据、普通文件恒可读、
 *     stdin(fd 0) 有按键；EPOLLOUT v1 未分类；
 *   - 等待循环：pit64 毫秒时基算 deadline + process64_yield() 让出。
 *     syscall 入口经 FMASK 屏蔽 IF（进入时 RFLAGS.IF=0，tick 冻结），等待前
 *     显式 sti 让定时器中断送达；跨子系统取数都在 irqsave 锁内完成，锁释放
 *     后才允许抢占；返回用户态时 RFLAGS 由 syscall 返回帧恢复。
 *   - **EPOLLET 边沿触发不支持**：注册时遇 EPOLLET 返回 -EINVAL；
 *   - **EFD_SEMAPHORE 信号量模式不支持**：eventfd2 带该标志返回 -EINVAL；
 *   - 错误返回负 errno，码表与 syscall64_full.c 本地 HEPU_* 一致。
 *
 * 边界：本模块只持有「实例表 / 注册表 / 计数器」；fd 号仍由 fs_fd_alloc 分配，
 * fd 表本身留在 syscall 层，不外露。注册表未按进程槽位隔离（v1 接受，靠
 * fd 快照防止跨槽泄漏）。
 * ========================================================================== */
#ifndef HEPU_EPOLL64_H
#define HEPU_EPOLL64_H

#include <stdint.h>
#include <stddef.h>

/* ---- Linux 兼容常量（v1 子集） ---- */
#define EPOLLIN        0x00000001u
#define EPOLLOUT       0x00000004u
#define EPOLLET        0x80000000u

#define EPOLL_CTL_ADD  1
#define EPOLL_CTL_MOD  3
#define EPOLL_CTL_DEL  2

#define EFD_SEMAPHORE  0x00000001u

/* 用户态 struct epoll_event（Linux x86_64 布局，16 字节）：
 *   uint32_t events; uint32_t _pad; uint64_t data; */
typedef struct {
    uint32_t events;
    uint32_t _pad;
    uint64_t data;
} hepu_epoll_event_t;

/* fd 类别（供 sys_read/sys_write/sys_close 分流） */
#define EPOLL_FD_KIND_NORMAL   0
#define EPOLL_FD_KIND_EPOLL    1
#define EPOLL_FD_KIND_EVENTFD  2

/* ---- 实现入口（syscall64_full.c 的 wrap 层调用） ---- */

/* epoll_create(size) / epoll_create1(flags)：返回合法 fd（>=3），失败负 errno。 */
int64_t epoll64_create(int flags);

/* epoll_ctl(epfd, op, fd, user_event)：ADD/MOD/DEL；user_event 为用户态
 * struct epoll_event 指针（DEL 时可空）。成功 0，失败负 errno。 */
int64_t epoll64_ctl(int epfd, int op, int fd, const void *user_event);

/* epoll_wait(epfd, user_events, maxevents, timeout_ms)：
 *   timeout < 0  无限等待；== 0 立即返回；> 0 毫秒超时。
 *   就绪 >0 填 events 并返回数量；超时返回 0；错误返回负 errno。 */
int64_t epoll64_wait(int epfd, void *user_events, int maxevents, int timeout_ms);

/* eventfd2(initval, flags)：返回合法 fd；EFD_SEMAPHORE 不支持 -> -EINVAL。 */
int64_t eventfd64_create(int initval, int flags);

/* eventfd 读写（由 sys_read/sys_write 分流进来）。 */
int64_t eventfd64_read(int fd, char *ubuf, int count);
int64_t eventfd64_write(int fd, const char *ubuf, int count);

/* 分流查询：返回该 fd 的类别；未登记返回 EPOLL_FD_KIND_NORMAL。 */
int epoll64_fd_kind(int fd);

/* close 时回收注册表条目（由 wrap_close 调用）。 */
void epoll64_on_close(int fd);

/* pselect6 最小实现：
 *   nfds / readfds(用户位图) / writefds / exceptfds / timeout(timespec*，可空) /
 *   sigmask(忽略)。返回就绪读 fd 数；超时 0；错误负 errno。
 *   v1 只判定读就绪并回写 readfds 位图；write/except 位图忽略。 */
int64_t pselect64_impl(int nfds, uint64_t readfds, uint64_t writefds,
                       uint64_t exceptfds, uint64_t timeout, uint64_t sigmask);

#endif /* HEPU_EPOLL64_H */
