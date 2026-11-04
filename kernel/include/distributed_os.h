#ifndef DISTRIBUTED_OS_H
#define DISTRIBUTED_OS_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 分布式设备互联
 * 多设备发现 / 协同计算 / 资源共享 / 分布式任务 / 数据同步
 * ========================================================================== */

#define DOS_MAX_DEVICES 32
#define DOS_MAX_TASKS 64
#define DOS_MAX_SERVICES 32
#define DOS_MAX_FILES 128
#define DOS_NAME_LEN 128

/* 设备类型 */
typedef enum {
    DOS_DEV_PHONE = 0,
    DOS_DEV_TABLET,
    DOS_DEV_LAPTOP,
    DOS_DEV_DESKTOP,
    DOS_DEV_TV,
    DOS_DEV_WATCH,
    DOS_DEV_EARBUDS,
    DOS_DEV_GLASSES,
    DOS_DEV_CAR,
    DOS_DEV_IOT,
    DOS_DEV_SERVER,
} dos_device_type_t;

/* 设备状态 */
typedef enum {
    DOS_DEV_OFFLINE = 0,
    DOS_DEV_ONLINE,
    DOS_DEV_CONNECTING,
    DOS_DEV_CONNECTED,
    DOS_DEV_BUSY,
    DOS_DEV_SLEEPING,
} dos_device_state_t;

/* 连接类型 */
typedef enum {
    DOS_CONN_WIFI = 0,
    DOS_CONN_BLUETOOTH,
    DOS_CONN_USB,
    DOS_CONN_5G,
    DOS_CONN_LAN,
    DOS_CONN_P2P,
} dos_conn_type_t;

/* 任务状态 */
typedef enum {
    DOS_TASK_PENDING = 0,
    DOS_TASK_RUNNING,
    DOS_TASK_COMPLETED,
    DOS_TASK_FAILED,
    DOS_TASK_CANCELLED,
    DOS_TASK_MIGRATING,
} dos_task_state_t;

/* 任务优先级 */
typedef enum {
    DOS_PRIO_LOW = 0,
    DOS_PRIO_NORMAL,
    DOS_PRIO_HIGH,
    DOS_PRIO_URGENT,
} dos_priority_t;

/* 服务类型 */
typedef enum {
    DOS_SVC_COMPUTE = 0,   /* 计算资源 */
    DOS_SVC_STORAGE,       /* 存储资源 */
    DOS_SVC_NETWORK,       /* 网络资源 */
    DOS_SVC_GPU,           /* GPU资源 */
    DOS_SVC_NPU,           /* NPU资源 */
    DOS_SVC_DISPLAY,       /* 显示资源 */
    DOS_SVC_AUDIO,         /* 音频资源 */
    DOS_SVC_SENSOR,        /* 传感器资源 */
    DOS_SVC_CAMERA,        /* 相机资源 */
    DOS_SVC_FILE,          /* 文件共享 */
    DOS_SVC_CLIPBOARD,     /* 剪贴板共享 */
    DOS_SVC_NOTIFICATION,  /* 通知同步 */
} dos_service_type_t;

/* 设备信息 */
typedef struct {
    int used;
    int id;
    char name[DOS_NAME_LEN];
    char device_id[64];
    dos_device_type_t type;
    dos_device_state_t state;
    dos_conn_type_t conn_type;
    /* 网络 */
    uint32_t ip_addr;
    uint16_t port;
    char mac_addr[18];
    /* 能力 */
    int cpu_cores;
    float cpu_ghz;
    uint64_t memory_total;
    uint64_t memory_free;
    uint64_t storage_total;
    uint64_t storage_free;
    int gpu_available;
    int npu_available;
    /* 信号 */
    int signal_strength;
    int latency_ms;
    int bandwidth_mbps;
    /* 认证 */
    int authenticated;
    int trust_level;  /* 0-100 */
    /* 服务 */
    int services[16];  /* 支持的服务类型 */
    int service_count;
} dos_device_t;

/* 分布式任务 */
typedef struct {
    int used;
    int id;
    char name[DOS_NAME_LEN];
    dos_task_state_t state;
    dos_priority_t priority;
    int device_id;       /* 执行设备 */
    int origin_device;   /* 发起设备 */
    /* 负载 */
    uint32_t code_size;
    uint32_t data_size;
    uint32_t result_size;
    /* 时间 */
    uint64_t submit_time;
    uint64_t start_time;
    uint64_t complete_time;
    /* 统计 */
    uint64_t cpu_time;
    uint64_t memory_used;
    int progress;  /* 0-100 */
    /* 回调 */
    int callback_id;
} dos_task_t;

