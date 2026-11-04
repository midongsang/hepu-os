/* ============================================================================
 * 赫普OS - HAL设备枚举与设备树 (V0.97新增)
 * 实现：设备节点树、设备枚举、设备属性、驱动匹配
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "hal_device.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"

#define DEVICE_MAX 64
#define DEVICE_NAME_MAX 32
#define DEVICE_PROP_MAX 8
#define DEVICE_CHILD_MAX 16

/* 设备属性 */
typedef struct {
    char     key[16];
    uint32_t value;
} device_prop_t;

/* 设备节点 */
typedef struct device_node {
    char           name[DEVICE_NAME_MAX];
    device_type_t  type;
    uint32_t       hardware_id;
    uint32_t       base_addr;
    uint32_t       irq;
    uint8_t        status;     /* 0=不存在, 1=存在, 2=已初始化, 3=故障 */
    uint8_t        driver_id;  /* 绑定的驱动ID */
    device_prop_t  props[DEVICE_PROP_MAX];
    uint8_t        prop_count;
    struct device_node *parent;
    struct device_node *children[DEVICE_CHILD_MAX];
    uint8_t        child_count;
} device_node_t;

static device_node_t devices[DEVICE_MAX];
static device_node_t *root = NULL;
static uint32_t device_count = 0;

void hal_device_init(void)
{
    memset(devices, 0, sizeof(devices));
    device_count = 0;

    /* 创建根设备节点 */
    root = &devices[0];
    strncpy(root->name, "root", DEVICE_NAME_MAX - 1);
    root->type = DEVICE_BUS;
    root->status = 2;  /* 已初始化 */
    root->parent = NULL;
    root->child_count = 0;
    device_count = 1;

    vga_puts("[HAL-DEV] device enum subsystem init done \n");
}

/* 创建设备节点 */
device_node_t *hal_device_create(const char *name, device_type_t type,
                                 uint32_t hw_id, uint32_t base, uint32_t irq)
{
    if (device_count >= DEVICE_MAX) return NULL;
    device_node_t *dev = &devices[device_count++];
    strncpy(dev->name, name, DEVICE_NAME_MAX - 1);
    dev->type = type;
    dev->hardware_id = hw_id;
    dev->base_addr = base;
    dev->irq = irq;
    dev->status = 1;  /* 存在，未初始化 */
    dev->driver_id = 0xFF;
    dev->prop_count = 0;
    dev->parent = NULL;
    dev->child_count = 0;
    return dev;
}

/* 添加子设备 */
int hal_device_add_child(device_node_t *parent, device_node_t *child)
{
    if (!parent || !child) return -1;
    if (parent->child_count >= DEVICE_CHILD_MAX) return -1;
    parent->children[parent->child_count++] = child;
    child->parent = parent;
    return 0;
}

/* 设置设备属性 */
int hal_device_set_prop(device_node_t *dev, const char *key, uint32_t value)
{
    if (!dev || dev->prop_count >= DEVICE_PROP_MAX) return -1;
    strncpy(dev->props[dev->prop_count].key, key, 15);
    dev->props[dev->prop_count].value = value;
    dev->prop_count++;
    return 0;
}

/* 获取设备属性 */
int hal_device_get_prop(device_node_t *dev, const char *key, uint32_t *value)
{
    if (!dev) return -1;
    for (int i = 0; i < dev->prop_count; i++) {
        if (strcmp(dev->props[i].key, key) == 0) {
            if (value) *value = dev->props[i].value;
            return 0;
        }
    }
    return -1;
}

/* 按名字查找设备 */
device_node_t *hal_device_find(const char *name)
{
    if (!name) return NULL;
    for (uint32_t i = 0; i < device_count; i++) {
        if (strcmp(devices[i].name, name) == 0) {
            return &devices[i];
        }
    }
    return NULL;
}

/* 按类型查找设备 */
device_node_t *hal_device_find_by_type(device_type_t type, int index)
{
    int found = 0;
    for (uint32_t i = 0; i < device_count; i++) {
        if (devices[i].type == type) {
            if (found == index) return &devices[i];
            found++;
        }
    }
    return NULL;
}

/* 设置设备状态 */
void hal_device_set_status(device_node_t *dev, uint8_t status)
{
    if (dev) dev->status = status;
}

/* 绑定驱动 */
int hal_device_bind_driver(device_node_t *dev, uint8_t driver_id)
{
    if (!dev) return -1;
    dev->driver_id = driver_id;
    dev->status = 2;  /* 已初始化 */
    return 0;
}

/* 设备统计 */
void hal_device_stats(uint32_t *total, uint32_t *initialized, uint32_t *faulty)
{
    uint32_t init = 0, fault = 0;
    for (uint32_t i = 0; i < device_count; i++) {
        if (devices[i].status == 2) init++;
        if (devices[i].status == 3) fault++;
    }
    if (total) *total = device_count;
    if (initialized) *initialized = init;
    if (faulty) *faulty = fault;
}

/* 打印设备树 */
void hal_device_dump(void)
{
    vga_printf("[HAL-DEV] DTB(%d device):\n", device_count);
    for (uint32_t i = 0; i < device_count; i++) {
        const char *status_name = "unknown";
        switch (devices[i].status) {
            case 0: status_name = "absent"; break;
            case 1: status_name = "uninit"; break;
            case 2: status_name = "init"; break;
            case 3: status_name = "fault"; break;
        }
        vga_printf("  [%d] %s (type=%d, hw=0x%x, base=0x%x, irq=%d) %s\n",
                   i, devices[i].name, devices[i].type,
                   devices[i].hardware_id, devices[i].base_addr,
                   devices[i].irq, status_name);
    }
}
