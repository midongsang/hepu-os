/* ============================================================================
 * 赫普OS - DRM/KMS 接口框架实现
 * V8.94: 初始实现（方案B第一步）
 * 显示模式设置、帧缓冲管理、平面/CRTC/连接器管理
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "drm.h"
#include "../include/vga.h"
#include "../include/kheap.h"

/* 全局DRM设备 */
static drm_device_t g_drm_device;
static int g_drm_initialized = 0;

/* ============================================================================
 * 初始化/关闭
 * ============================================================================ */

int drm_init(void)
{
    if (g_drm_initialized) return 0;

    memset(&g_drm_device, 0, sizeof(drm_device_t));
    strncpy(g_drm_device.name, "hepu-drm", 31);
    g_drm_device.next_fb_id = 1;

    /* 创建默认CRTC */
    g_drm_device.crtc_count = 1;
    g_drm_device.crtcs[0].id = 0;
    g_drm_device.crtcs[0].possible_encoders = 0x1;
    g_drm_device.crtcs[0].active = 0;
    g_drm_device.crtcs[0].used = 1;

    /* 创建默认编码器 */
    g_drm_device.encoder_count = 1;
    g_drm_device.encoders[0].id = 0;
    g_drm_device.encoders[0].possible_crtcs = 0x1;
    g_drm_device.encoders[0].possible_clones = 0;
    g_drm_device.encoders[0].used = 1;

    /* 创建默认连接器（虚拟显示器） */
    g_drm_device.connector_count = 1;
    g_drm_device.connectors[0].id = 0;
    g_drm_device.connectors[0].connector_type = DRM_CONNECTOR_VIRTUAL;
    g_drm_device.connectors[0].connector_type_id = 0;
    g_drm_device.connectors[0].status = DRM_CONNECTOR_CONNECTED;
    g_drm_device.connectors[0].encoder_id = 0;
    g_drm_device.connectors[0].used = 1;

    /* 添加标准显示模式 */
    drm_mode_t modes[] = {
        {0, 640, 0,0,0,0, 480, 0,0,0,0, 60, 0, "640x480"},
        {0, 800, 0,0,0,0, 600, 0,0,0,0, 60, 0, "800x600"},
        {0, 1024,0,0,0,0, 768, 0,0,0,0, 60, 0, "1024x768"},
        {0, 1280,0,0,0,0, 720, 0,0,0,0, 60, 0, "1280x720"},
        {0, 1280,0,0,0,0, 1024,0,0,0,0, 60, 0, "1280x1024"},
        {0, 1366,0,0,0,0, 768, 0,0,0,0, 60, 0, "1366x768"},
        {0, 1440,0,0,0,0, 900, 0,0,0,0, 60, 0, "1440x900"},
        {0, 1600,0,0,0,0, 900, 0,0,0,0, 60, 0, "1600x900"},
        {0, 1920,0,0,0,0, 1080,0,0,0,0, 60, 0, "1920x1080"},
    };
    int mode_count = sizeof(modes) / sizeof(modes[0]);
    if (mode_count > DRM_MAX_MODE) mode_count = DRM_MAX_MODE;
    memcpy(g_drm_device.connectors[0].modes, modes, mode_count * sizeof(drm_mode_t));
    g_drm_device.connectors[0].mode_count = mode_count;

    /* 创建主平面 */
    g_drm_device.plane_count = 1;
    g_drm_device.planes[0].id = 0;
    g_drm_device.planes[0].type = DRM_PLANE_TYPE_PRIMARY;
    g_drm_device.planes[0].possible_crtcs = 0x1;
    g_drm_device.planes[0].enabled = 0;
    g_drm_device.planes[0].used = 1;

    /* 创建光标平面 */
    if (g_drm_device.plane_count < DRM_MAX_PLANE) {
        g_drm_device.planes[1].id = 1;
        g_drm_device.planes[1].type = DRM_PLANE_TYPE_CURSOR;
        g_drm_device.planes[1].possible_crtcs = 0x1;
        g_drm_device.planes[1].enabled = 0;
        g_drm_device.planes[1].used = 1;
        g_drm_device.plane_count = 2;
    }

    g_drm_initialized = 1;
    vga_puts("[DRM] DRM/KMS framework initialized\n");
    vga_printf("[DRM]   CRTCs: %u, Connectors: %u, Planes: %u\n",
               g_drm_device.crtc_count,
               g_drm_device.connector_count,
               g_drm_device.plane_count);
    return 0;
}

