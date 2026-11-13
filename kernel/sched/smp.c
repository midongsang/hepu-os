/* ============================================================================
 * 赫普OS - 多核 SMP 框架（遗留，未进入 64 位镜像）
 *
 * 【SB-24 归属标注】本文件与 kernel/include/smp.h 是早期 SMP 框架的遗留实现，
 * 不在 kernel/arch/x86_64/Makefile 的构建列表里，当前 64 位镜像完全不使用它。
 *
 * 危险点：本文件与 arch 版存在「同名不同义」——
 *   - 本文件：struct smp_cpu_t / cpu_count / MAX_CPUS = 8
 *   - arch 版：cpu_info_t / cpu_count / MAX_CPUS = 16
 * 如果后续误把本文件编进构建，两套 MAX_CPUS 会让核 ID 8..15 静默越界。
 *
 * 接口以 kernel/arch/x86_64/smp.h 与 kernel/arch/x86_64/spinlock.h 为准。
 * 本文件的删除/重构留待独立立项（已登记 SB-24），此处仅做归属标注，
 * 不改代码、不改行为。
 * ========================================================================== */

#include <stdint.h>
#include "smp.h"
#include "vga.h"
#include "string.h"

static smp_cpu_t cpus[MAX_CPUS];
static int cpu_count = 1;
static spinlock_t __attribute__((unused)) smp_lock = SPINLOCK_INIT;

void smp_init(void)
{
    for (int i = 0; i < MAX_CPUS; i++) {
        memset(&cpus[i], 0, sizeof(smp_cpu_t));
        cpus[i].cpu_id = i;
        cpus[i].lapic_id = i;
    }
    cpus[0].online = 1;
    cpus[0].current_pid = 0;
    cpu_count = 1;

    vga_puts("[SMP] SMP init done \n");
    vga_printf(" BSP(CPU0) online, max support %d cores \n", MAX_CPUS);
}

int smp_start_ap(uint32_t apic_id)
{
    if (apic_id == 0 || apic_id >= MAX_CPUS) return -1;
    if (cpus[apic_id].online) return -1;

    /* 原型阶段: 模拟AP启动
     * 实际硬件: 发送INIT-SIPI-SIPI序列到LAPIC
     */
    cpus[apic_id].online = 1;
    cpus[apic_id].current_pid = -1;
    cpu_count++;

    vga_printf("[SMP] CPU%d(APIC %d) start ok \n", apic_id, apic_id);
    return 0;
}

uint32_t smp_cpu_id(void)
{
    /* 原型阶段: 始终返回0
     * 实际硬件: 读取LAPIC ID寄存器
     */
    return 0;
}

int smp_cpu_count(void)
{
    return cpu_count;
}

uint32_t smp_online_cpus(void)
{
    uint32_t mask = 0;
    for (int i = 0; i < MAX_CPUS; i++)
        if (cpus[i].online) mask |= (1u << i);
    return mask;
}

smp_cpu_t *smp_get_cpu_state(uint32_t cpu_id)
{
    if (cpu_id >= MAX_CPUS) return NULL;
    return &cpus[cpu_id];
}

int smp_send_ipi(uint32_t target_cpu, uint32_t msg)
{
    if (target_cpu >= MAX_CPUS || !cpus[target_cpu].online) return -1;
    cpus[target_cpu].ipi_pending = 1;
    cpus[target_cpu].ipi_msg = msg;
    return 0;
}

void smp_handle_ipi(void)
{
    uint32_t cpu = smp_cpu_id();
    if (cpus[cpu].ipi_pending) {
        uint32_t msg = cpus[cpu].ipi_msg;
        cpus[cpu].ipi_pending = 0;
        vga_printf("[SMP] CPU%d recv IPI: 0x%x\n", cpu, msg);
    }
}

