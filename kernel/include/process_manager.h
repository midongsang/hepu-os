/* ============================================================================
 * 赫普OS - 进程管理器（任务管理器）（V8.73新增）
 * 进程列表、CPU/内存监控、进程管理
 * ========================================================================== */

#ifndef _PROCESS_MANAGER_H
#define _PROCESS_MANAGER_H

#include <stdint.h>

/* 进程状态 */
typedef enum {
    PROC_STATE_RUNNING = 0,
    PROC_STATE_READY,
    PROC_STATE_BLOCKED,
    PROC_STATE_SLEEPING,
    PROC_STATE_ZOMBIE,
    PROC_STATE_STOPPED,
    PROC_STATE_DEAD
} proc_state_t;

/* 进程信息 */
typedef struct {
    uint32_t pid;
    uint32_t ppid;
    char     name[64];
    proc_state_t state;
    uint32_t priority;
    uint64_t cpu_time_ms;     /* CPU时间（毫秒） */
    uint32_t memory_usage_kb; /* 内存使用（KB） */
    uint32_t threads;
    uint32_t open_files;
    uint8_t  user_id;
    uint8_t  group_id;
    uint64_t start_time;
} proc_info_t;

/* 系统资源信息 */
typedef struct {
    uint32_t total_memory_kb;
    uint32_t used_memory_kb;
    uint32_t free_memory_kb;
    uint32_t cached_memory_kb;
    uint32_t total_swap_kb;
    uint32_t used_swap_kb;
    float    cpu_usage_percent;
    uint32_t cpu_cores;
    uint32_t active_processes;
    uint32_t total_processes;
    uint32_t active_threads;
    uint64_t uptime_ms;
    uint32_t load_average[3]; /* 1/5/15分钟 */
} system_resources_t;

/* 进程管理器状态 */
typedef struct {
    proc_info_t *processes;
    int          process_count;
    int          max_processes;
    int          selected_index;
    int          scroll_offset;
    system_resources_t resources;
    int          initialized;
    int          sort_column;    /* 0=PID, 1=CPU, 2=内存, 3=名称 */
    int          sort_ascending;
    int          refresh_interval_ms;
    uint64_t     last_refresh;
} process_manager_t;

/* 初始化进程管理器 */
int process_manager_init(void);

/* 关闭进程管理器 */
void process_manager_shutdown(void);

/* 刷新进程列表 */
int process_manager_refresh(void);

/* 获取进程列表 */
int process_manager_get_processes(proc_info_t *procs, int max_count);

/* 获取进程信息 */
int process_manager_get_process_info(uint32_t pid, proc_info_t *info);

/* 获取系统资源信息 */
int process_manager_get_resources(system_resources_t *res);

/* 终止进程 */
int process_manager_kill(uint32_t pid, int signal);

/* 暂停进程 */
int process_manager_stop(uint32_t pid);

/* 恢复进程 */
int process_manager_continue(uint32_t pid);

/* 改变进程优先级 */
int process_manager_set_priority(uint32_t pid, int priority);

/* 选择进程 */
void process_manager_select(int index);

/* 滚动 */
void process_manager_scroll(int lines);

/* 排序 */
void process_manager_sort(int column);

/* 绘制UI */
void process_manager_draw_ui(int x, int y, int width, int height);

/* 处理键盘 */
void process_manager_handle_key(char key);

/* 获取管理器状态 */
int process_manager_get_state(process_manager_t *state);

#endif /* _PROCESS_MANAGER_H */
