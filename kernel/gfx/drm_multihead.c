/* ============================================================================
 * 赫普OS - DRM/KMS 多显示器支持
 * V8.96: 多CRTC/连接器管理、扩展/镜像模式、热插拔检测
 * 方案B第三步：多显示器支持扩展
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "drm.h"
#include "../include/vga.h"

/* 多显示器配置 */
#define MULTIHEAD_MAX_MONITORS  4

typedef enum {
    MULTIHEAD_MODE_EXTEND = 0,   /* 扩展模式 */
    MULTIHEAD_MODE_CLONE,        /* 镜像模式 */
    MULTIHEAD_MODE_SINGLE        /* 单显示器模式 */
} multihead_mode_t;

typedef struct {
    int          active;
    uint32_t     crtc_id;
    uint32_t     connector_id;
    uint32_t     encoder_id;
    drm_mode_t   mode;
    int32_t      position_x;     /* 在虚拟桌面中的位置 */
    int32_t      position_y;
    int          primary;        /* 是否为主显示器 */
} multihead_monitor_t;

typedef struct {
    int                  initialized;
    multihead_mode_t     mode;
    multihead_monitor_t  monitors[MULTIHEAD_MAX_MONITORS];
    int                  monitor_count;
    uint32_t             virtual_width;   /* 虚拟桌面总宽度 */
    uint32_t             virtual_height;  /* 虚拟桌面总高度 */
    int                  primary_index;
} multihead_state_t;

static multihead_state_t g_mh_state;

/* 前向声明 */
void drm_multihead_update_virtual_size(void);

/* ============================================================================
 * 初始化
 * ========================================================================== */

int drm_multihead_init(void)
{
    if (g_mh_state.initialized) return 0;

    memset(&g_mh_state, 0, sizeof(multihead_state_t));
    g_mh_state.mode = MULTIHEAD_MODE_EXTEND;
    g_mh_state.monitor_count = 0;
    g_mh_state.primary_index = 0;

    vga_puts("[DRM-MH] Multi-head support initialized\n");
    g_mh_state.initialized = 1;
    return 0;
}

/* ============================================================================
 * 显示器管理
 * ========================================================================== */

/* 添加显示器 */
int drm_multihead_add_monitor(uint32_t crtc_id, uint32_t connector_id,
                               uint32_t encoder_id, drm_mode_t *mode)
{
    if (!g_mh_state.initialized || !mode) return -1;
    if (g_mh_state.monitor_count >= MULTIHEAD_MAX_MONITORS) return -1;

    int idx = g_mh_state.monitor_count;
    multihead_monitor_t *mon = &g_mh_state.monitors[idx];

    mon->active = 1;
    mon->crtc_id = crtc_id;
    mon->connector_id = connector_id;
    mon->encoder_id = encoder_id;
    mon->mode = *mode;
    mon->primary = (idx == 0) ? 1 : 0;

    /* 扩展模式：水平排列 */
    if (g_mh_state.mode == MULTIHEAD_MODE_EXTEND) {
        mon->position_x = 0;
        mon->position_y = 0;
        for (int i = 0; i < idx; i++) {
            mon->position_x += g_mh_state.monitors[i].mode.hdisplay;
        }
    } else {
        /* 镜像模式：所有显示器在同一位置 */
        mon->position_x = 0;
        mon->position_y = 0;
    }

    g_mh_state.monitor_count++;

    /* 更新虚拟桌面尺寸 */
    drm_multihead_update_virtual_size();

    vga_printf("[DRM-MH] Monitor %d added: %ux%u at (%d,%d)\n",
               idx, mode->hdisplay, mode->vdisplay,
               mon->position_x, mon->position_y);

    return idx;
}

/* 移除显示器 */
int drm_multihead_remove_monitor(int index)
{
    if (!g_mh_state.initialized) return -1;
    if (index < 0 || index >= g_mh_state.monitor_count) return -1;

    vga_printf("[DRM-MH] Monitor %d removed\n", index);

    /* 移动后续显示器 */
    for (int i = index; i < g_mh_state.monitor_count - 1; i++) {
        g_mh_state.monitors[i] = g_mh_state.monitors[i + 1];
    }
    g_mh_state.monitor_count--;

    /* 如果移除了主显示器，设置第一个为主 */
    if (g_mh_state.primary_index == index && g_mh_state.monitor_count > 0) {
        g_mh_state.monitors[0].primary = 1;
        g_mh_state.primary_index = 0;
    }

    drm_multihead_update_virtual_size();
    return 0;
}

