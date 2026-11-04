/* ============================================================================
 * 赫普OS - 显示合成器 (SurfaceFlinger式)
 * 图层合成 + Vsync + HDR + 截图 + FPS统计
 * ========================================================================== */

#include <stdint.h>
#include "compositor.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"

static compositor_layer_t layers[COMPOSITOR_MAX_LAYERS];
static compositor_display_t displays[COMPOSITOR_MAX_DISPLAYS];
static int layer_count = 0;
static int compositor_initialized = 0;

void compositor_init(void)
{
    for (int i = 0; i < COMPOSITOR_MAX_LAYERS; i++) {
        layers[i].used = 0;
    }
    for (int i = 0; i < COMPOSITOR_MAX_DISPLAYS; i++) {
        displays[i].used = 0;
    }
    layer_count = 0;
    compositor_initialized = 1;

    /* 注册主显示 */
    compositor_display_t *d = &displays[0];
    memset(d, 0, sizeof(compositor_display_t));
    d->used = 1;
    d->name = "Primary Display";
    d->state = DISPLAY_CONNECTED;
    d->current_mode.width = 320;
    d->current_mode.height = 200;
    d->current_mode.refresh_rate = 60;
    d->current_mode.color_depth = 16;
    d->pitch = 320 * 2;
    d->fb_size = d->pitch * 200;
    d->framebuffer = kmalloc(d->fb_size);
    /* V3.83 诊断(走串口防滚动): 分配后立即检查下一个块头, 二分定位越界写时机 */
    {
        extern void *kheap_peek_next(void *blk);
        extern void serial_puts(uint16_t base, const char *s);
        extern void serial_puthex(uint16_t base, uint32_t val);
        extern void serial_putc(uint16_t base, char c);
        void *nx = kheap_peek_next(d->framebuffer);
        serial_puts(0x3F8, "[COMP] fb=");
        serial_puthex(0x3F8, (uint32_t)d->framebuffer);
        serial_puts(0x3F8, " next=");
        serial_puthex(0x3F8, (uint32_t)nx);
        serial_puts(0x3F8, "\n");
    }
    d->vsync_enabled = 1;
    d->brightness = 200;
    d->hdr_enabled = 0;

    /* 支持的模式 */
    d->mode_count = 4;
    d->supported_modes[0].width = 320; d->supported_modes[0].height = 200; d->supported_modes[0].refresh_rate = 60;
    d->supported_modes[1].width = 640; d->supported_modes[1].height = 480; d->supported_modes[1].refresh_rate = 60;
    d->supported_modes[2].width = 720; d->supported_modes[2].height = 1280; d->supported_modes[2].refresh_rate = 60;
    d->supported_modes[3].width = 1080; d->supported_modes[3].height = 1920; d->supported_modes[3].refresh_rate = 60;

    /* 创建默认图层: 背景层 */
    int bg = compositor_create_layer(LAYER_TYPE_COLOR, 0, 0, 320, 200);
    compositor_set_z_order(bg, 0);
    compositor_set_alpha(bg, 1.0f);

    vga_puts("[COMPOSITOR] compositor init done \n");
    vga_printf(" main display: %dx%d@%dHz, %dbpp\n",
               d->current_mode.width, d->current_mode.height,
               d->current_mode.refresh_rate, d->current_mode.color_depth);
}

int compositor_create_layer(layer_type_t type, int x, int y, int w, int h)
{
    if (!compositor_initialized) return -1;
    for (int i = 0; i < COMPOSITOR_MAX_LAYERS; i++) {
        if (!layers[i].used) {
            memset(&layers[i], 0, sizeof(compositor_layer_t));
            layers[i].used = 1;
            layers[i].id = i;
            layers[i].type = type;
            layers[i].x = x;
            layers[i].y = y;
            layers[i].width = w;
            layers[i].height = h;
            layers[i].z_order = layer_count;
            layers[i].alpha = 1.0f;
            layers[i].blend = COMPOSITOR_OPAQUE;
            layers[i].visible = 1;
            layers[i].dirty = 1;
            layer_count++;
            return i;
        }
    }
    return -1;
}

int compositor_destroy_layer(int layer_id)
{
    if (layer_id < 0 || layer_id >= COMPOSITOR_MAX_LAYERS || !layers[layer_id].used) return -1;
    layers[layer_id].used = 0;
    layer_count--;
    return 0;
}

