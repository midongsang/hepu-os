#ifndef KERNEL_OPT_H
#define KERNEL_OPT_H

#include <stdint.h>

#define KOPT_MAX_CPU_STATS 8
#define KOPT_MAX_PROFILE_EVENTS 256
#define KOPT_MAX_BOOT_STAGES 32

/* 调度策略 */
typedef enum {
    SCHED_CFS = 0,  /* 完全公平调度 */
    SCHED_RT,  /* 实时调度 */
    SCHED_DEADLINE,  /* 截止时间调度 */
    SCHED_BATCH,  /* 批处理 */
    SCHED_IDLE,  /* 空闲调度 */
} sched_policy_t;

/* 调度器统计 */
typedef struct {
    uint64_t context_switches;
    uint64_t total_runtime;
    uint64_t idle_time;
    uint64_t run_time;
    uint64_t irq_time;
    uint64_t softirq_time;
    uint64_t syscalls;
    uint64_t interrupts;
    uint64_t preemptions;
    uint64_t migrations;
    int load_avg;
    int run_queue_length;
} sched_stats_t;

/* CPU统计 */
typedef struct {
    int cpu_id;
    int online;
    int frequency_mhz;
    int max_frequency;
    int min_frequency;
    int governor;  /* 0=performance,1=powersave,2=ondemand,3=conservative,4=schedutil */
    uint64_t user_time;
    uint64_t nice_time;
    uint64_t system_time;
    uint64_t idle_time;
    uint64_t iowait_time;
    uint64_t irq_time;
    uint64_t softirq_time;
    uint64_t steal_time;
    float usage_percent;
    int temperature;
    int cache_size_kb;
    int cores;
    int threads_per_core;
} cpu_stats_t;

/* 内存统计 */
typedef struct {
    uint64_t total_ram;
    uint64_t free_ram;
    uint64_t used_ram;
    uint64_t cached;
    uint64_t buffers;
    uint64_t swap_total;
    uint64_t swap_used;
    uint64_t swap_free;
    uint64_t kernel_heap_used;
    uint64_t page_cache_hits;
    uint64_t page_cache_misses;
    float cache_hit_rate;
    int page_size;
    int huge_pages;
    int compaction_enabled;
    int oom_count;
} memory_stats_t;

/* 中断统计 */
typedef struct {
    int irq;
    char name[64];
    uint64_t count;
    uint64_t cpu_mask;
    int affinity;
    int type;  /* 0=level,1=edge */
    int enabled;
} irq_stats_t;

/* 性能事件 */
typedef enum {
    PERF_CPU_CYCLES = 0,
    PERF_INSTRUCTIONS,
    PERF_CACHE_REFERENCES,
    PERF_CACHE_MISSES,
    PERF_BRANCH_INSTRUCTIONS,
    PERF_BRANCH_MISSES,
    PERF_BUS_CYCLES,
    PERF_STALLED_CYCLES_FRONTEND,
    PERF_STALLED_CYCLES_BACKEND,
    PERF_REF_CPU_CYCLES,
} perf_event_type_t;

/* 性能计数器 */
typedef struct {
    perf_event_type_t type;
    uint64_t count;
    uint64_t enabled;
    uint64_t running;
    float scale;
} perf_counter_t;

/* 启动阶段 */
typedef struct {
    char name[64];
    uint64_t start_time_us;
    uint64_t end_time_us;
    uint64_t duration_us;
    int completed;
} boot_stage_t;

/* 启动分析 */
typedef struct {
    uint64_t total_boot_time_us;
    uint64_t bootloader_time_us;
    uint64_t kernel_init_time_us;
    uint64_t userspace_time_us;
    boot_stage_t stages[KOPT_MAX_BOOT_STAGES];
    int stage_count;
    int optimized;
} boot_profile_t;

