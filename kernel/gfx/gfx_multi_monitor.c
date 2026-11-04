/* ============================================================================
 * 赫普OS - 多显示器管理实现
 * V8.61: 初始实现
 * 支持虚拟桌面、扩展/镜像模式、跨显示器渲染
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../include/graphics.h"
#include "../include/vga.h"

/* 多显示器配置 */
static gfx_multi_monitor_config_t mm_config;
static int initialized = 0;

/* 外部函数声明（来自gfx_core.c） */
extern int gfx_enum_monitors(gfx_monitor_t *monitors, int max_count);
extern gfx_monitor_t* gfx_get_primary_monitor(void);
extern int gfx_get_monitor_count(void);

/* 外部绘图函数（来自gfx_core.c） */
extern void gfx_put_pixel(uint32_t x, uint32_t y, gfx_color_t color);
extern void gfx_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, gfx_color_t color);
extern void gfx_draw_string(uint32_t x, uint32_t y, const char *str, gfx_color_t fg, gfx_color_t bg);

/* 初始化多显示器管理 */
int gfx_multi_monitor_init(void)
{
    if (initialized) return 0;

    memset(&mm_config, 0, sizeof(mm_config));
    mm_config.layout_mode = GFX_LAYOUT_SINGLE;
    mm_config.configured = 0;

    /* 获取显示器列表 */
    gfx_monitor_t monitors[GFX_MAX_MONITORS];
    int count = gfx_enum_monitors(monitors, GFX_MAX_MONITORS);

    if (count > 0) {
        /* 默认：单显示器模式，使用主显示器 */
        mm_config.virtual_width = monitors[0].width;
        mm_config.virtual_height = monitors[0].height;
        mm_config.positions[0].x = 0;
        mm_config.positions[0].y = 0;
        mm_config.positions[0].rotation = GFX_ROTATE_0;
    }

    initialized = 1;
    vga_printf("[GFX-MM] Multi-monitor manager initialized (%d monitors)\n", count);
    return 0;
}

/* 设置布局模式 */
int gfx_set_layout_mode(gfx_layout_mode_t mode)
{
    if (!initialized) gfx_multi_monitor_init();

    mm_config.layout_mode = mode;

    /* 根据布局模式重新计算虚拟桌面尺寸 */
    gfx_monitor_t monitors[GFX_MAX_MONITORS];
    int count = gfx_enum_monitors(monitors, GFX_MAX_MONITORS);

    if (mode == GFX_LAYOUT_SINGLE || count <= 1) {
        /* 单显示器模式 */
        if (count > 0) {
            mm_config.virtual_width = monitors[0].width;
            mm_config.virtual_height = monitors[0].height;
        }
    } else if (mode == GFX_LAYOUT_MIRROR) {
        /* 镜像模式：虚拟桌面尺寸 = 主显示器尺寸 */
        if (count > 0) {
            mm_config.virtual_width = monitors[0].width;
            mm_config.virtual_height = monitors[0].height;
        }
    } else if (mode == GFX_LAYOUT_EXTEND) {
        /* 扩展模式：自动排列 */
        gfx_auto_arrange_monitors();
    }

    vga_printf("[GFX-MM] Layout mode set to %d (virtual: %dx%d)\n",
               mode, mm_config.virtual_width, mm_config.virtual_height);
    return 0;
}

/* 获取布局模式 */
gfx_layout_mode_t gfx_get_layout_mode(void)
{
    if (!initialized) return GFX_LAYOUT_SINGLE;
    return mm_config.layout_mode;
}

/* 设置显示器位置 */
int gfx_set_monitor_position(uint32_t index, int x, int y)
{
    if (!initialized) gfx_multi_monitor_init();
    if (index >= GFX_MAX_MONITORS) return -1;

    mm_config.positions[index].x = x;
    mm_config.positions[index].y = y;

    /* 重新计算虚拟桌面尺寸 */
    gfx_monitor_t monitors[GFX_MAX_MONITORS];
    int count = gfx_enum_monitors(monitors, GFX_MAX_MONITORS);

    uint32_t max_x = 0, max_y = 0;
    for (int i = 0; i < count; i++) {
        uint32_t right = mm_config.positions[i].x + monitors[i].width;
        uint32_t bottom = mm_config.positions[i].y + monitors[i].height;
        if (right > max_x) max_x = right;
        if (bottom > max_y) max_y = bottom;
    }
    mm_config.virtual_width = max_x;
    mm_config.virtual_height = max_y;

    return 0;
}

