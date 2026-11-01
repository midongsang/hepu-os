/* ============================================================================
 * 赫普OS - 统一驱动框架
 * 整合hal.c/hhal.c/driver_manager.c三层框架
 * 以driver_manager为主框架，hal和hhal为兼容层
 * ========================================================================== */

#include <stdint.h>
#include "unified_driver.h"
#include "vga.h"
#include "string.h"

static int unified_initialized = 0;
static int total_drivers = 0;

/* 类别名称映射 */
static const char *category_names[] = {
    "Storage", "Network", "Input", "Display", "Audio",
    "USB", "Sensor", "Camera", "Modem", "Bluetooth",
    "GPS", "Other"
};

void unified_driver_init(void)
{
    /* 初始化主框架driver_manager */
    extern void driver_manager_init(void);
    driver_manager_init();

    /* 初始化兼容层hal和hhal */
    extern void hal_init(void);
    hal_init();
    extern void hhal_init(void);
    hhal_init();

    unified_initialized = 1;
    total_drivers = 0;

    vga_puts("[UDRV] unified driver framework init \n");
    vga_puts("[UDRV] main framework: driver_manager(state machine +6 ops)\n");
    vga_puts("[UDRV] compat layer: hal(base HAL) + hhal(enhanced HAL)\n");
}

int udrv_register(udrv_desc_t *desc)
{
    if (!unified_initialized || !desc || !desc->name) return -1;
    /* V3.21: category 上限校验，防止 category_names[] 越界读 */
    if (desc->category < 0 || desc->category >= 12) return -1;

    /* 转换为driver_manager描述符 */
    dm_desc_t dm_desc;
    memset(&dm_desc, 0, sizeof(dm_desc));
    strncpy(dm_desc.name, desc->name, 31);
    dm_desc.name[31] = 0;
    dm_desc.category = desc->category;
    dm_desc.ops = &desc->ops;
    dm_desc.private_data = desc->private_data;

    int id = driver_register(&dm_desc);
    if (id >= 0) {
        total_drivers++;
        vga_printf("[UDRV] register driver: %s(%s)\n",
                   desc->name, category_names[desc->category]);
    }
    return id;
}

int udrv_unregister(int driver_id)
{
    if (!unified_initialized) return -1;
    int ret = driver_unregister(driver_id);
    if (ret == 0 && total_drivers > 0) total_drivers--;
    return ret;
}

int udrv_probe(int driver_id, void *dev)
{
    if (!unified_initialized) return -1;
    return driver_probe(driver_id, dev);
}

int udrv_suspend_all(void)
{
    if (!unified_initialized) return -1;
    vga_puts("[UDRV] bulk suspend all driver \n");
    return driver_suspend_all();
}

int udrv_resume_all(void)
{
    if (!unified_initialized) return -1;
    vga_puts("[UDRV] bulk resume all driver \n");
    return driver_resume_all();
}

int udrv_list_by_category(udrv_category_t category, dm_desc_t *descs, int max)
{
    if (!unified_initialized || !descs || max <= 0) return 0;
    return driver_find_by_category(category, descs, max);
}

int udrv_get_count(void)
{
    return total_drivers;
}
