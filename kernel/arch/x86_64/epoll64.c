/* ============================================================================
 * HepuOS 64 位 —— epoll / eventfd2 / pselect6 最小实现（v1，仅 LT）
 * 头文件与限制说明见 epoll64.h。
 * ========================================================================== */
#include "epoll64.h"
#include "spinlock.h"
#include "fs_sys.h"

/* ---- 负 errno（与 syscall64_full.c 本地码表一致；本模块不另立一套） ---- */
#define HEPU_EPERM   (-1)
#define HEPU_EBADF   (-9)
#define HEPU_EAGAIN  (-11)
#define HEPU_ENOMEM  (-12)
#define HEPU_EFAULT  (-14)
#define HEPU_EEXIST  (-17)
#define HEPU_EINVAL  (-22)
#define HEPU_EMFILE  (-24)
#define HEPU_ENOSYS  (-38)

/* ---- 来自 syscall 层 / 其它子系统的外部接口 ---- */
extern int fs_fd_alloc(const char *kpath, int mode);
extern int fs_fd_release(int fd);
extern int fs_fd_snapshot(int fd, fs_fd_info_t *out);
extern int copy_from_user(void *kernel_dst, const void *user_src, size_t size);
extern int copy_to_user(void *user_dst, const void *kernel_src, size_t size);
extern int keyboard64_haschar(void);
extern uint64_t pit64_get_ticks(void);
extern void process64_yield(void);
/* 管道当前可读字节数（syscall64_full.c 提供，锁内取 head/tail）。 */
extern int syscall_pipe_bytes(int pipe_buf);

#define MAX_EPOLL_SETS    8
#define MAX_EVENTS_PER    16
#define MAX_EVENTFDS      8

typedef struct {
    int            used;
    int            tgt_fd;
    uint32_t       events;
    uint64_t       data;
} epoll_entry_t;

typedef struct {
    int            used;
    int            epfd;
    epoll_entry_t  ent[MAX_EVENTS_PER];
} epoll_set_t;

typedef struct {
    int            used;
    int            fd;
    uint64_t       count;
} eventfd_t;

static epoll_set_t g_epolls[MAX_EPOLL_SETS];
static eventfd_t   g_eventfds[MAX_EVENTFDS];
static spinlock_t  g_epoll_lock;

/* -------------------------------------------------------------------------- */
/* 内部查找                                                                     */
/* -------------------------------------------------------------------------- */

static epoll_set_t *find_epoll(int epfd)
{
    for (int i = 0; i < MAX_EPOLL_SETS; i++) {
        if (g_epolls[i].used && g_epolls[i].epfd == epfd) return &g_epolls[i];
    }
    return 0;
}

static eventfd_t *find_eventfd(int fd)
{
    for (int i = 0; i < MAX_EVENTFDS; i++) {
        if (g_eventfds[i].used && g_eventfds[i].fd == fd) return &g_eventfds[i];
    }
    return 0;
}

int epoll64_fd_kind(int fd)
{
    if (find_epoll(fd))   return EPOLL_FD_KIND_EPOLL;
    if (find_eventfd(fd)) return EPOLL_FD_KIND_EVENTFD;
    return EPOLL_FD_KIND_NORMAL;
}

/* -------------------------------------------------------------------------- */
/* 就绪判定（LT）。返回就绪事件掩码；0 = 暂不可读。                              */
/* -------------------------------------------------------------------------- */
static uint32_t fd_readable_events(int tgt_fd)
{
    /* eventfd：计数 > 0 即可读。 */
    eventfd_t *ev = find_eventfd(tgt_fd);
    if (ev) return ev->count > 0 ? EPOLLIN : 0;

    /* epoll fd 自身不参与就绪判定。 */
    if (find_epoll(tgt_fd)) return 0;

    /* stdin：键盘缓冲非空。 */
    if (tgt_fd == 0) return keyboard64_haschar() ? EPOLLIN : 0;

    fs_fd_info_t fi;
    if (fs_fd_snapshot(tgt_fd, &fi) != 0) return 0;

    if (fi.is_pipe) {
        return syscall_pipe_bytes(fi.pipe_buf) > 0 ? EPOLLIN : 0;
    }
    /* 普通文件：v1 恒视为可读（Linux 常规文件 read 不阻塞，EOF 仍返回 0）。 */
    return EPOLLIN;
}

