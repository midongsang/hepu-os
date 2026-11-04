#ifndef HAL_DEVICE_H
#define HAL_DEVICE_H

#include <stdint.h>

/* 设备类型 */
typedef enum {
    DEVICE_BUS = 0,
    DEVICE_CPU,
    DEVICE_MEMORY,
    DEVICE_STORAGE,
    DEVICE_NETWORK,
    DEVICE_DISPLAY,
    DEVICE_INPUT,
    DEVICE_AUDIO,
    DEVICE_SENSOR,
    DEVICE_CAMERA,
    DEVICE_MODEM,
    DEVICE_USB,
    DEVICE_GPIO,
    DEVICE_TIMER,
    DEVICE_INTERRUPT,
    DEVICE_OTHER
} device_type_t;

/* 前向声明 */
typedef struct device_node device_node_t;

/* 初始化设备枚举子系统 */
void hal_device_init(void);

/* 创建设备节点 */
device_node_t *hal_device_create(const char *name, device_type_t type,
                                 uint32_t hw_id, uint32_t base, uint32_t irq);

/* 添加子设备 */
int hal_device_add_child(device_node_t *parent, device_node_t *child);

/* 属性管理 */
int hal_device_set_prop(device_node_t *dev, const char *key, uint32_t value);
int hal_device_get_prop(device_node_t *dev, const char *key, uint32_t *value);

/* 查找 */
device_node_t *hal_device_find(const char *name);
device_node_t *hal_device_find_by_type(device_type_t type, int index);

/* 状态/驱动绑定 */
void hal_device_set_status(device_node_t *dev, uint8_t status);
int  hal_device_bind_driver(device_node_t *dev, uint8_t driver_id);

/* 统计/调试 */
void hal_device_stats(uint32_t *total, uint32_t *initialized, uint32_t *faulty);
void hal_device_dump(void);

#endif
