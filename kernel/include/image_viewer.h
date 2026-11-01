/* ============================================================================
 * 赫普OS - 图片查看器（V8.71新增）
 * 支持BMP/PNG/JPEG图片查看、缩放、旋转
 * ========================================================================== */

#ifndef _IMAGE_VIEWER_H
#define _IMAGE_VIEWER_H

#include <stdint.h>

/* 图片格式 */
typedef enum {
    IMAGE_FORMAT_UNKNOWN = 0,
    IMAGE_FORMAT_BMP,
    IMAGE_FORMAT_PNG,
    IMAGE_FORMAT_JPEG,
    IMAGE_FORMAT_GIF,
    IMAGE_FORMAT_RAW
} image_format_t;

/* 图片数据 */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t *pixels;      /* RGBA格式 */
    image_format_t format;
    int      loaded;
    char     filename[256];
} image_t;

/* 查看器状态 */
typedef struct {
    image_t  current_image;
    float    zoom;          /* 缩放比例 0.1-10.0 */
    int      offset_x;
    int      offset_y;
    int      rotation;      /* 0/90/180/270 */
    int      initialized;
    char     current_dir[256];
} image_viewer_t;

/* 初始化图片查看器 */
int image_viewer_init(void);

/* 关闭图片查看器 */
void image_viewer_shutdown(void);

/* 加载图片文件 */
int image_viewer_load(const char *filename);

/* 卸载当前图片 */
void image_viewer_unload(void);

/* 检测图片格式 */
image_format_t image_detect_format(const uint8_t *data, int len);

/* 解析BMP */
int image_parse_bmp(const uint8_t *data, int len, image_t *image);

/* 解析PNG（简化版） */
int image_parse_png(const uint8_t *data, int len, image_t *image);

/* 解析JPEG（简化版） */
int image_parse_jpeg(const uint8_t *data, int len, image_t *image);

/* 缩放 */
int image_viewer_zoom(float factor);

/* 平移 */
int image_viewer_pan(int dx, int dy);

/* 旋转 */
int image_viewer_rotate(int degrees);

/* 适应窗口 */
int image_viewer_fit_window(int window_w, int window_h);

/* 原始大小 */
int image_viewer_actual_size(void);

/* 绘制图片 */
void image_viewer_draw(int x, int y, int width, int height);

/* 绘制UI */
void image_viewer_draw_ui(int x, int y, int width, int height);

/* 处理键盘 */
void image_viewer_handle_key(char key);

/* 获取图片信息 */
int image_viewer_get_info(image_t *info);

#endif /* _IMAGE_VIEWER_H */
