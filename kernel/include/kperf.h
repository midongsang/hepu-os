#ifndef KPERF_H
#define KPERF_H

#include <stdint.h>

#define KPERF_MAX_EVENTS 64
#define KPERF_MAX_COUNTERS 16
#define KPERF_MAX_CALLCHAIN 32
#define KPERF_MAX_SAMPLES 1024
#define KPERF_MAX_SYMBOLS 256
#define KPERF_MAX_PROCESSES 128

/* 性能事件类型 */
typedef enum {
    KPERF_EVENT_CPU_CYCLES = 0,
    KPERF_EVENT_INSTRUCTIONS,
    KPERF_EVENT_CACHE_REFERENCES,
    KPERF_EVENT_CACHE_MISSES,
    KPERF_EVENT_BRANCH_INSTRUCTIONS,
    KPERF_EVENT_BRANCH_MISSES,
    KPERF_EVENT_BUS_CYCLES,
    KPERF_EVENT_STALLED_CYCLES_FRONTEND,
    KPERF_EVENT_STALLED_CYCLES_BACKEND,
    KPERF_EVENT_REF_CPU_CYCLES,
    KPERF_EVENT_CPU_MIGRATIONS,
    KPERF_EVENT_PAGE_FAULTS,
    KPERF_EVENT_CONTEXT_SWITCHES,
    KPERF_EVENT_CPU_MIGRATION,
    KPERF_EVENT_MINOR_FAULTS,
    KPERF_EVENT_MAJOR_FAULTS,
    KPERF_EVENT_ALIGNMENT_FAULTS,
    KPERF_EVENT_EMULATION_FAULTS,
    KPERF_EVENT_DUMMY,
    KPERF_EVENT_BPF_OUTPUT,
} kkperf_event_type_t;

/* 性能事件 */
typedef struct {
    int used;
    int id;
    kkperf_event_type_t type;
    int enabled;
    uint64_t count;
    uint64_t time_enabled;
    uint64_t time_running;
    int pid;  /* -1=所有进程 */
    int cpu;  /* -1=所有CPU */
    int frequency;  /* 采样频率 */
    int period;  /* 采样周期 */
    int sample_type;  /* 采样类型位掩码 */
    int read_format;  /* 读取格式位掩码 */
    int inherit;  /* 继承子进程 */
    int pinned;  /* 固定到CPU */
    int exclusive;  /* 独占 */
} kperf_event_t;

/* 性能计数器 */
typedef struct {
    int used;
    int id;
    kkperf_event_type_t type;
    uint64_t value;
    uint64_t enabled_time;
    uint64_t running_time;
    int cpu;
    int active;
} kperf_counter_t;

/* 采样记录 */
typedef struct {
    uint64_t ip;  /* 指令指针 */
    uint64_t pid;
    uint64_t tid;
    uint64_t time;
    uint64_t addr;
    uint64_t id;
    uint64_t stream_id;
    uint32_t cpu;
    uint32_t res;
    uint64_t period;
    uint64_t callchain[KPERF_MAX_CALLCHAIN];
    int callchain_depth;
    char symbol[64];
    uint64_t symbol_offset;
} perf_sample_t;

/* 符号信息 */
typedef struct {
    uint64_t start;
    uint64_t end;
    char name[64];
    char module[64];
    int hot;  /* 热点计数 */
} perf_symbol_t;

/* 进程性能统计 */
typedef struct {
    int pid;
    char name[64];
    uint64_t cpu_time;
    uint64_t user_time;
    uint64_t kernel_time;
    uint64_t min_flt;
    uint64_t maj_flt;
    uint64_t context_switches;
    uint64_t instructions;
    uint64_t cycles;
    uint64_t cache_misses;
    uint64_t branch_misses;
    double cpi;  /* cycles per instruction */
    double ipc;  /* instructions per cycle */
    double cache_miss_rate;
    double branch_miss_rate;
} perf_process_t;

/* 火焰图节点 */
typedef struct {
    char name[64];
    uint64_t count;
    int depth;
    int parent;
    int children[32];
    int child_count;
} flame_node_t;