/* 获取显示器位置 */
int gfx_get_monitor_position(uint32_t index, int *x, int *y)
{
    if (!initialized || index >= GFX_MAX_MONITORS) return -1;
    if (x) *x = mm_config.positions[index].x;
    if (y) *y = mm_config.positions[index].y;
    return 0;
}

/* 设置显示器旋转 */
int gfx_set_monitor_rotation(uint32_t index, gfx_rotation_t rotation)
{
    if (!initialized || index >= GFX_MAX_MONITORS) return -1;
    mm_config.positions[index].rotation = rotation;
    return 0;
}

/* 获取虚拟桌面尺寸 */
int gfx_get_virtual_desktop_size(uint32_t *width, uint32_t *height)
{
    if (!initialized) gfx_multi_monitor_init();
    if (width) *width = mm_config.virtual_width;
    if (height) *height = mm_config.virtual_height;
    return 0;
}

/* 虚拟桌面坐标 -> 显示器坐标 */
int gfx_virtual_to_monitor(uint32_t index, int vx, int vy, int *mx, int *my)
{
    if (!initialized || index >= GFX_MAX_MONITORS) return -1;

    int local_x = vx - mm_config.positions[index].x;
    int local_y = vy - mm_config.positions[index].y;

    /* 处理旋转 */
    gfx_monitor_t monitors[GFX_MAX_MONITORS];
    gfx_enum_monitors(monitors, GFX_MAX_MONITORS);

    if (index < GFX_MAX_MONITORS) {
        uint32_t w = monitors[index].width;
        uint32_t h = monitors[index].height;

        switch (mm_config.positions[index].rotation) {
            case GFX_ROTATE_90:
                *mx = h - 1 - local_y;
                *my = local_x;
                break;
            case GFX_ROTATE_180:
                *mx = w - 1 - local_x;
                *my = h - 1 - local_y;
                break;
            case GFX_ROTATE_270:
                *mx = local_y;
                *my = w - 1 - local_x;
                break;
            default:
                *mx = local_x;
                *my = local_y;
                break;
        }
    } else {
        *mx = local_x;
        *my = local_y;
    }

    return 0;
}

/* 显示器坐标 -> 虚拟桌面坐标 */
int gfx_monitor_to_virtual(uint32_t index, int mx, int my, int *vx, int *vy)
{
    if (!initialized || index >= GFX_MAX_MONITORS) return -1;

    int local_x = mx, local_y = my;

    /* 处理旋转（逆变换） */
    gfx_monitor_t monitors[GFX_MAX_MONITORS];
    gfx_enum_monitors(monitors, GFX_MAX_MONITORS);

    if (index < GFX_MAX_MONITORS) {
        uint32_t w = monitors[index].width;
        uint32_t h = monitors[index].height;

        switch (mm_config.positions[index].rotation) {
            case GFX_ROTATE_90:
                local_x = my;
                local_y = h - 1 - mx;
                break;
            case GFX_ROTATE_180:
                local_x = w - 1 - mx;
                local_y = h - 1 - my;
                break;
            case GFX_ROTATE_270:
                local_x = w - 1 - my;
                local_y = mx;
                break;
            default:
                break;
        }
    }

    *vx = local_x + mm_config.positions[index].x;
    *vy = local_y + mm_config.positions[index].y;
    return 0;
}

