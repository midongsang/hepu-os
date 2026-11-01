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

#endif /* SPINLOCK_H */
