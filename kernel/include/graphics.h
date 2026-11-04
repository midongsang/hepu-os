#ifndef GRAPHICS_H
#define GRAPHICS_H

/* ============================================================================
 * HepuOS 可插拔图形子系统 - 统一API头文件
 * 融合VGA/VBE/virtio-gpu优点，全新自研设计
 *
 * 核心思想：上层统一绘图API，底层多后端自动探测切换
 * 后端优先级：virtio-gpu > VBE > VGA(仅文本)
 * ========================================================================== */

#include <stdint.h>

/* 颜色：32bit ARGB */
typedef uint32_t gfx_color_t;

/* 图形后端类型 */
typedef enum {
    GFX_BACKEND_NONE = 0,
    GFX_BACKEND_VGA,      /* VGA文本模式（应急控制台） */
    GFX_BACKEND_VBE,      /* VBE LFB软渲染 */
    GFX_BACKEND_VIRTIO,   /* virtio-gpu 2D硬件加速 */
    GFX_BACKEND_GOP       /* UEFI GOP帧缓冲 */
} gfx_backend_type_t;

/* 图形后端抽象结构体 */
typedef struct gfx_backend {
    /* 后端类型 */
    gfx_backend_type_t type;
    /* 后端名称 */
    const char *name;
    /* 初始化后端，返回0成功 */
    int (*init)(void);
    /* 关闭图形后端 */
    void (*shutdown)(void);
    /* 设置分辨率 */
    int (*set_mode)(uint32_t w, uint32_t h, uint32_t bpp);
    /* 获取当前分辨率 */
    void (*get_mode)(uint32_t *w, uint32_t *h, uint32_t *bpp);
    /* 矩形填充 */
    void (*fill_rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, gfx_color_t color);
    /* 绘制像素 */
    void (*put_pixel)(uint32_t x, uint32_t y, gfx_color_t color);
    /* 绘制ASCII字符（点阵字体） */
    void (*draw_char)(uint32_t x, uint32_t y, char c, gfx_color_t fg, gfx_color_t bg);
    /* 硬件Blit，把离屏表面拷贝到屏幕 */
    void (*blit)(void* src, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy);
    /* 获取屏幕帧缓冲虚拟地址 */
    void* (*get_fb_vaddr)(void);
    /* 获取帧缓冲物理地址 */
    uint32_t (*get_fb_paddr)(void);
    /* 刷新画面到屏幕（交换缓冲区） */
    void (*present)(void);
    /* 是否支持双缓冲 */
    int (*has_double_buffer)(void);
} gfx_backend_t;

/* 图形子系统配置 */
typedef struct {
    /* 用户指定的显存分区大小（字节），0=自动 */
    uint32_t vram_partition_size;
    /* 用户指定的后端，GFX_BACKEND_NONE=自动探测 */
    gfx_backend_type_t forced_backend;
    /* 默认分辨率 */
    uint32_t default_width;
    uint32_t default_height;
    uint32_t default_bpp;
    /* 是否启用双缓冲 */
    int double_buffer;
} gfx_config_t;

/* 显存分区信息 */
typedef struct {
    uint32_t total_size;       /* 总显存大小 */
    uint32_t used_size;        /* 已使用 */
    uint32_t free_size;        /* 空闲 */
    uint32_t partition_count;  /* 分区数量 */
    uint32_t partition_sizes[8]; /* 各分区大小 */
} gfx_vram_info_t;

/* === 核心管理函数 === */

/* 初始化图形子系统（自动探测后端） */
int gfx_init(gfx_config_t *config);

/* 自动探测并选择图形后端 */
int gfx_probe_backend(void);

/* 获取当前激活后端 */
gfx_backend_t* gfx_get_active_backend(void);

/* 获取当前后端类型 */
gfx_backend_type_t gfx_get_backend_type(void);

/* 注册后端 */
int gfx_register_backend(gfx_backend_t *backend);

/* 切换后端 */
int gfx_switch_backend(gfx_backend_type_t type);

/* 关闭图形子系统 */
void gfx_shutdown(void);

/* === 显存分区管理（用户可自定义大小） === */

