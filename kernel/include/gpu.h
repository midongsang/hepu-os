#ifndef GPU_H
#define GPU_H

#include <stdint.h>

#define GPU_MAX_DEVICES 2
#define GPU_MAX_TEXTURES 32

/* GPU类型 */
typedef enum {
    GPU_TYPE_SOFTWARE = 0,
    GPU_TYPE_VESA,
    GPU_TYPE_INTEL,
    GPU_TYPE_AMD,
    GPU_TYPE_NVIDIA,
    GPU_TYPE_MALI,
    GPU_TYPE_ADRENO,
} gpu_type_t;

/* 像素格式 */
typedef enum {
    GPU_FMT_RGB565 = 0,
    GPU_FMT_RGB888,
    GPU_FMT_RGBA8888,
    GPU_FMT_BGR888,
    GPU_FMT_ARGB8888,
} gpu_format_t;

/* 纹理 */
typedef struct {
    int used;
    uint32_t width;
    uint32_t height;
    gpu_format_t format;
    void *data;
    uint32_t size;
    int texture_id;
} gpu_texture_t;

/* 顶点 */
typedef struct {
    float x, y, z;
    float u, v;
    uint8_t r, g, b, a;
} gpu_vertex_t;

/* 矩形 */
typedef struct {
    int x, y;
    int w, h;
} gpu_rect_t;

/* 颜色 */
typedef struct {
    uint8_t r, g, b, a;
} gpu_color_t;

/* GPU设备 */
typedef struct {
    int used;
    const char *name;
    gpu_type_t type;
    uint32_t io_base;
    uint32_t mem_base;
    int irq;
    uint32_t width;
    uint32_t height;
    gpu_format_t format;
    void *framebuffer;
    uint32_t fb_size;
    uint32_t pitch;
    int initialized;
    gpu_texture_t textures[GPU_MAX_TEXTURES];
    int texture_count;
    /* 统计 */
    uint64_t triangles_drawn;
    uint64_t pixels_filled;
    uint64_t draw_calls;
} gpu_device_t;

/* 初始化GPU子系统 */
void gpu_init(void);

/* 设置模式 */
int gpu_set_mode(int device, uint32_t width, uint32_t height, gpu_format_t format);

/* 清屏 */
int gpu_clear(int device, gpu_color_t color);

/* 绘制像素 */
int gpu_putpixel(int device, int x, int y, gpu_color_t color);

/* 读取像素 */
gpu_color_t gpu_getpixel(int device, int x, int y);

/* 绘制线 */
int gpu_draw_line(int device, int x1, int y1, int x2, int y2, gpu_color_t color);

/* 绘制矩形 (空心) */
int gpu_draw_rect(int device, int x, int y, int w, int h, gpu_color_t color);

/* 填充矩形 */
int gpu_fill_rect(int device, int x, int y, int w, int h, gpu_color_t color);

/* 绘制三角形 (空心) */
int gpu_draw_triangle(int device, int x1, int y1, int x2, int y2, int x3, int y3, gpu_color_t color);

/* 填充三角形 (光栅化) */
int gpu_fill_triangle(int device, gpu_vertex_t *v1, gpu_vertex_t *v2, gpu_vertex_t *v3);

/* 绘制圆形 */
int gpu_draw_circle(int device, int cx, int cy, int radius, gpu_color_t color);

/* 填充圆形 */
int gpu_fill_circle(int device, int cx, int cy, int radius, gpu_color_t color);

/* 绘制文字 */
int gpu_draw_text(int device, int x, int y, const char *text, gpu_color_t color);

/* 纹理管理 */
int gpu_create_texture(int device, uint32_t width, uint32_t height, gpu_format_t format, const void *data);
int gpu_delete_texture(int device, int tex_id);
int gpu_bind_texture(int device, int tex_id);

/* 绘制纹理 */
int gpu_draw_texture(int device, int x, int y, int w, int h, int tex_id);

/* 翻转 (双缓冲) */
int gpu_flip(int device);

/* 获取帧缓冲 */
void *gpu_get_framebuffer(int device);

/* 获取分辨率 */
int gpu_get_resolution(int device, uint32_t *width, uint32_t *height);

/* GPU中断处理 */
void gpu_handle_irq(void);

/* 获取统计信息 */
void gpu_get_stats(int device, uint64_t *triangles, uint64_t *pixels, uint64_t *draw_calls);

#endif /* GPU_H */
