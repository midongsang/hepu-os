/* ============================================================================
 * 赫普OS 64位 - 存储硬件抽象层 (Storage HAL)
 * 通过统一驱动框架调用存储驱动，实现驱动与文件系统解耦
 * V0.10.88: P2-1深化，存储抽象层
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "../../shared/drivers/driver_framework.h"
#include "blk_cache64.h"

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

/* V0.13.36: 存储设备选择 (0=IDE 1=USB) */
static int g_storage_dev_sel = 0;
void storage_hal_set_device(int sel) { g_storage_dev_sel = sel; }
int storage_hal_get_device(void) { return g_storage_dev_sel; }

/* 读取扇区（通过驱动框架, 无缓存原始路径） */
int storage_hal_raw_read(uint32_t lba, uint8_t count, uint8_t *buffer)
{
    if (!storage_dev.initialized) return -1;
    if (!buffer || count == 0) return -1;

    /* V0.13.36: USB 海量存储设备 */
    if (g_storage_dev_sel == 1) {
        extern int usb_storage_read_sectors(uint32_t, uint8_t, uint8_t *);
        return usb_storage_read_sectors(lba, count, buffer);
    }

    /* V0.13.63: 真机 SATA 走 AHCI 控制器 — 有 AHCI 盘时优先走 AHCI,
     * 否则真机 SATA 装系统后 rootfs 读不到 (IDE 只兼容老盘) */
    {
        extern int ahci64_disk_present(void);
        extern int ahci64_read_disk(uint32_t, uint8_t, uint8_t *);
        if (ahci64_disk_present()) return ahci64_read_disk(lba, count, buffer);
    }

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

/* 写入扇区（通过驱动框架, 无缓存原始路径） */
int storage_hal_raw_write(uint32_t lba, uint8_t count, const uint8_t *buffer)
{
    if (!storage_dev.initialized) return -1;
    if (!buffer || count == 0) return -1;

    /* V0.13.36: USB 海量存储设备 */
    if (g_storage_dev_sel == 1) {
        extern int usb_storage_write_sectors(uint32_t, uint8_t, const uint8_t *);
        return usb_storage_write_sectors(lba, count, buffer);
    }

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

/* ===== V0.13.58: 统一块缓存层包装 ===== */
/* 带缓存读: 命中缓存, 未命中经原始路径填充 */
int storage_hal_read(uint32_t lba, uint8_t count, uint8_t *buffer)
{
    if (!storage_dev.initialized) return -1;
    return blk_cache_read(lba, count, buffer);
}

/* 带缓存写: 写穿直落盘 + 更新缓存 */
int storage_hal_write(uint32_t lba, uint8_t count, const uint8_t *buffer)
{
    if (!storage_dev.initialized) return -1;
    return blk_cache_write(lba, count, buffer);
}