/* 获取坐标所在的显示器 */
int gfx_get_monitor_at_point(int x, int y)
{
    if (!initialized) return 0;

    gfx_monitor_t monitors[GFX_MAX_MONITORS];
    int count = gfx_enum_monitors(monitors, GFX_MAX_MONITORS);

    for (int i = 0; i < count; i++) {
        int px = mm_config.positions[i].x;
        int py = mm_config.positions[i].y;
        if (x >= px && x < px + (int)monitors[i].width &&
            y >= py && y < py + (int)monitors[i].height) {
            return i;
        }
    }
    return 0;  /* 默认返回主显示器 */
}

/* 在虚拟桌面上绘制像素 */
void gfx_virtual_put_pixel(int x, int y, gfx_color_t color)
{
    if (!initialized) gfx_multi_monitor_init();

    if (mm_config.layout_mode == GFX_LAYOUT_SINGLE) {
        /* 单显示器模式：直接绘制 */
        gfx_put_pixel(x, y, color);
        return;
    }

    if (mm_config.layout_mode == GFX_LAYOUT_MIRROR) {
        /* 镜像模式：在所有显示器上绘制相同内容 */
        gfx_monitor_t monitors[GFX_MAX_MONITORS];
        int count = gfx_enum_monitors(monitors, GFX_MAX_MONITORS);
        for (int i = 0; i < count; i++) {
            if (x < (int)monitors[i].width && y < (int)monitors[i].height) {
                gfx_put_pixel(x, y, color);
            }
        }
        return;
    }

    /* 扩展模式：找到坐标所在的显示器并绘制 */
    int monitor_idx = gfx_get_monitor_at_point(x, y);
    int mx, my;
    if (gfx_virtual_to_monitor(monitor_idx, x, y, &mx, &my) == 0) {
        gfx_monitor_t monitors[GFX_MAX_MONITORS];
        gfx_enum_monitors(monitors, GFX_MAX_MONITORS);
        if (monitor_idx < GFX_MAX_MONITORS &&
            mx >= 0 && mx < (int)monitors[monitor_idx].width &&
            my >= 0 && my < (int)monitors[monitor_idx].height) {
            gfx_put_pixel(mx, my, color);
        }
    }
}

/* 在虚拟桌面上填充矩形 */
void gfx_virtual_fill_rect(int x, int y, int width, int height, gfx_color_t color)
{
    if (!initialized) gfx_multi_monitor_init();

    if (mm_config.layout_mode == GFX_LAYOUT_SINGLE) {
        gfx_fill_rect(x, y, width, height, color);
        return;
    }

    /* 简单实现：逐像素绘制（优化版本可以按显示器裁剪） */
    for (int py = y; py < y + height; py++) {
        for (int px = x; px < x + width; px++) {
            gfx_virtual_put_pixel(px, py, color);
        }
    }
}

/* 在虚拟桌面上绘制字符串 */
void gfx_virtual_draw_string(int x, int y, const char *str, gfx_color_t fg, gfx_color_t bg)
{
    if (!initialized) gfx_multi_monitor_init();

    if (mm_config.layout_mode == GFX_LAYOUT_SINGLE) {
        gfx_draw_string(x, y, str, fg, bg);
        return;
    }

    /* 简单实现：逐字符绘制 */
    int cx = x;
    while (*str) {
        /* 每个字符8x16像素 */
        for (int py = 0; py < 16; py++) {
            for (int px = 0; px < 8; px++) {
                /* 简化：假设字符位图，实际应该从字体获取 */
                gfx_color_t c = (px < 4 && py < 8) ? fg : bg;
                gfx_virtual_put_pixel(cx + px, y + py, c);
            }
        }
        cx += 8;
        str++;
    }
}

/* 自动排列显示器（横向扩展） */
int gfx_auto_arrange_monitors(void)
{
    if (!initialized) gfx_multi_monitor_init();

    gfx_monitor_t monitors[GFX_MAX_MONITORS];
    int count = gfx_enum_monitors(monitors, GFX_MAX_MONITORS);

    if (count <= 0) return -1;

    /* 横向排列：显示器0在左，显示器1在右，依此类推 */
    int current_x = 0;
    uint32_t max_height = 0;

    for (int i = 0; i < count; i++) {
        mm_config.positions[i].x = current_x;
        mm_config.positions[i].y = 0;
        mm_config.positions[i].rotation = GFX_ROTATE_0;
        current_x += monitors[i].width;
        if (monitors[i].height > max_height) {
            max_height = monitors[i].height;
        }
    }

    mm_config.virtual_width = current_x;
    mm_config.virtual_height = max_height;
    mm_config.layout_mode = GFX_LAYOUT_EXTEND;

    vga_printf("[GFX-MM] Auto-arranged %d monitors: virtual desktop %dx%d\n",
               count, mm_config.virtual_width, mm_config.virtual_height);

    return 0;
}

