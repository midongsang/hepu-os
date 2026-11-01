#ifndef HDPK_TEMPLATE_H
#define HDPK_TEMPLATE_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS H-DPK社区模板机制
 * 官方输出芯片平台模板，社区填充机型配置，降低适配门槛
 * ========================================================================== */

#define HT_MAX_TEMPLATES 32
#define HT_MAX_DEVICES 128
#define HT_MAX_COMPONENTS 32
#define HT_NAME_LEN 128
#define HT_PATH_LEN 256

/* 芯片平台 */
typedef enum {
    HT_PLATFORM_UNKNOWN = 0,
    HT_PLATFORM_QUALCOMM_SNAPDRAGON,
    HT_PLATFORM_MEDIATEK_DIMENSITY,
    HT_PLATFORM_MEDIATEK_HELIO,
    HT_PLATFORM_UNISOC_TIGER,
    HT_PLATFORM_HISILICON_KIRIN,
    HT_PLATFORM_SAMSUNG_EXYNOS,
    HT_PLATFORM_GOOGLE_TENSOR,
    HT_PLATFORM_APPLE_A,
    HT_PLATFORM_X86,
    HT_PLATFORM_RISCV,
    HT_PLATFORM_GENERIC_ARM,
} ht_platform_t;

/* 硬件组件类型 */
typedef enum {
    HT_COMP_CPU = 0,
    HT_COMP_GPU,
    HT_COMP_MODEM,
    HT_COMP_WIFI,
    HT_COMP_BLUETOOTH,
    HT_COMP_CAMERA,
    HT_COMP_AUDIO,
    HT_COMP_SENSOR,
    HT_COMP_TOUCH,
    HT_COMP_DISPLAY,
    HT_COMP_BATTERY,
    HT_COMP_USB,
    HT_COMP_NFC,
    HT_COMP_FINGERPRINT,
    HT_COMP_GPS,
    HT_COMP_OTHER,
} ht_component_t;

/* 组件支持状态 */
typedef enum {
    HT_STATUS_NOT_SUPPORTED = 0,
    HT_STATUS_WIP,            /* 开发中 */
    HT_STATUS_PARTIAL,        /* 部分支持 */
    HT_STATUS_WORKING,        /* 可用 */
    HT_STATUS_PERFECT,        /* 完美支持 */
} ht_status_t;

/* 组件配置 */
typedef struct {
    int used;
    ht_component_t type;
    char name[HT_NAME_LEN];
    char driver_path[HT_PATH_LEN];
    ht_status_t status;
    char notes[256];
    int required;  /* 是否必需组件 */
} ht_component_config_t;

/* 平台模板 */
typedef struct {
    int used;
    int id;
    ht_platform_t platform;
    char platform_name[HT_NAME_LEN];
    char chip_model[64];
    char description[256];
    /* 组件配置 */
    ht_component_config_t components[HT_MAX_COMPONENTS];
    int component_count;
    /* 内核配置 */
    uint32_t kernel_base_addr;
    uint32_t ram_start;
    uint32_t ram_size;
    char dtb_compatible[128];
    /* 维护信息 */
    char maintainer[64];
    uint64_t last_update;
    int version;
    int verified;
} ht_template_t;

/* 机型配置（基于模板） */
typedef struct {
    int used;
    int id;
    char brand[64];
    char model[64];
    char codename[64];
    int template_id;
    ht_platform_t platform;
    /* 覆盖配置 */
    ht_component_config_t components[HT_MAX_COMPONENTS];
    int component_count;
    /* 设备信息 */
    uint32_t ram_size_mb;
    uint32_t storage_gb;
    char display_resolution[32];
    int android_version_base;  /* 基于哪个安卓版本的驱动 */
    /* 支持等级 */
    int support_level;  /* 1=完整,2=基本,3=社区 */
    /* 社区信息 */
    char contributor[64];
    uint64_t create_time;
    uint64_t last_update;
    int download_count;
    int rating;  /* 1-5星 */
    int verified;
} ht_device_t;

/* 模板管理器状态 */
typedef struct {
    int initialized;
    int enabled;
    /* 模板 */
    ht_template_t templates[HT_MAX_TEMPLATES];
    int template_count;
    /* 机型 */
    ht_device_t devices[HT_MAX_DEVICES];
    int device_count;
    /* 统计 */
    uint64_t total_downloads;
    uint64_t total_contributions;
    int community_active;
} hdpk_template_t;

/* 初始化 */
void hdpk_template_init(void);

/* 模板管理 */
int hdpk_template_register(ht_platform_t platform, const char *name, const char *chip);
int hdpk_template_get(int id, ht_template_t *tmpl);
int hdpk_template_list(ht_template_t *tmpls, int max);
int hdpk_template_list_by_platform(ht_platform_t platform, ht_template_t *tmpls, int max);
int hdpk_template_update_component(int template_id, ht_component_t type,
                                   const char *name, ht_status_t status, const char *notes);
int hdpk_template_verify(int template_id);

/* 机型管理 */
int hdpk_device_register(const char *brand, const char *model, const char *codename,
                         int template_id);
int hdpk_device_get(int id, ht_device_t *device);
int hdpk_device_list(ht_device_t *devices, int max);
int hdpk_device_list_by_template(int template_id, ht_device_t *devices, int max);
int hdpk_device_list_by_support(int level, ht_device_t *devices, int max);
int hdpk_device_search(const char *keyword, ht_device_t *devices, int max);
int hdpk_device_update_component(int device_id, ht_component_t type,
                                 const char *name, ht_status_t status, const char *notes);
int hdpk_device_set_support_level(int device_id, int level);
int hdpk_device_rate(int device_id, int rating);
int hdpk_device_verify(int device_id);

/* 社区贡献 */
int hdpk_contribute_template(const ht_template_t *tmpl);
int hdpk_contribute_device(const ht_device_t *device);
int hdpk_get_community_stats(hdpk_template_t *stats);

/* V3.87-E15: 小缓冲摘要 API(避免调用方在栈上分配超大结构体)
 * hdpk_community_stats: 模板数/机型数/社区贡献数 */
int hdpk_community_stats(int *nt, int *nd, uint32_t *contribs);
/* 模板摘要: 名称/芯片/组件数/已验证 */
int hdpk_template_summary(int id, char *name, int nsz, char *chip, int csz,
                          int *comps, int *ver);
/* 机型摘要: 品牌/型号/支持等级/内存MB */
int hdpk_device_summary(int id, char *brand, int bsz, char *model, int msz,
                        int *lvl, uint32_t *ram_mb);

/* 平台名称 */
int hdpk_platform_name(ht_platform_t platform, char *buf, int size);
int hdpk_component_name(ht_component_t comp, char *buf, int size);
int hdpk_status_name(ht_status_t status, char *buf, int size);

#endif /* HDPK_TEMPLATE_H */