/* -------------------------------------------------------------------------- */
/* epoll_create                                                                 */
/* -------------------------------------------------------------------------- */
int64_t epoll64_create(int flags)
{
    (void)flags;  /* EPOLL_CLOEXEC v1 不支持，忽略 */

    uint64_t f = spinlock_lock_irqsave(&g_epoll_lock);
    epoll_set_t *set = 0;
    for (int i = 0; i < MAX_EPOLL_SETS; i++) {
        if (!g_epolls[i].used) { set = &g_epolls[i]; break; }
    }
    if (!set) { spinlock_unlock_irqrestore(&g_epoll_lock, f); return HEPU_EMFILE; }

    int fd = fs_fd_alloc("/epoll/set", 2);
    if (fd < 0) { spinlock_unlock_irqrestore(&g_epoll_lock, f); return HEPU_EMFILE; }

    set->used  = 1;
    set->epfd  = fd;
    for (int i = 0; i < MAX_EVENTS_PER; i++) set->ent[i].used = 0;
    spinlock_unlock_irqrestore(&g_epoll_lock, f);
    return fd;
}

/* -------------------------------------------------------------------------- */
/* epoll_ctl                                                                    */
/* -------------------------------------------------------------------------- */
int64_t epoll64_ctl(int epfd, int op, int fd, const void *user_event)
{
    epoll_set_t *set = find_epoll(epfd);
    if (!set) return HEPU_EBADF;

    /* 不允许把 epoll fd 自己注册进另一个 epoll（v1 不处理环）。 */
    if (find_epoll(fd)) return HEPU_EINVAL;

    /* 目标 fd 必须在当前 fd 表内存在。 */
    fs_fd_info_t fi;
    if (fs_fd_snapshot(fd, &fi) != 0) return HEPU_EBADF;

    if (op == EPOLL_CTL_DEL) {
        uint64_t f = spinlock_lock_irqsave(&g_epoll_lock);
        for (int i = 0; i < MAX_EVENTS_PER; i++) {
            if (set->ent[i].used && set->ent[i].tgt_fd == fd) {
                set->ent[i].used = 0;
                break;
            }
        }
        spinlock_unlock_irqrestore(&g_epoll_lock, f);
        return 0;
    }

    /* ADD / MOD 需要读用户态 epoll_event。 */
    hepu_epoll_event_t ev;
    if (!user_event) return HEPU_EFAULT;
    if (copy_from_user(&ev, user_event, sizeof(ev)) != 0) return HEPU_EFAULT;

    /* v1 仅 LT：边沿触发直接拒绝。 */
    if (ev.events & EPOLLET) return HEPU_EINVAL;

    uint64_t f = spinlock_lock_irqsave(&g_epoll_lock);

    int slot = -1;
    for (int i = 0; i < MAX_EVENTS_PER; i++) {
        if (set->ent[i].used && set->ent[i].tgt_fd == fd) { slot = i; break; }
    }

    if (op == EPOLL_CTL_ADD) {
        if (slot >= 0) { spinlock_unlock_irqrestore(&g_epoll_lock, f); return HEPU_EEXIST; }
        for (int i = 0; i < MAX_EVENTS_PER; i++) {
            if (!set->ent[i].used) { slot = i; break; }
        }
        if (slot < 0) { spinlock_unlock_irqrestore(&g_epoll_lock, f); return HEPU_ENOMEM; }
    } else if (op == EPOLL_CTL_MOD) {
        if (slot < 0) { spinlock_unlock_irqrestore(&g_epoll_lock, f); return HEPU_EBADF; }
    } else {
        spinlock_unlock_irqrestore(&g_epoll_lock, f);
        return HEPU_EINVAL;
    }

    set->ent[slot].used    = 1;
    set->ent[slot].tgt_fd  = fd;
    set->ent[slot].events  = ev.events;
    set->ent[slot].data    = ev.data;
    spinlock_unlock_irqrestore(&g_epoll_lock, f);
    return 0;
}

