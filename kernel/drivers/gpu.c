/* ============================================================================
 * 赫普OS - GPU 驱动
 * 软件光栅化 + 2D加速 + 纹理 + 三角形填充
 * ========================================================================== */

#include <stdint.h>
#include "gpu.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"

static gpu_device_t devices[GPU_MAX_DEVICES];
static int gpu_initialized = 0;

/* 8x8 字体 */
static const uint8_t font8x8[128][8] = {
    ['A'] = {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00},
    ['B'] = {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00},
    ['C'] = {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},
    ['D'] = {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00},
    ['E'] = {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00},
    ['F'] = {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00},
    ['G'] = {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3E,0x00},
    ['H'] = {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00},
    ['I'] = {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    ['J'] = {0x1E,0x0C,0x0C,0x0C,0x6C,0x6C,0x38,0x00},
    ['K'] = {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00},
    ['L'] = {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00},
    ['M'] = {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00},
    ['N'] = {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00},
    ['O'] = {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    ['P'] = {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00},
    ['Q'] = {0x3C,0x66,0x66,0x66,0x66,0x6C,0x36,0x00},
    ['R'] = {0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x00},
    ['S'] = {0x3E,0x60,0x60,0x3C,0x06,0x06,0x7C,0x00},
    ['T'] = {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    ['U'] = {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    ['V'] = {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00},
    ['W'] = {0x63,0x63,0x6B,0x7F,0x7F,0x77,0x63,0x00},
    ['X'] = {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00},
    ['Y'] = {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00},
    ['Z'] = {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00},
    ['0'] = {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00},
    ['1'] = {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    ['2'] = {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00},
    ['3'] = {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
    ['4'] = {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00},
    ['5'] = {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},
    ['6'] = {0x3C,0x60,0x7C,0x66,0x66,0x66,0x3C,0x00},
    ['7'] = {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00},
    ['8'] = {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},
    ['9'] = {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00},
    [' '] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    ['.'] = {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    [','] = {0x00,0x00,0x00,0x00,0x00,0x18,0x30,0x00},
    ['!'] = {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00},
    ['?'] = {0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x00},
    ['-'] = {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    ['+'] = {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    ['='] = {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},
    [':'] = {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00},
    ['/'] = {0x06,0x0C,0x18,0x30,0x60,0x40,0x00,0x00},
};

void gpu_init(void)
{
    for (int i = 0; i < GPU_MAX_DEVICES; i++) {
        devices[i].used = 0;
    }
    gpu_initialized = 1;

    /* 注册默认软件GPU */
    gpu_device_t *gpu = &devices[0];
    memset(gpu, 0, sizeof(gpu_device_t));
    gpu->used = 1;
    gpu->name = "Software GPU";
    gpu->type = GPU_TYPE_SOFTWARE;
    gpu->width = 320;
    gpu->height = 200;
    gpu->format = GPU_FMT_RGB565;
    gpu->pitch = gpu->width * 2;
    gpu->fb_size = gpu->pitch * gpu->height;
    gpu->framebuffer = kmalloc(gpu->fb_size);
    gpu->initialized = 1;

    vga_puts("[GPU] GPU subsystem init done \n");
    vga_printf(" device: %s, %dx%d, %s\n", gpu->name, gpu->width, gpu->height,
               gpu->format == GPU_FMT_RGB565 ? "RGB565" : "RGB888");
}

static inline void set_pixel_rgb565(gpu_device_t *gpu, int x, int y, gpu_color_t color)
{
    if (x < 0 || x >= (int)gpu->width || y < 0 || y >= (int)gpu->height) return;
    uint16_t *fb = (uint16_t *)gpu->framebuffer;
    fb[y * gpu->width + x] = ((color.r >> 3) << 11) | ((color.g >> 2) << 5) | (color.b >> 3);
}

int gpu_set_mode(int device, uint32_t width, uint32_t height, gpu_format_t format)
{
    if (device < 0 || device >= GPU_MAX_DEVICES || !devices[device].used) return -1;
    devices[device].width = width;
    devices[device].height = height;
    devices[device].format = format;
    devices[device].pitch = width * (format == GPU_FMT_RGB565 ? 2 : 3);
    if (devices[device].framebuffer) kfree(devices[device].framebuffer);
    devices[device].fb_size = devices[device].pitch * height;
    devices[device].framebuffer = kmalloc(devices[device].fb_size);
    return 0;
}

int gpu_clear(int device, gpu_color_t color)
{
    if (device < 0 || device >= GPU_MAX_DEVICES || !devices[device].used) return -1;
    gpu_device_t *gpu = &devices[device];
    for (uint32_t y = 0; y < gpu->height; y++) {
        for (uint32_t x = 0; x < gpu->width; x++) {
            set_pixel_rgb565(gpu, x, y, color);
        }
    }
    gpu->draw_calls++;
    return 0;
}

int gpu_putpixel(int device, int x, int y, gpu_color_t color)
{
    if (device < 0 || device >= GPU_MAX_DEVICES || !devices[device].used) return -1;
    set_pixel_rgb565(&devices[device], x, y, color);
    devices[device].pixels_filled++;
    return 0;
}

gpu_color_t gpu_getpixel(int device, int x, int y)
{
    gpu_color_t c = {0, 0, 0, 0};
    if (device < 0 || device >= GPU_MAX_DEVICES || !devices[device].used) return c;
    gpu_device_t *gpu = &devices[device];
    if (x < 0 || x >= (int)gpu->width || y < 0 || y >= (int)gpu->height) return c;
    uint16_t *fb = (uint16_t *)gpu->framebuffer;
    uint16_t px = fb[y * gpu->width + x];
    c.r = (px >> 11) << 3;
    c.g = ((px >> 5) & 0x3F) << 2;
    c.b = (px & 0x1F) << 3;
    return c;
}

int gpu_draw_line(int device, int x1, int y1, int x2, int y2, gpu_color_t color)
{
    if (device < 0 || device >= GPU_MAX_DEVICES || !devices[device].used) return -1;
    int dx = (x2 > x1) ? x2 - x1 : x1 - x2;
    int dy = (y2 > y1) ? y2 - y1 : y1 - y2;
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    while (1) {
        gpu_putpixel(device, x1, y1, color);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 < dx) { err += dx; y1 += sy; }
    }
    devices[device].draw_calls++;
    return 0;
}

int gpu_draw_rect(int device, int x, int y, int w, int h, gpu_color_t color)
{
    gpu_draw_line(device, x, y, x + w, y, color);
    gpu_draw_line(device, x + w, y, x + w, y + h, color);
    gpu_draw_line(device, x + w, y + h, x, y + h, color);
    gpu_draw_line(device, x, y + h, x, y, color);
    return 0;
}

int gpu_fill_rect(int device, int x, int y, int w, int h, gpu_color_t color)
{
    if (device < 0 || device >= GPU_MAX_DEVICES || !devices[device].used) return -1;
    gpu_device_t *gpu = &devices[device];
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            set_pixel_rgb565(gpu, i, j, color);
        }
    }
    gpu->pixels_filled += w * h;
    gpu->draw_calls++;
    return 0;
}

int gpu_draw_triangle(int device, int x1, int y1, int x2, int y2, int x3, int y3, gpu_color_t color)
{
    gpu_draw_line(device, x1, y1, x2, y2, color);
    gpu_draw_line(device, x2, y2, x3, y3, color);
    gpu_draw_line(device, x3, y3, x1, y1, color);
    return 0;
}

/* 三角形光栅化 (扫描线填充) */
int gpu_fill_triangle(int device, gpu_vertex_t *v1, gpu_vertex_t *v2, gpu_vertex_t *v3)
{
    if (device < 0 || device >= GPU_MAX_DEVICES || !devices[device].used) return -1;
    gpu_device_t *gpu = &devices[device];

    int x1 = (int)v1->x, y1 = (int)v1->y;
    int x2 = (int)v2->x, y2 = (int)v2->y;
    int x3 = (int)v3->x, y3 = (int)v3->y;

    /* 按y排序 */
    if (y1 > y2) { int t=x1;x1=x2;x2=t; t=y1;y1=y2;y2=t; }
    if (y2 > y3) { int t=x2;x2=x3;x3=t; t=y2;y2=y3;y3=t; }
    if (y1 > y2) { int t=x1;x1=x2;x2=t; t=y1;y1=y2;y2=t; }

    gpu_color_t color = {v1->r, v1->g, v1->b, v1->a};

    int total_height = y3 - y1;
    for (int i = 0; i < total_height; i++) {
        int y = y1 + i;
        int second_half = i > y2 - y1 || y2 == y1;
        int segment_height = second_half ? y3 - y2 : y2 - y1;
        if (segment_height == 0) continue;

        float alpha = (float)i / total_height;
        float beta = (float)(i - (second_half ? y2 - y1 : 0)) / segment_height;

        int ax = x1 + (x3 - x1) * alpha;
        int bx = second_half ? x2 + (x3 - x2) * beta : x1 + (x2 - x1) * beta;

        if (ax > bx) { int t = ax; ax = bx; bx = t; }
        for (int x = ax; x <= bx; x++) {
            set_pixel_rgb565(gpu, x, y, color);
        }
    }
    gpu->triangles_drawn++;
    gpu->draw_calls++;
    return 0;
}

int gpu_draw_circle(int device, int cx, int cy, int radius, gpu_color_t color)
{
    int x = radius, y = 0, err = 0;
    while (x >= y) {
        gpu_putpixel(device, cx + x, cy + y, color);
        gpu_putpixel(device, cx + y, cy + x, color);
        gpu_putpixel(device, cx - y, cy + x, color);
        gpu_putpixel(device, cx - x, cy + y, color);
        gpu_putpixel(device, cx - x, cy - y, color);
        gpu_putpixel(device, cx - y, cy - x, color);
        gpu_putpixel(device, cx + y, cy - x, color);
        gpu_putpixel(device, cx + x, cy - y, color);
        if (err <= 0) { y++; err += 2*y + 1; }
        if (err > 0) { x--; err -= 2*x + 1; }
    }
    devices[device].draw_calls++;
    return 0;
}

int gpu_fill_circle(int device, int cx, int cy, int radius, gpu_color_t color)
{
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            if (x*x + y*y <= radius*radius) {
                gpu_putpixel(device, cx + x, cy + y, color);
            }
        }
    }
    devices[device].draw_calls++;
    return 0;
}

int gpu_draw_text(int device, int x, int y, const char *text, gpu_color_t color)
{
    if (!text) return -1;
    int cx = x;
    while (*text) {
        unsigned char c = (unsigned char)*text;
        if ((c < 128 && font8x8[c][0] != 0) || c == ' ') {
            for (int row = 0; row < 8; row++) {
                for (int col = 0; col < 8; col++) {
                    if (font8x8[c][row] & (1 << (7 - col))) {
                        gpu_putpixel(device, cx + col, y + row, color);
                    }
                }
            }
        }
        cx += 8;
        text++;
    }
    devices[device].draw_calls++;
    return 0;
}

int gpu_create_texture(int device, uint32_t width, uint32_t height, gpu_format_t format, const void *data)
{
    if (device < 0 || device >= GPU_MAX_DEVICES || !devices[device].used) return -1;
    gpu_device_t *gpu = &devices[device];
    for (int i = 0; i < GPU_MAX_TEXTURES; i++) {
        if (!gpu->textures[i].used) {
            gpu->textures[i].used = 1;
            gpu->textures[i].width = width;
            gpu->textures[i].height = height;
            gpu->textures[i].format = format;
            gpu->textures[i].texture_id = i;
            uint32_t bpp = (format == GPU_FMT_RGB565) ? 2 : 3;
            gpu->textures[i].size = width * height * bpp;
            gpu->textures[i].data = kmalloc(gpu->textures[i].size);
            if (data) memcpy(gpu->textures[i].data, data, gpu->textures[i].size);
            gpu->texture_count++;
            return i;
        }
    }
    return -1;
}

int gpu_delete_texture(int device, int tex_id)
{
    if (device < 0 || device >= GPU_MAX_DEVICES || !devices[device].used) return -1;
    if (tex_id < 0 || tex_id >= GPU_MAX_TEXTURES) return -1;
    if (devices[device].textures[tex_id].data) kfree(devices[device].textures[tex_id].data);
    devices[device].textures[tex_id].used = 0;
    return 0;
}

int gpu_draw_texture(int device, int x, int y, int w, int h, int tex_id)
{
    if (device < 0 || device >= GPU_MAX_DEVICES || !devices[device].used) return -1;
    if (tex_id < 0 || tex_id >= GPU_MAX_TEXTURES || !devices[device].textures[tex_id].used) return -1;
    gpu_device_t *gpu = &devices[device];
    gpu_texture_t *tex = &gpu->textures[tex_id];
    uint16_t *src = (uint16_t *)tex->data;
    for (int j = 0; j < h && j < (int)tex->height; j++) {
        for (int i = 0; i < w && i < (int)tex->width; i++) {
            uint16_t px = src[j * tex->width + i];
            gpu_color_t c = {(px >> 11) << 3, ((px >> 5) & 0x3F) << 2, (px & 0x1F) << 3, 255};
            set_pixel_rgb565(gpu, x + i, y + j, c);
        }
    }
    gpu->draw_calls++;
    return 0;
}

int gpu_flip(int device)
{
    /* 原型阶段: 软件渲染直接写入帧缓冲, 无需翻转 */
    (void)device;
    return 0;
}

void *gpu_get_framebuffer(int device)
{
    if (device < 0 || device >= GPU_MAX_DEVICES || !devices[device].used) return NULL;
    return devices[device].framebuffer;
}

int gpu_get_resolution(int device, uint32_t *width, uint32_t *height)
{
    if (device < 0 || device >= GPU_MAX_DEVICES || !devices[device].used) return -1;
    if (width) *width = devices[device].width;
    if (height) *height = devices[device].height;
    return 0;
}

void gpu_handle_irq(void)
{
    /* 原型阶段 */
}

void gpu_get_stats(int device, uint64_t *triangles, uint64_t *pixels, uint64_t *draw_calls)
{
    if (device < 0 || device >= GPU_MAX_DEVICES || !devices[device].used) return;
    if (triangles) *triangles = devices[device].triangles_drawn;
    if (pixels) *pixels = devices[device].pixels_filled;
    if (draw_calls) *draw_calls = devices[device].draw_calls;
}
