/* ============================================================================
 * 赫普OS - 统一驱动框架实现（共享）
 * 架构无关，32位和64位共用
 * V0.10.81: 统一源码树P2-1，驱动管理器核心逻辑
 * ========================================================================== */

#include "driver_framework.h"

/* 外部函数 */
extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);

/* 驱动管理器实例 */
static drv_manager_t drv_mgr;

/* 初始化驱动框架 */
void drv_init(void)
{
    for (int i = 0; i < DRV_MAX_COUNT; i++) {
        drv_mgr.drivers[i] = 0;
    }
    drv_mgr.count = 0;
    drv_mgr.initialized = 1;
    vga_puts("[DRV] Unified driver framework initialized\n");
}

/* 注册驱动 */
int drv_register(drv_desc_t *desc)
{
    if (!drv_mgr.initialized || !desc) return -1;
    if (drv_mgr.count >= DRV_MAX_COUNT) {
        vga_puts("[DRV] ERROR: Too many drivers\n");
        return -1;
    }

    /* 检查是否已注册 */
    for (int i = 0; i < drv_mgr.count; i++) {
        if (drv_mgr.drivers[i] == desc) {
            return i;
        }
    }

    int id = drv_mgr.count;
    desc->id = id;
    desc->state = DRV_STATE_LOADED;
    drv_mgr.drivers[id] = desc;
    drv_mgr.count++;

    vga_puts("[DRV] Registered: ");
    vga_puts(desc->name);
    vga_puts(" (ID=");
    char buf[8];
    int n = id, i = 0;
    char tmp[8];
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
    vga_puts(buf);
    vga_puts(")\n");

    return id;
}

/* 注销驱动 */
int drv_unregister(int driver_id)
{
    if (!drv_mgr.initialized || driver_id < 0 || driver_id >= drv_mgr.count) return -1;

    drv_desc_t *desc = drv_mgr.drivers[driver_id];
    if (!desc) return -1;

    /* 调用remove */
    if (desc->ops.remove) {
        desc->ops.remove(desc->private_data);
    }

    desc->state = DRV_STATE_UNLOADED;

    /* 移除并压缩数组 */
    for (int i = driver_id; i < drv_mgr.count - 1; i++) {
        drv_mgr.drivers[i] = drv_mgr.drivers[i + 1];
        if (drv_mgr.drivers[i]) {
            drv_mgr.drivers[i]->id = i;
        }
    }
    drv_mgr.drivers[drv_mgr.count - 1] = 0;
    drv_mgr.count--;

    return 0;
}

/* 探测驱动 */
int drv_probe(int driver_id, void *dev)
{
    if (!drv_mgr.initialized || driver_id < 0 || driver_id >= drv_mgr.count) return -1;

    drv_desc_t *desc = drv_mgr.drivers[driver_id];
    if (!desc || !desc->ops.probe) return -1;

    int ret = desc->ops.probe(dev);
    if (ret == 0) {
        desc->state = DRV_STATE_PROBED;
        desc->private_data = dev;
    }

    return ret;
}

/* 获取驱动描述符 */
drv_desc_t *drv_get(int driver_id)
{
    if (!drv_mgr.initialized || driver_id < 0 || driver_id >= drv_mgr.count) return 0;
    return drv_mgr.drivers[driver_id];
}

/* 按名称查找驱动 */
drv_desc_t *drv_find_by_name(const char *name)
{
    if (!drv_mgr.initialized || !name) return 0;

    for (int i = 0; i < drv_mgr.count; i++) {
        drv_desc_t *desc = drv_mgr.drivers[i];
        if (desc && desc->name) {
            int match = 1;
            for (int j = 0; name[j] && desc->name[j]; j++) {
                if (name[j] != desc->name[j]) { match = 0; break; }
            }
            if (match && name[0] == desc->name[0]) {
                return desc;
            }
        }
    }
    return 0;
}

/* 按类别列出驱动 */
int drv_list_by_category(drv_category_t category, drv_desc_t **descs, int max)
{
    if (!drv_mgr.initialized || !descs || max <= 0) return 0;

    int count = 0;
    for (int i = 0; i < drv_mgr.count && count < max; i++) {
        if (drv_mgr.drivers[i] && drv_mgr.drivers[i]->category == category) {
            descs[count++] = drv_mgr.drivers[i];
        }
    }
    return count;
}

/* 获取驱动总数 */
int drv_get_count(void)
{
    return drv_mgr.count;
}

/* 获取活跃驱动数 */
int drv_get_active_count(void)
{
    int count = 0;
    for (int i = 0; i < drv_mgr.count; i++) {
        if (drv_mgr.drivers[i] && drv_mgr.drivers[i]->state == DRV_STATE_ACTIVE) {
            count++;
        }
    }
    return count;
}

/* 挂起所有驱动 */
int drv_suspend_all(void)
{
    int suspended = 0;
    for (int i = 0; i < drv_mgr.count; i++) {
        drv_desc_t *desc = drv_mgr.drivers[i];
        if (desc && desc->ops.suspend && desc->state == DRV_STATE_ACTIVE) {
            if (desc->ops.suspend(desc->private_data) == 0) {
                desc->state = DRV_STATE_SUSPENDED;
                suspended++;
            }
        }
    }
    return suspended;
}

/* 恢复所有驱动 */
int drv_resume_all(void)
{
    int resumed = 0;
    for (int i = 0; i < drv_mgr.count; i++) {
        drv_desc_t *desc = drv_mgr.drivers[i];
        if (desc && desc->ops.resume && desc->state == DRV_STATE_SUSPENDED) {
            if (desc->ops.resume(desc->private_data) == 0) {
                desc->state = DRV_STATE_ACTIVE;
                resumed++;
            }
        }
    }
    return resumed;
}

/* 打印驱动列表 */
void drv_print_list(void)
{
    vga_puts("\n=== Driver List (");
    char buf[8];
    int n = drv_mgr.count, i = 0;
    char tmp[8];
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
    vga_puts(buf);
    vga_puts(" drivers) ===\n");

    static const char *category_names[] = {
        "STORAGE", "NETWORK", "INPUT", "DISPLAY", "AUDIO",
        "USB", "SENSOR", "CAMERA", "TIMER", "RTC",
        "INTERRUPT", "BUS", "OTHER"
    };

    static const char *state_names[] = {
        "UNLOADED", "LOADED", "PROBED", "ACTIVE", "SUSPENDED", "ERROR"
    };

    for (int i = 0; i < drv_mgr.count; i++) {
        drv_desc_t *desc = drv_mgr.drivers[i];
        if (!desc) continue;

        vga_puts("  [");
        n = i; i = 0;
        do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
        j = 0;
        while (i > 0) buf[j++] = tmp[--i];
        buf[j] = 0;
        vga_puts(buf);
        vga_puts("] ");
        vga_puts(desc->name ? desc->name : "unknown");
        vga_puts(" (");
        if (desc->category < DRV_CATEGORY_MAX) {
            vga_puts(category_names[desc->category]);
        } else {
            vga_puts("UNKNOWN");
        }
        vga_puts(", ");
        if (desc->state <= DRV_STATE_ERROR) {
            vga_puts(state_names[desc->state]);
        } else {
            vga_puts("?");
        }
        vga_puts(")\n");
    }
    vga_puts("========================\n\n");
}