int smp_schedule(int pid)
{
    /* 简单负载均衡: 选择当前进程最少的CPU */
    int best_cpu = 0;
    int min_load = 0x7FFFFFFF;
    for (int i = 0; i < MAX_CPUS; i++) {
        if (cpus[i].online) {
            int load = (cpus[i].current_pid >= 0) ? 1 : 0;
            if (load < min_load) {
                min_load = load;
                best_cpu = i;
            }
        }
    }
    cpus[best_cpu].current_pid = pid;
    return best_cpu;
}

/* ---- 原子操作 ---- */

uint32_t smp_atomic_inc(volatile uint32_t *val)
{
    return __sync_add_and_fetch(val, 1);
}

uint32_t smp_atomic_dec(volatile uint32_t *val)
{
    return __sync_sub_and_fetch(val, 1);
}

int smp_atomic_cas(volatile uint32_t *val, uint32_t old, uint32_t new)
{
    return __sync_bool_compare_and_swap(val, old, new);
}

/* ---- 自旋锁 ---- */

/* V3.94-E40: 锁争用/超时统计前向声明(spin_lock 使用点在其后) */
static uint32_t spin_waits = 0;
static uint32_t spin_timeouts = 0;

/* V3.94-E40: 获取调用者返回地址(栈顶) */
static inline uint32_t lock_caller_pc(void)
{
    uint32_t pc;
    __asm__ volatile ("movl (%%esp), %0" : "=r"(pc));
    return pc;
}

void spin_lock(spinlock_t *lock)
{
    while (!smp_atomic_cas(&lock->lock, 0, 1)) {
        spin_waits++;
        __asm__ volatile ("pause");
    }
    /* E-40: 记录持锁起点与持有者(死锁检测用; 中断上下文安全-仅读全局tick) */
    extern uint32_t sched_get_ticks(void);
    lock->hold_tick = sched_get_ticks();
    lock->holder_pc = lock_caller_pc();
}

void spin_unlock(spinlock_t *lock)
{
    __asm__ volatile ("" ::: "memory");
    lock->hold_tick = 0;
    lock->holder_pc = 0;
    lock->lock = 0;
}

int spin_trylock(spinlock_t *lock)
{
    return smp_atomic_cas(&lock->lock, 0, 1);
}

/* ---- V3.87-E3: 中断安全锁 + 超时检测 + 统计 ---- */


void spin_stats(uint32_t *waits, uint32_t *timeouts)
{
    if (waits)     *waits = spin_waits;
    if (timeouts)  *timeouts = spin_timeouts;
}

/* 关中断并保存 EFLAGS (x86: pushfl/popfl) */
static inline uint32_t eflags_save(void)
{
    uint32_t f;
    __asm__ volatile ("pushfl; popl %0" : "=r"(f) :: "memory");
    return f;
}

static inline void eflags_restore(uint32_t f)
{
    __asm__ volatile ("pushl %0; popfl" :: "r"(f) : "memory");
}

void spin_lock_irqsave(spinlock_t *lock, uint32_t *flags)
{
    if (!flags) { spin_lock(lock); return; }
    *flags = eflags_save();          /* 先保存(含当前IF) */
    __asm__ volatile ("cli");        /* 关中断 */
    if (!smp_atomic_cas(&lock->lock, 0, 1)) {
        spin_waits++;
        while (!smp_atomic_cas(&lock->lock, 0, 1)) {
            __asm__ volatile ("pause");
        }
    }
    /* E-40: 记录持锁监控字段 */
    extern uint32_t sched_get_ticks(void);
    lock->hold_tick = sched_get_ticks();
    lock->holder_pc = lock_caller_pc();
}

void spin_unlock_irqrestore(spinlock_t *lock, uint32_t flags)
{
    __asm__ volatile ("" ::: "memory");
    lock->hold_tick = 0;
    lock->holder_pc = 0;
    lock->lock = 0;
    eflags_restore(flags);           /* 恢复原中断状态(含 IF) */
}