int compositor_set_position(int layer_id, int x, int y)
{
    if (layer_id < 0 || layer_id >= COMPOSITOR_MAX_LAYERS || !layers[layer_id].used) return -1;
    layers[layer_id].x = x;
    layers[layer_id].y = y;
    layers[layer_id].dirty = 1;
    return 0;
}

int compositor_set_size(int layer_id, int w, int h)
{
    if (layer_id < 0 || layer_id >= COMPOSITOR_MAX_LAYERS || !layers[layer_id].used) return -1;
    layers[layer_id].width = w;
    layers[layer_id].height = h;
    layers[layer_id].dirty = 1;
    return 0;
}

int compositor_set_z_order(int layer_id, int z)
{
    if (layer_id < 0 || layer_id >= COMPOSITOR_MAX_LAYERS || !layers[layer_id].used) return -1;
    layers[layer_id].z_order = z;
    layers[layer_id].dirty = 1;
    return 0;
}

int compositor_set_alpha(int layer_id, float alpha)
{
    if (layer_id < 0 || layer_id >= COMPOSITOR_MAX_LAYERS || !layers[layer_id].used) return -1;
    layers[layer_id].alpha = alpha;
    layers[layer_id].blend = (alpha < 1.0f) ? COMPOSITOR_BLEND : COMPOSITOR_OPAQUE;
    layers[layer_id].dirty = 1;
    return 0;
}

int compositor_set_blend(int layer_id, compositor_blend_t blend)
{
    if (layer_id < 0 || layer_id >= COMPOSITOR_MAX_LAYERS || !layers[layer_id].used) return -1;
    layers[layer_id].blend = blend;
    layers[layer_id].dirty = 1;
    return 0;
}

int compositor_set_visible(int layer_id, int visible)
{
    if (layer_id < 0 || layer_id >= COMPOSITOR_MAX_LAYERS || !layers[layer_id].used) return -1;
    layers[layer_id].visible = visible ? 1 : 0;
    layers[layer_id].dirty = 1;
    return 0;
}

int compositor_set_buffer(int layer_id, void *buffer, uint32_t size)
{
    if (layer_id < 0 || layer_id >= COMPOSITOR_MAX_LAYERS || !layers[layer_id].used) return -1;
    layers[layer_id].buffer = buffer;
    layers[layer_id].buffer_size = size;
    layers[layer_id].dirty = 1;
    return 0;
}

int compositor_invalidate(int layer_id)
{
    if (layer_id < 0 || layer_id >= COMPOSITOR_MAX_LAYERS || !layers[layer_id].used) return -1;
    layers[layer_id].dirty = 1;
    return 0;
}

/* Alpha混合: src over dst */
static inline uint16_t blend_pixel(uint16_t dst, uint16_t src, float alpha)
{
    if (alpha >= 1.0f) return src;
    if (alpha <= 0.0f) return dst;

    int dr = (dst >> 11) & 0x1F, dg = (dst >> 5) & 0x3F, db = dst & 0x1F;
    int sr = (src >> 11) & 0x1F, sg = (src >> 5) & 0x3F, sb = src & 0x1F;

    int r = (int)(sr * alpha + dr * (1 - alpha));
    int g = (int)(sg * alpha + dg * (1 - alpha));
    int b = (int)(sb * alpha + db * (1 - alpha));

    return ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F);
}

