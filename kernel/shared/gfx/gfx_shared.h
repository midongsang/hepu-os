/* ============================================================================
 * 赫普OS - 图形子系统共享（架构无关）
 * 32位和64位共用的图形数据结构、常量和基础算法
 * V0.10.86: P2-3图形子系统共享，统一源码树
 * ========================================================================== */

#ifndef SHARED_GFX_H
#define SHARED_GFX_H

#include <stdint.h>

/* 颜色格式：32位ARGB */
typedef uint32_t gfx_color_t;

/* 颜色宏 */
#define GFX_ARGB(a, r, g, b) \
    (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define GFX_RGB(r, g, b) GFX_ARGB(0xFF, r, g, b)

/* 常用颜色 */
#define GFX_BLACK       GFX_RGB(0x00, 0x00, 0x00)
#define GFX_WHITE       GFX_RGB(0xFF, 0xFF, 0xFF)
#define GFX_RED         GFX_RGB(0xFF, 0x00, 0x00)
#define GFX_GREEN       GFX_RGB(0x00, 0xFF, 0x00)
#define GFX_BLUE        GFX_RGB(0x00, 0x00, 0xFF)
#define GFX_YELLOW      GFX_RGB(0xFF, 0xFF, 0x00)
#define GFX_CYAN        GFX_RGB(0x00, 0xFF, 0xFF)
#define GFX_MAGENTA     GFX_RGB(0xFF, 0x00, 0xFF)
#define GFX_GRAY        GFX_RGB(0x80, 0x80, 0x80)
#define GFX_LIGHT_GRAY  GFX_RGB(0xC0, 0xC0, 0xC0)
#define GFX_DARK_GRAY   GFX_RGB(0x40, 0x40, 0x40)

/* 点 */
typedef struct {
    int x;
    int y;
} gfx_point_t;

/* 矩形 */
typedef struct {
    int x;
    int y;
    int width;
    int height;
} gfx_rect_t;

/* 尺寸 */
typedef struct {
    int width;
    int height;
} gfx_size_t;

/* 帧缓冲信息 */
typedef struct {
    void    *framebuffer;   /* 帧缓冲地址 */
    uint32_t width;         /* 宽度（像素） */
    uint32_t height;        /* 高度（像素） */
    uint32_t pitch;         /* 每行字节数 */
    uint8_t  bpp;           /* 每像素位数 */
    uint8_t  initialized;   /* 是否初始化 */
} gfx_framebuffer_t;

/* 字体类型 */
typedef enum {
    GFX_FONT_BITMAP = 0,    /* 位图字体 */
    GFX_FONT_TTF,           /* TrueType字体 */
    GFX_FONT_TYPE_MAX
} gfx_font_type_t;

/* 字体信息 */
typedef struct {
    const char *name;
    gfx_font_type_t type;
    int width;          /* 字符宽度（位图字体） */
    int height;         /* 字符高度 */
    int ascent;         /* 基线以上高度 */
    int descent;        /* 基线以下高度 */
    int line_gap;       /* 行间距 */
    void *data;         /* 字体数据 */
    uint32_t data_size; /* 字体数据大小 */
} gfx_font_t;

/* 图形后端类型 */
typedef enum {
    GFX_BACKEND_VGA = 0,    /* VGA文本模式 */
    GFX_BACKEND_VBE,        /* VBE线性帧缓冲 */
    GFX_BACKEND_GOP,        /* UEFI GOP */
    GFX_BACKEND_VIRTIO_GPU, /* VirtIO GPU */
    GFX_BACKEND_TYPE_MAX
} gfx_backend_type_t;

/* 图形后端操作表 */
typedef struct {
    int  (*init)(gfx_framebuffer_t *fb);
    void (*set_pixel)(gfx_framebuffer_t *fb, int x, int y, gfx_color_t color);
    gfx_color_t (*get_pixel)(gfx_framebuffer_t *fb, int x, int y);
    void (*fill_rect)(gfx_framebuffer_t *fb, int x, int y, int w, int h, gfx_color_t color);
    void (*draw_hline)(gfx_framebuffer_t *fb, int x, int y, int w, gfx_color_t color);
    void (*draw_vline)(gfx_framebuffer_t *fb, int x, int y, int h, gfx_color_t color);
    void (*clear)(gfx_framebuffer_t *fb, gfx_color_t color);
    void (*scroll)(gfx_framebuffer_t *fb, int lines);
    int  (*set_mode)(int width, int height, int bpp);
    void (*swap_buffers)(void);
} gfx_backend_ops_t;

/* 窗口状态 */
typedef enum {
    GFX_WINDOW_NORMAL = 0,
    GFX_WINDOW_MINIMIZED,
    GFX_WINDOW_MAXIMIZED,
    GFX_WINDOW_FULLSCREEN
} gfx_window_state_t;

/* 窗口 */
typedef struct gfx_window {
    int id;
    char title[64];
    gfx_rect_t rect;
    gfx_window_state_t state;
    int visible;
    int focused;
    int z_order;
    void *content;
    struct gfx_window *next;
    struct gfx_window *prev;
} gfx_window_t;

/* 鼠标光标 */
typedef struct {
    int x;
    int y;
    int visible;
    const uint8_t *bitmap;
    int width;
    int height;
    int hot_x;
    int hot_y;
} gfx_cursor_t;

/* 基础算法：点是否在矩形内 */
static inline int gfx_point_in_rect(gfx_point_t p, gfx_rect_t r) {
    return (p.x >= r.x && p.x < r.x + r.width &&
            p.y >= r.y && p.y < r.y + r.height);
}

/* 基础算法：矩形是否相交 */
static inline int gfx_rect_intersect(gfx_rect_t a, gfx_rect_t b, gfx_rect_t *out) {
    int x1 = a.x > b.x ? a.x : b.x;
    int y1 = a.y > b.y ? a.y : b.y;
    int x2 = (a.x + a.width) < (b.x + b.width) ? (a.x + a.width) : (b.x + b.width);
    int y2 = (a.y + a.height) < (b.y + b.height) ? (a.y + a.height) : (b.y + b.height);
    if (x1 >= x2 || y1 >= y2) return 0;
    if (out) {
        out->x = x1;
        out->y = y1;
        out->width = x2 - x1;
        out->height = y2 - y1;
    }
    return 1;
}

/* 基础算法：颜色混合（alpha混合） */
static inline gfx_color_t gfx_blend(gfx_color_t src, gfx_color_t dst) {
    uint8_t sa = (src >> 24) & 0xFF;
    if (sa == 0xFF) return src;
    if (sa == 0) return dst;
    uint8_t sr = (src >> 16) & 0xFF;
    uint8_t sg = (src >> 8) & 0xFF;
    uint8_t sb = src & 0xFF;
    uint8_t dr = (dst >> 16) & 0xFF;
    uint8_t dg = (dst >> 8) & 0xFF;
    uint8_t db = dst & 0xFF;
    uint8_t da = (dst >> 24) & 0xFF;
    uint8_t r = (sr * sa + dr * (255 - sa)) / 255;
    uint8_t g = (sg * sa + dg * (255 - sa)) / 255;
    uint8_t b = (sb * sa + db * (255 - sa)) / 255;
    uint8_t a = sa + (da * (255 - sa)) / 255;
    return GFX_ARGB(a, r, g, b);
}

/* 基础算法：颜色插值 */
static inline gfx_color_t gfx_lerp_color(gfx_color_t c1, gfx_color_t c2, int t, int t_max) {
    if (t <= 0) return c1;
    if (t >= t_max) return c2;
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    uint8_t r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    uint8_t r = r1 + (r2 - r1) * t / t_max;
    uint8_t g = g1 + (g2 - g1) * t / t_max;
    uint8_t b = b1 + (b2 - b1) * t / t_max;
    return GFX_RGB(r, g, b);
}

#endif /* SHARED_GFX_H */