/* 带超时: 循环上限 max_spins 次(约 1e7 次CAS~数十ms), 超时记录并返回 -1 */
int spin_lock_timeout(spinlock_t *lock, uint32_t max_spins)
{
    uint32_t n = 0;
    while (!smp_atomic_cas(&lock->lock, 0, 1)) {
        if (++n > max_spins) {
            spin_timeouts++;
            return -1;
        }
        spin_waits++;
        __asm__ volatile ("pause");
    }
    extern uint32_t sched_get_ticks(void);
    lock->hold_tick = sched_get_ticks();
    lock->holder_pc = lock_caller_pc();
    return 0;
}

/* ============================================================================
 * V3.94-E40: 死锁检测——锁监控表 + 持锁超时告警
 *
 * 原理: spin_lock 系列获取锁时记录 hold_tick/holder_pc;
 * lock_monitor_scan() 由 sched_tick 每 tick 调用(noinline, 仅 +4B),
 * 对注册的锁检查持锁时长, 超过阈值(500 ticks=5s)打印告警并计数.
 * 告警每 100 ticks 至多一次, 防刷屏; 只告警不干预(避免误杀).
 * ========================================================================== */
#define LOCK_MONITOR_MAX  16
#define LOCK_WARN_TICKS   500   /* 持锁 5 秒视为死锁嫌疑 */
#define LOCK_WARN_INTERVAL 100  /* 同一锁两次告警最小间隔 */

typedef struct {
    spinlock_t *lock;
    const char *name;
    uint32_t last_warn_tick;
    uint32_t warns;
} lock_monitor_t;

static lock_monitor_t lock_monitors[LOCK_MONITOR_MAX];
static uint32_t lock_monitor_count = 0;
static uint32_t lock_held_now = 0;

int lock_monitor_register(spinlock_t *lock, const char *name)
{
    if (!lock || !name) return -1;
    if (lock_monitor_count >= LOCK_MONITOR_MAX) return -1;
    for (uint32_t i = 0; i < lock_monitor_count; i++) {
        if (lock_monitors[i].lock == lock) return 0;  /* 已注册 */
    }
    lock_monitors[lock_monitor_count].lock = lock;
    lock_monitors[lock_monitor_count].name = name;
    lock_monitors[lock_monitor_count].last_warn_tick = 0;
    lock_monitors[lock_monitor_count].warns = 0;
    lock_monitor_count++;
    return 0;
}

void lock_monitor_stats(uint32_t *registered, uint32_t *held_now,
                        uint32_t *total_warns)
{
    uint32_t w = 0;
    for (uint32_t i = 0; i < lock_monitor_count; i++)
        w += lock_monitors[i].warns;
    if (registered) *registered = lock_monitor_count;
    if (held_now)   *held_now   = lock_held_now;
    if (total_warns)*total_warns = w;
}

__attribute__((noinline))
void lock_monitor_scan(void)
{
    extern uint32_t sched_get_ticks(void);
    uint32_t t = sched_get_ticks();
    uint32_t held = 0;
    for (uint32_t i = 0; i < lock_monitor_count; i++) {
        lock_monitor_t *m = &lock_monitors[i];
        if (!m->lock) continue;
        if (m->lock->lock) {
            held++;
            uint32_t dur = t - m->lock->hold_tick;
            if (dur > LOCK_WARN_TICKS &&
                t - m->last_warn_tick > LOCK_WARN_INTERVAL) {
                vga_printf("[LOCKWARN] %s held %d ticks by pc=0x%x\n",
                           m->name, dur, m->lock->holder_pc);
                m->last_warn_tick = t;
                m->warns++;
            }
        }
    }
    lock_held_now = held;
}

/* 供 shell 的 lockhold: 获取一把测试锁(不释放, 模拟死锁场景) */
static spinlock_t lock_test = SPINLOCK_INIT;
spinlock_t *lock_test_get(void)
{
    lock_monitor_register(&lock_test, "lock_test");  /* E-40: 注册进监控 */
    spin_lock(&lock_test);
    return &lock_test;
}
void lock_test_release(void)
{
    spin_unlock(&lock_test);
}