/* 获取多显示器配置 */
void gfx_get_multi_monitor_config(gfx_multi_monitor_config_t *config)
{
    if (!initialized) gfx_multi_monitor_init();
    if (config) memcpy(config, &mm_config, sizeof(mm_config));
}

/* 应用多显示器配置 */
int gfx_apply_multi_monitor_config(gfx_multi_monitor_config_t *config)
{
    if (!config) return -1;
    if (!initialized) gfx_multi_monitor_init();

    memcpy(&mm_config, config, sizeof(mm_config));
    mm_config.configured = 1;

    vga_printf("[GFX-MM] Config applied: mode=%d, virtual=%dx%d\n",
               mm_config.layout_mode, mm_config.virtual_width, mm_config.virtual_height);

    return 0;
}

/* ============================================================================
 * V8.68: 多显示器深化
 * 帧缓冲管理、窗口移动、热插拔重配置、任务栏策略
 * ========================================================================== */

#include "../include/kheap.h"

/* 每个显示器的离屏帧缓冲 */
typedef struct {
    uint32_t *buffer;
    uint32_t width;
    uint32_t height;
    int initialized;
} monitor_fb_t;

static monitor_fb_t monitor_fbs[GFX_MAX_MONITORS];
static int primary_monitor = 0;
static gfx_taskbar_policy_t taskbar_policy = GFX_TASKBAR_PRIMARY_ONLY;

/* 初始化显示器帧缓冲 */
int gfx_monitor_fb_init(uint32_t index)
{
    if (index >= GFX_MAX_MONITORS) return -1;

    gfx_monitor_t monitors[GFX_MAX_MONITORS];
    int count = gfx_enum_monitors(monitors, GFX_MAX_MONITORS);
    if (index >= (uint32_t)count) return -2;

    monitor_fb_t *fb = &monitor_fbs[index];
    if (fb->initialized) gfx_monitor_fb_destroy(index);

    fb->width = monitors[index].width;
    fb->height = monitors[index].height;
    fb->buffer = (uint32_t *)kmalloc(fb->width * fb->height * sizeof(uint32_t));
    if (!fb->buffer) return -3;

    memset(fb->buffer, 0, fb->width * fb->height * sizeof(uint32_t));
    fb->initialized = 1;

    vga_printf("[GFX-MM] Monitor %d framebuffer initialized: %dx%d\n",
               index, fb->width, fb->height);
    return 0;
}

/* 销毁显示器帧缓冲 */
void gfx_monitor_fb_destroy(uint32_t index)
{
    if (index >= GFX_MAX_MONITORS) return;
    monitor_fb_t *fb = &monitor_fbs[index];
    if (fb->buffer) {
        kfree(fb->buffer);
        fb->buffer = NULL;
    }
    fb->initialized = 0;
    fb->width = 0;
    fb->height = 0;
}

/* 将帧缓冲刷新到实际显示器 */
int gfx_monitor_fb_flush(uint32_t index)
{
    if (index >= GFX_MAX_MONITORS) return -1;
    monitor_fb_t *fb = &monitor_fbs[index];
    if (!fb->initialized || !fb->buffer) return -2;

    /* 简化：将离屏缓冲复制到主帧缓冲对应区域 */
    int mx, my;
    gfx_get_monitor_position(index, &mx, &my);

    for (uint32_t y = 0; y < fb->height; y++) {
        for (uint32_t x = 0; x < fb->width; x++) {
            gfx_color_t color = fb->buffer[y * fb->width + x];
            gfx_put_pixel(mx + x, my + y, color);
        }
    }
    return 0;
}

