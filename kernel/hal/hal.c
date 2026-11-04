/* ============================================================================
 * 赫普OS - H-HAL 硬件抽象层 (HEPU Hardware Abstraction Layer)
 *
 * 核心设计：
 * 1. 基础镜像与设备驱动解耦
 * 2. 新设备：对接现代驱动标准
 * 3. 老设备：驱动转换桥接，复用安卓已有驱动
 * 4. 驱动故障隔离，不影响整机
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "hal.h"
#include "vga.h"

#define MAX_DRIVERS 32
#define DRIVER_NAME_LEN 32

/* 驱动注册项 */
typedef struct {
    int            in_use;
    char           name[DRIVER_NAME_LEN];
    driver_type_t  type;
    driver_ops_t   ops;
    uint32_t       hardware_id;
    int            fault_count;  /* 故障计数，用于故障隔离 */
} driver_entry_t;

static driver_entry_t drivers[MAX_DRIVERS];
static device_support_level_t device_level = DEVICE_FULL;
static char device_model[64] = "generic-x86";

void hal_init(void)
{
    for (int i = 0; i < MAX_DRIVERS; i++) {
        drivers[i].in_use = 0;
        drivers[i].fault_count = 0;
    }
    vga_puts("[HAL] H-HAL HAL init done \n");
    vga_printf("[HAL] device model: %s\n", device_model);
    vga_printf("[HAL] supports etc level: %s\n",
               device_level == DEVICE_FULL ? " 完整 supports " :
               device_level == DEVICE_PARTIAL ? " has 限 supports " : " not supports ");
}

/* 注册驱动 */
int hal_register_driver(const char *name, driver_type_t type,
                        uint32_t hw_id, const driver_ops_t *ops)
{
    for (int i = 0; i < MAX_DRIVERS; i++) {
        if (!drivers[i].in_use) {
            drivers[i].in_use = 1;
            drivers[i].type = type;
            drivers[i].hardware_id = hw_id;
            drivers[i].fault_count = 0;
            if (ops) drivers[i].ops = *ops;
            for (int j = 0; j < DRIVER_NAME_LEN - 1 && name[j]; j++) {
                drivers[i].name[j] = name[j];
            }
            drivers[i].name[DRIVER_NAME_LEN - 1] = '\0';

            if (drivers[i].ops.init) {
                int ret = drivers[i].ops.init();
                if (ret != 0) {
                    drivers[i].fault_count++;
                    vga_printf("[HAL] driver %s init failed(code=%d)\n",
                               name, ret);
                    /* 故障隔离：单个驱动失败不影响系统 */
                    drivers[i].in_use = 0;
                    return -1;
                }
            }
            vga_printf("[HAL] driver registered: %s(type=%d)\n", name, type);
            return i;
        }
    }
    return -1;
}

/* 驱动故障隔离：调用驱动时捕获异常，故障次数过多则停用 */
int hal_driver_ioctl(int driver_id, uint32_t cmd, void *arg)
{
    if (driver_id < 0 || driver_id >= MAX_DRIVERS ||
        !drivers[driver_id].in_use) {
        return -1;
    }
    if (drivers[driver_id].fault_count > 3) {
        vga_printf("[HAL] driver %s faults too many , isolated & disabled \n",
                   drivers[driver_id].name);
        return -2;
    }
    if (drivers[driver_id].ops.ioctl) {
        int ret = drivers[driver_id].ops.ioctl(cmd, arg);
        if (ret != 0) drivers[driver_id].fault_count++;
        return ret;
    }
    return -3;
}

/* 设置设备支持等级 */
void hal_set_device_level(device_support_level_t level)
{
    device_level = level;
}

/* 获取设备支持等级 */
device_support_level_t hal_get_device_level(void)
{
    return device_level;
}

/* 设置设备型号 */
void hal_set_device_model(const char *model)
{
    for (int i = 0; i < 63 && model[i]; i++) {
        device_model[i] = model[i];
    }
    device_model[63] = '\0';
}