/* 设置显存分区大小（用户自定义） */
int gfx_set_vram_partition(uint32_t index, uint32_t size);

/* 获取显存分区信息 */
void gfx_get_vram_info(gfx_vram_info_t *info);

/* 从显存分区分配内存 */
void* gfx_vram_alloc(uint32_t size);

/* 释放显存分区内存 */
void gfx_vram_free(void *ptr);

/* === 多显示器支持（V7.09，V7.29完善） === */

#define GFX_MAX_MONITORS 4
#define GFX_MAX_MODES_PER_MONITOR 16

/* 显示器模式 */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t refresh_rate;
} gfx_mode_t;

/* 显示器信息 */
typedef struct {
    uint32_t index;           /* 显示器索引 */
    char     name[32];        /* 显示器名称 */
    char     manufacturer[16];/* 厂商ID */
    char     serial[16];      /* 序列号 */
    uint32_t width;           /* 当前宽度 */
    uint32_t height;          /* 当前高度 */
    uint32_t bpp;             /* 当前色深 */
    uint32_t refresh_rate;    /* 刷新率(Hz) */
    uint32_t physical_width;  /* 物理宽度(mm) */
    uint32_t physical_height; /* 物理高度(mm) */
    uint8_t  connected;       /* 是否连接 */
    uint8_t  primary;         /* 是否主显示器 */
    uint8_t  enabled;         /* 是否启用 */
    uint8_t  edid_valid;      /* EDID是否有效 */
    uint32_t framebuffer;     /* 帧缓冲物理地址 */
    gfx_mode_t modes[GFX_MAX_MODES_PER_MONITOR]; /* 支持的模式列表 */
    uint32_t mode_count;      /* 支持的模式数量 */
} gfx_monitor_t;

/* EDID数据结构（简化版） */
typedef struct {
    uint8_t  header[8];       /* EDID头 0x00 0xFF... */
    uint16_t manufacturer_id; /* 厂商ID */
    uint16_t product_code;    /* 产品代码 */
    uint32_t serial_number;   /* 序列号 */
    uint8_t  week;            /* 生产周 */
    uint8_t  year;            /* 生产年 */
    uint8_t  version;         /* EDID版本 */
    uint8_t  revision;        /* EDID修订 */
    uint32_t max_h_image;     /* 最大水平图像尺寸(cm) */
    uint32_t max_v_image;     /* 最大垂直图像尺寸(cm) */
    uint8_t  gamma;           /* Gamma */
    uint8_t  features;        /* 特性 */
    uint8_t  color_data[10];  /* 颜色数据 */
    uint8_t  timing_data[19]; /* 时序数据 */
    uint16_t timing_desc[4];  /* 时序描述符 */
    uint8_t  extension;       /* 扩展块数 */
    uint8_t  checksum;        /* 校验和 */
} gfx_edid_t;

/* 解析EDID数据 */
int gfx_edid_parse(const uint8_t *edid_data, uint32_t size, gfx_monitor_t *monitor);

/* 枚举所有连接的显示器 */
int gfx_enum_monitors(gfx_monitor_t *monitors, int max_count);

/* 获取当前主显示器 */
gfx_monitor_t* gfx_get_primary_monitor(void);

/* 设置主显示器 */
int gfx_set_primary_monitor(uint32_t index);

/* 设置显示器分辨率 */
int gfx_set_monitor_mode(uint32_t index, uint32_t width, uint32_t height, uint32_t bpp);

/* 启用/禁用显示器 */
int gfx_enable_monitor(uint32_t index, int enable);

/* 获取显示器数量 */
int gfx_get_monitor_count(void);

/* 检测显示器热插拔 */
int gfx_monitor_hotplug_detect(void);

/* 获取显示器支持的模式列表 */
int gfx_get_monitor_modes(uint32_t index, gfx_mode_t *modes, int max_modes);

/* ============================================================================
 * 多显示器管理（V8.61）
 * ========================================================================== */

