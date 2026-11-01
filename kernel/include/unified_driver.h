#ifndef UNIFIED_DRIVER_H
#define UNIFIED_DRIVER_H

#include <stdint.h>
#include "driver_manager.h"

/* 统一驱动框架入口
 * 整合hal.c/hhal.c/driver_manager.c三层框架
 * 推荐使用driver_manager接口，hal和hhal保留为兼容层
 */

/* 驱动类别（统一） */
typedef enum {
    UDRV_CATEGORY_STORAGE = 0,   /* 存储：IDE/AHCI/NVMe */
    UDRV_CATEGORY_NETWORK,       /* 网络：eth/wlan */
    UDRV_CATEGORY_INPUT,         /* 输入：键盘/鼠标/触屏 */
    UDRV_CATEGORY_DISPLAY,       /* 显示：GPU/显示控制器 */
    UDRV_CATEGORY_AUDIO,         /* 音频：codec */
    UDRV_CATEGORY_USB,           /* USB：host/device */
    UDRV_CATEGORY_SENSOR,        /* 传感器：加速度/陀螺仪 */
    UDRV_CATEGORY_CAMERA,        /* 摄像头 */
    UDRV_CATEGORY_MODEM,         /* 调制解调器：RIL */
    UDRV_CATEGORY_BLUETOOTH,     /* 蓝牙 */
    UDRV_CATEGORY_GPS,           /* GPS */
    UDRV_CATEGORY_OTHER,         /* 其他 */
    UDRV_CATEGORY_MAX
} udrv_category_t;

/* 统一驱动描述符 */
typedef struct {
    const char *name;
    udrv_category_t category;
    const char *chipset;
    dm_ops_t ops;  /* 复用driver_manager的操作接口 */
    void *private_data;
} udrv_desc_t;

void unified_driver_init(void);
int udrv_register(udrv_desc_t *desc);
int udrv_unregister(int driver_id);
int udrv_probe(int driver_id, void *dev);
int udrv_suspend_all(void);
int udrv_resume_all(void);
int udrv_list_by_category(udrv_category_t category, dm_desc_t *descs, int max);
int udrv_get_count(void);

#endif /* UNIFIED_DRIVER_H */