/* 更新虚拟桌面尺寸 */
void drm_multihead_update_virtual_size(void)
{
    if (g_mh_state.mode == MULTIHEAD_MODE_EXTEND) {
        g_mh_state.virtual_width = 0;
        g_mh_state.virtual_height = 0;
        for (int i = 0; i < g_mh_state.monitor_count; i++) {
            uint32_t right = g_mh_state.monitors[i].position_x +
                             g_mh_state.monitors[i].mode.hdisplay;
            uint32_t bottom = g_mh_state.monitors[i].position_y +
                              g_mh_state.monitors[i].mode.vdisplay;
            if (right > g_mh_state.virtual_width) g_mh_state.virtual_width = right;
            if (bottom > g_mh_state.virtual_height) g_mh_state.virtual_height = bottom;
        }
    } else if (g_mh_state.monitor_count > 0) {
        /* 镜像模式：使用主显示器尺寸 */
        g_mh_state.virtual_width = g_mh_state.monitors[0].mode.hdisplay;
        g_mh_state.virtual_height = g_mh_state.monitors[0].mode.vdisplay;
    } else {
        g_mh_state.virtual_width = 0;
        g_mh_state.virtual_height = 0;
    }
}

/* ============================================================================
 * 模式切换
 * ========================================================================== */

int drm_multihead_set_mode(multihead_mode_t mode)
{
    if (!g_mh_state.initialized) return -1;

    g_mh_state.mode = mode;

    /* 重新排列显示器位置 */
    for (int i = 0; i < g_mh_state.monitor_count; i++) {
        if (mode == MULTIHEAD_MODE_EXTEND) {
            g_mh_state.monitors[i].position_x = 0;
            for (int j = 0; j < i; j++) {
                g_mh_state.monitors[i].position_x +=
                    g_mh_state.monitors[j].mode.hdisplay;
            }
            g_mh_state.monitors[i].position_y = 0;
        } else {
            g_mh_state.monitors[i].position_x = 0;
            g_mh_state.monitors[i].position_y = 0;
        }
    }

    drm_multihead_update_virtual_size();

    const char *mode_name = "unknown";
    switch (mode) {
        case MULTIHEAD_MODE_EXTEND: mode_name = "extend"; break;
        case MULTIHEAD_MODE_CLONE:  mode_name = "clone"; break;
        case MULTIHEAD_MODE_SINGLE: mode_name = "single"; break;
    }
    vga_printf("[DRM-MH] Mode set to %s, virtual=%ux%u\n",
               mode_name, g_mh_state.virtual_width, g_mh_state.virtual_height);

    return 0;
}

multihead_mode_t drm_multihead_get_mode(void)
{
    return g_mh_state.mode;
}

/* ============================================================================
 * 坐标转换
 * ========================================================================== */

/* 虚拟桌面坐标 -> 显示器坐标 */
int drm_multihead_virtual_to_monitor(int32_t vx, int32_t vy,
                                      int *monitor_index,
                                      int32_t *mx, int32_t *my)
{
    if (!g_mh_state.initialized || !monitor_index || !mx || !my) return -1;

    for (int i = 0; i < g_mh_state.monitor_count; i++) {
        multihead_monitor_t *mon = &g_mh_state.monitors[i];
        if (!mon->active) continue;

        int32_t left = mon->position_x;
        int32_t right = mon->position_x + mon->mode.hdisplay;
        int32_t top = mon->position_y;
        int32_t bottom = mon->position_y + mon->mode.vdisplay;

        if (vx >= left && vx < right && vy >= top && vy < bottom) {
            *monitor_index = i;
            *mx = vx - left;
            *my = vy - top;
            return 0;
        }
    }

    return -1;  /* 坐标不在任何显示器上 */
}

/* 显示器坐标 -> 虚拟桌面坐标 */
int drm_multihead_monitor_to_virtual(int monitor_index, int32_t mx, int32_t my,
                                      int32_t *vx, int32_t *vy)
{
    if (!g_mh_state.initialized || !vx || !vy) return -1;
    if (monitor_index < 0 || monitor_index >= g_mh_state.monitor_count) return -1;

    multihead_monitor_t *mon = &g_mh_state.monitors[monitor_index];
    *vx = mon->position_x + mx;
    *vy = mon->position_y + my;
    return 0;
}

/* ============================================================================
 * 主显示器管理
 * ========================================================================== */

