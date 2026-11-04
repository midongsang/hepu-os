#ifndef HDPK_H
#define HDPK_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS H-DPK 设备适配包格式
 * HEPU Device PacKage
 * 完整系统 = 赫普OS通用基础镜像 + H-DPK机型设备适配包
 * ========================================================================== */

#define HDPK_MAGIC 0x4B504448  /* "HDPK" 小端 */
#define HDPK_VERSION 0x00010000  /* V1.0 */
#define HDPK_HEADER_SIZE 256
#define HDPK_MAX_DRIVERS 64
#define HDPK_MAX_DEVICES 32
#define HDPK_NAME_LEN 64
#define HDPK_VERSION_STR_LEN 32

/* H-DPK 头部结构 */
typedef struct __attribute__((packed)) {
    uint32_t magic;           /* 魔数 "HDPK" */
    uint32_t version;         /* 包格式版本 */
    uint32_t header_size;     /* 头部大小 */
    uint32_t total_size;      /* 包总大小 */
    uint32_t kernel_min_ver;  /* 最低内核版本要求 */
    uint32_t kernel_max_ver;  /* 最高内核版本兼容 */
    char     package_name[HDPK_NAME_LEN];
    char     device_model[HDPK_NAME_LEN];
    char     device_brand[HDPK_NAME_LEN];
    char     chipset[HDPK_NAME_LEN];
    char     soc_vendor[HDPK_NAME_LEN];
    uint32_t soc_id;
    char     package_version[HDPK_VERSION_STR_LEN];
    char     maintainer[HDPK_NAME_LEN];
    char     license[HDPK_VERSION_STR_LEN];
    uint32_t build_time;      /* YYYYMMDD */
    uint32_t driver_count;
    uint32_t driver_offset;   /* 驱动描述区偏移 */
    uint32_t config_offset;   /* 配置区偏移 */
    uint32_t firmware_offset; /* 固件区偏移 */
    uint32_t firmware_size;
    uint32_t manifest_offset; /* manifest偏移 */
    uint32_t manifest_size;
    uint8_t  signature[64];   /* 包签名 */
    uint8_t  sha256[32];      /* 包内容哈希 */
    uint8_t  reserved[32];
} hdpk_header_t;

/* 驱动描述符 */
typedef struct __attribute__((packed)) {
    char     name[HDPK_NAME_LEN];
    char     version[HDPK_VERSION_STR_LEN];
    uint32_t category;        /* hhal_hw_category_t */
    uint32_t adapter_type;    /* hhal_adapter_type_t */
    uint32_t hw_id;
    uint32_t compatible_id;
    uint32_t priority;
    uint32_t code_offset;     /* 驱动代码在包内偏移 */
    uint32_t code_size;
    uint32_t firmware_offset;
    uint32_t firmware_size;
    uint8_t  isolated;        /* 是否用户态隔离 */
    uint8_t  required;        /* 是否必需驱动 */
    uint8_t  reserved[2];
} hdpk_driver_t;

/* 设备描述符 */
typedef struct __attribute__((packed)) {
    char     name[HDPK_NAME_LEN];
    uint32_t category;
    uint32_t hw_id;
    uint32_t reg_base;
    uint32_t irq;
    uint8_t  present;
    uint8_t  required;
    uint8_t  reserved[2];
} hdpk_device_t;

/* H-DPK 加载状态 */
typedef enum {
    HDPK_STATE_NOT_LOADED = 0,
    HDPK_STATE_LOADING,
    HDPK_STATE_VERIFYING,
    HDPK_STATE_LOADED,
    HDPK_STATE_ACTIVE,
    HDPK_STATE_ERROR,
    HDPK_STATE_INCOMPATIBLE,
} hdpk_state_t;

/* 已加载的H-DPK包 */
typedef struct {
    int used;
    int id;
    hdpk_state_t state;
    hdpk_header_t header;
    hdpk_driver_t drivers[HDPK_MAX_DRIVERS];
    int driver_count;
    hdpk_device_t devices[HDPK_MAX_DEVICES];
    int device_count;
    void *data;               /* 包数据指针 */
    uint32_t data_size;
    int verified;
    int compatible;
    char error_message[128];
} hdpk_package_t;

/* H-DPK 管理器状态 */
typedef struct {
    int initialized;
    int enabled;
    hdpk_package_t packages[16];
    int package_count;
    int active_package;
    /* 统计 */
    uint64_t total_loads;
    uint64_t total_verifications;
    uint64_t failed_verifications;
    uint64_t incompatible_count;
} hdpk_manager_t;

/* 初始化 */
void hdpk_init(void);

/* 包管理 */
int hdpk_load(const void *data, uint32_t size);
int hdpk_unload(int package_id);
int hdpk_activate(int package_id);
int hdpk_deactivate(int package_id);
int hdpk_get(int package_id, hdpk_package_t *pkg);
int hdpk_list(hdpk_package_t *pkgs, int max);
int hdpk_get_active(hdpk_package_t *pkg);

/* 校验 */
int hdpk_verify_header(const hdpk_header_t *header);
int hdpk_verify_signature(int package_id);
int hdpk_verify_checksum(int package_id);
int hdpk_check_compatibility(int package_id, uint32_t kernel_version);

/* 驱动加载 */
int hdpk_load_drivers(int package_id);
int hdpk_get_driver(int package_id, int index, hdpk_driver_t *drv);
int hdpk_list_drivers(int package_id, hdpk_driver_t *drivers, int max);

/* 设备信息 */
int hdpk_get_device_info(int package_id, hdpk_device_t *devices, int max);
int hdpk_get_device_model(int package_id, char *model, char *brand, char *chipset);

/* 统计 */
int hdpk_get_stats(hdpk_manager_t *stats);

#endif /* HDPK_H */
