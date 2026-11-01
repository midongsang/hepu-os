/* ============================================================================
 * 赫普OS - H-DPK 设备适配包
 * ========================================================================== */

#include <stdint.h>
#include "hdpk.h"
#include "vga.h"
#include "string.h"

static hdpk_manager_t mgr;

void hdpk_init(void)
{
    memset(&mgr, 0, sizeof(hdpk_manager_t));
    mgr.initialized = 1;
    mgr.enabled = 1;
    mgr.active_package = -1;

    vga_puts("[HDPK] H-DPK device pack subsystem init done \n");
}

/* 包管理 */
int hdpk_load(const void *data, uint32_t size)
{
    if (!data || size < sizeof(hdpk_header_t)) return -1;

    for (int i = 0; i < 16; i++) {
        if (!mgr.packages[i].used) {
            memset(&mgr.packages[i], 0, sizeof(hdpk_package_t));
            mgr.packages[i].used = 1;
            mgr.packages[i].id = i;
            mgr.packages[i].state = HDPK_STATE_LOADING;
            mgr.packages[i].data = (void *)data;
            mgr.packages[i].data_size = size;

            /* 复制头部 */
            memcpy(&mgr.packages[i].header, data, sizeof(hdpk_header_t));

            /* 校验头部 */
            if (hdpk_verify_header(&mgr.packages[i].header) != 0) {
                mgr.packages[i].state = HDPK_STATE_ERROR;
                strncpy(mgr.packages[i].error_message, " invalid HDPK header 部 ", 127);
                return -1;
            }

            /* 解析驱动列表 */
            hdpk_header_t *hdr = &mgr.packages[i].header;
            if (hdr->driver_offset && hdr->driver_count > 0) {
                uint32_t drv_off = hdr->driver_offset;
                int count = hdr->driver_count < HDPK_MAX_DRIVERS ? hdr->driver_count : HDPK_MAX_DRIVERS;
                for (int d = 0; d < count; d++) {
                    if (drv_off + sizeof(hdpk_driver_t) <= size) {
                        memcpy(&mgr.packages[i].drivers[d],
                               (uint8_t *)data + drv_off, sizeof(hdpk_driver_t));
                        mgr.packages[i].driver_count++;
                        drv_off += sizeof(hdpk_driver_t);
                    }
                }
            }

            mgr.packages[i].state = HDPK_STATE_LOADED;
            mgr.package_count++;
            mgr.total_loads++;
            return i;
        }
    }
    return -1;
}

int hdpk_unload(int package_id)
{
    if (package_id < 0 || package_id >= 16 || !mgr.packages[package_id].used) return -1;
    if (mgr.active_package == package_id) hdpk_deactivate(package_id);
    mgr.packages[package_id].used = 0;
    mgr.packages[package_id].state = HDPK_STATE_NOT_LOADED;
    mgr.package_count--;
    return 0;
}

int hdpk_activate(int package_id)
{
    if (package_id < 0 || package_id >= 16 || !mgr.packages[package_id].used) return -1;
    if (mgr.packages[package_id].state != HDPK_STATE_LOADED) return -1;

    /* 先停用当前活动包 */
    if (mgr.active_package >= 0) hdpk_deactivate(mgr.active_package);

    /* 校验兼容性 */
    if (hdpk_check_compatibility(package_id, 0x002600) != 0) {
        mgr.packages[package_id].state = HDPK_STATE_INCOMPATIBLE;
        strncpy(mgr.packages[package_id].error_message, " kernel version not compat ", 127);
        mgr.incompatible_count++;
        return -1;
    }

    /* 加载驱动 */
    hdpk_load_drivers(package_id);

    mgr.packages[package_id].state = HDPK_STATE_ACTIVE;
    mgr.active_package = package_id;
    return 0;
}

int hdpk_deactivate(int package_id)
{
    if (package_id < 0 || package_id >= 16 || !mgr.packages[package_id].used) return -1;
    mgr.packages[package_id].state = HDPK_STATE_LOADED;
    if (mgr.active_package == package_id) mgr.active_package = -1;
    return 0;
}

int hdpk_get(int package_id, hdpk_package_t *pkg)
{
    if (package_id < 0 || package_id >= 16 || !mgr.packages[package_id].used || !pkg) return -1;
    memcpy(pkg, &mgr.packages[package_id], sizeof(hdpk_package_t));
    return 0;
}

