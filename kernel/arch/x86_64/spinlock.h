/* ============================================================================
 * 赫普OS - 64位自旋锁（多处理器支持基础）
 * V0.10.136: SMP支持第一步，自旋锁实现
 * ========================================================================== */

#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <stdint.h>

/* 自旋锁结构 */
typedef struct {
    volatile uint32_t lock;  /* 0=未锁定, 1=已锁定 */
    uint32_t owner_cpu;      /* 持有锁的CPU ID */
    uint32_t recursion_count; /* 递归计数（支持同一CPU递归加锁） */
} spinlock_t;

/* 初始化自旋锁 */
void spinlock_init(spinlock_t *lock);

/* 加锁（阻塞等待） */
void spinlock_lock(spinlock_t *lock);

/* 解锁 */
void spinlock_unlock(spinlock_t *lock);

/* 尝试加锁（非阻塞） */
int spinlock_trylock(spinlock_t *lock);

/* 检查是否已锁定 */
int spinlock_is_locked(spinlock_t *lock);

/* -------------------------------------------------------------------------
 * 关中断版加锁（V0.58.5）
 *
 * 为什么需要这一对: spinlock_lock() 本身不关中断。如果一个临界区会被本核
 * 中断上下文重入(时钟中断里也去拿同一把锁), 就必须先 cli 再拿锁, 否则
 * 自己会等自己 -> 自死锁。返回的 flags 用于解锁时按原状态恢复 IF。
 *
 * 注意: 这对函数**不支持嵌套**。spinlock_unlock() 在递归层只做
 * recursion_count-- 而不真正释放锁, 此时 irqrestore 若照 flags 开中断,
 * 就会出现"锁还拿着但中断已开"。嵌套场景请改用外层加锁 + 内部无锁版。
 * ------------------------------------------------------------------------- */
static inline uint64_t spinlock_lock_irqsave(spinlock_t *lock)
{
    uint64_t flags;
    __asm__ volatile("pushfq\n\tpopq %0" : "=r"(flags) : : "memory");
    __asm__ volatile("cli" ::: "memory");
    spinlock_lock(lock);
    return flags;
}

static inline void spinlock_unlock_irqrestore(spinlock_t *lock, uint64_t flags)
{
    spinlock_unlock(lock);
    if (flags & (1ULL << 9)) {   /* 原来是开着的才开回去 */
        __asm__ volatile("sti" ::: "memory");
    }
}

#endif /* SPINLOCK_H */
