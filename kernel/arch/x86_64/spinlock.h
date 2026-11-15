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

/* -------------------------------------------------------------------------
 * SB-26 诊断：最后一次加锁点，用**影子变量**而不是结构字段。
 *
 * 为什么不是字段：给 spinlock_t 加 8 字节会让它 12 -> 24，整个内核的
 * .bss/.data 布局跟着挪，而仓库里存在依赖固定布局/时序的路径 —— 实测
 * 加字段后 `-smp 12` 从 3/3 掉到 0/3（回退即恢复）。诊断手段不能把系统
 * 弄坏，所以改成两个全局变量：结构大小不变，只多 16 字节 .bss。
 *
 * 语义：记录"最近一次成功加锁"的锁地址 + 返回地址。unlock 发现 owner
 * 不匹配时，若记录的地址正是这把锁，这条 lock_at 就是那次没被放掉的加锁点。
 * ------------------------------------------------------------------------- */
extern volatile uint64_t g_sl_last_lock_addr;
extern volatile uint64_t g_sl_last_lock_site;

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