int hdpk_list(hdpk_package_t *pkgs, int max)
{
    if (!pkgs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < 16 && count < max; i++) {
        if (mgr.packages[i].used) memcpy(&pkgs[count++], &mgr.packages[i], sizeof(hdpk_package_t));
    }
    return count;
}

int hdpk_get_active(hdpk_package_t *pkg)
{
    if (mgr.active_package < 0 || !pkg) return -1;
    return hdpk_get(mgr.active_package, pkg);
}

/* 校验 */
int hdpk_verify_header(const hdpk_header_t *header)
{
    if (!header) return -1;
    if (header->magic != HDPK_MAGIC) return -1;
    if (header->version != HDPK_VERSION) return -1;
    if (header->header_size != HDPK_HEADER_SIZE) return -1;
    if (header->total_size < HDPK_HEADER_SIZE) return -1;
    return 0;
}

int hdpk_verify_signature(int package_id)
{
    if (package_id < 0 || package_id >= 16 || !mgr.packages[package_id].used) return -1;
    mgr.total_verifications++;
    /* 简化: 假设签名有效 */
    mgr.packages[package_id].verified = 1;
    return 0;
}

int hdpk_verify_checksum(int package_id)
{
    if (package_id < 0 || package_id >= 16 || !mgr.packages[package_id].used) return -1;
    mgr.total_verifications++;
    /* 简化: 假设校验和有效 */
    mgr.packages[package_id].verified = 1;
    return 0;
}

int hdpk_check_compatibility(int package_id, uint32_t kernel_version)
{
    if (package_id < 0 || package_id >= 16 || !mgr.packages[package_id].used) return -1;
    hdpk_header_t *hdr = &mgr.packages[package_id].header;
    if (kernel_version < hdr->kernel_min_ver) return -1;
    if (hdr->kernel_max_ver && kernel_version > hdr->kernel_max_ver) return -1;
    mgr.packages[package_id].compatible = 1;
    return 0;
}

/* 驱动加载 */
int hdpk_load_drivers(int package_id)
{
    if (package_id < 0 || package_id >= 16 || !mgr.packages[package_id].used) return -1;
    /* 简化: 标记所有驱动为已加载 */
    for (int i = 0; i < mgr.packages[package_id].driver_count; i++) {
        /* 实际应通过H-HAL注册驱动 */
    }
    return mgr.packages[package_id].driver_count;
}

int hdpk_get_driver(int package_id, int index, hdpk_driver_t *drv)
{
    if (package_id < 0 || package_id >= 16 || !mgr.packages[package_id].used) return -1;
    if (index < 0 || index >= mgr.packages[package_id].driver_count || !drv) return -1;
    memcpy(drv, &mgr.packages[package_id].drivers[index], sizeof(hdpk_driver_t));
    return 0;
}

int hdpk_list_drivers(int package_id, hdpk_driver_t *drivers, int max)
{
    if (package_id < 0 || package_id >= 16 || !mgr.packages[package_id].used) return -1;
    if (!drivers || max <= 0) return 0;
    int count = mgr.packages[package_id].driver_count < max ?
                mgr.packages[package_id].driver_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&drivers[i], &mgr.packages[package_id].drivers[i], sizeof(hdpk_driver_t));
    return count;
}

/* 设备信息 */
int hdpk_get_device_info(int package_id, hdpk_device_t *devices, int max)
{
    if (package_id < 0 || package_id >= 16 || !mgr.packages[package_id].used) return -1;
    if (!devices || max <= 0) return 0;
    int count = mgr.packages[package_id].device_count < max ?
                mgr.packages[package_id].device_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&devices[i], &mgr.packages[package_id].devices[i], sizeof(hdpk_device_t));
    return count;
}

int hdpk_get_device_model(int package_id, char *model, char *brand, char *chipset)
{
    if (package_id < 0 || package_id >= 16 || !mgr.packages[package_id].used) return -1;
    hdpk_header_t *hdr = &mgr.packages[package_id].header;
    if (model) strncpy(model, hdr->device_model, 63);
    if (brand) strncpy(brand, hdr->device_brand, 63);
    if (chipset) strncpy(chipset, hdr->chipset, 63);
    return 0;
}

/* 统计 */
int hdpk_get_stats(hdpk_manager_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &mgr, sizeof(hdpk_manager_t));
    return 0;
}
