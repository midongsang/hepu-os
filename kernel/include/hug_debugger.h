#ifndef HUG_DEBUGGER_H
#define HUG_DEBUGGER_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS .hug 调试器与模拟器
 * 断点 / 单步 / 内存查看 / 性能分析 / 模拟器
 * ========================================================================== */

#define HDBG_MAX_BREAKPOINTS 64
#define HDBG_MAX_WATCHPOINTS 32
#define HDBG_MAX_CALLSTACK 64
#define HDBG_MAX_LOGS 1024
#define HDBG_MAX_THREADS 32

/* 调试状态 */
typedef enum {
    HDBG_STATE_IDLE = 0,
    HDBG_STATE_RUNNING,
    HDBG_STATE_PAUSED,
    HDBG_STATE_STEPPING,
    HDBG_STATE_BREAKPOINT_HIT,
    HDBG_STATE_EXCEPTION,
    HDBG_STATE_TERMINATED,
} hdbg_state_t;

/* 断点类型 */
typedef enum {
    HDBG_BP_SOFTWARE = 0,
    HDBG_BP_HARDWARE,
    HDBG_BP_CONDITIONAL,
    HDBG_BP_LOG,
} hdbg_bp_type_t;

/* 断点 */
typedef struct {
    int used;
    int id;
    hdbg_bp_type_t type;
    uint32_t address;
    char symbol[128];
    int line;
    char file[256];
    int hit_count;
    int enabled;
    char condition[256];
    char log_message[256];
} hdbg_breakpoint_t;

/* 观察点 */
typedef struct {
    int used;
    int id;
    uint32_t address;
    int size;  /* 1/2/4字节 */
    int type;  /* 0=read,1=write,2=read/write */
    int hit_count;
    int enabled;
} hdbg_watchpoint_t;

/* 调用栈帧 */
typedef struct {
    uint32_t address;
    uint32_t frame_pointer;
    uint32_t stack_pointer;
    char function[128];
    int line;
    char file[256];
} hdbg_stack_frame_t;

/* 寄存器 */
typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eip, eflags;
    uint32_t cs, ds, es, fs, gs, ss;
    uint32_t cr0, cr2, cr3, cr4;
} hdbg_registers_t;

/* 线程信息 */
typedef struct {
    int used;
    int id;
    char name[64];
    hdbg_state_t state;
    uint32_t entry_point;
    uint32_t stack_top;
    uint32_t stack_size;
    int priority;
    uint64_t cpu_time;
} hdbg_thread_t;

/* 日志条目 */
typedef struct {
    uint64_t timestamp;
    int level;  /* 0=debug,1=info,2=warn,3=error */
    char tag[64];
    char message[512];
} hdbg_log_entry_t;

/* 性能计数器 */
typedef struct {
    uint64_t cpu_cycles;
    uint64_t instructions;
    uint64_t cache_misses;
    uint64_t branch_mispredicts;
    uint64_t memory_access;
    uint64_t io_operations;
    uint64_t syscalls;
    uint64_t context_switches;
} hdbg_perf_counters_t;

/* 模拟器配置 */
typedef struct {
    int enabled;
    int screen_width;
    int screen_height;
    int screen_dpi;
    int ram_size_mb;
    int storage_size_mb;
    int cpu_cores;
    int cpu_speed_mhz;
    int network_speed;  /* kbps */
    int battery_level;  /* 0-100 */
    int gps_enabled;
    int camera_enabled;
    int sensors_enabled;
    char device_model[64];
    char os_version[32];
} hdbg_emulator_config_t;

/* 调试器状态 */
typedef struct {
    int initialized;
    int enabled;
    hdbg_state_t state;
    int attached_pid;
    /* 断点/观察点 */
    hdbg_breakpoint_t breakpoints[HDBG_MAX_BREAKPOINTS];
    int breakpoint_count;
    hdbg_watchpoint_t watchpoints[HDBG_MAX_WATCHPOINTS];
    int watchpoint_count;
    /* 调用栈 */
    hdbg_stack_frame_t callstack[HDBG_MAX_CALLSTACK];
    int callstack_depth;
    /* 寄存器 */
    hdbg_registers_t registers;
    /* 线程 */
    hdbg_thread_t threads[HDBG_MAX_THREADS];
    int thread_count;
    int current_thread;
    /* 日志 */
    hdbg_log_entry_t logs[HDBG_MAX_LOGS];
    int log_count;
    int log_pos;
    /* 性能 */
    hdbg_perf_counters_t perf;
    uint64_t start_time;
    /* 模拟器 */
    hdbg_emulator_config_t emulator;
    /* 统计 */
    uint64_t total_breakpoint_hits;
    uint64_t total_steps;
    uint64_t total_sessions;
} hug_debugger_t;

/* 初始化 */
void hug_debugger_init(void);

/* 调试控制 */
int hdbg_attach(int pid);
int hdbg_detach(void);
int hdbg_run(void);
int hdbg_pause(void);
int hdbg_step(void);
int hdbg_step_over(void);
int hdbg_step_out(void);
int hdbg_continue(void);
int hdbg_stop(void);

/* 断点 */
int hdbg_breakpoint_add(uint32_t address, const char *condition);
int hdbg_breakpoint_add_symbol(const char *symbol, const char *condition);
int hdbg_breakpoint_remove(int id);
int hdbg_breakpoint_enable(int id, int enable);
int hdbg_breakpoint_get(int id, hdbg_breakpoint_t *bp);
int hdbg_breakpoint_list(hdbg_breakpoint_t *bps, int max);

/* 观察点 */
int hdbg_watchpoint_add(uint32_t address, int size, int type);
int hdbg_watchpoint_remove(int id);
int hdbg_watchpoint_list(hdbg_watchpoint_t *wps, int max);

/* 内存/寄存器 */
int hdbg_read_memory(uint32_t address, void *buf, int size);
int hdbg_write_memory(uint32_t address, const void *buf, int size);
int hdbg_read_registers(hdbg_registers_t *regs);
int hdbg_write_register(int reg, uint32_t value);
int hdbg_disassemble(uint32_t address, int count, char *output, int max);

/* 调用栈/线程 */
int hdbg_get_callstack(hdbg_stack_frame_t *frames, int max);
int hdbg_list_threads(hdbg_thread_t *threads, int max);
int hdbg_switch_thread(int id);

/* 日志 */
int hdbg_log(int level, const char *tag, const char *message);
int hdbg_get_logs(hdbg_log_entry_t *logs, int max);
int hdbg_clear_logs(void);

/* 性能分析 */
int hdbg_perf_start(void);
int hdbg_perf_stop(void);
int hdbg_perf_get(hdbg_perf_counters_t *counters);
int hdbg_perf_reset(void);

/* 模拟器 */
int hdbg_emulator_start(const hdbg_emulator_config_t *config);
int hdbg_emulator_stop(void);
int hdbg_emulator_get_config(hdbg_emulator_config_t *config);
int hdbg_emulator_set_property(const char *key, const char *value);
int hdbg_emulator_send_key(int keycode);
int hdbg_emulator_touch(int x, int y, int action);

/* 状态查询 */
int hdbg_get_state(hdbg_state_t *state);
int hdbg_get_stats(hug_debugger_t *stats);

#endif /* HUG_DEBUGGER_H */
