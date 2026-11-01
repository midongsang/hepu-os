#ifndef KDEBUG_H
#define KDEBUG_H

#include <stdint.h>

#define KDBG_MAX_BREAKPOINTS 32
#define KDBG_MAX_WATCHPOINTS 16
#define KDBG_MAX_BACKTRACE 32
#define KDBG_MAX_REGISTERS 32
#define KDBG_MAX_COMMANDS 32
#define KDBG_MAX_BREAK_LOG 256

/* 断点类型 */
typedef enum {
    KDBG_BREAKPOINT_SOFTWARE = 0,
    KDBG_BREAKPOINT_HARDWARE,
    KDBG_BREAKPOINT_CONDITIONAL,
} kdbg_kdbg_breakpoint_type_t;

/* 断点 */
typedef struct {
    int used;
    int id;
    kdbg_kdbg_breakpoint_type_t type;
    uint64_t address;
    int enabled;
    int hit_count;
    uint64_t last_hit_time;
    char condition[128];
    char command[256];  /* 命中时执行的命令 */
    int temporary;  /* 临时断点,命中一次后删除 */
} kdbg_breakpoint_t;

/* 观察点 */
typedef struct {
    int used;
    int id;
    uint64_t address;
    int length;  /* 1/2/4/8字节 */
    int type;  /* 0=read,1=write,2=read/write */
    int enabled;
    int hit_count;
} kdbg_watchpoint_t;

/* 寄存器 */
typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rip, rflags;
    uint64_t cs, ds, es, fs, gs, ss;
    uint64_t cr0, cr2, cr3, cr4;
    uint64_t dr0, dr1, dr2, dr3, dr6, dr7;
    uint64_t efer;
} registers_t;

/* 栈帧 */
typedef struct {
    uint64_t address;
    uint64_t frame_pointer;
    uint64_t return_address;
    char function_name[64];
    uint64_t function_offset;
} stack_frame_t;

/* 调试命令 */
typedef struct {
    char name[32];
    char description[128];
    char usage[128];
    int (*handler)(const char *args);
} debug_command_t;

/* 调试状态 */
typedef enum {
    DEBUG_STATE_RUNNING = 0,
    DEBUG_STATE_STOPPED,
    DEBUG_STATE_SINGLE_STEP,
    DEBUG_STATE_STEPPING_OVER,
    DEBUG_STATE_STEPPING_OUT,
    DEBUG_STATE_CONTINUING,
} debug_state_t;

/* 断点命中日志 */
typedef struct {
    uint64_t address;
    uint64_t timestamp;
    int breakpoint_id;
    registers_t regs;
} break_log_t;

/* 内存区域 */
typedef struct {
    uint64_t start;
    uint64_t end;
    int permissions;  /* 0=none,1=read,2=write,4=exec */
    char name[64];
} memory_region_t;

/* 内核调试器状态 */
typedef struct {
    int initialized;
    int enabled;
    debug_state_t state;
    /* 断点 */
    kdbg_breakpoint_t breakpoints[KDBG_MAX_BREAKPOINTS];
    int breakpoint_count;
    /* 观察点 */
    kdbg_watchpoint_t watchpoints[KDBG_MAX_WATCHPOINTS];
    int watchpoint_count;
    /* 寄存器 */
    registers_t registers;
    int registers_valid;
    /* 栈回溯 */
    stack_frame_t backtrace[KDBG_MAX_BACKTRACE];
    int backtrace_depth;
    /* 命令 */
    debug_command_t commands[KDBG_MAX_COMMANDS];
    int command_count;
    /* 日志 */
    break_log_t break_log[KDBG_MAX_BREAK_LOG];
    int break_log_count;
    /* GDB stub */
    int gdb_stub_enabled;
    int gdb_connected;
    char gdb_protocol[16];  /* "gdb" / "rsp" */
    int gdb_port;
    /* 单步 */
    int single_step_enabled;
    uint64_t step_over_address;
    /* 统计 */
    uint64_t total_breaks;
    uint64_t total_steps;
    uint64_t total_continues;
    uint64_t total_memory_reads;
    uint64_t total_memory_writes;
} kdebug_t;

/* 初始化调试器 */
void kdebug_init(void);

/* 断点管理 */
int kdbg_breakpoint_add(uint64_t address, kdbg_kdbg_breakpoint_type_t type);
int kdbg_breakpoint_remove(int bp_id);
int kdbg_breakpoint_enable(int bp_id);
int kdbg_breakpoint_disable(int bp_id);
int kdbg_breakpoint_list(kdbg_breakpoint_t *bps, int max);
int kdbg_breakpoint_get(int bp_id, kdbg_breakpoint_t *bp);
int kdbg_breakpoint_set_condition(int bp_id, const char *condition);
int kdbg_breakpoint_set_command(int bp_id, const char *command);

/* 观察点 */
int kdbg_watchpoint_add(uint64_t address, int length, int type);
int kdbg_watchpoint_remove(int wp_id);
int kdbg_watchpoint_list(kdbg_watchpoint_t *wps, int max);

/* 执行控制 */
int kdbg_continue(void);
int kdbg_step(void);
int kdbg_step_over(void);
int kdbg_step_out(void);
int kdbg_stop(void);
int kdbg_get_state(debug_state_t *state);

/* 寄存器 */
int kdbg_registers_get(registers_t *regs);
int kdbg_registers_set(const registers_t *regs);
int kdbg_register_get(const char *name, uint64_t *value);
int kdbg_register_set(const char *name, uint64_t value);

/* 内存 */
int kdbg_memory_read(uint64_t address, void *buffer, int size);
int kdbg_memory_write(uint64_t address, const void *buffer, int size);
int kdbg_memory_dump(uint64_t address, int rows, int cols);
int kdbg_memory_search(uint64_t start, uint64_t end, const void *pattern, int size, uint64_t *result);

/* 栈回溯 */
int kdbg_backtrace(int max_depth);
int kdbg_backtrace_get(stack_frame_t *frames, int max);
int kdbg_backtrace_print(void);

/* 符号 */
int kdbg_symbol_lookup(uint64_t address, char *name, int max, uint64_t *offset);
int kdbg_symbol_resolve(const char *name, uint64_t *address);

/* 命令 */
int kdbg_command_register(const char *name, const char *desc, const char *usage, int (*handler)(const char*));
int kdbg_command_execute(const char *command);
int kdbg_command_list(debug_command_t *commands, int max);

/* GDB stub */
int kdbg_gdb_start(int port);
int kdbg_gdb_stop(void);
int kdbg_gdb_handle_packet(const char *packet, char *response, int max_response);
int kdbg_gdb_get_status(int *connected, int *port);

/* 统计 */
int kdbg_get_stats(kdebug_t *stats);

#endif /* KDEBUG_H */