void drm_shutdown(void)
{
    if (!g_drm_initialized) return;

    /* 销毁所有帧缓冲 */
    for (int i = 0; i < DRM_MAX_FB; i++) {
        if (g_drm_device.framebuffers[i].used) {
            if (g_drm_device.framebuffers[i].vaddr) {
                kfree(g_drm_device.framebuffers[i].vaddr);
            }
            g_drm_device.framebuffers[i].used = 0;
        }
    }

    g_drm_initialized = 0;
    vga_puts("[DRM] DRM/KMS framework shutdown\n");
}

drm_device_t *drm_get_device(void)
{
    return &g_drm_device;
}

/* ============================================================================
 * 模式设置
 * ============================================================================ */

int drm_set_mode(uint32_t crtc_id, drm_mode_t *mode, uint32_t fb_id)
{
    if (!g_drm_initialized || !mode) return -1;
    if (crtc_id >= g_drm_device.crtc_count) return -1;

    drm_crtc_t *crtc = &g_drm_device.crtcs[crtc_id];
    if (!crtc->used) return -1;

    /* 检查帧缓冲是否存在 */
    if (fb_id > 0) {
        drm_framebuffer_t *fb = drm_framebuffer_get(fb_id);
        if (!fb) return -1;
        if (fb->width < mode->hdisplay || fb->height < mode->vdisplay) {
            return -1;
        }
    }

    /* 调用后端 */
    if (g_drm_device.set_mode) {
        int ret = g_drm_device.set_mode(crtc_id, mode, fb_id);
        if (ret != 0) return ret;
    }

    /* 更新CRTC状态 */
    crtc->mode = *mode;
    crtc->fb_id = fb_id;
    crtc->active = 1;

    /* 更新连接器当前模式 */
    for (int i = 0; i < (int)g_drm_device.connector_count; i++) {
        if (g_drm_device.connectors[i].encoder_id == crtc->possible_encoders) {
            g_drm_device.connectors[i].current_mode = *mode;
        }
    }

    vga_printf("[DRM] Mode set on CRTC%u: %ux%u@%uHz\n",
               crtc_id, mode->hdisplay, mode->vdisplay, mode->vrefresh);
    return 0;
}

int drm_page_flip(uint32_t crtc_id, uint32_t fb_id)
{
    if (!g_drm_initialized) return -1;
    if (crtc_id >= g_drm_device.crtc_count) return -1;

    drm_crtc_t *crtc = &g_drm_device.crtcs[crtc_id];
    if (!crtc->used || !crtc->active) return -1;

    /* 检查帧缓冲 */
    drm_framebuffer_t *fb = drm_framebuffer_get(fb_id);
    if (!fb) return -1;

    /* 调用后端 */
    if (g_drm_device.page_flip) {
        int ret = g_drm_device.page_flip(crtc_id, fb_id);
        if (ret != 0) return ret;
    }

    crtc->fb_id = fb_id;
    return 0;
}

int drm_get_connector_modes(uint32_t connector_id, drm_mode_t *modes, int max)
{
    if (!g_drm_initialized || !modes || max <= 0) return 0;
    if (connector_id >= g_drm_device.connector_count) return 0;

    drm_connector_t *conn = &g_drm_device.connectors[connector_id];
    if (!conn->used) return 0;

    int count = conn->mode_count;
    if (count > max) count = max;
    memcpy(modes, conn->modes, count * sizeof(drm_mode_t));
    return count;
}

drm_connector_status_t drm_get_connector_status(uint32_t connector_id)
{
    if (!g_drm_initialized) return DRM_CONNECTOR_UNKNOWN;
    if (connector_id >= g_drm_device.connector_count) return DRM_CONNECTOR_UNKNOWN;
    return g_drm_device.connectors[connector_id].status;
}

