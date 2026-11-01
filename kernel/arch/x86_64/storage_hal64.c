/* ============================================================================
 * 赫普OS 64位 - 存储硬件抽象层 (Storage HAL)
 * 通过统一驱动框架调用存储驱动，实现驱动与文件系统解耦
 * V0.10.88: P2-1深化，存储抽象层
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "../../shared/drivers/driver_framework.h"

/* 外部函数 */
extern void vga_puts(const char *s);
extern int  ide64_read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer);
extern int  ide64_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buffer);

/* 存储设备信息 */
typedef struct {
    int      initialized;
    int      driver_id;      /* 驱动框架中的ID */
    uint32_t total_sectors;  /* 总扇区数 */
    uint32_t sector_size;    /* 扇区大小 */
    uint8_t  device_type;    /* 0=IDE, 1=AHCI, 2=NVMe */
} storage_dev_t;

static storage_dev_t storage_dev;

/* 初始化存储HAL */
int storage_hal_init(void)
{
    storage_dev.initialized = 0;
    storage_dev.driver_id = -1;
    storage_dev.total_sectors = 0;
    storage_dev.sector_size = 512;
    storage_dev.device_type = 0; /* IDE */

    /* 查找IDE驱动 */
    drv_desc_t *ide_drv = drv_find_by_name("IDE");
    if (ide_drv) {
        storage_dev.driver_id = ide_drv->id;
        storage_dev.initialized = 1;
        vga_puts("[STORAGE] HAL initialized via driver framework (IDE)\n");
        return 0;
    }

    /* 回退：直接使用IDE驱动 */
    storage_dev.initialized = 1;
    vga_puts("[STORAGE] HAL initialized (direct IDE fallback)\n");
    return 0;
}

/* 读取扇区（通过驱动框架） */
int storage_hal_read(uint32_t lba, uint8_t count, uint8_t *buffer)
{
    if (!storage_dev.initialized) return -1;
    if (!buffer || count == 0) return -1;

    /* 通过驱动框架调用 */
    if (storage_dev.driver_id >= 0) {
        drv_desc_t *drv = drv_get(storage_dev.driver_id);
        if (drv && drv->ops.read) {
            return drv->ops.read(drv->private_data, lba, buffer, count * 512);
        }
    }

    /* 回退：直接调用IDE驱动 */
    return ide64_read_sectors(lba, count, buffer);
}

/* 写入扇区（通过驱动框架） */
int storage_hal_write(uint32_t lba, uint8_t count, const uint8_t *buffer)
{
    if (!storage_dev.initialized) return -1;
    if (!buffer || count == 0) return -1;

    /* 通过驱动框架调用 */
    if (storage_dev.driver_id >= 0) {
        drv_desc_t *drv = drv_get(storage_dev.driver_id);
        if (drv && drv->ops.write) {
            return drv->ops.write(drv->private_data, lba, buffer, count * 512);
        }
    }

    /* 回退：直接调用IDE驱动 */
    return ide64_write_sectors(lba, count, buffer);
}

/* 获取存储设备信息 */
int storage_hal_get_info(uint32_t *total_sectors, uint32_t *sector_size)
{
    if (!storage_dev.initialized) return -1;
    if (total_sectors) *total_sectors = storage_dev.total_sectors;
    if (sector_size) *sector_size = storage_dev.sector_size;
    return 0;
}

/* 检查存储HAL是否初始化 */
int storage_hal_is_initialized(void)
{
    return storage_dev.initialized;
}