/* 系统调用跟踪 */
typedef struct {
    int syscall_number;
    char name[64];
    uint64_t count;
    uint64_t total_time_ns;
    uint64_t max_time_ns;
    uint64_t min_time_ns;
    uint64_t errors;
} syscall_trace_t;

/* 性能分析状态 */
typedef struct {
    int initialized;
    int enabled;
    /* 事件 */
    kperf_event_t events[KPERF_MAX_EVENTS];
    int event_count;
    /* 计数器 */
    kperf_counter_t counters[KPERF_MAX_COUNTERS];
    int counter_count;
    /* 采样 */
    perf_sample_t samples[KPERF_MAX_SAMPLES];
    int sample_count;
    int sample_index;
    /* 符号 */
    perf_symbol_t symbols[KPERF_MAX_SYMBOLS];
    int symbol_count;
    /* 进程 */
    perf_process_t processes[KPERF_MAX_PROCESSES];
    int process_count;
    /* 火焰图 */
    flame_node_t flame[KPERF_MAX_SYMBOLS];
    int flame_node_count;
    /* 系统调用跟踪 */
    syscall_trace_t syscall_traces[256];
    int syscall_trace_count;
    int syscall_tracing_enabled;
    /* 配置 */
    int sampling_frequency;
    int callchain_enabled;
    int userspace_callchain;
    int kernel_callchain;
    /* 统计 */
    uint64_t total_samples;
    uint64_t total_events;
    uint64_t profiling_start_time;
    uint64_t profiling_end_time;
    uint64_t profiling_duration_ms;
} kperf_t;

/* 初始化性能分析 */
void kperf_init(void);

/* 事件管理 */
int kperf_event_create(kkperf_event_type_t type, int pid, int cpu);
int kperf_event_enable(int event_id);
int kperf_event_disable(int event_id);
int kperf_event_read(int event_id, uint64_t *value);
int kperf_event_reset(int event_id);
int kperf_event_list(kperf_event_t *events, int max);
int kperf_event_get(int event_id, kperf_event_t *event);

/* 计数器 */
int kperf_counter_add(kkperf_event_type_t type, int cpu);
int kperf_counter_read(int counter_id, uint64_t *value);
int kperf_counter_start(int counter_id);
int kperf_counter_stop(int counter_id);
int kperf_counter_list(kperf_counter_t *counters, int max);

/* 采样 */
int kperf_sample_record(perf_sample_t *sample);
int kperf_sample_get(int index, perf_sample_t *sample);
int kperf_sample_list(perf_sample_t *samples, int max);
int kperf_sample_flush(void);

/* 符号 */
int kperf_symbol_add(uint64_t start, uint64_t end, const char *name, const char *module);
int kperf_symbol_lookup(uint64_t addr, perf_symbol_t *symbol);
int kperf_symbol_list(perf_symbol_t *symbols, int max);
int kperf_symbol_hot_list(perf_symbol_t *symbols, int max);

/* 进程统计 */
int kperf_process_update(int pid, const char *name, perf_process_t *stats);
int kperf_process_get(int pid, perf_process_t *process);
int kperf_process_list(perf_process_t *processes, int max);
int kperf_process_top(perf_process_t *processes, int max, int sort_by);

/* 火焰图 */
int kperf_flame_add(const char *stack[], int depth, uint64_t count);
int kperf_flame_get(flame_node_t *nodes, int max);
int kperf_flame_generate(void);
int kperf_flame_print(void);

/* 系统调用跟踪 */
int kperf_syscall_trace_start(void);
int kperf_syscall_trace_stop(void);
int kperf_syscall_trace_record(int syscall, uint64_t duration_ns, int error);
int kperf_syscall_trace_get(int syscall, syscall_trace_t *trace);
int kperf_syscall_trace_list(syscall_trace_t *traces, int max);
int kperf_syscall_trace_top(syscall_trace_t *traces, int max, int sort_by);

/* 性能分析控制 */
int kperf_start(int frequency, int callchain);
int kperf_stop(void);
int kperf_report(void);
int kperf_get_stats(kperf_t *stats);

#endif /* KPERF_H */
