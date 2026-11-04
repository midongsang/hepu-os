/* ============================================================================
 * 赫普OS - DRM/KMS 后端适配器
 * V8.95: 将现有gfx_core后端（GOP/VBE/virtio-gpu）包装为DRM后端
 * 方案B第二步：DRM/KMS与现有图形子系统集成
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "drm.h"
#include "graphics.h"
#include "../include/vga.h"

/* 外部图形后端函数（来自gfx_core） */
extern int gfx_set_mode(uint32_t width, uint32_t height, uint32_t bpp);
extern void *gfx_get_framebuffer(void);
extern uint32_t gfx_get_width(void);
extern uint32_t gfx_get_height(void);
extern uint32_t gfx_get_bpp(void);
extern void gfx_present(void);
extern int gfx_is_initialized(void);

/* DRM帧缓冲映射表（DRM FB ID -> 实际内存） */
typedef struct {
    uint32_t fb_id;
    void    *vaddr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    int      used;
} drm_fb_map_t;

#define DRM_FB_MAP_MAX 16
static drm_fb_map_t g_fb_map[DRM_FB_MAP_MAX];
static uint32_t g_next_fb_id = 1;

/* 当前显示的帧缓冲 */
static uint32_t g_current_fb = 0;
static int g_drm_backend_initialized = 0;

/* ============================================================================
 * DRM后端回调函数
 * ========================================================================== */

/* 设置显示模式 */
static int drm_backend_set_mode(uint32_t crtc_id, drm_mode_t *mode, uint32_t fb_id)
{
    if (!mode) return -1;

    vga_printf("[DRM-BE] set_mode: CRTC%u %ux%u@%uHz, FB=%u\n",
               crtc_id, mode->hdisplay, mode->vdisplay,
               mode->vrefresh, fb_id);

    /* 调用gfx_core设置模式 */
    int ret = gfx_set_mode(mode->hdisplay, mode->vdisplay, 32);
    if (ret != 0) {
        vga_puts("[DRM-BE] gfx_set_mode failed\n");
        return -1;
    }

    /* 如果指定了帧缓冲，将其内容复制到主帧缓冲 */
    if (fb_id > 0) {
        drm_fb_map_t *fb = NULL;
        for (int i = 0; i < DRM_FB_MAP_MAX; i++) {
            if (g_fb_map[i].used && g_fb_map[i].fb_id == fb_id) {
                fb = &g_fb_map[i];
                break;
            }
        }
        if (fb && fb->vaddr) {
            void *dst = gfx_get_framebuffer();
            if (dst) {
                uint32_t copy_size = mode->hdisplay * mode->vdisplay * 4;
                if (fb->width * fb->height * 4 < copy_size) {
                    copy_size = fb->width * fb->height * 4;
                }
                memcpy(dst, fb->vaddr, copy_size);
                gfx_present();
            }
        }
        g_current_fb = fb_id;
    }

    return 0;
}

/* 页翻转 */
static int drm_backend_page_flip(uint32_t crtc_id, uint32_t fb_id)
{
    if (fb_id == 0) return -1;

    /* 查找帧缓冲 */
    drm_fb_map_t *fb = NULL;
    for (int i = 0; i < DRM_FB_MAP_MAX; i++) {
        if (g_fb_map[i].used && g_fb_map[i].fb_id == fb_id) {
            fb = &g_fb_map[i];
            break;
        }
    }
    if (!fb || !fb->vaddr) return -1;

    /* 复制到主帧缓冲并刷新 */
    void *dst = gfx_get_framebuffer();
    if (dst) {
        uint32_t w = gfx_get_width();
        uint32_t h = gfx_get_height();
        uint32_t copy_size = w * h * 4;
        if (fb->width * fb->height * 4 < copy_size) {
            copy_size = fb->width * fb->height * 4;
        }
        memcpy(dst, fb->vaddr, copy_size);
        gfx_present();
    }

    g_current_fb = fb_id;
    return 0;
}

