#ifndef SERVICES_H
#define SERVICES_H

#include <stdint.h>

#define SERVICE_MAX 32
#define SERVICE_NAME_LEN 32
#define SERVICE_DESC_LEN 64
#define SERVICE_MAX_DEPS 8

/* 服务状态（状态机生命周期） */
typedef enum {
    SERVICE_INACTIVE = 0,
    SERVICE_ACTIVATING,
    SERVICE_ACTIVE,
    SERVICE_DEACTIVATING,
    SERVICE_FAILED,
} service_state_t;

/* 服务健康状态（第二阶段：状态机接受请求 ≠ 后端健康）。
 * - HEALTH_STUB：无真实 daemon/进程，仅状态机空壳；
 * - HEALTH_HEALTHY：真实后端已拉起并自检通过；
 * - HEALTH_FAILED：后端拉起失败 / 超过重试上限后放弃。
 */
typedef enum {
    HEALTH_UNKNOWN = 0,
    HEALTH_STUB,
    HEALTH_HEALTHY,
    HEALTH_FAILED,
} service_health_t;

/* 服务类型 */
typedef enum {
    SERVICE_TYPE_SIMPLE = 0,  /* 长期运行进程 */
    SERVICE_TYPE_ONESHOT,     /* 一次性执行 */
    SERVICE_TYPE_DAEMON,      /* 守护进程 */
    SERVICE_TYPE_SOCKET,      /* 套接字激活 */
} service_type_t;

/* 服务启动策略 */
typedef enum {
    SERVICE_START_BOOT = 0,   /* 开机启动 */
    SERVICE_START_DEMAND,     /* 按需启动 */
    SERVICE_START_MANUAL,     /* 手动启动 */
} service_start_t;

/* 服务定义 */
typedef struct {
    char name[SERVICE_NAME_LEN];
    char description[SERVICE_DESC_LEN];
    service_type_t type;
    service_start_t start_policy;
    int (*start_func)(void);
    int (*stop_func)(void);
    int (*status_func)(void);
    char depends[SERVICE_MAX_DEPS][SERVICE_NAME_LEN];
    int dep_count;
    int pid;
    int enabled;
    int restart_on_failure;
    int stub;                 /* 1=空壳：未拉起真实 daemon/进程，仅状态机 */
    int max_retries;          /* 失败后自动重试上限；<=0 用模块默认值 */
} service_def_t;

/* 服务运行时状态 */
typedef struct {
    int used;
    service_def_t def;
    service_state_t state;
    service_health_t health;
    uint64_t start_time;
    uint64_t stop_time;
    int exit_code;
    int restart_count;        /* 历史累计启动次数（含成功） */
    int fail_count;           /* 连续失败次数 */
    int retry_attempt;        /* 已安排的自动重试次数 */
    uint32_t backoff_until;   /* 退避到期 tick；未到点不重试 */
    uint32_t memory_usage;
    uint32_t cpu_usage;
} service_t;

/* 初始化服务管理器 */
void services_init(void);

/* 注册服务 */
int services_register(service_def_t *def);

/* 启动服务 */
int services_start(const char *name);

/* 停止服务 */
int services_stop(const char *name);

/* 重启服务 */
int services_restart(const char *name);

/* 重新加载配置 */
int services_reload(const char *name);

/* 获取服务状态 */
int services_status(const char *name, service_t *status);

/* 列出所有服务 */
int services_list(service_t *list, int max);

/* 启用/禁用开机启动 */
int services_enable(const char *name);
int services_disable(const char *name);

/* 检查服务是否活跃（状态机 ACTIVE，不含健康判断） */
int services_is_active(const char *name);

/* 检查服务是否健康（HEALTH_HEALTHY 才算；STUB/FAILED 均不算） */
int services_is_healthy(const char *name);

/* 等待服务启动 */
int services_wait(const char *name, int timeout_ms);

/* 全图校验：缺依赖 / 循环依赖检测并记事件；无误返回 0，否则 -1。 */
int services_validate_graph(void);

/* 启动所有开机服务（按依赖拓扑顺序） */
int services_start_boot(void);

/* 停止所有服务 */
int services_stop_all(void);

/* 服务管理器主循环 (监控/退避重试) */
void services_monitor_tick(void);

/* 生产调度入口：主循环/等待点节流调用；内部按间隔收敛到 monitor_tick。
 * 只能在非中断上下文调用；中断里不得直接拉起 services_start。 */
void services_monitor_poll(void);

/* 获取服务数量 */
int services_count(void);

/* 获取活跃服务数量 */
int services_active_count(void);

/* 获取健康服务数量（HEALTH_HEALTHY） */
int services_healthy_count(void);

/* 测试钩子：替换单调 tick 来源；传 0 恢复平台默认时钟。 */
void services_set_clock_hook(uint64_t (*fn)(void));

#endif /* SERVICES_H */
