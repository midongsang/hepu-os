#ifndef TRACE_H
#define TRACE_H

#include <stdint.h>

#define TRACE_MAX_EVENTS 1024
#define TRACE_MAX_PROBES 32
#define TRACE_NAME_LEN 32
#define TRACE_LOG_SIZE 65536

/* 跟踪事件类型 */
typedef enum {
    TRACE_EVENT_ENTER = 0,   /* 函数进入 */
    TRACE_EVENT_EXIT,        /* 函数退出 */
    TRACE_EVENT_IRQ,         /* 中断 */
    TRACE_EVENT_SCHED,       /* 调度 */
    TRACE_EVENT_SYSCALL,     /* 系统调用 */
    TRACE_EVENT_TIMER,       /* 定时器 */
    TRACE_EVENT_IO,          /* IO操作 */
    TRACE_EVENT_MEM,         /* 内存分配 */
    TRACE_EVENT_POWER,       /* 电源事件 */
    TRACE_EVENT_CUSTOM,      /* 自定义 */
} trace_event_type_t;

/* 跟踪事件 */
typedef struct {
    uint64_t timestamp;      /* 时间戳 (us) */
    trace_event_type_t type;
    uint32_t cpu;
    uint32_t pid;
    char name[TRACE_NAME_LEN];
    uint32_t arg1;
    uint32_t arg2;
    uint32_t duration;       /* 持续时间 (us) */
} trace_event_t;

/* 跟踪探针 */
typedef struct {
    int used;
    int id;
    char name[TRACE_NAME_LEN];
    void *address;
    int hit_count;
    uint64_t total_time;
    uint64_t min_time;
    uint64_t max_time;
    int enabled;
} trace_probe_t;

/* 性能计数器 */
typedef struct {
    uint64_t cpu_cycles;
    uint64_t instructions;
    uint64_t cache_refs;
    uint64_t cache_misses;
    uint64_t branch_misses;
    uint64_t page_faults;
    uint64_t context_switches;
    uint64_t cpu_migrations;
} perf_counters_t;

/* 性能事件 */
typedef struct {
    const char *name;
    uint64_t count;
    uint64_t time_enabled;
    uint64_t time_running;
} perf_event_t;

/* 日志级别 */
typedef enum {
    LOG_EMERG = 0,
    LOG_ALERT,
    LOG_CRIT,
    LOG_ERR,
    LOG_WARNING,
    LOG_NOTICE,
    LOG_INFO,
    LOG_DEBUG,
} log_level_t;

/* 日志条目 */
typedef struct {
    uint64_t timestamp;
    log_level_t level;
    const char *tag;
    char message[128];
} log_entry_t;

/* 初始化跟踪子系统 */
void trace_init(void);

/* 启用/禁用跟踪 */
int trace_enable(int enable);

/* 跟踪函数进入 */
void trace_enter(const char *name, uint32_t arg1, uint32_t arg2);

/* 跟踪函数退出 */
void trace_exit(const char *name, uint32_t retval);

/* 跟踪自定义事件 */
void trace_event(trace_event_type_t type, const char *name, uint32_t arg1, uint32_t arg2);

/* 读取跟踪事件 */
int trace_read(trace_event_t *events, int max);

/* 获取跟踪事件数 */
int trace_count(void);

/* 清空跟踪缓冲区 */
void trace_clear(void);

/* 探针管理 */
int trace_probe_register(const char *name, void *address);
int trace_probe_enable(int id, int enable);
int trace_probe_get_stats(int id, trace_probe_t *stats);
int trace_probe_list(trace_probe_t *probes, int max);

/* 性能计数器 */
void perf_init(void);
int perf_start(void);
int perf_stop(void);
int perf_read(perf_counters_t *counters);
int perf_reset(void);

/* 性能事件 */
int perf_event_create(const char *name);
int perf_event_read(int id, perf_event_t *event);
int perf_event_destroy(int id);

/* 内核日志 */
void log_init(void);
void log_write(log_level_t level, const char *tag, const char *fmt, ...);
int log_read(log_entry_t *entries, int max);
int log_count(void);
void log_clear(void);
void log_set_level(log_level_t level);

/* 打印跟踪统计 */
void trace_print_stats(void);

/* 打印性能统计 */
void perf_print_stats(void);

/* 导出跟踪数据 */
int trace_export(void *buf, uint32_t size);

#endif /* TRACE_H */
