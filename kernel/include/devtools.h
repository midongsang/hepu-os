#ifndef DEVTOOLS_H
#define DEVTOOLS_H

#include <stdint.h>

#define DT_MAX_BREAKPOINTS 64
#define DT_MAX_WATCHPOINTS 16
#define DT_MAX_PROFILE_EVENTS 1024
#define DT_MAX_LOG_ENTRIES 256
#define DT_MAX_SYMBOLS 1024

/* 调试器状态 */
typedef enum {
    DEBUGGER_STOPPED = 0,
    DEBUGGER_RUNNING,
    DEBUGGER_PAUSED,
    DEBUGGER_STEPPING,
    DEBUGGER_BREAKPOINT_HIT,
    DEBUGGER_WATCHPOINT_HIT,
    DEBUGGER_EXCEPTION,
} debugger_state_t;

/* 断点类型 */
typedef enum {
    BP_TYPE_SOFTWARE = 0,
    BP_TYPE_HARDWARE,
    BP_TYPE_CONDITIONAL,
    BP_TYPE_LOG,
} breakpoint_type_t;

/* 断点 */
typedef struct {
    int used;
    int id;
    breakpoint_type_t type;
    uint32_t address;
    int enabled;
    int hit_count;
    char condition[128];
    char log_message[256];
    uint8_t original_byte;
} breakpoint_t;

/* 观察点 */
typedef struct {
    int used;
    int id;
    uint32_t address;
    int size;  /* 1,2,4字节 */
    int read;  /* 读时触发 */
    int write; /* 写时触发 */
    int enabled;
    int hit_count;
} watchpoint_t;

/* 寄存器状态 */
typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eip, eflags;
    uint32_t cs, ds, es, fs, gs, ss;
    uint32_t cr0, cr2, cr3;
} reg_state_t;

/* 性能事件类型 */
typedef enum {
    PROF_EVENT_FUNCTION_ENTER = 0,
    PROF_EVENT_FUNCTION_EXIT,
    PROF_EVENT_ALLOC,
    PROF_EVENT_FREE,
    PROF_EVENT_SYSCALL,
    PROF_EVENT_INTERRUPT,
    PROF_EVENT_CUSTOM,
} prof_event_type_t;

/* 性能事件 */
typedef struct {
    prof_event_type_t type;
    uint32_t timestamp;
    uint32_t duration;
    uint32_t address;
    char name[64];
    int cpu_id;
    uint32_t arg1, arg2;
} prof_event_t;

/* 函数性能统计 */
typedef struct {
    char name[64];
    uint32_t address;
    uint64_t call_count;
    uint64_t total_time;
    uint64_t max_time;
    uint64_t min_time;
    float avg_time;
} func_profile_t;

/* 日志级别 */
typedef enum {
    DT_LOG_VERBOSE = 0,
    DT_LOG_DEBUG,
    DT_LOG_INFO,
    DT_LOG_WARNING,
    DT_LOG_ERROR,
    DT_LOG_FATAL,
} dt_log_level_t;

/* 日志条目 */
typedef struct {
    uint64_t timestamp;
    dt_log_level_t level;
    char tag[32];
    char message[256];
    int pid;
    int tid;
} dt_log_entry_t;

/* 符号表条目 */
typedef struct {
    uint32_t address;
    char name[64];
    int size;
    char type;  /* 'T'=text, 'D'=data, 'B'=bss */
} symbol_t;

/* 热重载状态 */
typedef struct {
    int enabled;
    int watch_count;
    char watch_paths[16][256];
    uint64_t last_reload;
    int reload_count;
    int auto_reload;
} hot_reload_t;

/* 调试器 */
typedef struct {
    int initialized;
    debugger_state_t state;
    breakpoint_t breakpoints[DT_MAX_BREAKPOINTS];
    int breakpoint_count;
    watchpoint_t watchpoints[DT_MAX_WATCHPOINTS];
    int watchpoint_count;
    reg_state_t registers;
    uint32_t current_address;
    int step_over;
    int step_count;
} debugger_t;