/* 创建哑缓冲（通用内存分配） */
static int drm_backend_create_dumb(uint32_t width, uint32_t height, uint32_t bpp,
                                    uint32_t *handle, uint32_t *pitch, uint64_t *offset)
{
    if (!handle || !pitch || !offset) return -1;
    if (width == 0 || height == 0) return -1;

    /* 查找空闲槽位 */
    int slot = -1;
    for (int i = 0; i < DRM_FB_MAP_MAX; i++) {
        if (!g_fb_map[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    /* 计算大小 */
    uint32_t p = width * (bpp / 8);
    uint32_t size = p * height;

    /* 分配内存 */
    extern void *kmalloc(uint32_t size);
    void *vaddr = kmalloc(size);
    if (!vaddr) return -1;
    memset(vaddr, 0, size);

    /* 初始化映射 */
    g_fb_map[slot].fb_id = g_next_fb_id++;
    g_fb_map[slot].vaddr = vaddr;
    g_fb_map[slot].width = width;
    g_fb_map[slot].height = height;
    g_fb_map[slot].pitch = p;
    g_fb_map[slot].used = 1;

    *handle = g_fb_map[slot].fb_id;
    *pitch = p;
    *offset = 0;

    vga_printf("[DRM-BE] create_dumb: %ux%u@%u, handle=%u, size=%u\n",
               width, height, bpp, *handle, size);

    return 0;
}

/* 映射哑缓冲 */
static int drm_backend_map_dumb(uint32_t handle, void **vaddr)
{
    if (!vaddr) return -1;

    for (int i = 0; i < DRM_FB_MAP_MAX; i++) {
        if (g_fb_map[i].used && g_fb_map[i].fb_id == handle) {
            *vaddr = g_fb_map[i].vaddr;
            return 0;
        }
    }
    return -1;
}

/* 销毁哑缓冲 */
static int drm_backend_destroy_dumb(uint32_t handle)
{
    for (int i = 0; i < DRM_FB_MAP_MAX; i++) {
        if (g_fb_map[i].used && g_fb_map[i].fb_id == handle) {
            extern void kfree(void *ptr);
            if (g_fb_map[i].vaddr) {
                kfree(g_fb_map[i].vaddr);
            }
            g_fb_map[i].used = 0;
            vga_printf("[DRM-BE] destroy_dumb: handle=%u\n", handle);
            return 0;
        }
    }
    return -1;
}

/* ============================================================================
 * 初始化
 * ========================================================================== */

int drm_backend_init(void)
{
    if (g_drm_backend_initialized) return 0;

    memset(g_fb_map, 0, sizeof(g_fb_map));
    g_next_fb_id = 1;
    g_current_fb = 0;

    /* 检查gfx_core是否已初始化 */
    if (!gfx_is_initialized()) {
        vga_puts("[DRM-BE] gfx_core not initialized, DRM backend will use software mode\n");
    }

    /* 注册DRM后端 */
    int ret = drm_register_backend(
        "hepu-gfx",
        drm_backend_set_mode,
        drm_backend_page_flip,
        drm_backend_create_dumb,
        drm_backend_map_dumb,
        drm_backend_destroy_dumb
    );

    if (ret != 0) {
        vga_puts("[DRM-BE] Failed to register DRM backend\n");
        return -1;
    }

    g_drm_backend_initialized = 1;
    vga_puts("[DRM-BE] DRM backend adapter initialized (gfx_core integration)\n");
    return 0;
}

/* 获取当前帧缓冲ID */
uint32_t drm_backend_get_current_fb(void)
{
    return g_current_fb;
}

/* 获取主帧缓冲（用于直接渲染） */
void *drm_backend_get_primary_fb(uint32_t *width, uint32_t *height, uint32_t *pitch)
{
    if (width) *width = gfx_get_width();
    if (height) *height = gfx_get_height();
    if (pitch) *pitch = gfx_get_width() * gfx_get_bpp() / 8;
    return gfx_get_framebuffer();
}