int drm_multihead_set_primary(int index)
{
    if (!g_mh_state.initialized) return -1;
    if (index < 0 || index >= g_mh_state.monitor_count) return -1;

    for (int i = 0; i < g_mh_state.monitor_count; i++) {
        g_mh_state.monitors[i].primary = (i == index) ? 1 : 0;
    }
    g_mh_state.primary_index = index;

    vga_printf("[DRM-MH] Primary monitor set to %d\n", index);
    return 0;
}

int drm_multihead_get_primary(void)
{
    return g_mh_state.primary_index;
}

/* ============================================================================
 * 查询
 * ========================================================================== */

int drm_multihead_get_monitor_count(void)
{
    return g_mh_state.monitor_count;
}

int drm_multihead_get_monitor(int index, multihead_monitor_t *monitor)
{
    if (!monitor) return -1;
    if (index < 0 || index >= g_mh_state.monitor_count) return -1;
    *monitor = g_mh_state.monitors[index];
    return 0;
}

void drm_multihead_get_virtual_size(uint32_t *width, uint32_t *height)
{
    if (width) *width = g_mh_state.virtual_width;
    if (height) *height = g_mh_state.virtual_height;
}

/* ============================================================================
 * 热插拔检测（简化版：定期轮询连接器状态）
 * ========================================================================== */

int drm_multihead_hotplug_poll(void)
{
    if (!g_mh_state.initialized) return 0;

    int changed = 0;
    drm_device_t *dev = drm_get_device();
    if (!dev) return 0;

    /* 检查每个连接器状态 */
    for (int i = 0; i < (int)dev->connector_count; i++) {
        drm_connector_status_t status = drm_get_connector_status(i);

        /* 查找是否已添加 */
        int found = -1;
        for (int j = 0; j < g_mh_state.monitor_count; j++) {
            if (g_mh_state.monitors[j].connector_id == (uint32_t)i) {
                found = j;
                break;
            }
        }

        if (status == DRM_CONNECTOR_CONNECTED && found < 0) {
            /* 新连接：添加显示器 */
            drm_mode_t modes[DRM_MAX_MODE];
            int count = drm_get_connector_modes(i, modes, DRM_MAX_MODE);
            if (count > 0) {
                /* 使用第一个模式（通常是首选模式） */
                drm_multihead_add_monitor(i, i, i, &modes[0]);
                changed = 1;
            }
        } else if (status == DRM_CONNECTOR_DISCONNECTED && found >= 0) {
            /* 断开：移除显示器 */
            drm_multihead_remove_monitor(found);
            changed = 1;
        }
    }

    return changed;
}

/* ============================================================================
 * 跨显示器绘图辅助
 * ========================================================================== */

/* 在虚拟桌面坐标绘制点（自动分配到对应显示器） */
int drm_multihead_put_pixel(int32_t vx, int32_t vy, uint32_t color)
{
    int mon_idx;
    int32_t mx, my;

    if (drm_multihead_virtual_to_monitor(vx, vy, &mon_idx, &mx, &my) != 0) {
        return -1;
    }

    /* 这里应该调用具体后端的put_pixel */
    /* 简化：返回成功，实际绘制由上层处理 */
    (void)color;
    return 0;
}

/* 填充矩形（可能跨多个显示器） */
int drm_multihead_fill_rect(int32_t vx, int32_t vy, uint32_t w, uint32_t h,
                             uint32_t color)
{
    if (!g_mh_state.initialized) return -1;

    int drawn = 0;
    for (int i = 0; i < g_mh_state.monitor_count; i++) {
        multihead_monitor_t *mon = &g_mh_state.monitors[i];
        if (!mon->active) continue;

        /* 计算矩形与显示器的交集 */
        int32_t left = mon->position_x;
        int32_t right = mon->position_x + mon->mode.hdisplay;
        int32_t top = mon->position_y;
        int32_t bottom = mon->position_y + mon->mode.vdisplay;

        int32_t clip_x = (vx > left) ? vx : left;
        int32_t clip_y = (vy > top) ? vy : top;
        int32_t clip_right = ((int32_t)(vx + w) < right) ? (int32_t)(vx + w) : right;
        int32_t clip_bottom = ((int32_t)(vy + h) < bottom) ? (int32_t)(vy + h) : bottom;

        if (clip_x < clip_right && clip_y < clip_bottom) {
            /* 在这个显示器上绘制 */
            int32_t mx = clip_x - left;
            int32_t my = clip_y - top;
            uint32_t mw = clip_right - clip_x;
            uint32_t mh = clip_bottom - clip_y;

            /* 实际绘制由后端处理 */
            (void)mx; (void)my; (void)mw; (void)mh; (void)color;
            drawn++;
        }
    }

    return drawn;
}
