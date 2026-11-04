/* ============================================================================
 * 赫普OS - 统一驱动框架（共享）
 * 架构无关，32位和64位共用
 * V0.10.81: 从32位unified_driver.h移植，统一源码树P2-1
 * ========================================================================== */

#ifndef SHARED_DRIVER_H
#define SHARED_DRIVER_H

#include <stdint.h>

/* 驱动类别 */
typedef enum {
    DRV_CATEGORY_STORAGE = 0,   /* 存储：IDE/AHCI/NVMe */
    DRV_CATEGORY_NETWORK,       /* 网络：eth/wlan */
    DRV_CATEGORY_INPUT,         /* 输入：键盘/鼠标/触屏 */
    DRV_CATEGORY_DISPLAY,       /* 显示：GPU/显示控制器 */
    DRV_CATEGORY_AUDIO,         /* 音频：codec */
    DRV_CATEGORY_USB,           /* USB：host/device */
    DRV_CATEGORY_SENSOR,        /* 传感器：加速度/陀螺仪 */
    DRV_CATEGORY_CAMERA,        /* 摄像头 */
    DRV_CATEGORY_TIMER,         /* 定时器：PIT/HPET */
    DRV_CATEGORY_RTC,           /* 实时时钟 */
    DRV_CATEGORY_INTERRUPT,     /* 中断控制器：PIC/APIC */
    DRV_CATEGORY_BUS,           /* 总线：PCI/PCIe */
    DRV_CATEGORY_OTHER,         /* 其他 */
    DRV_CATEGORY_MAX
} drv_category_t;

/* 驱动状态 */
typedef enum {
    DRV_STATE_UNLOADED = 0,
    DRV_STATE_LOADED,
    DRV_STATE_PROBED,
    DRV_STATE_ACTIVE,
    DRV_STATE_SUSPENDED,
    DRV_STATE_ERROR
} drv_state_t;

/* 驱动操作表（ops操作表，行业通用叫法） */
typedef struct {
    int  (*init)(void *dev);           /* 初始化 */
    int  (*probe)(void *dev);          /* 探测设备 */
    int  (*remove)(void *dev);         /* 移除设备 */
    int  (*suspend)(void *dev);        /* 挂起 */
    int  (*resume)(void *dev);         /* 恢复 */
    int  (*ioctl)(void *dev, int cmd, void *arg);  /* 控制 */
    int  (*read)(void *dev, uint64_t offset, void *buf, uint32_t size);
    int  (*write)(void *dev, uint64_t offset, const void *buf, uint32_t size);
    void (*irq_handler)(void *dev);    /* 中断处理 */
} drv_ops_t;

/* 驱动描述符 */
typedef struct {
    const char    *name;           /* 驱动名称 */
    drv_category_t category;       /* 驱动类别 */
    const char    *chipset;        /* 芯片组 */
    const char    *description;    /* 描述 */
    drv_ops_t      ops;            /* 操作表 */
    void          *private_data;   /* 私有数据 */
    drv_state_t    state;          /* 状态 */
    int            id;             /* 驱动ID */
} drv_desc_t;

/* 驱动管理器 */
#define DRV_MAX_COUNT 64

typedef struct {
    drv_desc_t *drivers[DRV_MAX_COUNT];
    int         count;
    int         initialized;
} drv_manager_t;

/* 初始化驱动框架 */
void drv_init(void);

/* 注册驱动 */
int drv_register(drv_desc_t *desc);

/* 注销驱动 */
int drv_unregister(int driver_id);

/* 探测驱动 */
int drv_probe(int driver_id, void *dev);

/* 获取驱动描述符 */
drv_desc_t *drv_get(int driver_id);

/* 按名称查找驱动 */
drv_desc_t *drv_find_by_name(const char *name);

/* 按类别列出驱动 */
int drv_list_by_category(drv_category_t category, drv_desc_t **descs, int max);
int drv_get_all(drv_desc_t **descs, int max);   /* V0.13.9: 全部驱动列表 */

/* 获取驱动总数 */
int drv_get_count(void);

/* 获取活跃驱动数 */
int drv_get_active_count(void);

/* 挂起所有驱动 */
int drv_suspend_all(void);

/* 恢复所有驱动 */
int drv_resume_all(void);

/* 打印驱动列表 */
void drv_print_list(void);

#endif /* SHARED_DRIVER_H */