/* 显示器布局模式 */
typedef enum {
    GFX_LAYOUT_EXTEND = 0,    /* 扩展模式：桌面扩展到所有显示器 */
    GFX_LAYOUT_MIRROR,        /* 镜像模式：所有显示器显示相同内容 */
    GFX_LAYOUT_SINGLE,        /* 单显示器模式：只使用主显示器 */
} gfx_layout_mode_t;

/* 显示器旋转方向 */
typedef enum {
    GFX_ROTATE_0 = 0,         /* 正常方向 */
    GFX_ROTATE_90,            /* 顺时针90度 */
    GFX_ROTATE_180,           /* 180度 */
    GFX_ROTATE_270,           /* 逆时针90度 */
} gfx_rotation_t;

/* 虚拟桌面中的显示器位置 */
typedef struct {
    int x;                     /* 在虚拟桌面中的X偏移 */
    int y;                     /* 在虚拟桌面中的Y偏移 */
    gfx_rotation_t rotation;   /* 旋转方向 */
} gfx_monitor_position_t;

/* 多显示器配置 */
typedef struct {
    gfx_layout_mode_t layout_mode;  /* 布局模式 */
    uint32_t virtual_width;         /* 虚拟桌面总宽度 */
    uint32_t virtual_height;        /* 虚拟桌面总高度 */
    gfx_monitor_position_t positions[GFX_MAX_MONITORS]; /* 各显示器位置 */
    int configured;                  /* 是否已配置 */
} gfx_multi_monitor_config_t;

/* 初始化多显示器管理 */
int gfx_multi_monitor_init(void);

/* 设置布局模式 */
int gfx_set_layout_mode(gfx_layout_mode_t mode);

/* 获取布局模式 */
gfx_layout_mode_t gfx_get_layout_mode(void);

/* 设置显示器在虚拟桌面中的位置 */
int gfx_set_monitor_position(uint32_t index, int x, int y);

/* 获取显示器在虚拟桌面中的位置 */
int gfx_get_monitor_position(uint32_t index, int *x, int *y);

/* 设置显示器旋转 */
int gfx_set_monitor_rotation(uint32_t index, gfx_rotation_t rotation);

/* 获取虚拟桌面尺寸 */
int gfx_get_virtual_desktop_size(uint32_t *width, uint32_t *height);

/* 将虚拟桌面坐标转换为显示器坐标 */
int gfx_virtual_to_monitor(uint32_t index, int vx, int vy, int *mx, int *my);

/* 将显示器坐标转换为虚拟桌面坐标 */
int gfx_monitor_to_virtual(uint32_t index, int mx, int my, int *vx, int *vy);

/* 获取坐标所在的显示器索引 */
int gfx_get_monitor_at_point(int x, int y);

/* 在虚拟桌面上绘制像素 */
void gfx_virtual_put_pixel(int x, int y, gfx_color_t color);

/* 在虚拟桌面上填充矩形 */
void gfx_virtual_fill_rect(int x, int y, int width, int height, gfx_color_t color);

/* 在虚拟桌面上绘制字符串 */
void gfx_virtual_draw_string(int x, int y, const char *str, gfx_color_t fg, gfx_color_t bg);

/* 自动排列显示器（横向扩展） */
int gfx_auto_arrange_monitors(void);

/* 获取多显示器配置 */
void gfx_get_multi_monitor_config(gfx_multi_monitor_config_t *config);

/* 应用多显示器配置 */
int gfx_apply_multi_monitor_config(gfx_multi_monitor_config_t *config);

/* === 多显示器深化（V8.68） === */

/* 帧缓冲管理 */
int gfx_monitor_fb_init(uint32_t index);
void gfx_monitor_fb_destroy(uint32_t index);
int gfx_monitor_fb_flush(uint32_t index);
void *gfx_monitor_fb_get(uint32_t index);

/* 窗口跨显示器移动 */
int gfx_move_window_to_monitor(int window_x, int window_y,
                                int window_w, int window_h,
                                uint32_t target_monitor,
                                int *new_x, int *new_y);

/* 显示器热插拔重配置 */
int gfx_monitor_hotplug_reconfigure(void);

