#ifndef APP_CONTAINER_H
#define APP_CONTAINER_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 容器化应用隔离
 * 轻量级沙箱 / 命名空间 / cgroups / 能力限制 / 资源配额
 * ========================================================================== */

#define AC_MAX_CONTAINERS 64
#define AC_MAX_MOUNTS 32
#define AC_MAX_CAPS 64
#define AC_NAME_LEN 128

/* 容器状态 */
typedef enum {
    AC_STATE_CREATED = 0,
    AC_STATE_RUNNING,
    AC_STATE_PAUSED,
    AC_STATE_STOPPED,
    AC_STATE_ERROR,
} ac_state_t;

/* 命名空间类型 */
typedef enum {
    AC_NS_MOUNT = 0,
    AC_NS_PID,
    AC_NS_NET,
    AC_NS_USER,
    AC_NS_IPC,
    AC_NS_UTS,
    AC_NS_CGROUP,
} ac_ns_type_t;

/* 隔离级别 */
typedef enum {
    AC_ISO_NONE = 0,      /* 无隔离 */
    AC_ISO_LIGHT,         /* 轻隔离（文件系统+权限） */
    AC_ISO_STANDARD,      /* 标准（+命名空间+cgroups） */
    AC_ISO_STRICT,        /* 严格（+系统调用过滤+网络隔离） */
    AC_ISO_FULL,          /* 完全（+虚拟化+独立内核） */
} ac_isolation_t;

/* 挂载点 */
typedef struct {
    int used;
    char source[256];
    char target[256];
    int readonly;
    int type;  /* 0=bind,1=tmpfs,2=overlay */
} ac_mount_t;

/* 能力限制 */
typedef struct {
    int network_access;
    int file_write;
    int camera;
    int microphone;
    int location;
    int contacts;
    int sms;
    int phone;
    int storage;
    int bluetooth;
    int nfc;
    int sensors;
    int background_run;
    int auto_start;
    int notification;
    int overlay;
} ac_caps_t;

/* 资源配额 */
typedef struct {
    uint64_t memory_limit;     /* 内存上限 bytes */
    uint64_t memory_swap;      /* swap上限 */
    int cpu_shares;            /* CPU份额 */
    float cpu_quota;           /* CPU配额（核数） */
    int cpu_affinity[8];       /* CPU亲和性 */
    uint64_t disk_limit;       /* 磁盘上限 */
    int io_priority;           /* IO优先级 */
    int process_limit;         /* 进程数上限 */
    int fd_limit;              /* 文件描述符上限 */
    uint64_t network_bandwidth; /* 网络带宽 bps */
} ac_quota_t;

/* 资源使用 */
typedef struct {
    uint64_t memory_used;
    uint64_t memory_peak;
    float cpu_usage;
    uint64_t disk_used;
    uint64_t network_rx;
    uint64_t network_tx;
    int process_count;
    int fd_count;
    uint64_t uptime;
} ac_usage_t;

/* 容器 */
typedef struct {
    int used;
    int id;
    char name[AC_NAME_LEN];
    char app_id[AC_NAME_LEN];
    char image[256];
    ac_state_t state;
    ac_isolation_t isolation;
    /* 命名空间 */
    int namespaces[8];  /* 每种命名空间是否启用 */
    /* 挂载 */
    ac_mount_t mounts[AC_MAX_MOUNTS];
    int mount_count;
    /* 能力 */
    ac_caps_t caps;
    /* 配额 */
    ac_quota_t quota;
    /* 使用 */
    ac_usage_t usage;
    /* 进程 */
    int pid;
    int root_pid;
    /* 安全 */
    int seccomp_enabled;
    int selinux_context;
    int apparmor_profile;
    /* 统计 */
    uint64_t start_time;
    uint64_t total_restarts;
} ac_container_t;

/* 容器配置 */
typedef struct {
    char name[AC_NAME_LEN];
    char app_id[AC_NAME_LEN];
    char image[256];
    ac_isolation_t isolation;
    ac_caps_t caps;
    ac_quota_t quota;
    int auto_start;
    int restart_policy;  /* 0=no,1=on-failure,2=always */
} ac_config_t;

/* 容器管理器状态 */
typedef struct {
    int initialized;
    int enabled;
    ac_container_t containers[AC_MAX_CONTAINERS];
    int container_count;
    int running_count;
    uint64_t total_memory_allocated;
    uint64_t total_disk_allocated;
} app_container_t;

/* 初始化 */
void app_container_init(void);

/* 容器生命周期 */
int ac_create(const ac_config_t *config);
int ac_destroy(int container_id);
int ac_start(int container_id);
int ac_stop(int container_id);
int ac_pause(int container_id);
int ac_resume(int container_id);
int ac_restart(int container_id);
int ac_get(int container_id, ac_container_t *cont);
int ac_list(ac_container_t *conts, int max);
int ac_get_by_app(const char *app_id, ac_container_t *cont);

/* 命名空间 */
int ac_ns_enable(int container_id, ac_ns_type_t ns);
int ac_ns_disable(int container_id, ac_ns_type_t ns);
int ac_ns_is_enabled(int container_id, ac_ns_type_t ns);

/* 挂载 */
int ac_mount_add(int container_id, const char *source, const char *target, int readonly);
int ac_mount_remove(int container_id, const char *target);
int ac_mount_list(int container_id, ac_mount_t *mounts, int max);

/* 能力 */
int ac_caps_set(int container_id, const ac_caps_t *caps);
int ac_caps_get(int container_id, ac_caps_t *caps);
int ac_cap_grant(int container_id, const char *cap);
int ac_cap_revoke(int container_id, const char *cap);

/* 配额 */
int ac_quota_set(int container_id, const ac_quota_t *quota);
int ac_quota_get(int container_id, ac_quota_t *quota);
int ac_quota_set_memory(int container_id, uint64_t limit);
int ac_quota_set_cpu(int container_id, float quota);
int ac_quota_set_disk(int container_id, uint64_t limit);

/* 资源使用 */
int ac_usage_get(int container_id, ac_usage_t *usage);
int ac_usage_update(int container_id);

/* 安全 */
int ac_seccomp_set(int container_id, int enable);
int ac_selinux_set_context(int container_id, int context);

/* 事件 */
int ac_on_app_launch(const char *app_id);
int ac_on_app_exit(const char *app_id);

/* 统计 */
int ac_get_stats(app_container_t *stats);

#endif /* APP_CONTAINER_H */