/* -------------------------------------------------------------------------- */
/* epoll_wait                                                                   */
/* -------------------------------------------------------------------------- */
int64_t epoll64_wait(int epfd, void *user_events, int maxevents, int timeout_ms)
{
    epoll_set_t *set = find_epoll(epfd);
    if (!set) return HEPU_EBADF;
    if (maxevents <= 0 || !user_events) return HEPU_EINVAL;

    int infinite = (timeout_ms < 0);
    uint64_t deadline = 0;
    if (!infinite) deadline = pit64_get_ticks() + (uint64_t)timeout_ms;

    /* syscall 入口经 FMASK 屏蔽了 IF（进入时 RFLAGS.IF=0），此时 APIC/PIT
     * 定时器中断送不进来，pit64_get_ticks() 冻结、超时永远到不了。本等待
     * 循环在锁外轮询 + 让出，重新开中断让 tick 推进；所有跨子系统取数都在
     * irqsave 锁内完成，锁释放后才允许中断抢占。返回用户态时 RFLAGS 由
     * syscall 返回帧恢复，无需在此 cli。 */
    __asm__ volatile("sti");

    hepu_epoll_event_t local[MAX_EVENTS_PER];

    for (;;) {
        /* 锁内取一份条目快照，就绪判定在锁外做（避免跨子系统持锁）。 */
        epoll_entry_t snap[MAX_EVENTS_PER];
        int nent = 0;
        uint64_t f = spinlock_lock_irqsave(&g_epoll_lock);
        for (int i = 0; i < MAX_EVENTS_PER; i++) {
            if (set->ent[i].used) snap[nent++] = set->ent[i];
        }
        spinlock_unlock_irqrestore(&g_epoll_lock, f);

        int nready = 0;
        for (int i = 0; i < nent && nready < maxevents && nready < MAX_EVENTS_PER; i++) {
            uint32_t rev = fd_readable_events(snap[i].tgt_fd);
            if (rev) {
                local[nready].events = rev;
                local[nready]._pad   = 0;
                local[nready].data   = snap[i].data;
                nready++;
            }
        }

        if (nready > 0) {
            if (copy_to_user(user_events, local,
                             (size_t)nready * sizeof(hepu_epoll_event_t)) != 0)
                return HEPU_EFAULT;
            return nready;
        }

        if (timeout_ms == 0) return 0;
        if (!infinite && pit64_get_ticks() >= deadline) return 0;

        /* 让出 CPU，由调度器轮转；不关中断忙等。 */
        process64_yield();
    }
}

/* -------------------------------------------------------------------------- */
/* eventfd2                                                                     */
/* -------------------------------------------------------------------------- */
int64_t eventfd64_create(int initval, int flags)
{
    if (flags & EFD_SEMAPHORE) return HEPU_EINVAL;  /* 信号量模式 v1 不支持 */

    uint64_t f = spinlock_lock_irqsave(&g_epoll_lock);
    eventfd_t *ev = 0;
    for (int i = 0; i < MAX_EVENTFDS; i++) {
        if (!g_eventfds[i].used) { ev = &g_eventfds[i]; break; }
    }
    if (!ev) { spinlock_unlock_irqrestore(&g_epoll_lock, f); return HEPU_EMFILE; }

    int fd = fs_fd_alloc("/eventfd", 2);
    if (fd < 0) { spinlock_unlock_irqrestore(&g_epoll_lock, f); return HEPU_EMFILE; }

    ev->used  = 1;
    ev->fd    = fd;
    ev->count = (uint64_t)initval;
    spinlock_unlock_irqrestore(&g_epoll_lock, f);
    return fd;
}

int64_t eventfd64_read(int fd, char *ubuf, int count)
{
    eventfd_t *ev = find_eventfd(fd);
    if (!ev) return HEPU_EBADF;
    if (count < 8) return HEPU_EINVAL;

    uint64_t val;
    uint64_t f = spinlock_lock_irqsave(&g_epoll_lock);
    if (ev->count == 0) {
        spinlock_unlock_irqrestore(&g_epoll_lock, f);
        return HEPU_EAGAIN;  /* v1 不阻塞读空 eventfd */
    }
    val = ev->count;
    ev->count = 0;           /* Linux 非信号量语义：取走并清零 */
    spinlock_unlock_irqrestore(&g_epoll_lock, f);

    if (copy_to_user(ubuf, &val, 8) != 0) return HEPU_EFAULT;
    return 8;
}

