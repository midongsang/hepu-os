#ifndef DEVICE_SUPPORT_H
#define DEVICE_SUPPORT_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 设备三级支持体系
 * 完整支持 / 有限支持 / 不支持
 * 刷机前自动检测硬件并展示支持等级
 * ========================================================================== */

#define DS_MAX_HW_ITEMS 32
#define DS_NAME_LEN 64
#define DS_DESC_LEN 128

/* 支持等级 */
typedef enum {
    SUPPORT_FULL = 0,       /* 完整支持: 通话/相机/指纹/移动通信全部正常 */
    SUPPORT_PARTIAL = 1,    /* 有限支持: 部分硬件失效, 适合WiFi备用机 */
    SUPPORT_UNSUPPORTED = 2,/* 不支持: 硬件配置过低, 禁止刷入 */
    SUPPORT_UNKNOWN = 3,    /* 未检测 */
} support_level_t;

/* 硬件项状态 */
typedef enum {
    HW_WORKING = 0,         /* 正常工作 */
    HW_PARTIAL = 1,         /* 部分功能可用 */
    HW_BROKEN = 2,          /* 不可用 */
    HW_NOT_PRESENT = 3,     /* 硬件不存在 */
    HW_UNTESTED = 4,        /* 未测试 */
} hw_status_t;

/* 硬件类别 */
typedef enum {
    HW_ITEM_CPU = 0,
    HW_ITEM_GPU,
    HW_ITEM_DISPLAY,
    HW_ITEM_TOUCH,
    HW_ITEM_KEYBOARD,
    HW_ITEM_STORAGE,
    HW_ITEM_RAM,
    HW_ITEM_WIFI,
    HW_ITEM_BLUETOOTH,
    HW_ITEM_CELLULAR,      /* 移动通信/基带 */
    HW_ITEM_CALL,          /* 通话 */
    HW_ITEM_SMS,           /* 短信 */
    HW_ITEM_CAMERA_FRONT,
    HW_ITEM_CAMERA_BACK,
    HW_ITEM_FINGERPRINT,
    HW_ITEM_AUDIO,
    HW_ITEM_MIC,
    HW_ITEM_SPEAKER,
    HW_ITEM_EARPIECE,
    HW_ITEM_GPS,
    HW_ITEM_SENSORS,
    HW_ITEM_USB,
    HW_ITEM_BATTERY,
    HW_ITEM_CHARGING,
    HW_ITEM_NFC,
    HW_ITEM_LED,
    HW_ITEM_VIBRATOR,
    HW_ITEM_BUTT,
} hw_item_t;

/* 硬件支持项 */
typedef struct {
    int used;
    hw_item_t item;
    char name[DS_NAME_LEN];
    hw_status_t status;
    support_level_t level;  /* 该项对整体等级的影响 */
    int required;           /* 是否为完整支持必需项 */
    char description[DS_DESC_LEN];
    char driver_name[DS_NAME_LEN];
} hw_support_item_t;

/* 设备支持报告 */
typedef struct {
    int valid;
    char device_model[DS_NAME_LEN];
    char device_brand[DS_NAME_LEN];
    char chipset[DS_NAME_LEN];
    uint32_t ram_size_mb;
    uint32_t storage_size_gb;
    support_level_t overall_level;
    int full_count;
    int partial_count;
    int broken_count;
    int not_present_count;
    hw_support_item_t items[DS_MAX_HW_ITEMS];
    int item_count;
    char recommendation[256];
    uint64_t detect_time;
} device_support_report_t;

/* 支持体系状态 */
typedef struct {
    int initialized;
    int enabled;
    device_support_report_t current_report;
    int report_generated;
    /* 统计 */
    uint64_t total_detections;
    uint64_t full_support_count;
    uint64_t partial_support_count;
    uint64_t unsupported_count;
} device_support_t;

/* 初始化 */
void device_support_init(void);

/* 设备检测 */
int device_support_detect(void);
int device_support_detect_item(hw_item_t item);
int device_support_set_item_status(hw_item_t item, hw_status_t status, const char *desc);

/* 报告生成 */
int device_support_generate_report(device_support_report_t *report);
int device_support_get_report(device_support_report_t *report);
int device_support_get_level(support_level_t *level);

/* 等级判断 */
support_level_t device_support_calculate_level(const device_support_report_t *report);
const char *device_support_level_name(support_level_t level);
const char *device_support_level_description(support_level_t level);

/* 硬件项信息 */
int device_support_get_item(hw_item_t item, hw_support_item_t *out);
int device_support_list_items(hw_support_item_t *items, int max);
const char *device_support_item_name(hw_item_t item);

/* 刷机准入 */
int device_support_check_flash_allowed(void);
int device_support_get_warning(char *buf, int size);

/* 统计 */
int device_support_get_stats(device_support_t *stats);

#endif /* DEVICE_SUPPORT_H */