/* 任务栏多显示器策略 */
typedef enum {
    GFX_TASKBAR_PRIMARY_ONLY = 0,
    GFX_TASKBAR_ALL_MONITORS,
} gfx_taskbar_policy_t;

int gfx_set_taskbar_policy(gfx_taskbar_policy_t policy);
gfx_taskbar_policy_t gfx_get_taskbar_policy(void);

/* 获取/设置主显示器索引 */
int gfx_get_primary_monitor_index(void);
int gfx_set_primary_monitor_index(uint32_t index);

/* === 字体管理器（V7.12） === */

/* 字体类型 */
#define FONT_TYPE_BITMAP_8x16    0
#define FONT_TYPE_ANTIALIASED    1
#define FONT_TYPE_TRUETYPE       2

/* 初始化字体管理器 */
void font_manager_init(void);

/* 设置当前字号（8-64px） */
void font_set_size(uint16_t size);

/* 设置当前字体类型 */
void font_set_type(uint8_t type);

/* 获取当前字号 */
uint16_t font_get_size(void);

/* 获取字符串宽度 */
uint32_t font_get_string_width(const char *str, uint16_t size);

/* 使用字体管理器绘制字符（支持字号缩放和抗锯齿） */
void gfx_draw_char_font(uint32_t x, uint32_t y, char c, gfx_color_t fg, uint16_t size);

/* 使用字体管理器绘制字符串 */
void gfx_draw_string_font(uint32_t x, uint32_t y, const char *str, gfx_color_t fg, uint16_t size);

/* V7.18: 字体类型切换 */
int font_load_truetype(uint8_t *data, uint32_t size);  /* 加载TrueType字体并切换 */
void font_use_bitmap(void);        /* 切换到位图字体 */
void font_use_antialiased(void);   /* 切换到抗锯齿位图字体 */
const char* font_get_type_name(void);  /* 获取当前字体类型名称 */

/* V7.23: 多字体管理 */
int font_register(uint8_t *data, uint32_t size, const char *name);
int font_select(int index);
int font_get_count(void);
const char* font_get_name(int index);
int font_get_current(void);
int font_set_default(int index);
int font_fallback(uint32_t codepoint);

/* V7.24: 字体配置文件加载 */
void font_load_default_config(void);

/* === TrueType字体解析器（V7.17，基础框架） === */

/* 初始化TrueType解析器 */
void tt_font_init(void);

/* 加载TrueType字体文件 */
int tt_font_load(uint8_t *data, uint32_t size);

/* 检查字体是否已加载 */
int tt_font_is_loaded(void);

/* 字符到字形索引映射 */
uint16_t tt_char_to_glyph(uint32_t codepoint);

/* 获取字形度量 */
/* V7.33: 获取units_per_em */
uint16_t tt_get_units_per_em(void);
/* V7.33: 获取字形数量 */
uint16_t tt_get_num_glyphs(void);
int tt_get_glyph_metrics(uint16_t glyph_index, int16_t *advance_width, int16_t *left_side_bearing);

/* 设置像素大小 */
void tt_set_pixel_size(uint16_t pixels);

/* 获取像素高度 */
int tt_get_pixel_height(void);

/* V7.30: TrueType Hinting（字形提示） */
/* 启用/禁用Hinting */
void tt_set_hinting(int enable);
/* 获取Hinting状态 */
int tt_get_hinting(void);
/* 执行字形指令（内部使用） */
int tt_hint_execute(uint8_t *instructions, uint16_t length,
                     int16_t *coords_x, int16_t *coords_y,
                     uint8_t *flags, int num_points,
                     int num_contours, uint16_t *end_pts);

/* 渲染字形到位图（简化版） */
int tt_render_glyph(uint16_t glyph_index, uint8_t *bitmap,
                     uint16_t bitmap_w, uint16_t bitmap_h,
                     int *offset_x, int *offset_y);

/* === 全局上层绘图API，硬件无关 === */

static inline int gfx_set_mode(uint32_t w, uint32_t h, uint32_t bpp)
{
    gfx_backend_t* be = gfx_get_active_backend();
    if (!be || !be->set_mode) return -1;
    return be->set_mode(w, h, bpp);
}

