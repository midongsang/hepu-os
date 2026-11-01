#ifndef OTA_H
#define OTA_H

#include <stdint.h>

#define OTA_MAX_PARTITIONS 4
#define OTA_VERSION_LEN 32
#define OTA_SIGNATURE_LEN 256
#define OTA_HASH_LEN 32

/* OTA状态 */
typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_INSTALLING,
    OTA_STATE_SUCCESS,
    OTA_STATE_FAILED,
    OTA_STATE_ROLLBACK,
} ota_state_t;

/* OTA错误码 */
typedef enum {
    OTA_OK = 0,
    OTA_ERR_INVALID_PACKAGE,
    OTA_ERR_SIGNATURE,
    OTA_ERR_HASH,
    OTA_ERR_VERSION,
    OTA_ERR_STORAGE,
    OTA_ERR_NETWORK,
    OTA_ERR_INSUFFICIENT_SPACE,
    OTA_ERR_INSTALL,
    OTA_ERR_ROLLBACK_FAILED,
} ota_error_t;

/* 分区槽位 (A/B分区) */
typedef struct {
    int slot;           /* 0=A, 1=B */
    int active;         /* 当前活动槽位 */
    int bootable;       /* 可启动 */
    int successful;     /* 启动成功标记 */
    int retries;        /* 重试次数 */
    char version[OTA_VERSION_LEN];
    uint64_t size;
    uint32_t crc32;
} ota_slot_t;

/* OTA包头部 */
typedef struct {
    uint32_t magic;          /* "HEPU" = 0x55504548 */
    uint32_t version;        /* 包格式版本 */
    char from_version[OTA_VERSION_LEN];
    char to_version[OTA_VERSION_LEN];
    uint64_t package_size;
    uint64_t payload_size;
    uint32_t payload_crc32;
    uint8_t  payload_hash[OTA_HASH_LEN];
    uint8_t  signature[OTA_SIGNATURE_LEN];
    uint32_t signature_len;
    uint64_t timestamp;
    uint32_t flags;
    uint32_t partition_count;
} __attribute__((packed)) ota_header_t;

/* 分区更新信息 */
typedef struct {
    char name[16];
    uint64_t offset;
    uint64_t size;
    uint32_t crc32;
    uint8_t  hash[OTA_HASH_LEN];
} __attribute__((packed)) ota_partition_t;

/* OTA进度回调 */
typedef void (*ota_progress_cb_t)(int percent, ota_state_t state);

/* OTA设备 */
typedef struct {
    int initialized;
    ota_state_t state;
    ota_error_t last_error;
    ota_slot_t slots[2];
    int current_slot;
    int update_available;
    char latest_version[OTA_VERSION_LEN];
    uint64_t download_size;
    uint64_t downloaded;
    int progress;
    ota_progress_cb_t progress_cb;
    /* 统计 */
    uint64_t total_updates;
    uint64_t successful_updates;
    uint64_t failed_updates;
    uint64_t rollbacks;
} ota_device_t;

/* 初始化OTA */
void ota_init(void);

/* 检查更新 */
int ota_check_update(const char *server_url);

/* 下载更新包 */
int ota_download(const char *url, ota_progress_cb_t cb);

/* 验证更新包 */
int ota_verify(const char *package_path);

/* 安装更新 */
int ota_install(const char *package_path);

/* 应用更新 (切换活动槽位) */
int ota_apply(void);

/* 回滚 */
int ota_rollback(void);

/* 标记启动成功 */
int ota_mark_boot_success(void);

/* 获取当前状态 */
int ota_get_status(ota_device_t *status);

/* 获取当前版本 */
int ota_get_version(char *buf, int len);

/* 获取最新版本 */
int ota_get_latest_version(char *buf, int len);

/* 设置进度回调 */
void ota_set_progress_callback(ota_progress_cb_t cb);

/* 取消更新 */
int ota_cancel(void);

/* 清理临时文件 */
int ota_cleanup(void);

/* A/B分区信息 */
int ota_get_slot_info(int slot, ota_slot_t *info);

/* 差分更新支持 */
int ota_apply_delta(const char *delta_path);

/* OTA中断处理 */
void ota_handle_irq(void);

#endif /* OTA_H */
