#ifndef HHAL_H
#define HHAL_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS H-HAL 强化硬件抽象层
 * 新老设备两套适配入口 + 驱动隔离 + H-DPK加载接口
 * ========================================================================== */

#define HHAL_MAX_DRIVERS 128
#define HHAL_MAX_DEVICES 64
#define HHAL_MAX_ADAPTERS 32
#define HHAL_NAME_LEN 64
#define HHAL_VERSION_LEN 32

/* 设备适配入口类型 */
typedef enum {
    ADAPTER_NEW = 0,      /* 新设备: 对接厂商现代硬件驱动标准 */
    ADAPTER_LEGACY = 1,   /* 老旧设备: 兼容桥, 适配老安卓内核驱动接口 */
    ADAPTER_GENERIC = 2,  /* 通用驱动: 不依赖特定硬件 */
} hhal_adapter_type_t;

/* 硬件类别 */
typedef enum {
    HW_CATEGORY_CPU = 0,
    HW_CATEGORY_GPU,
    HW_CATEGORY_DISPLAY,
    HW_CATEGORY_TOUCH,
    HW_CATEGORY_KEYBOARD,
    HW_CATEGORY_STORAGE,
    HW_CATEGORY_NETWORK,
    HW_CATEGORY_WIFI,
    HW_CATEGORY_BLUETOOTH,
    HW_CATEGORY_AUDIO,
    HW_CATEGORY_CAMERA,
    HW_CATEGORY_SENSOR,
    HW_CATEGORY_GPS,
    HW_CATEGORY_MODEM,
    HW_CATEGORY_RADIO,
    HW_CATEGORY_USB,
    HW_CATEGORY_BATTERY,
    HW_CATEGORY_FINGERPRINT,
    HW_CATEGORY_NFC,
    HW_CATEGORY_OTHER,
} hhal_hw_category_t;

/* 驱动状态 */
typedef enum {
    DRV_STATE_UNLOADED = 0,
    DRV_STATE_LOADING,
    DRV_STATE_LOADED,
    DRV_STATE_INITIALIZING,
    DRV_STATE_READY,
    DRV_STATE_ERROR,
    DRV_STATE_UNLOADING,
} hhal_driver_state_t;

/* 驱动操作表 */
typedef struct {
    int  (*probe)(void *dev);       /* 设备探测 */
    int  (*init)(void *dev);        /* 初始化 */
    int  (*open)(void *dev);        /* 打开 */
    int  (*close)(void *dev);       /* 关闭 */
    int  (*read)(void *dev, uint32_t offset, void *buf, uint32_t size);
    int  (*write)(void *dev, uint32_t offset, const void *buf, uint32_t size);
    int  (*ioctl)(void *dev, uint32_t cmd, void *arg);
    void (*shutdown)(void *dev);
    void (*suspend)(void *dev);
    void (*resume)(void *dev);
} hhal_driver_ops_t;

/* 驱动描述符 */
typedef struct {
    int used;
    int id;
    char name[HHAL_NAME_LEN];
    char version[HHAL_VERSION_LEN];
    char author[HHAL_NAME_LEN];
    char license[HHAL_VERSION_LEN];
    hhal_hw_category_t category;
    hhal_adapter_type_t adapter_type;
    hhal_driver_state_t state;
    uint32_t hw_id;           /* 硬件ID */
    uint32_t compatible_id;   /* 兼容ID列表掩码 */
    int priority;             /* 加载优先级 */
    int isolated;             /* 是否隔离运行(用户态) */
    int crash_count;
    /* 操作表 */
    hhal_driver_ops_t ops;
    /* 统计 */
    uint64_t call_count;
    uint64_t error_count;
    uint64_t last_error_time;
} hhal_driver_t;

/* 硬件设备实例 */
typedef struct {
    int used;
    int id;
    char name[HHAL_NAME_LEN];
    hhal_hw_category_t category;
    int driver_id;            /* 绑定的驱动 */
    uint32_t hw_id;
    uint32_t reg_base;
    uint32_t irq;
    int present;
    int working;
    char description[128];
} hhal_device_t;

/* 适配器(新设备/老设备桥) */
typedef struct {
    int used;
    int id;
    char name[HHAL_NAME_LEN];
    hhal_adapter_type_t type;
    char chipset[HHAL_NAME_LEN];  /* 芯片平台, 如 "qualcomm_sm8550" */
    int supported;
    int driver_count;
    int device_count;
    /* 适配接口 */
    int (*detect)(void);
    int (*load_driver)(const char *name);
    int (*translate_ioctl)(uint32_t cmd, void *arg);  /* 老设备协议转换 */
} hhal_adapter_t;

/* H-HAL全局状态 */
typedef struct {
    int initialized;
    int adapter_mode;         /* 0=自动检测, 1=强制新设备, 2=强制兼容模式 */
    char board_name[HHAL_NAME_LEN];
    char chipset[HHAL_NAME_LEN];
    char soc_vendor[HHAL_NAME_LEN];
    uint32_t soc_id;
    /* 驱动 */
    hhal_driver_t drivers[HHAL_MAX_DRIVERS];
    int driver_count;
    /* 设备 */
    hhal_device_t devices[HHAL_MAX_DEVICES];
    int device_count;
    /* 适配器 */
    hhal_adapter_t adapters[HHAL_MAX_ADAPTERS];
    int adapter_count;
    int active_adapter;
    /* 统计 */
    uint64_t total_driver_calls;
    uint64_t total_driver_errors;
    uint64_t driver_crash_recoveries;
} hhal_t;

/* 初始化 */
void hhal_init(void);

/* 驱动管理 */
int hhal_driver_register(const char *name, hhal_hw_category_t category,
                         hhal_adapter_type_t adapter, uint32_t hw_id,
                         const hhal_driver_ops_t *ops, int priority);
int hhal_driver_unregister(int driver_id);
int hhal_driver_get(int driver_id, hhal_driver_t *drv);
int hhal_driver_list(hhal_driver_t *drivers, int max);
int hhal_driver_find_by_category(hhal_hw_category_t category, hhal_driver_t *drivers, int max);
int hhal_driver_probe(int driver_id, void *dev);
int hhal_driver_ioctl(int driver_id, uint32_t cmd, void *arg);

/* 设备管理 */
int hhal_device_register(const char *name, hhal_hw_category_t category,
                         uint32_t hw_id, uint32_t reg_base, uint32_t irq);
int hhal_device_get(int device_id, hhal_device_t *dev);
int hhal_device_list(hhal_device_t *devices, int max);
int hhal_device_bind_driver(int device_id, int driver_id);

/* 适配器管理 */
int hhal_adapter_register(const char *name, hhal_adapter_type_t type, const char *chipset);
int hhal_adapter_get(int adapter_id, hhal_adapter_t *adapter);
int hhal_adapter_list(hhal_adapter_t *adapters, int max);
int hhal_adapter_detect(void);
int hhal_adapter_set_mode(int mode);

/* 驱动隔离与故障恢复 */
int hhal_driver_isolate(int driver_id);
int hhal_driver_recover(int driver_id);
int hhal_get_crash_count(int driver_id);

/* 统计 */
int hhal_get_stats(hhal_t *stats);

#endif /* HHAL_H */
