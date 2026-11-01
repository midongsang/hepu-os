/* ============================================================================
 * 赫普OS - 64位自旋锁实现
 * 使用x86原子操作指令（xchg, lock cmpxchg）
 * ========================================================================== */

#include "spinlock.h"

/* 外部函数 */
extern void vga_puts(const char *str);
extern uint32_t apic64_get_id(void);

/* 初始化自旋锁 */
void spinlock_init(spinlock_t *lock)
{
    lock->lock = 0;
    lock->owner_cpu = 0xFFFFFFFF;  /* 无主 */
    lock->recursion_count = 0;
}

/* 原子交换指令：返回旧值并设置新值 */
static inline uint32_t atomic_xchg(volatile uint32_t *ptr, uint32_t val)
{
    uint32_t result;
    __asm__ volatile(
        "xchgl %0, %1"
        : "=r"(result), "+m"(*ptr)
        : "0"(val)
        : "memory"
    );
    return result;
}

/* 原子比较并交换 */
static inline uint32_t atomic_cmpxchg(volatile uint32_t *ptr, uint32_t expected, uint32_t new_val)
{
    uint32_t result;
    __asm__ volatile(
        "lock cmpxchgl %2, %1"
        : "=a"(result), "+m"(*ptr)
        : "r"(new_val), "0"(expected)
        : "memory"
    );
    return result;
}

/* 加锁（阻塞等待，支持递归） */
void spinlock_lock(spinlock_t *lock)
{
    uint32_t cpu_id = apic64_get_id();

    /* 递归加锁：同一CPU可以多次加锁 */
    if (lock->lock == 1 && lock->owner_cpu == cpu_id) {
        lock->recursion_count++;
        return;
    }

    /* 自旋等待锁释放 */
    while (atomic_xchg(&lock->lock, 1) == 1) {
        /* 自旋等待，PAUSE指令减少功耗 */
        __asm__ volatile("pause" ::: "memory");
    }

    /* 获取锁成功 */
    lock->owner_cpu = cpu_id;
    lock->recursion_count = 1;
}

/* 解锁 */
void spinlock_unlock(spinlock_t *lock)
{
    uint32_t cpu_id = apic64_get_id();

    /* 检查是否是锁的持有者 */
    if (lock->owner_cpu != cpu_id) {
        vga_puts("[SPINLOCK] ERROR: unlock by non-owner CPU\n");
        return;
    }

    /* 递归解锁 */
    lock->recursion_count--;
    if (lock->recursion_count > 0) {
        return;  /* 还有递归层，不释放锁 */
    }

    /* 释放锁 */
    lock->owner_cpu = 0xFFFFFFFF;
    lock->lock = 0;
}

/* 尝试加锁（非阻塞） */
int spinlock_trylock(spinlock_t *lock)
{
    uint32_t cpu_id = apic64_get_id();

    /* 递归加锁 */
    if (lock->lock == 1 && lock->owner_cpu == cpu_id) {
        lock->recursion_count++;
        return 1;  /* 成功 */
    }

    /* 尝试获取锁 */
    if (atomic_cmpxchg(&lock->lock, 0, 1) == 0) {
        lock->owner_cpu = cpu_id;
        lock->recursion_count = 1;
        return 1;  /* 成功 */
    }

    return 0;  /* 失败 */
}

/* 检查是否已锁定 */
int spinlock_is_locked(spinlock_t *lock)
{
    return lock->lock == 1;
}
