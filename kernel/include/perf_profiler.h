#ifndef PERF_PROFILER_H
#define PERF_PROFILER_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 性能分析器
 * 采样 / 热点 / 火焰图 / 基准测试 / 计数器
 * ========================================================================== */

#define PP_MAX_EVENTS 256
#define PP_MAX_FUNCTIONS 512
#define PP_MAX_CALLCHAINS 64
#define PP_NAME_LEN 128

/* 性能事件类型 */
typedef enum {
    PP_EVENT_CPU_CYCLES = 0,
    PP_EVENT_INSTRUCTIONS,
    PP_EVENT_CACHE_MISSES,
    PP_EVENT_BRANCH_MISSES,
    PP_EVENT_CONTEXT_SWITCHES,
    PP_EVENT_PAGE_FAULTS,
    PP_EVENT_SYSCALLS,
    PP_EVENT_INTERRUPTS,
    PP_EVENT_MEMORY_ACCESS,
    PP_EVENT_IO_OPERATIONS,
    PP_EVENT_CPU_MIGRATIONS,
    PP_EVENT_TASK_SCHEDULE,
} pp_event_type_t;

/* 函数统计 */
typedef struct {
    int used;
    int id;
    char name[PP_NAME_LEN];
    uint32_t address;
    uint64_t call_count;
    uint64_t total_time_ns;
    uint64_t self_time_ns;
    uint64_t min_time_ns;
    uint64_t max_time_ns;
    uint64_t avg_time_ns;
    int recursion_depth;
    int is_hotspot;
} pp_function_t;

/* 调用链 */
typedef struct {
    int used;
    int id;
    uint32_t addresses[32];
    char names[32][PP_NAME_LEN];
    int depth;
    uint64_t count;
    uint64_t total_time_ns;
} pp_callchain_t;

/* 性能事件 */
typedef struct {
    uint64_t timestamp;
    pp_event_type_t type;
    uint32_t cpu;
    uint32_t pid;
    uint64_t value;
    char detail[256];
} pp_event_t;

/* 基准测试结果 */
typedef struct {
    char name[PP_NAME_LEN];
    uint64_t iterations;
    uint64_t total_time_ns;
    uint64_t avg_time_ns;
    uint64_t min_time_ns;
    uint64_t max_time_ns;
    uint64_t std_dev_ns;
    uint64_t ops_per_second;
    int cpu_usage;
    int memory_usage_kb;
} pp_benchmark_t;

/* 硬件计数器 */
typedef struct {
    uint64_t cpu_cycles;
    uint64_t instructions;
    uint64_t cache_references;
    uint64_t cache_misses;
    uint64_t branch_instructions;
    uint64_t branch_misses;
    uint64_t bus_cycles;
    uint64_t stalled_cycles;
    double ipc;  /* 每周期指令数 */
    double cache_miss_rate;
    double branch_miss_rate;
} pp_hw_counters_t;

/* 采样配置 */
typedef struct {
    int enabled;
    int frequency;  /* Hz */
    int event_type;
    int cpu_mask;
    int pid_filter;
    int callchain_depth;
} pp_sampling_t;

/* 性能分析器状态 */
typedef struct {
    int initialized;
    int enabled;
    int profiling;
    /* 采样 */
    pp_sampling_t sampling;
    /* 函数 */
    pp_function_t functions[PP_MAX_FUNCTIONS];
    int function_count;
    /* 调用链 */
    pp_callchain_t callchains[PP_MAX_CALLCHAINS];
    int callchain_count;
    /* 事件 */
    pp_event_t events[PP_MAX_EVENTS];
    int event_count;
    int event_pos;
    /* 计数器 */
    pp_hw_counters_t counters;
    uint64_t counter_start;
    /* 统计 */
    uint64_t total_samples;
    uint64_t total_events;
    uint64_t profiling_time_ns;
} perf_profiler_t;

/* 初始化 */
void perf_profiler_init(void);

/* 采样控制 */
int pp_start_sampling(int frequency, pp_event_type_t event);
int pp_stop_sampling(void);
int pp_is_sampling(void);
int pp_set_sampling_config(const pp_sampling_t *config);
int pp_get_sampling_config(pp_sampling_t *config);

/* 函数追踪 */
int pp_function_enter(const char *name, uint32_t address);
int pp_function_exit(const char *name, uint64_t duration_ns);
int pp_get_function(const char *name, pp_function_t *func);
int pp_list_functions(pp_function_t *funcs, int max);
int pp_get_hotspots(pp_function_t *funcs, int max, int threshold_ns);
int pp_reset_functions(void);

/* 调用链 */
int pp_record_callchain(uint32_t *addresses, int depth);
int pp_get_callchains(pp_callchain_t *chains, int max);

/* 事件 */
int pp_record_event(pp_event_type_t type, uint64_t value, const char *detail);
int pp_get_events(pp_event_t *events, int max);
int pp_clear_events(void);

/* 硬件计数器 */
int pp_counters_start(void);
int pp_counters_stop(void);
int pp_counters_read(pp_hw_counters_t *counters);
int pp_counters_reset(void);

/* 基准测试 */
int pp_benchmark_start(const char *name);
int pp_benchmark_stop(const char *name, pp_benchmark_t *result);
int pp_benchmark_run(const char *name, uint64_t iterations,
                     void (*func)(void), pp_benchmark_t *result);

/* 分析 */
int pp_generate_flamegraph(char *buf, int size);
int pp_generate_report(char *buf, int size);
int pp_get_top_functions(pp_function_t *funcs, int max);

/* 统计 */
int pp_get_stats(perf_profiler_t *stats);

#endif /* PERF_PROFILER_H */