/* 性能分析器 */
typedef struct {
    int initialized;
    int profiling;
    prof_event_t events[DT_MAX_PROFILE_EVENTS];
    int event_count;
    int event_head;
    func_profile_t functions[256];
    int func_count;
    uint64_t total_samples;
    uint64_t sample_interval;
    int cpu_usage;
    int memory_usage_kb;
    int page_faults;
    int context_switches;
} profiler_t;

/* 日志系统 */
typedef struct {
    int initialized;
    dt_log_entry_t entries[DT_MAX_LOG_ENTRIES];
    int entry_count;
    int entry_head;
    dt_log_level_t min_level;
    int log_to_serial;
    int log_to_vga;
    int log_to_file;
} logger_t;

/* SDK信息 */
typedef struct {
    char version[32];
    char build_date[32];
    char build_hash[16];
    int api_level;
    int min_api;
    int target_api;
    char compiler[32];
    char arch[16];
} sdk_info_t;

/* 开发者工具状态 */
typedef struct {
    int initialized;
    debugger_t debugger;
    profiler_t profiler;
    logger_t logger;
    hot_reload_t hot_reload;
    symbol_t symbols[DT_MAX_SYMBOLS];
    int symbol_count;
    sdk_info_t sdk;
    /* 开发者选项 */
    int usb_debugging;
    int wifi_debugging;
    int show_touches;
    int show_fps;
    int show_layout_bounds;
    int gpu_overdraw;
    int force_hw_rendering;
    int disable_animations;
    int strict_mode;
    int profile_gpu_rendering;
} devtools_t;

/* 初始化开发者工具 */
void devtools_init(void);

/* 调试器 */
int debugger_attach(void);
int debugger_detach(void);
int debugger_pause(void);
int debugger_resume(void);
int debugger_step(void);
int debugger_step_over(void);
int debugger_step_out(void);
int debugger_get_state(debugger_state_t *state);
int debugger_get_registers(reg_state_t *regs);
int debugger_set_register(int reg, uint32_t value);
int debugger_read_memory(uint32_t address, void *buf, int len);
int debugger_write_memory(uint32_t address, const void *buf, int len);

/* 断点 */
int breakpoint_add(uint32_t address, breakpoint_type_t type);
int breakpoint_remove(int bp_id);
int breakpoint_enable(int bp_id, int enable);
int breakpoint_list(breakpoint_t *bps, int max);
int breakpoint_set_condition(int bp_id, const char *condition);

/* 观察点 */
int watchpoint_add(uint32_t address, int size, int read, int write);
int watchpoint_remove(int wp_id);
int watchpoint_list(watchpoint_t *wps, int max);

/* 性能分析 */
int profiler_start(void);
int profiler_stop(void);
int profiler_is_running(void);
int profiler_get_events(prof_event_t *events, int max);
int profiler_get_functions(func_profile_t *funcs, int max);
int profiler_get_stats(int *cpu, int *mem_kb, int *faults, int *switches);
int profiler_reset(void);

/* 日志 */
int dt_log_write(dt_log_level_t level, const char *tag, const char *message);
int dt_log_read(dt_log_entry_t *entries, int max);
int dt_log_set_level(dt_log_level_t level);
int dt_log_clear(void);
int dt_log_count(void);

/* 符号表 */
int symbol_add(uint32_t address, const char *name, int size, char type);
int symbol_lookup(uint32_t address, symbol_t *sym);
int symbol_find(const char *name, symbol_t *sym);
int symbol_list(symbol_t *syms, int max);

/* 热重载 */
int hotreload_enable(int enable);
int hotreload_add_watch(const char *path);
int hotreload_remove_watch(const char *path);
int hotreload_trigger(void);
int hotreload_get_status(hot_reload_t *status);

/* SDK */
int sdk_get_info(sdk_info_t *info);
int sdk_get_api_level(void);

/* 开发者选项 */
int devopt_set_usb_debugging(int enable);
int devopt_set_show_fps(int enable);
int devopt_set_show_touches(int enable);
int devopt_set_disable_animations(int enable);
int devopt_set_strict_mode(int enable);

/* 辅助函数 */
int devtools_hexdump(uint32_t address, int len);
int devtools_backtrace(uint32_t *frames, int max);
int devtools_dump_registers(void);

#endif /* DEVTOOLS_H */
