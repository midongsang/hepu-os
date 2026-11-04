#ifndef BOOT_OPTIMIZER_H
#define BOOT_OPTIMIZER_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 启动速度优化
 * 并行初始化 / 延迟加载 / 预加载 / 启动分析
 * ========================================================================== */

#define BO_MAX_MODULES 256
#define BO_MAX_PHASES 16
#define BO_NAME_LEN 128

/* 启动阶段 */
typedef enum {
    BO_PHASE_ROM = 0,
    BO_PHASE_BOOTLOADER,
    BO_PHASE_KERNEL_ENTRY,
    BO_PHASE_ARCH_INIT,
    BO_PHASE_MEMORY_INIT,
    BO_PHASE_DRIVER_INIT,
    BO_PHASE_CORE_SERVICES,
    BO_PHASE_FILESYSTEM,
    BO_PHASE_NETWORK,
    BO_PHASE_GRAPHICS,
    BO_PHASE_USERSPACE,
    BO_PHASE_APP_LAUNCH,
    BO_PHASE_COMPLETE,
} bo_phase_t;

/* 初始化优先级 */
typedef enum {
    BO_PRIO_CRITICAL = 0,  /* 必须立即初始化 */
    BO_PRIO_HIGH,          /* 早期初始化 */
    BO_PRIO_NORMAL,        /* 正常初始化 */
    BO_PRIO_LOW,           /* 延迟初始化 */
    BO_PRIO_IDLE,          /* 空闲时初始化 */
} bo_priority_t;

/* 模块初始化记录 */
typedef struct {
    int used;
    int id;
    char name[BO_NAME_LEN];
    bo_phase_t phase;
    bo_priority_t priority;
    int parallel;  /* 可并行 */
    int lazy;      /* 延迟加载 */
    uint64_t start_time;
    uint64_t end_time;
    uint64_t duration_us;
    int status;  /* 0=pending,1=running,2=done,3=failed,4=skipped */
    int dependency_count;
    int dependencies[32];
} bo_module_t;

/* 启动阶段统计 */
typedef struct {
    bo_phase_t phase;
    uint64_t start_time;
    uint64_t end_time;
    uint64_t duration_us;
    int module_count;
    int failed_count;
} bo_phase_stat_t;

/* 启动配置 */
typedef struct {
    int parallel_init;     /* 并行初始化 */
    int lazy_load;         /* 延迟加载 */
    int preload_apps;      /* 预加载应用 */
    int boot_profile;      /* 启动分析 */
    int fast_boot;         /* 快速启动模式 */
    int silent_boot;       /* 静默启动 */
    int max_parallel;      /* 最大并行数 */
    int idle_init_delay;   /* 空闲初始化延迟ms */
} bo_config_t;

/* 启动优化器状态 */
typedef struct {
    int initialized;
    int enabled;
    bo_config_t config;
    /* 模块 */
    bo_module_t modules[BO_MAX_MODULES];
    int module_count;
    /* 阶段 */
    bo_phase_t current_phase;
    bo_phase_stat_t phase_stats[BO_MAX_PHASES];
    /* 时间 */
    uint64_t boot_start_time;
    uint64_t boot_end_time;
    uint64_t total_boot_time_us;
    /* 统计 */
    int optimized_count;
    int skipped_count;
    int parallel_count;
    int lazy_count;
    uint64_t time_saved_us;
} boot_optimizer_t;

/* 初始化 */
void boot_optimizer_init(void);

/* 模块注册 */
int bo_register(const char *name, bo_phase_t phase, bo_priority_t priority,
                int parallel, int lazy);
int bo_register_dependency(int module_id, int dep_id);
int bo_get_module(int id, bo_module_t *mod);
int bo_list_modules(bo_module_t *mods, int max);

/* 启动流程 */
int bo_start_boot(void);
int bo_begin_phase(bo_phase_t phase);
int bo_end_phase(bo_phase_t phase);
int bo_module_start(int module_id);
int bo_module_complete(int module_id);
int bo_module_fail(int module_id);
int bo_module_skip(int module_id);
int bo_complete_boot(void);

/* 优化 */
int bo_optimize(void);
int bo_apply_config(const bo_config_t *config);
int bo_get_config(bo_config_t *config);
int bo_set_parallel(int enable);
int bo_set_lazy_load(int enable);
int bo_set_fast_boot(int enable);

/* 分析 */
int bo_get_boot_time(uint64_t *time_us);
int bo_get_phase_stats(bo_phase_stat_t *stats, int max);
int bo_get_slow_modules(bo_module_t *mods, int max, int threshold_us);
int bo_get_critical_path(bo_module_t *mods, int max);
int bo_generate_report(char *buf, int size);

/* 统计 */
int bo_get_stats(boot_optimizer_t *stats);

#endif /* BOOT_OPTIMIZER_H */
