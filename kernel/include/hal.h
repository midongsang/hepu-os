#ifndef HAL_H
#define HAL_H

#include <stdint.h>

/* 设备支持等级 */
typedef enum {
    DEVICE_FULL = 0,
    DEVICE_PARTIAL = 1,
    DEVICE_UNSUPPORTED = 2
} device_support_level_t;

/* 驱动类型 */
typedef enum {
    DRIVER_TYPE_DISPLAY = 0,
    DRIVER_TYPE_INPUT,
    DRIVER_TYPE_STORAGE,
    DRIVER_TYPE_NETWORK,
    DRIVER_TYPE_AUDIO,
    DRIVER_TYPE_CAMERA,
    DRIVER_TYPE_SENSOR,
    DRIVER_TYPE_MODEM,
    DRIVER_TYPE_OTHER
} driver_type_t;

/* 驱动操作表 */
typedef struct {
    int  (*init)(void);
    int  (*read)(uint32_t offset, void *buf, uint32_t size);
    int  (*write)(uint32_t offset, const void *buf, uint32_t size);
    int  (*ioctl)(uint32_t cmd, void *arg);
    void (*shutdown)(void);
} driver_ops_t;

void hal_init(void);
int  hal_register_driver(const char *name, driver_type_t type,
                         uint32_t hw_id, const driver_ops_t *ops);
int  hal_driver_ioctl(int driver_id, uint32_t cmd, void *arg);
void hal_set_device_level(device_support_level_t level);
device_support_level_t hal_get_device_level(void);
void hal_set_device_model(const char *model);

#endif
