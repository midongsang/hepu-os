#ifndef COMPAT_H
#define COMPAT_H

#include <stdint.h>

#define COMPAT_MAX_ANDROID_APPS 32
#define COMPAT_MAX_LINUX_APPS 32
#define COMPAT_MAX_SYS_CALLS 512
#define COMPAT_MAX_LIBRARIES 64

/* 兼容层类型 */
typedef enum {
    COMPAT_NONE = 0,
    COMPAT_ANDROID,
    COMPAT_LINUX,
    COMPAT_WINDOWS,
    COMPAT_WEB,
} compat_type_t;

/* Android应用状态 */
typedef enum {
    ANDROID_APP_UNINSTALLED = 0,
    ANDROID_APP_INSTALLED,
    ANDROID_APP_RUNNING,
    ANDROID_APP_PAUSED,
    ANDROID_APP_STOPPED,
    ANDROID_APP_ERROR,
} android_app_state_t;

/* Android应用信息 */
typedef struct {
    int used;
    int id;
    char package_name[128];
    char app_name[128];
    char version_name[32];
    int version_code;
    int target_sdk;
    int min_sdk;
    android_app_state_t state;
    int pid;
    int uid;
    char data_dir[256];
    char apk_path[256];
    int permissions[32];
    int permission_count;
    int compat_mode;  /* 0=自动,1=兼容,2=高性能 */
    uint64_t install_time;
    uint64_t last_launch;
    int launch_count;
} android_app_t;

/* Linux应用信息 */
typedef struct {
    int used;
    int id;
    char name[128];
    char path[256];
    char args[256];
    char env[512];
    int pid;
    int state;  /* 0=停止,1=运行,2=暂停 */
    int exit_code;
    int compat_mode;
    uint64_t start_time;
    uint64_t cpu_time;
    int memory_usage_kb;
} linux_app_t;

/* 系统调用映射 */
typedef struct {
    int host_call;      /* 赫普OS系统调用号 */
    int android_call;   /* Android系统调用号 */
    int linux_call;     /* Linux系统调用号 */
    const char *name;
    int implemented;
    int arg_count;
} syscall_map_t;

/* 兼容库 */
typedef struct {
    int used;
    int id;
    char name[64];
    char version[32];
    compat_type_t type;
    int loaded;
    uint32_t base_address;
    uint32_t size;
    int symbol_count;
} compat_library_t;

/* 容器/沙箱 */
typedef struct {
    int id;
    compat_type_t type;
    int pid;
    char name[64];
    int isolation_level;  /* 0=无,1=轻量,2=完整 */
    int network_access;
    int file_access;
    int device_access;
    uint32_t memory_limit;  /* KB */
    int cpu_limit;  /* 百分比 */
    char rootfs[256];
    char bind_mounts[16][256];
    int mount_count;
} compat_container_t;

/* 兼容性层状态 */
typedef struct {
    int initialized;
    int android_compat_enabled;
    int linux_compat_enabled;
    /* Android应用 */
    android_app_t android_apps[COMPAT_MAX_ANDROID_APPS];
    int android_app_count;
    /* Linux应用 */
    linux_app_t linux_apps[COMPAT_MAX_LINUX_APPS];
    int linux_app_count;
    /* 系统调用映射 */
    syscall_map_t syscall_map[COMPAT_MAX_SYS_CALLS];
    int syscall_count;
    /* 兼容库 */
    compat_library_t libraries[COMPAT_MAX_LIBRARIES];
    int library_count;
    /* 容器 */
    compat_container_t containers[16];
    int container_count;
    /* 统计 */
    uint64_t total_android_launches;
    uint64_t total_linux_launches;
    int compat_warnings;
} compat_t;

/* 初始化兼容性层 */
void compat_init(void);

/* Android兼容 */
int android_compat_enable(int enable);
int android_app_install(const char *apk_path);
int android_app_uninstall(const char *package);
int android_app_launch(const char *package);
int android_app_stop(const char *package);
int android_app_get(const char *package, android_app_t *app);
int android_app_list(android_app_t *apps, int max);
int android_app_get_state(const char *package);
int android_compat_is_enabled(void);

/* Linux兼容 */
int linux_compat_enable(int enable);
int linux_app_run(const char *path, const char *args, const char *env);
int linux_app_stop(int pid);
int linux_app_get(int pid, linux_app_t *app);
int linux_app_list(linux_app_t *apps, int max);
int linux_compat_is_enabled(void);

/* 系统调用兼容 */
int compat_syscall_register(int host_call, int android_call, int linux_call, const char *name);
int compat_syscall_translate(compat_type_t type, int foreign_call);
int compat_syscall_get_map(syscall_map_t *map, int max);

/* 兼容库 */
int compat_library_load(const char *name, compat_type_t type);
int compat_library_unload(int lib_id);
int compat_library_get(int lib_id, compat_library_t *lib);
int compat_library_list(compat_library_t *libs, int max);

/* 容器管理 */
int compat_container_create(compat_type_t type, const char *name);
int compat_container_destroy(int container_id);
int compat_container_start(int container_id);
int compat_container_stop(int container_id);
int compat_container_get(int container_id, compat_container_t *container);
int compat_container_list(compat_container_t *containers, int max);

/* 桥接函数 */
int compat_bridge_call(compat_type_t type, const char *function, uint32_t *args, int arg_count, uint32_t *result);
int compat_convert_path(compat_type_t type, const char *foreign_path, char *host_path, int len);
int compat_convert_error(int foreign_error);

/* 统计 */
int compat_android_app_count(void);
int compat_linux_app_count(void);
int compat_container_count(void);

#endif /* COMPAT_H */
