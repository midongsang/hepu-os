/* ============================================================================
 * 赫普OS - 隔离域（Domain）统一接口
 *
 * 上层系统共用此头文件，32位/64位内核各自实现 domain_* 接口。
 * 切换架构时上层代码零改动。
 *
 * 域是权限、能力、QoS、监视器的主键。进程挂靠域，而非用 env_type 充当域。
 * ========================================================================== */

#ifndef HEPU_SHARED_DOMAIN_H
#define HEPU_SHARED_DOMAIN_H

#include <stdint.h>

/* QoS 等级（P2-3） */
typedef enum {
    QOS_CLASS_RT         = 0,  /* 实时：最高权重，抢占式 */
    QOS_CLASS_LATENCY    = 1,  /* 延迟敏感：交互/GUI */
    QOS_CLASS_THROUGHPUT = 2,  /* 吞吐：批处理/服务 */
    QOS_CLASS_AI         = 3,  /* AI计算：可抢占 */
    QOS_CLASS_BACKGROUND = 4,  /* 后台：最低权重 */
    QOS_CLASS_COUNT
} qos_class_t;

#define DOMAIN_MAX_SLOTS  64
#define DOMAIN_NAME_LEN   32

/* 域类型：与 DESIGN 0.14 隔离域树一致 */
enum domain_type {
    DOMAIN_TYPE_KERNEL         = 0,  /* 内核域（受保护，不可终止） */
    DOMAIN_TYPE_SYSTEM         = 1,  /* 系统服务域 */
    DOMAIN_TYPE_GUI            = 2,  /* 图形合成器域 */
    DOMAIN_TYPE_LINUX_COMPAT   = 3,  /* Linux 兼容实例 */
    DOMAIN_TYPE_ANDROID_COMPAT = 4,  /* Android 兼容实例 */
    DOMAIN_TYPE_USER           = 5,  /* 用户应用域 */
    DOMAIN_TYPE_COUNT
};

/* 预置域 ID */
#define DOMAIN_ID_KERNEL        0
#define DOMAIN_ID_SYSTEM        1
#define DOMAIN_ID_GUI           2
#define DOMAIN_ID_LINUX_COMPAT0 3
#define DOMAIN_ID_INVALID      -1

/* 域对象（32位/64位共用结构定义） */
typedef struct {
    int      id;                  /* 域 ID */
    int      type;                /* domain_type */
    int      parent_id;           /* 父域 ID，-1 表示根 */
    char     name[DOMAIN_NAME_LEN];
    int      active;              /* 是否在用 */
    uint32_t proc_count;          /* 该域下进程数（统计） */
    uint64_t mem_pages;           /* 该域总内存占用（页，统计） */
    /* P2-3: QoS 字段 */
    qos_class_t qos_class;        /* QoS 等级 */
    uint32_t    cpu_weight;       /* CPU 调度权重（由 class 映射，可覆写） */
} domain_t;

/* 初始化域表，创建预置域（kernel/system/gui/linux-compat-0） */
void domain_init(void);

/* 创建域，返回域 ID；失败返回 -1 */
int domain_create(const char *name, int type, int parent_id);

/* 按 ID 查询域，不存在返回 NULL */
domain_t *domain_get(int id);

/* 按类型查询第一个活动域 */
domain_t *domain_get_by_type(int type);

/* 活动域数量 */
int domain_count(void);

/* 列出所有活动域，返回实际项数 */
int domain_list(domain_t *out, int max);

/* env_type → 预置 domain_id 映射 */
int domain_env_to_id(int env_type);

/* 按域统计内存占用（页） */
uint64_t domain_mem_usage(int domain_id);

/* 刷新域的统计（proc_count / mem_pages），由调度 tick 或显式调用 */
void domain_refresh_stats(void);

#endif /* HEPU_SHARED_DOMAIN_H */