int compositor_composite(void)
{
    if (!compositor_initialized || !displays[0].used) return -1;

    compositor_display_t *d = &displays[0];
    uint16_t *fb = (uint16_t *)d->framebuffer;

    /* 按Z序排序并合成 */
    for (int z = 0; z < COMPOSITOR_MAX_LAYERS; z++) {
        compositor_layer_t *layer = NULL;
        for (int i = 0; i < COMPOSITOR_MAX_LAYERS; i++) {
            if (layers[i].used && layers[i].visible && layers[i].z_order == z) {
                layer = &layers[i];
                break;
            }
        }
        if (!layer) continue;

        /* 合成图层到帧缓冲 */
        if (layer->type == LAYER_TYPE_COLOR) {
            /* 纯色填充 */
            uint16_t color = 0x0000;  /* 黑色背景 */
            for (int y = layer->y; y < layer->y + layer->height && y < d->current_mode.height; y++) {
                for (int x = layer->x; x < layer->x + layer->width && x < d->current_mode.width; x++) {
                    if (y >= 0 && x >= 0) {
                        if (layer->blend == COMPOSITOR_BLEND) {
                            fb[y * d->current_mode.width + x] = blend_pixel(
                                fb[y * d->current_mode.width + x], color, layer->alpha);
                        } else {
                            fb[y * d->current_mode.width + x] = color;
                        }
                    }
                }
            }
        } else if (layer->buffer && layer->type == LAYER_TYPE_TEXTURE) {
            /* 纹理合成 */
            uint16_t *tex = (uint16_t *)layer->buffer;
            for (int y = 0; y < layer->height && y + layer->y < d->current_mode.height; y++) {
                for (int x = 0; x < layer->width && x + layer->x < d->current_mode.width; x++) {
                    int dst_x = layer->x + x;
                    int dst_y = layer->y + y;
                    if (dst_x >= 0 && dst_y >= 0) {
                        uint16_t src = tex[y * layer->width + x];
                        if (layer->blend == COMPOSITOR_BLEND) {
                            fb[dst_y * d->current_mode.width + dst_x] = blend_pixel(
                                fb[dst_y * d->current_mode.width + dst_x], src, layer->alpha);
                        } else {
                            fb[dst_y * d->current_mode.width + dst_x] = src;
                        }
                    }
                }
            }
        }
        layer->dirty = 0;
    }

    d->frames_composited++;
    return 0;
}

void compositor_handle_vsync(void)
{
    if (!compositor_initialized) return;
    for (int i = 0; i < COMPOSITOR_MAX_DISPLAYS; i++) {
        if (displays[i].used && displays[i].vsync_enabled) {
            displays[i].vsync_count++;
            /* 触发合成 */
            compositor_composite();
        }
    }
}

int compositor_set_vsync(int enable)
{
    if (!compositor_initialized) return -1;
    displays[0].vsync_enabled = enable ? 1 : 0;
    return 0;
}

int compositor_set_brightness(int display, int brightness)
{
    if (display < 0 || display >= COMPOSITOR_MAX_DISPLAYS || !displays[display].used) return -1;
    if (brightness < 0) brightness = 0;
    if (brightness > 255) brightness = 255;
    displays[display].brightness = brightness;
    return 0;
}

int compositor_set_mode(int display, int width, int height, int refresh)
{
    if (display < 0 || display >= COMPOSITOR_MAX_DISPLAYS || !displays[display].used) return -1;
    displays[display].current_mode.width = width;
    displays[display].current_mode.height = height;
    displays[display].current_mode.refresh_rate = refresh;
    displays[display].pitch = width * 2;
    if (displays[display].framebuffer) kfree(displays[display].framebuffer);
    displays[display].fb_size = displays[display].pitch * height;
    displays[display].framebuffer = kmalloc(displays[display].fb_size);
    vga_printf("[COMPOSITOR] display mode switch: %dx%d@%dHz\n", width, height, refresh);
    return 0;
}

int compositor_get_display_info(int display, compositor_display_t *info)
{
    if (display < 0 || display >= COMPOSITOR_MAX_DISPLAYS || !displays[display].used) return -1;
    if (info) memcpy(info, &displays[display], sizeof(compositor_display_t));
    return 0;
}

int compositor_get_layer_info(int layer_id, compositor_layer_t *info)
{
    if (layer_id < 0 || layer_id >= COMPOSITOR_MAX_LAYERS || !layers[layer_id].used) return -1;
    if (info) memcpy(info, &layers[layer_id], sizeof(compositor_layer_t));
    return 0;
}

int compositor_screenshot(void *buffer, uint32_t size)
{
    if (!buffer || !compositor_initialized || !displays[0].used) return -1;
    uint32_t copy_size = (size < displays[0].fb_size) ? size : displays[0].fb_size;
    memcpy(buffer, displays[0].framebuffer, copy_size);
    return copy_size;
}

int compositor_set_hdr(int display, int enable)
{
    if (display < 0 || display >= COMPOSITOR_MAX_DISPLAYS || !displays[display].used) return -1;
    displays[display].hdr_enabled = enable ? 1 : 0;
    vga_printf("[COMPOSITOR] HDR %s\n", enable ? " enabled " : " disabled ");
    return 0;
}

float compositor_get_fps(int display)
{
    if (display < 0 || display >= COMPOSITOR_MAX_DISPLAYS || !displays[display].used) return 0;
    return (float)displays[display].current_mode.refresh_rate;
}

int compositor_layer_count(void)
{
    return layer_count;
}