/* 分布式服务 */
typedef struct {
    int used;
    int id;
    char name[DOS_NAME_LEN];
    dos_service_type_t type;
    int device_id;
    int available;
    uint32_t capacity;
    uint32_t used_capacity;
    int latency_ms;
} dos_service_t;

/* 共享文件 */
typedef struct {
    int used;
    int id;
    char name[DOS_NAME_LEN];
    char path[256];
    uint64_t size;
    int device_id;
    int shared;
    int permissions;  /* 0=read,1=write,2=readwrite */
} dos_file_t;

/* 同步配置 */
typedef struct {
    int clipboard_sync;
    int notification_sync;
    int file_sync;
    int photo_sync;
    int contact_sync;
    int message_sync;
    int call_sync;
    int auto_continue;  /* 应用接力 */
} dos_sync_config_t;

/* 分布式OS状态 */
typedef struct {
    int initialized;
    int enabled;
    int discovery_running;
    /* 设备 */
    dos_device_t devices[DOS_MAX_DEVICES];
    int device_count;
    int local_device_id;
    /* 任务 */
    dos_task_t tasks[DOS_MAX_TASKS];
    int task_count;
    /* 服务 */
    dos_service_t services[DOS_MAX_SERVICES];
    int service_count;
    /* 文件 */
    dos_file_t files[DOS_MAX_FILES];
    int file_count;
    /* 同步 */
    dos_sync_config_t sync;
    /* 统计 */
    uint64_t total_tasks;
    uint64_t completed_tasks;
    uint64_t data_transferred;
    uint64_t sync_count;
} distributed_os_t;

/* 初始化 */
void distributed_os_init(void);

/* 设备发现 */
int dos_discovery_start(void);
int dos_discovery_stop(void);
int dos_device_register(dos_device_t *dev);
int dos_device_unregister(int device_id);
int dos_device_get(int device_id, dos_device_t *dev);
int dos_device_list(dos_device_t *devs, int max);
int dos_device_connect(int device_id);
int dos_device_disconnect(int device_id);
int dos_device_get_local(dos_device_t *dev);
int dos_device_set_trust(int device_id, int level);
int dos_device_authenticate(int device_id);

/* 分布式任务 */
int dos_task_submit(const char *name, int device_id, dos_priority_t priority,
                    uint32_t code_size, uint32_t data_size);
int dos_task_cancel(int task_id);
int dos_task_get(int task_id, dos_task_t *task);
int dos_task_list(dos_task_t *tasks, int max);
int dos_task_migrate(int task_id, int new_device);
int dos_task_complete(int task_id, uint32_t result_size);
int dos_task_get_progress(int task_id, int *progress);

/* 服务管理 */
int dos_service_register(const char *name, dos_service_type_t type,
                         int device_id, uint32_t capacity);
int dos_service_unregister(int service_id);
int dos_service_get(int service_id, dos_service_t *svc);
int dos_service_list(dos_service_t *svcs, int max);
int dos_service_find(dos_service_type_t type, dos_service_t *svcs, int max);
int dos_service_allocate(int service_id, uint32_t amount);
int dos_service_release(int service_id, uint32_t amount);

/* 资源调度 */
int dos_schedule_task(dos_task_t *task, int *best_device);
int dos_get_best_device(dos_service_type_t service, int *device_id);
int dos_load_balance(void);

/* 文件共享 */
int dos_file_share(const char *path, int device_id, int permissions);
int dos_file_unshare(int file_id);
int dos_file_list(dos_file_t *files, int max);
int dos_file_get(int file_id, dos_file_t *file);

/* 数据同步 */
int dos_sync_set_config(const dos_sync_config_t *config);
int dos_sync_get_config(dos_sync_config_t *config);
int dos_sync_clipboard(const char *text);
int dos_sync_notification(const char *title, const char *text, int device_id);
int dos_sync_file(const char *path, int device_id);
int dos_continue_app(const char *app_id, int from_device, int to_device);

/* 统计 */
int dos_get_stats(distributed_os_t *stats);

#endif /* DISTRIBUTED_OS_H */
