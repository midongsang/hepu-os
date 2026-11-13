/* ============================================================================
 * 赫普OS - 64 位原子操作与内存屏障
 * V0.58.5 / SB-01: 统一内核里的原子原语，收敛散落的编译器内建
 *
 * 为什么要有这个头文件:
 *   此前原子操作有三种来源 —— spinlock.c 里两个文件私有的 static inline、
 *   kheap64.c 的 __atomic_exchange_n、klog64.c 的 __sync_lock_test_and_set。
 *   同一件事三套写法，屏障语义还不一致（有的带 memory clobber，有的靠
 *   __ATOMIC_ACQUIRE）。多核下这一点很容易变成"看起来锁住了其实没同步"。
 *
 * 设计约定:
 *   1. 所有"读-改-写"都用带 lock 前缀的指令，天然是完整屏障，不需要额外 fence;
 *   2. load/store 用普通 movq —— x86 对 8 字节对齐的访问本身就是原子的，
 *      但要在需要跨核可见的地方配合 mb()/barrier_compiler();
 *   3. 一律用裸指针接口（atomic_u64 只是 volatile 的别名），这样现有
 *      volatile uint64_t 变量不用改变类型就能直接用。
 * ========================================================================== */

#ifndef ATOMIC64_H
#define ATOMIC64_H

#include <stdint.h>

/* 类型别名: 让调用方可以写 atomic_u64 g_ticks; 也可以直接传 volatile 变量 */
typedef volatile uint32_t atomic_u32;
typedef volatile uint64_t atomic_u64;

/* ---------------------------------------------------------------------------
 * 内存屏障
 * ------------------------------------------------------------------------- */

/* 全屏障: 之前的所有读写都在之后的所有读写之前完成 */
static inline void mb(void)
{
    __asm__ volatile("mfence" ::: "memory");
}

/* 读屏障 */
static inline void rmb(void)
{
    __asm__ volatile("lfence" ::: "memory");
}

/* 写屏障 */
static inline void wmb(void)
{
    __asm__ volatile("sfence" ::: "memory");
}

/* 编译器屏障: 只阻止编译器重排，不产生指令 */
static inline void barrier_compiler(void)
{
    __asm__ volatile("" ::: "memory");
}

/* ---------------------------------------------------------------------------
 * 32 位
 * ------------------------------------------------------------------------- */

static inline uint32_t atomic_load32(volatile uint32_t *p)
{
    uint32_t v = *p;
    barrier_compiler();
    return v;
}

static inline void atomic_store32(volatile uint32_t *p, uint32_t v)
{
    barrier_compiler();
    *p = v;
}

/* 交换并返回旧值 */
static inline uint32_t atomic_xchg32(volatile uint32_t *p, uint32_t val)
{
    uint32_t result;
    __asm__ volatile(
        "xchgl %0, %1"
        : "=r"(result), "+m"(*p)
        : "0"(val)
        : "memory");
    return result;
}

/* CAS: 相等则写入 new_val，返回旧值（调用方与 expected 比较判断成功） */
static inline uint32_t atomic_cmpxchg32(volatile uint32_t *p, uint32_t expected, uint32_t new_val)
{
    uint32_t result;
    __asm__ volatile(
        "lock cmpxchgl %2, %1"
        : "=a"(result), "+m"(*p)
        : "r"(new_val), "0"(expected)
        : "memory");
    return result;
}

static inline void atomic_inc32(volatile uint32_t *p)
{
    __asm__ volatile("lock incl %0" : "+m"(*p) : : "memory");
}

static inline void atomic_dec32(volatile uint32_t *p)
{
    __asm__ volatile("lock decl %0" : "+m"(*p) : : "memory");
}

static inline void atomic_add32(volatile uint32_t *p, uint32_t delta)
{
    __asm__ volatile("lock addl %1, %0" : "+m"(*p) : "r"(delta) : "memory");
}

/* 加法并返回旧值（xadd 语义） */
static inline uint32_t atomic_xadd32(volatile uint32_t *p, uint32_t delta)
{
    __asm__ volatile("lock xaddl %1, %0" : "+m"(*p), "+r"(delta) : : "memory");
    return delta;   /* xadd 把原值留在源操作数里 */
}

/* ---------------------------------------------------------------------------
 * 64 位
 * ------------------------------------------------------------------------- */

static inline uint64_t atomic_load64(volatile uint64_t *p)
{
    uint64_t v = *p;
    barrier_compiler();
    return v;
}

static inline void atomic_store64(volatile uint64_t *p, uint64_t v)
{
    barrier_compiler();
    *p = v;
}

static inline uint64_t atomic_xchg64(volatile uint64_t *p, uint64_t val)
{
    uint64_t result;
    __asm__ volatile(
        "xchgq %0, %1"
        : "=r"(result), "+m"(*p)
        : "0"(val)
        : "memory");
    return result;
}

static inline uint64_t atomic_cmpxchg64(volatile uint64_t *p, uint64_t expected, uint64_t new_val)
{
    uint64_t result;
    __asm__ volatile(
        "lock cmpxchgq %2, %1"
        : "=a"(result), "+m"(*p)
        : "r"(new_val), "0"(expected)
        : "memory");
    return result;
}

static inline void atomic_inc64(volatile uint64_t *p)
{
    __asm__ volatile("lock incq %0" : "+m"(*p) : : "memory");
}

static inline void atomic_dec64(volatile uint64_t *p)
{
    __asm__ volatile("lock decq %0" : "+m"(*p) : : "memory");
}

static inline void atomic_add64(volatile uint64_t *p, uint64_t delta)
{
    __asm__ volatile("lock addq %1, %0" : "+m"(*p) : "r"(delta) : "memory");
}

static inline void atomic_sub64(volatile uint64_t *p, uint64_t delta)
{
    __asm__ volatile("lock subq %1, %0" : "+m"(*p) : "r"(delta) : "memory");
}

static inline uint64_t atomic_xadd64(volatile uint64_t *p, uint64_t delta)
{
    __asm__ volatile("lock xaddq %1, %0" : "+m"(*p), "+r"(delta) : : "memory");
    return delta;
}

#endif /* ATOMIC64_H */