/* 内核配置 */
typedef struct {
    int hz;  /* 时钟频率 */
    int tickless;  /* 无时钟 */
    int preempt;  /* 抢占 */
    int preempt_rt;  /* 实时抢占 */
    int smp;  /* 多处理器 */
    int max_cpus;
    int highmem;  /* 高端内存 */
    int debug;  /* 调试 */
    int profiling;  /* 性能分析 */
    int ftrace;  /* 函数追踪 */
    int kprobes;  /* 内核探针 */
} kernel_config_t;

/* 内核优化状态 */
typedef struct {
    int initialized;
    /* 调度 */
    sched_policy_t sched_policy;
    sched_stats_t sched_stats;
    /* CPU */
    cpu_stats_t cpus[KOPT_MAX_CPU_STATS];
    int cpu_count;
    int online_cpus;
    /* 内存 */
    memory_stats_t memory;
    /* 中断 */
    irq_stats_t irqs[256];
    int irq_count;
    /* 性能 */
    perf_counter_t perf_counters[10];
    int perf_counter_count;
    int profiling_enabled;
    /* 启动 */
    boot_profile_t boot;
    /* 配置 */
    kernel_config_t config;
    /* 优化状态 */
    int optimization_level;  /* 0=none,1=basic,2=aggressive,3=extreme */
    int power_save_mode;
    int performance_mode;
    uint64_t uptime_us;
} kernel_opt_t;

/* 初始化内核优化 */
void kernel_opt_init(void);

/* 调度器 */
int kopt_set_scheduler(sched_policy_t policy);
int kopt_get_scheduler(sched_policy_t *policy);
int kopt_get_sched_stats(sched_stats_t *stats);
int kopt_set_process_priority(int pid, int priority);
int kopt_set_process_affinity(int pid, int cpu_mask);
int kopt_get_process_affinity(int pid, int *cpu_mask);

/* CPU管理 */
int kopt_get_cpu_count(void);
int kopt_get_cpu_stats(int cpu_id, cpu_stats_t *stats);
int kopt_get_all_cpu_stats(cpu_stats_t *stats, int max);
int kopt_set_cpu_governor(int cpu_id, int governor);
int kopt_set_cpu_frequency(int cpu_id, int freq_mhz);
int kopt_cpu_online(int cpu_id, int online);
int kopt_get_cpu_usage(int cpu_id, float *percent);

/* 内存管理 */
int kopt_get_memory_stats(memory_stats_t *stats);
int kopt_memory_compact(void);
int kopt_memory_drop_caches(int level);  /* 1=pagecache,2=dentries/inodes,3=all */
int kopt_set_swapiness(int swappiness);
int kopt_get_swapiness(void);
int kopt_oom_score_adj(int pid, int score);

/* 中断管理 */
int kopt_get_irq_stats(irq_stats_t *stats, int max);
int kopt_set_irq_affinity(int irq, int cpu_mask);
int kopt_get_irq_affinity(int irq, int *cpu_mask);
int kopt_enable_irq(int irq);
int kopt_disable_irq(int irq);

/* 性能分析 */
int kopt_perf_enable(int enable);
int kopt_perf_add_counter(perf_event_type_t type);
int kopt_perf_read_counter(int counter_id, perf_counter_t *counter);
int kopt_perf_reset(void);
int kopt_perf_get_stats(perf_counter_t *counters, int max);

/* 启动优化 */
int kopt_boot_record_stage(const char *name);
int kopt_boot_get_profile(boot_profile_t *profile);
int kopt_boot_optimize(void);
int kopt_boot_get_time(uint64_t *boot_time_us);

/* 内核配置 */
int kopt_get_config(kernel_config_t *config);
int kopt_set_tickless(int enable);
int kopt_set_preempt(int enable);
int kopt_set_debug(int enable);

/* 系统优化 */
int kopt_set_optimization_level(int level);
int kopt_get_optimization_level(void);
int kopt_set_power_save(int enable);
int kopt_set_performance_mode(int enable);
int kopt_get_uptime(uint64_t *uptime_us);

/* 统计 */
int kopt_get_all_stats(kernel_opt_t *stats);

#endif /* KERNEL_OPT_H */
