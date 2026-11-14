/* ============================================================================
 * 赫普OS - 64位自旋锁实现
 * 使用x86原子操作指令（xchg, lock cmpxchg）
 * ========================================================================== */

#include "spinlock.h"
#include "atomic64.h"

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
    while (atomic_xchg32(&lock->lock, 1) == 1) {
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
        /* SB-26 诊断: 打印锁地址 + 记录在案的 owner + 当前 cpu,
         * 这样能直接对上 nm 里的符号, 知道是哪把锁被谁放了。
         * 用 nolock 版本输出 —— 出问题的锁可能正是打印锁本身。 */
        extern void vga_puts_nolock(const char *);
        char b[96]; int n = 0;
        const char *p1 = "[SPINLOCK] ERR addr=0x";
        while (*p1) b[n++] = *p1++;
        uint64_t a = (uint64_t)lock;
        for (int sh = 60; sh >= 0; sh -= 4) b[n++] = (char)(((a >> sh) & 0xF) < 10 ? '0' + ((a >> sh) & 0xF) : 'a' + ((a >> sh) & 0xF) - 10);
        const char *p2 = " owner=0x";
        while (*p2) b[n++] = *p2++;
        uint32_t o = lock->owner_cpu;
        if (o == 0xFFFFFFFFu) { b[n++] = '-'; b[n++] = '1'; }
        else { b[n++] = (char)(o < 10 ? '0' + o : 'a' + o - 10); }
        const char *p3 = " cur=0x";
        while (*p3) b[n++] = *p3++;
        b[n++] = (char)(cpu_id < 10 ? '0' + cpu_id : 'a' + cpu_id - 10);
        const char *p4 = " rec=";
        while (*p4) b[n++] = *p4++;
        { uint32_t r = lock->recursion_count; b[n++] = (char)(r < 10 ? '0' + r : 'a' + r - 10); }
        const char *p5 = " at=0x";
        while (*p5) b[n++] = *p5++;
        uint64_t ra = (uint64_t)__builtin_return_address(0);
        for (int sh = 60; sh >= 0; sh -= 4) b[n++] = (char)(((ra >> sh) & 0xF) < 10 ? '0' + ((ra >> sh) & 0xF) : 'a' + ((ra >> sh) & 0xF) - 10);
        b[n++] = '\n'; b[n] = 0;
        vga_puts_nolock(b);
        /* 止血: 即使 owner 不匹配也强制释放, 否则这把锁永久卡住,
         * 后续任何 CPU 进来都是死自旋(表现为桌面再也起不来)。 */
        lock->owner_cpu = 0xFFFFFFFFu;
        lock->recursion_count = 0;
        lock->lock = 0;
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
    if (atomic_cmpxchg32(&lock->lock, 0, 1) == 0) {
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