int64_t eventfd64_write(int fd, const char *ubuf, int count)
{
    eventfd_t *ev = find_eventfd(fd);
    if (!ev) return HEPU_EBADF;
    if (count < 8) return HEPU_EINVAL;

    uint64_t val;
    if (copy_from_user(&val, ubuf, 8) != 0) return HEPU_EFAULT;

    uint64_t f = spinlock_lock_irqsave(&g_epoll_lock);
    /* 饱和到 UINT64_MAX，避免回绕。 */
    if (ev->count > UINT64_MAX - val) ev->count = UINT64_MAX;
    else ev->count += val;
    spinlock_unlock_irqrestore(&g_epoll_lock, f);
    return 8;
}

/* -------------------------------------------------------------------------- */
/* close 回收                                                                    */
/* -------------------------------------------------------------------------- */
void epoll64_on_close(int fd)
{
    uint64_t f = spinlock_lock_irqsave(&g_epoll_lock);

    eventfd_t *ev = find_eventfd(fd);
    if (ev) { ev->used = 0; ev->count = 0; }

    epoll_set_t *set = find_epoll(fd);
    if (set) {
        set->used = 0;
        for (int i = 0; i < MAX_EVENTS_PER; i++) set->ent[i].used = 0;
    }

    /* 从其它 epoll 集合里摘掉对该 fd 的引用。 */
    for (int i = 0; i < MAX_EPOLL_SETS; i++) {
        if (!g_epolls[i].used) continue;
        for (int j = 0; j < MAX_EVENTS_PER; j++) {
            if (g_epolls[i].ent[j].used && g_epolls[i].ent[j].tgt_fd == fd)
                g_epolls[i].ent[j].used = 0;
        }
    }
    spinlock_unlock_irqrestore(&g_epoll_lock, f);
}

/* -------------------------------------------------------------------------- */
/* pselect6 最小实现                                                            */
/* -------------------------------------------------------------------------- */
int64_t pselect64_impl(int nfds, uint64_t readfds, uint64_t writefds,
                       uint64_t exceptfds, uint64_t timeout, uint64_t sigmask)
{
    (void)writefds; (void)exceptfds; (void)sigmask;  /* v1 忽略 */

    if (nfds < 0) return HEPU_EINVAL;
    if (nfds > 16) nfds = 16;  /* MAX_FILES 上限 */
    if (!readfds) return HEPU_EINVAL;

    uint64_t rw = 0;
    if (copy_from_user(&rw, (const void *)readfds, 8) != 0) return HEPU_EFAULT;

    int infinite = (timeout == 0);  /* NULL 超时 = 无限等待 */
    uint64_t deadline = 0;
    if (!infinite) {
        int64_t tv_sec = 0;
        int64_t tv_nsec = 0;
        if (copy_from_user(&tv_sec, (const void *)timeout, 8) != 0) return HEPU_EFAULT;
        if (copy_from_user(&tv_nsec, (const void *)((const char *)timeout + 8), 8) != 0)
            return HEPU_EFAULT;
        int64_t ms = tv_sec * 1000 + tv_nsec / 1000000;
        if (ms < 0) ms = 0;
        deadline = pit64_get_ticks() + (uint64_t)ms;
    }

    /* 同 epoll_wait：syscall 入口 IF=0，定时等待前重新开中断让 tick 推进。 */
    __asm__ volatile("sti");

    for (;;) {
        uint64_t ready = 0;
        int nready = 0;
        for (int fd = 0; fd < nfds; fd++) {
            if (!(rw & (1UL << fd))) continue;
            if (fd_readable_events(fd)) { ready |= (1UL << fd); nready++; }
        }

        if (nready > 0) {
            if (copy_to_user((void *)readfds, &ready, 8) != 0) return HEPU_EFAULT;
            return nready;
        }

        if (!infinite && pit64_get_ticks() >= deadline) {
            uint64_t zero = 0;
            copy_to_user((void *)readfds, &zero, 8);
            return 0;
        }
        process64_yield();
    }
}