/* 获取显示器帧缓冲指针 */
void *gfx_monitor_fb_get(uint32_t index)
{
    if (index >= GFX_MAX_MONITORS) return NULL;
    return monitor_fbs[index].buffer;
}

/* 将窗口移动到指定显示器 */
int gfx_move_window_to_monitor(int window_x, int window_y,
                                int window_w, int window_h,
                                uint32_t target_monitor,
                                int *new_x, int *new_y)
{
    if (!initialized) gfx_multi_monitor_init();

    gfx_monitor_t monitors[GFX_MAX_MONITORS];
    int count = gfx_enum_monitors(monitors, GFX_MAX_MONITORS);
    if (target_monitor >= (uint32_t)count) return -1;

    /* 获取目标显示器在虚拟桌面中的位置 */
    int mx, my;
    if (gfx_get_monitor_position(target_monitor, &mx, &my) != 0) return -2;

    /* 计算窗口在目标显示器中的居中位置 */
    int target_w = monitors[target_monitor].width;
    int target_h = monitors[target_monitor].height;

    int nx = mx + (target_w - window_w) / 2;
    int ny = my + (target_h - window_h) / 2;

    /* 确保窗口在显示器范围内 */
    if (nx < mx) nx = mx;
    if (ny < my) ny = my;
    if (nx + window_w > mx + target_w) nx = mx + target_w - window_w;
    if (ny + window_h > my + target_h) ny = my + target_h - window_h;

    if (new_x) *new_x = nx;
    if (new_y) *new_y = ny;

    vga_printf("[GFX-MM] Window moved to monitor %d: (%d,%d)\n",
               target_monitor, nx, ny);
    return 0;
}

/* 显示器热插拔重配置 */
int gfx_monitor_hotplug_reconfigure(void)
{
    if (!initialized) gfx_multi_monitor_init();

    /* 重新枚举显示器 */
    gfx_monitor_t monitors[GFX_MAX_MONITORS];
    int count = gfx_enum_monitors(monitors, GFX_MAX_MONITORS);

    /* 销毁所有帧缓冲 */
    for (int i = 0; i < GFX_MAX_MONITORS; i++) {
        gfx_monitor_fb_destroy(i);
    }

    /* 重新初始化帧缓冲 */
    for (int i = 0; i < count; i++) {
        gfx_monitor_fb_init(i);
    }

    /* 重新排列显示器 */
    if (count > 1) {
        gfx_auto_arrange_monitors();
    } else {
        mm_config.layout_mode = GFX_LAYOUT_SINGLE;
        if (count > 0) {
            mm_config.virtual_width = monitors[0].width;
            mm_config.virtual_height = monitors[0].height;
        }
    }

    /* 确保主显示器有效 */
    if (primary_monitor >= count) primary_monitor = 0;

    vga_printf("[GFX-MM] Hotplug reconfigure: %d monitors, virtual %dx%d\n",
               count, mm_config.virtual_width, mm_config.virtual_height);
    return 0;
}

/* 设置任务栏策略 */
int gfx_set_taskbar_policy(gfx_taskbar_policy_t policy)
{
    taskbar_policy = policy;
    vga_printf("[GFX-MM] Taskbar policy set to %d\n", policy);
    return 0;
}

/* 获取任务栏策略 */
gfx_taskbar_policy_t gfx_get_taskbar_policy(void)
{
    return taskbar_policy;
}

/* 获取主显示器索引 */
int gfx_get_primary_monitor_index(void)
{
    return primary_monitor;
}

/* 设置主显示器 */
int gfx_set_primary_monitor_index(uint32_t index)
{
    gfx_monitor_t monitors[GFX_MAX_MONITORS];
    int count = gfx_enum_monitors(monitors, GFX_MAX_MONITORS);
    if (index >= (uint32_t)count) return -1;

    primary_monitor = index;
    vga_printf("[GFX-MM] Primary monitor set to %d\n", index);
    return 0;
}