static inline void gfx_get_mode(uint32_t *w, uint32_t *h, uint32_t *bpp)
{
    gfx_backend_t* be = gfx_get_active_backend();
    if (be && be->get_mode) be->get_mode(w, h, bpp);
}

static inline void gfx_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, gfx_color_t color)
{
    gfx_backend_t* be = gfx_get_active_backend();
    if (be && be->fill_rect) be->fill_rect(x, y, w, h, color);
}

static inline void gfx_put_pixel(uint32_t x, uint32_t y, gfx_color_t color)
{
    gfx_backend_t* be = gfx_get_active_backend();
    if (be && be->put_pixel) be->put_pixel(x, y, color);
}

/* V7.12: 从帧缓冲读取像素（用于Alpha混合） */
static inline gfx_color_t gfx_get_pixel(uint32_t x, uint32_t y)
{
    gfx_backend_t* be = gfx_get_active_backend();
    if (!be) return 0;

    void *fb = be->get_fb_vaddr ? be->get_fb_vaddr() : NULL;
    if (!fb) return 0;

    uint32_t w, h, bpp;
    if (be->get_mode) be->get_mode(&w, &h, &bpp);
    else { w = 1024; h = 768; bpp = 32; }

    if (x >= w || y >= h) return 0;

    if (bpp == 32) {
        uint32_t *fb32 = (uint32_t *)fb;
        return fb32[y * w + x];
    } else if (bpp == 24) {
        uint8_t *fb8 = (uint8_t *)fb + y * w * 3 + x * 3;
        return fb8[0] | (fb8[1] << 8) | (fb8[2] << 16);
    }
    return 0;
}

static inline void gfx_draw_char(uint32_t x, uint32_t y, char c, gfx_color_t fg, gfx_color_t bg)
{
    gfx_backend_t* be = gfx_get_active_backend();
    if (be && be->draw_char) be->draw_char(x, y, c, fg, bg);
}

static inline void gfx_blit_rect(void* src, uint32_t sx, uint32_t sy, uint32_t w, uint32_t h, uint32_t dx, uint32_t dy)
{
    gfx_backend_t* be = gfx_get_active_backend();
    if (be && be->blit) be->blit(src, sx, sy, w, h, dx, dy);
}

static inline void* gfx_get_fb_vaddr(void)
{
    gfx_backend_t* be = gfx_get_active_backend();
    if (be && be->get_fb_vaddr) return be->get_fb_vaddr();
    return (void*)0;
}

static inline uint32_t gfx_get_fb_paddr(void)
{
    gfx_backend_t* be = gfx_get_active_backend();
    if (be && be->get_fb_paddr) return be->get_fb_paddr();
    return 0;
}

static inline void gfx_present(void)
{
    gfx_backend_t* be = gfx_get_active_backend();
    if (be && be->present) be->present();
}

static inline int gfx_has_double_buffer(void)
{
    gfx_backend_t* be = gfx_get_active_backend();
    if (be && be->has_double_buffer) return be->has_double_buffer();
    return 0;
}

/* 高级绘图函数（基于基础API实现，所有后端自动支持） */

/* 绘制水平线 */
static inline void gfx_draw_hline(uint32_t x, uint32_t y, uint32_t w, gfx_color_t color)
{
    gfx_fill_rect(x, y, w, 1, color);
}

/* 绘制垂直线 */
static inline void gfx_draw_vline(uint32_t x, uint32_t y, uint32_t h, gfx_color_t color)
{
    gfx_fill_rect(x, y, 1, h, color);
}

/* 绘制矩形边框 */
static inline void gfx_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, gfx_color_t color)
{
    gfx_draw_hline(x, y, w, color);
    gfx_draw_hline(x, y + h - 1, w, color);
    gfx_draw_vline(x, y, h, color);
    gfx_draw_vline(x + w - 1, y, h, color);
}

