#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <stdint.h>

#define COMPOSITOR_MAX_LAYERS 32
#define COMPOSITOR_MAX_DISPLAYS 2

/* 图层类型 */
typedef enum {
    LAYER_TYPE_COLOR = 0,    /* 纯色层 */
    LAYER_TYPE_TEXTURE,      /* 纹理层 */
    LAYER_TYPE_SURFACE,      /* 应用表面 */
    LAYER_TYPE_STATUS_BAR,   /* 状态栏 */
    LAYER_TYPE_NAV_BAR,      /* 导航栏 */
    LAYER_TYPE_CURSOR,       /* 光标层 */
    LAYER_TYPE_OVERLAY,      /* 覆盖层 */
} layer_type_t;

/* 合成模式 */
typedef enum {
    COMPOSITOR_OPAQUE = 0,
    COMPOSITOR_BLEND,        /* Alpha混合 */
    COMPOSITOR_ADD,          /* 叠加 */
    COMPOSITOR_MULTIPLY,     /* 正片叠底 */
} compositor_blend_t;

/* 显示连接状态 */
typedef enum {
    DISPLAY_DISCONNECTED = 0,
    DISPLAY_CONNECTED,
    DISPLAY_SLEEPING,
} display_state_t;

/* 显示模式 */
typedef struct {
    int width;
    int height;
    int refresh_rate;  /* Hz */
    int color_depth;   /* bpp */
} display_mode_t;

/* 图层 */
typedef struct {
    int used;
    int id;
    layer_type_t type;
    int x, y;
    int width, height;
    int z_order;
    float alpha;
    compositor_blend_t blend;
    void *buffer;       /* 像素数据 (RGB565) */
    uint32_t buffer_size;
    int visible;
    int dirty;          /* 需要重绘 */
    int transform;      /* 旋转/翻转 */
    /* 裁剪区域 */
    int crop_x, crop_y, crop_w, crop_h;
} compositor_layer_t;

/* 显示设备 */
typedef struct {
    int used;
    const char *name;
    display_state_t state;
    display_mode_t current_mode;
    display_mode_t supported_modes[8];
    int mode_count;
    void *framebuffer;
    uint32_t fb_size;
    uint32_t pitch;
    int vsync_enabled;
    uint64_t vsync_count;
    int brightness;     /* 0-255 */
    int hdr_enabled;
    /* 统计 */
    uint64_t frames_composited;
    uint64_t frames_dropped;
    uint64_t last_frame_time;
    float fps;
} compositor_display_t;

/* 初始化显示合成器 */
void compositor_init(void);

/* 创建图层 */
int compositor_create_layer(layer_type_t type, int x, int y, int w, int h);

/* 销毁图层 */
int compositor_destroy_layer(int layer_id);

/* 设置图层位置 */
int compositor_set_position(int layer_id, int x, int y);

/* 设置图层大小 */
int compositor_set_size(int layer_id, int w, int h);

/* 设置图层Z序 */
int compositor_set_z_order(int layer_id, int z);

/* 设置图层透明度 */
int compositor_set_alpha(int layer_id, float alpha);

/* 设置图层混合模式 */
int compositor_set_blend(int layer_id, compositor_blend_t blend);

/* 设置图层可见性 */
int compositor_set_visible(int layer_id, int visible);

/* 设置图层缓冲区 */
int compositor_set_buffer(int layer_id, void *buffer, uint32_t size);

/* 标记图层脏 */
int compositor_invalidate(int layer_id);

/* 执行一次合成 */
int compositor_composite(void);

/* Vsync 中断处理 */
void compositor_handle_vsync(void);

/* 启用/禁用Vsync */
int compositor_set_vsync(int enable);

/* 设置显示亮度 */
int compositor_set_brightness(int display, int brightness);

/* 设置显示模式 */
int compositor_set_mode(int display, int width, int height, int refresh);

/* 获取显示信息 */
int compositor_get_display_info(int display, compositor_display_t *info);

/* 获取图层信息 */
int compositor_get_layer_info(int layer_id, compositor_layer_t *info);

/* 截图 */
int compositor_screenshot(void *buffer, uint32_t size);

/* 启用/禁用HDR */
int compositor_set_hdr(int display, int enable);

/* 获取FPS */
float compositor_get_fps(int display);

/* 获取活跃图层数 */
int compositor_layer_count(void);

#endif /* COMPOSITOR_H */
