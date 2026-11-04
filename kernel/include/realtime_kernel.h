#ifndef REALTIME_KERNEL_H
#define REALTIME_KERNEL_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 实时内核深化
 * 硬实时调度 / 确定性延迟 / CAN总线 / TSN时间敏感网络
 * ========================================================================== */

#define RT_MAX_TASKS 128
#define RT_MAX_TIMERS 64
#define RT_MAX_CAN_BUSES 4
#define RT_MAX_CAN_MSGS 256
#define RT_MAX_TSN_STREAMS 32
#define RT_NAME_LEN 128

/* 实时任务优先级 */
typedef enum {
    RTK_PRIO_CRITICAL = 0,  /* 最高优先级，硬实时 */
    RTK_PRIO_HIGH,
    RTK_PRIO_MEDIUM,
    RTK_PRIO_LOW,
    RTK_PRIO_BACKGROUND,
} rtk_priority_t;

/* 任务状态 */
typedef enum {
    RTK_TASK_CREATED = 0,
    RTK_TASK_READY,
    RTK_TASK_RUNNING,
    RTK_TASK_BLOCKED,
    RTK_TASK_SUSPENDED,
    RTK_TASK_COMPLETED,
    RTK_TASK_ERROR,
} rtk_task_state_t;

/* 调度策略 */
typedef enum {
    RTK_SCHED_FIFO = 0,     /* 先进先出 */
    RTK_SCHED_RR,           /* 轮询 */
    RTK_SCHED_EDF,          /* 最早截止期优先 */
    RTK_SCHED_RMS,          /* 速率单调调度 */
    RTK_SCHED_DMS,          /* 截止期单调调度 */
} rtk_sched_policy_t;

/* CAN帧格式 */
typedef enum {
    RTK_CAN_STD = 0,  /* 标准帧 11位ID */
    RTK_CAN_EXT,      /* 扩展帧 29位ID */
    RTK_CAN_FD,       /* CAN FD */
} rtk_can_format_t;

/* TSN调度类型 */
typedef enum {
    RTK_TSN_AVB = 0,       /* 音视频桥接 */
    RTK_TSN_TAS,           /* 时间感知整形 */
    RTK_TSN_CBS,           /* 信用整形 */
    RTK_TSN_SRP,           /* 流预留协议 */
    RTK_TSN_FRER,          /* 帧复制消除 */
    RTK_TSN_DCB,           /* 数据中心桥接 */
} rtk_tsn_type_t;

/* 实时任务 */
typedef struct {
    int used;
    int id;
    char name[RT_NAME_LEN];
    rtk_priority_t priority;
    rtk_sched_policy_t policy;
    rtk_task_state_t state;
    /* 时序 */
    uint64_t period_ns;       /* 周期 */
    uint64_t deadline_ns;     /* 截止期 */
    uint64_t execution_ns;    /* 执行时间 */
    uint64_t remaining_ns;    /* 剩余执行时间 */
    uint64_t next_release_ns; /* 下次释放时间 */
    uint64_t last_start_ns;
    uint64_t last_finish_ns;
    /* 统计 */
    uint64_t exec_count;
    uint64_t deadline_misses;
    uint64_t total_exec_ns;
    uint64_t max_exec_ns;
    uint64_t min_exec_ns;
    float avg_jitter_ns;
    /* CPU亲和性 */
    int cpu_affinity;
    int cpu_assigned;
} rtk_task_t;

/* 高精度定时器 */
typedef struct {
    int used;
    int id;
    char name[RT_NAME_LEN];
    uint64_t period_ns;
    uint64_t next_fire_ns;
    int periodic;
    int active;
    void (*callback)(void);
    uint64_t fire_count;
} rtk_timer_t;

/* CAN总线 */
typedef struct {
    int used;
    int id;
    char name[RT_NAME_LEN];
    int enabled;
    uint32_t bitrate;        /* bps */
    rtk_can_format_t format;
    int fd_enabled;
    uint32_t fd_bitrate;
    /* 统计 */
    uint64_t tx_frames;
    uint64_t rx_frames;
    uint64_t tx_errors;
    uint64_t rx_errors;
    uint64_t bus_off_count;
    int error_state;  /* 0=active,1=passive,2=bus-off */
    uint16_t tx_error_counter;
    uint16_t rx_error_counter;
} rtk_can_bus_t;

/* CAN消息 */
typedef struct {
    int used;
    int id;
    uint32_t can_id;
    rtk_can_format_t format;
    uint8_t data[64];
    uint8_t dlc;
    uint64_t timestamp_ns;
    int bus_id;
    int is_rx;
} rtk_can_msg_t;