/* 绘制线条（Bresenham算法） */
static inline void gfx_draw_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, gfx_color_t color)
{
    int dx = (int)(x1 > x0 ? x1 - x0 : x0 - x1);
    int dy = (int)(y1 > y0 ? y1 - y0 : y0 - y1);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int x = (int)x0, y = (int)y0;

    while (1) {
        gfx_put_pixel((uint32_t)x, (uint32_t)y, color);
        if (x == (int)x1 && y == (int)y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx) { err += dx; y += sy; }
    }
}

/* 绘制圆形（中点圆算法） */
static inline void gfx_draw_circle(uint32_t cx, uint32_t cy, uint32_t r, gfx_color_t color)
{
    int x = (int)r, y = 0;
    int err = 0;

    while (x >= y) {
        gfx_put_pixel(cx + x, cy + y, color);
        gfx_put_pixel(cx + y, cy + x, color);
        gfx_put_pixel(cx - y, cy + x, color);
        gfx_put_pixel(cx - x, cy + y, color);
        gfx_put_pixel(cx - x, cy - y, color);
        gfx_put_pixel(cx - y, cy - x, color);
        gfx_put_pixel(cx + y, cy - x, color);
        gfx_put_pixel(cx + x, cy - y, color);
        if (err <= 0) { y += 1; err += 2 * y + 1; }
        if (err > 0) { x -= 1; err -= 2 * x + 1; }
    }
}

/* 填充圆形 */
static inline void gfx_fill_circle(uint32_t cx, uint32_t cy, uint32_t r, gfx_color_t color)
{
    for (uint32_t y = 0; y <= r; y++) {
        /* 整数平方根：dx = sqrt(r^2 - y^2) */
        uint32_t val = r * r - y * y;
        uint32_t dx = 0;
        if (val > 0) {
            dx = val;
            /* 牛顿迭代法求平方根 */
            for (int i = 0; i < 10; i++) {
                if (dx == 0) break;
                dx = (dx + val / dx) / 2;
            }
        }
        gfx_draw_hline(cx - dx, cy - y, 2 * dx + 1, color);
        gfx_draw_hline(cx - dx, cy + y, 2 * dx + 1, color);
    }
}

/* 绘制圆角矩形 */
static inline void gfx_draw_rounded_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, gfx_color_t color)
{
    /* 四条边 */
    gfx_draw_hline(x + r, y, w - 2 * r, color);
    gfx_draw_hline(x + r, y + h - 1, w - 2 * r, color);
    gfx_draw_vline(x, y + r, h - 2 * r, color);
    gfx_draw_vline(x + w - 1, y + r, h - 2 * r, color);
    /* 四个角 */
    gfx_draw_circle(x + r, y + r, r, color);
    gfx_draw_circle(x + w - r - 1, y + r, r, color);
    gfx_draw_circle(x + r, y + h - r - 1, r, color);
    gfx_draw_circle(x + w - r - 1, y + h - r - 1, r, color);
}

/* 填充圆角矩形 */
static inline void gfx_fill_rounded_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, gfx_color_t color)
{
    /* 中间矩形 */
    gfx_fill_rect(x + r, y, w - 2 * r, h, color);
    /* 上下矩形 */
    gfx_fill_rect(x, y + r, w, h - 2 * r, color);
    /* 四个角 */
    gfx_fill_circle(x + r, y + r, r, color);
    gfx_fill_circle(x + w - r - 1, y + r, r, color);
    gfx_fill_circle(x + r, y + h - r - 1, r, color);
    gfx_fill_circle(x + w - r - 1, y + h - r - 1, r, color);
}

/* 绘制字符串（在gfx_core.c中实现） */
extern void gfx_draw_string(uint32_t x, uint32_t y, const char *str, gfx_color_t fg, gfx_color_t bg);

/* 清屏 */
static inline void gfx_clear_screen(gfx_color_t color)
{
    uint32_t w = 0, h = 0, bpp = 0;
    gfx_get_mode(&w, &h, &bpp);
    if (w > 0 && h > 0) {
        gfx_fill_rect(0, 0, w, h, color);
    }
}

