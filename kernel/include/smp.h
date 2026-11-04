#ifndef SMP_H
#define SMP_H

#include <stdint.h>

#define MAX_CPUS 8

/* CPU状态 */
typedef struct {
    uint32_t cpu_id;
    int online;
    int current_pid;
    uint32_t kernel_stack;
    uint32_t user_stack;
    uint32_t lapic_id;
    volatile int ipi_pending;
    uint32_t ipi_msg;
} smp_cpu_t;

/* 初始化SMP */
void smp_init(void);

/* 启动应用处理器(AP) */
int smp_start_ap(uint32_t apic_id);

/* 获取当前CPU ID */
uint32_t smp_cpu_id(void);

/* 获取在线CPU数量 */
int smp_cpu_count(void);

/* V3.96-E42: 在线 CPU 位图(bit i = CPU i 已上线; BSP 模拟 = 0x1) */
uint32_t smp_online_cpus(void);

/* 获取CPU状态 */
smp_cpu_t *smp_get_cpu_state(uint32_t cpu_id);

/* 发送处理器间中断(IPI) */
int smp_send_ipi(uint32_t target_cpu, uint32_t msg);

/* 处理IPI */
void smp_handle_ipi(void);

/* 多核调度: 选择下一个CPU运行进程 */
int smp_schedule(int pid);

/* 原子操作 */
uint32_t smp_atomic_inc(volatile uint32_t *val);
uint32_t smp_atomic_dec(volatile uint32_t *val);
int smp_atomic_cas(volatile uint32_t *val, uint32_t old, uint32_t new);

/* 自旋锁
 * V3.94-E40: 增加持锁监控字段(hold_tick/holder_pc)——死锁检测用.
 * 结构 4B→12B, SPINLOCK_INIT {0} 兼容(多余字段置 0). */
typedef struct {
    volatile uint32_t lock;
    uint32_t hold_tick;   /* 获取时的系统 tick (E-40) */
    uint32_t holder_pc;   /* 获取者调用地址 (E-40) */
} spinlock_t;

#define SPINLOCK_INIT {0, 0, 0}

void spin_lock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);
int spin_trylock(spinlock_t *lock);

/* V3.87-E3: 中断安全自旋锁(保存/恢复 EFLAGS.IF) */
void spin_lock_irqsave(spinlock_t *lock, uint32_t *flags);
void spin_unlock_irqrestore(spinlock_t *lock, uint32_t flags);

/* V3.87-E3: 带超时自旋锁(死锁检测)——超时返回 -1, 成功 0 */
int spin_lock_timeout(spinlock_t *lock, uint32_t max_spins);

/* V3.87-E3: 锁争用统计(供 perf) */
void spin_stats(uint32_t *waits, uint32_t *timeouts);

/* V3.94-E40: 死锁检测——注册被监控锁 + 周期扫描(持锁超时告警) */
int  lock_monitor_register(spinlock_t *lock, const char *name);
__attribute__((noinline)) void lock_monitor_scan(void);
void lock_monitor_stats(uint32_t *registered, uint32_t *held_now,
                        uint32_t *total_warns);
/* 供 shell locktest 模拟死锁场景 */
spinlock_t *lock_test_get(void);
void  lock_test_release(void);

#endif /* SMP_H */