/* TSN流 */
typedef struct {
    int used;
    int id;
    char name[RT_NAME_LEN];
    rtk_tsn_type_t type;
    uint64_t stream_id;
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t vlan_id;
    uint8_t pcp;  /* 优先级 0-7 */
    /* QoS */
    uint64_t period_ns;
    uint32_t max_frame_size;
    uint32_t max_interval_frames;
    uint64_t max_end_end_latency_ns;
    /* 门控 */
    int gate_enabled;
    uint64_t gate_open_ns;
    uint64_t gate_close_ns;
    /* 统计 */
    uint64_t tx_frames;
    uint64_t rx_frames;
    uint64_t dropped_frames;
    uint64_t avg_latency_ns;
    uint64_t max_latency_ns;
} rtk_tsn_stream_t;

/* 实时内核配置 */
typedef struct {
    int enabled;
    rtk_sched_policy_t default_policy;
    int preemption_enabled;
    int priority_inheritance;
    int interrupt_threading;
    uint64_t tick_ns;         /* 时钟节拍 */
    uint64_t max_latency_us;  /* 最大可接受延迟 */
    int cpu_partitioning;     /* CPU分区隔离 */
    int watchdog_enabled;
    uint64_t watchdog_timeout_ms;
} rtk_config_t;

/* 实时内核状态 */
typedef struct {
    int initialized;
    int enabled;
    rtk_config_t config;
    /* 任务 */
    rtk_task_t tasks[RT_MAX_TASKS];
    int task_count;
    int running_task;
    /* 定时器 */
    rtk_timer_t timers[RT_MAX_TIMERS];
    int timer_count;
    /* CAN */
    rtk_can_bus_t can_buses[RT_MAX_CAN_BUSES];
    int can_count;
    /* TSN */
    rtk_tsn_stream_t tsn_streams[RT_MAX_TSN_STREAMS];
    int tsn_count;
    /* 统计 */
    uint64_t total_context_switches;
    uint64_t total_interrupts;
    uint64_t max_irq_latency_ns;
    uint64_t max_sched_latency_ns;
    uint64_t total_deadline_misses;
    uint64_t uptime_ns;
} rtk_kernel_t;

/* 初始化 */
void rtk_kernel_init(void);

/* 配置 */
int rtk_set_config(const rtk_config_t *config);
int rtk_get_config(rtk_config_t *config);

/* 任务管理 */
int rtk_task_create(const char *name, rtk_priority_t prio, rtk_sched_policy_t policy,
                   uint64_t period_ns, uint64_t deadline_ns, void (*entry)(void));
int rtk_task_destroy(int task_id);
int rtk_task_suspend(int task_id);
int rtk_task_resume(int task_id);
int rtk_task_yield(void);
int rtk_task_get(int task_id, rtk_task_t *task);
int rtk_task_list(rtk_task_t *tasks, int max);
int rtk_task_set_priority(int task_id, rtk_priority_t prio);
int rtk_task_set_affinity(int task_id, int cpu);

/* 调度 */
int rtk_schedule(void);
int rtk_get_current_task(rtk_task_t *task);
int rtk_get_worst_case_latency(uint64_t *latency_ns);

/* 定时器 */
int rtk_timer_create(const char *name, uint64_t period_ns, int periodic, void (*callback)(void));
int rtk_timer_start(int timer_id);
int rtk_timer_stop(int timer_id);
int rtk_timer_destroy(int timer_id);
int rtk_timer_get(int timer_id, rtk_timer_t *timer);

/* CAN总线 */
int rtk_can_init(int bus_id, uint32_t bitrate, rtk_can_format_t format);
int rtk_can_shutdown(int bus_id);
int rtk_can_send(int bus_id, uint32_t can_id, uint8_t *data, uint8_t len);
int rtk_can_receive(int bus_id, rtk_can_msg_t *msg);
int rtk_can_get_bus(int bus_id, rtk_can_bus_t *bus);
int rtk_can_set_bitrate(int bus_id, uint32_t bitrate);
int rtk_can_get_stats(int bus_id, uint64_t *tx, uint64_t *rx, uint64_t *errors);

/* TSN */
int rtk_tsn_create_stream(const char *name, rtk_tsn_type_t type, uint16_t vlan_id,
                         uint8_t pcp, uint64_t period_ns, uint32_t max_size);
int rtk_tsn_destroy_stream(int stream_id);
int rtk_tsn_send(int stream_id, uint8_t *data, int len);
int rtk_tsn_receive(int stream_id, uint8_t *data, int max);
int rtk_tsn_get_stream(int stream_id, rtk_tsn_stream_t *stream);
int rtk_tsn_list_streams(rtk_tsn_stream_t *streams, int max);
int rtk_tsn_set_gate(int stream_id, uint64_t open_ns, uint64_t close_ns);

/* 延迟测量 */
int rtk_measure_irq_latency(uint64_t *latency_ns);
int rtk_measure_sched_latency(uint64_t *latency_ns);
int rtk_get_jitter(int task_id, float *jitter_ns);

/* 看门狗 */
int rtk_watchdog_start(uint64_t timeout_ms);
int rtk_watchdog_feed(void);
int rtk_watchdog_stop(void);

/* 统计 */
int rtk_get_stats(rtk_kernel_t *stats);

#endif /* REALTIME_KERNEL_H */