/* 绘制椭圆（中点椭圆算法） */
static inline void gfx_draw_ellipse(uint32_t cx, uint32_t cy, uint32_t rx, uint32_t ry, gfx_color_t color)
{
    int x = 0, y = (int)ry;
    int a2 = (int)(rx * rx), b2 = (int)(ry * ry);
    int d1 = b2 - a2 * (int)ry + a2 / 4;
    int dx = 2 * b2 * x, dy = 2 * a2 * y;

    while (dx < dy) {
        gfx_put_pixel(cx + x, cy + y, color);
        gfx_put_pixel(cx - x, cy + y, color);
        gfx_put_pixel(cx + x, cy - y, color);
        gfx_put_pixel(cx - x, cy - y, color);
        if (d1 < 0) {
            x++; dx += 2 * b2; d1 += dx + b2;
        } else {
            x++; y--; dx += 2 * b2; dy -= 2 * a2; d1 += dx - dy + b2;
        }
    }

    int d2 = b2 * (x + 1) * (x + 1) / 4 + a2 * (y - 1) * (y - 1) - a2 * b2;
    while (y >= 0) {
        gfx_put_pixel(cx + x, cy + y, color);
        gfx_put_pixel(cx - x, cy + y, color);
        gfx_put_pixel(cx + x, cy - y, color);
        gfx_put_pixel(cx - x, cy - y, color);
        if (d2 > 0) {
            y--; dy -= 2 * a2; d2 += a2 - dy;
        } else {
            y--; x++; dx += 2 * b2; dy -= 2 * a2; d2 += dx - dy + a2;
        }
    }
}

/* 绘制三角形（边框） */
static inline void gfx_draw_triangle(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t x3, uint32_t y3, gfx_color_t color)
{
    gfx_draw_line(x1, y1, x2, y2, color);
    gfx_draw_line(x2, y2, x3, y3, color);
    gfx_draw_line(x3, y3, x1, y1, color);
}

/* 填充三角形（扫描线算法） */
static inline void gfx_fill_triangle(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t x3, uint32_t y3, gfx_color_t color)
{
    int min_y = (int)(y1 < y2 ? (y1 < y3 ? y1 : y3) : (y2 < y3 ? y2 : y3));
    int max_y = (int)(y1 > y2 ? (y1 > y3 ? y1 : y3) : (y2 > y3 ? y2 : y3));

    for (int y = min_y; y <= max_y; y++) {
        int xs[3], count = 0;
        if ((y >= (int)y1 && y <= (int)y2) || (y >= (int)y2 && y <= (int)y1)) {
            if (y2 != y1) xs[count++] = (int)(x1 + (x2 - x1) * (y - (int)y1) / ((int)y2 - (int)y1));
        }
        if ((y >= (int)y2 && y <= (int)y3) || (y >= (int)y3 && y <= (int)y2)) {
            if (y3 != y2) xs[count++] = (int)(x2 + (x3 - x2) * (y - (int)y2) / ((int)y3 - (int)y2));
        }
        if ((y >= (int)y3 && y <= (int)y1) || (y >= (int)y1 && y <= (int)y3)) {
            if (y1 != y3) xs[count++] = (int)(x3 + (x1 - x3) * (y - (int)y3) / ((int)y1 - (int)y3));
        }
        if (count >= 2) {
            int min_x = xs[0] < xs[1] ? xs[0] : xs[1];
            int max_x = xs[0] > xs[1] ? xs[0] : xs[1];
            gfx_draw_hline((uint32_t)min_x, (uint32_t)y, (uint32_t)(max_x - min_x + 1), color);
        }
    }
}

/* 绘制字符串 */
void gfx_draw_string(uint32_t x, uint32_t y, const char *str, gfx_color_t fg, gfx_color_t bg);

/* 抗锯齿字体渲染（基于统一API，32位RGB Alpha混合） */
void gfx_draw_char_aa(uint32_t x, uint32_t y, char ch, gfx_color_t fg);
void gfx_draw_string_aa(uint32_t x, uint32_t y, const char *str, gfx_color_t fg);

/* VGA应急文本输出（内核最早期，内存管理器未就绪时可用） */
void gfx_vga_early_putchar(char c);
void gfx_vga_early_puts(const char *s);

#endif /* GRAPHICS_H */