/* ============================================================================
 * 帧缓冲管理
 * ============================================================================ */

int drm_framebuffer_create(uint32_t width, uint32_t height,
                           drm_format_t format, uint32_t *fb_id)
{
    if (!g_drm_initialized || !fb_id) return -1;
    if (width == 0 || height == 0) return -1;

    /* 查找空闲槽位 */
    int slot = -1;
    for (int i = 0; i < DRM_MAX_FB; i++) {
        if (!g_drm_device.framebuffers[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    /* 计算大小 */
    uint32_t bpp = 32;
    uint32_t pitch = width * (bpp / 8);
    uint32_t size = pitch * height;

    /* 分配内存 */
    void *vaddr = kmalloc(size);
    if (!vaddr) return -1;
    memset(vaddr, 0, size);

    /* 初始化帧缓冲 */
    drm_framebuffer_t *fb = &g_drm_device.framebuffers[slot];
    memset(fb, 0, sizeof(drm_framebuffer_t));
    fb->id = g_drm_device.next_fb_id++;
    fb->width = width;
    fb->height = height;
    fb->pitch = pitch;
    fb->bpp = bpp;
    fb->format = format;
    fb->vaddr = vaddr;
    fb->paddr = (uint32_t)vaddr;  /* 简化：物理地址=虚拟地址 */
    fb->size = size;
    fb->used = 1;

    *fb_id = fb->id;
    vga_printf("[DRM] Framebuffer %u created: %ux%u (%u bytes)\n",
               fb->id, width, height, size);
    return 0;
}

int drm_framebuffer_destroy(uint32_t fb_id)
{
    if (!g_drm_initialized) return -1;

    drm_framebuffer_t *fb = drm_framebuffer_get(fb_id);
    if (!fb) return -1;

    if (fb->vaddr) {
        kfree(fb->vaddr);
    }
    fb->used = 0;

    vga_printf("[DRM] Framebuffer %u destroyed\n", fb_id);
    return 0;
}

drm_framebuffer_t *drm_framebuffer_get(uint32_t fb_id)
{
    if (!g_drm_initialized) return NULL;
    for (int i = 0; i < DRM_MAX_FB; i++) {
        if (g_drm_device.framebuffers[i].used &&
            g_drm_device.framebuffers[i].id == fb_id) {
            return &g_drm_device.framebuffers[i];
        }
    }
    return NULL;
}

void *drm_framebuffer_map(uint32_t fb_id)
{
    drm_framebuffer_t *fb = drm_framebuffer_get(fb_id);
    if (!fb) return NULL;
    return fb->vaddr;
}

/* ============================================================================
 * 平面管理
 * ============================================================================ */

int drm_plane_set(uint32_t plane_id, uint32_t crtc_id, uint32_t fb_id,
                  int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    if (!g_drm_initialized) return -1;
    if (plane_id >= g_drm_device.plane_count) return -1;

    drm_plane_t *plane = &g_drm_device.planes[plane_id];
    if (!plane->used) return -1;

    /* 检查CRTC是否可绑定 */
    if (!(plane->possible_crtcs & (1 << crtc_id))) return -1;

    /* 检查帧缓冲 */
    if (fb_id > 0) {
        drm_framebuffer_t *fb = drm_framebuffer_get(fb_id);
        if (!fb) return -1;
    }

    plane->fb_id = fb_id;
    plane->x = x;
    plane->y = y;
    plane->w = w;
    plane->h = h;
    plane->enabled = 1;

    return 0;
}

int drm_plane_enable(uint32_t plane_id, int enable)
{
    if (!g_drm_initialized) return -1;
    if (plane_id >= g_drm_device.plane_count) return -1;

    drm_plane_t *plane = &g_drm_device.planes[plane_id];
    if (!plane->used) return -1;

    plane->enabled = enable ? 1 : 0;
    return 0;
}

/* ============================================================================
 * 资源枚举
 * ============================================================================ */

int drm_get_crtcs(drm_crtc_t *crtcs, int max)
{
    if (!g_drm_initialized || !crtcs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DRM_MAX_CRTC && count < max; i++) {
        if (g_drm_device.crtcs[i].used) {
            crtcs[count++] = g_drm_device.crtcs[i];
        }
    }
    return count;
}

int drm_get_connectors(drm_connector_t *connectors, int max)
{
    if (!g_drm_initialized || !connectors || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DRM_MAX_CONNECTOR && count < max; i++) {
        if (g_drm_device.connectors[i].used) {
            connectors[count++] = g_drm_device.connectors[i];
        }
    }
    return count;
}

int drm_get_encoders(drm_encoder_t *encoders, int max)
{
    if (!g_drm_initialized || !encoders || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DRM_MAX_ENCODER && count < max; i++) {
        if (g_drm_device.encoders[i].used) {
            encoders[count++] = g_drm_device.encoders[i];
        }
    }
    return count;
}

int drm_get_planes(drm_plane_t *planes, int max)
{
    if (!g_drm_initialized || !planes || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < DRM_MAX_PLANE && count < max; i++) {
        if (g_drm_device.planes[i].used) {
            planes[count++] = g_drm_device.planes[i];
        }
    }
    return count;
}

/* ============================================================================
 * 辅助函数
 * ============================================================================ */

int drm_mode_match(drm_mode_t *mode, uint32_t width, uint32_t height)
{
    if (!mode) return 0;
    return (mode->hdisplay == width && mode->vdisplay == height);
}

void drm_mode_set_name(drm_mode_t *mode)
{
    if (!mode) return;
    /* 简化：手动拼接 "WxH" */
    char buf[32];
    int pos = 0;
    uint32_t w = mode->hdisplay;
    uint32_t h = mode->vdisplay;

    /* 宽度转字符串 */
    char wbuf[16];
    int wlen = 0;
    if (w == 0) wbuf[wlen++] = '0';
    while (w > 0) { wbuf[wlen++] = '0' + (w % 10); w /= 10; }
    for (int i = wlen - 1; i >= 0 && pos < 30; i--) buf[pos++] = wbuf[i];
    buf[pos++] = 'x';

    /* 高度转字符串 */
    char hbuf[16];
    int hlen = 0;
    if (h == 0) hbuf[hlen++] = '0';
    while (h > 0) { hbuf[hlen++] = '0' + (h % 10); h /= 10; }
    for (int i = hlen - 1; i >= 0 && pos < 30; i--) buf[pos++] = hbuf[i];
    buf[pos] = '\0';

    strncpy(mode->name, buf, 31);
    mode->name[31] = '\0';
}

uint32_t drm_mode_vrefresh(drm_mode_t *mode)
{
    if (!mode || mode->htotal == 0 || mode->vtotal == 0) return 0;
    if (mode->clock == 0) return mode->vrefresh;
    return (mode->clock * 1000) / (mode->htotal * mode->vtotal);
}

/* ============================================================================
 * 后端注册
 * ============================================================================ */

int drm_register_backend(const char *name,
                         int (*set_mode)(uint32_t, drm_mode_t*, uint32_t),
                         int (*page_flip)(uint32_t, uint32_t),
                         int (*create_dumb)(uint32_t, uint32_t, uint32_t,
                                           uint32_t*, uint32_t*, uint64_t*),
                         int (*map_dumb)(uint32_t, void**),
                         int (*destroy_dumb)(uint32_t))
{
    if (!g_drm_initialized) {
        drm_init();
    }

    if (name) {
        strncpy(g_drm_device.name, name, 31);
    }
    g_drm_device.set_mode = set_mode;
    g_drm_device.page_flip = page_flip;
    g_drm_device.create_dumb = create_dumb;
    g_drm_device.map_dumb = map_dumb;
    g_drm_device.destroy_dumb = destroy_dumb;

    vga_printf("[DRM] Backend registered: %s\n", name ? name : "unknown");
    return 0;
}
