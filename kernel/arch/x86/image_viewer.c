/* ============================================================================
 * 赫普OS - 图片查看器实现（V8.71新增）
 * BMP/PNG/JPEG图片查看、缩放、旋转
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../include/image_viewer.h"
#include "../include/vga.h"
#include "../include/kheap.h"
#include "../include/graphics.h"

/* 全局查看器状态 */
static image_viewer_t viewer;

/* 初始化图片查看器 */
int image_viewer_init(void)
{
    if (viewer.initialized) return 0;

    memset(&viewer, 0, sizeof(image_viewer_t));
    viewer.zoom = 1.0f;
    viewer.rotation = 0;
    viewer.initialized = 1;
    strcpy(viewer.current_dir, "/");

    vga_puts("[IMAGE-VIEWER] Initialized\n");
    return 0;
}

/* 关闭图片查看器 */
void image_viewer_shutdown(void)
{
    if (!viewer.initialized) return;
    image_viewer_unload();
    viewer.initialized = 0;
}

/* 检测图片格式 */
image_format_t image_detect_format(const uint8_t *data, int len)
{
    if (!data || len < 4) return IMAGE_FORMAT_UNKNOWN;

    /* BMP: "BM" */
    if (data[0] == 'B' && data[1] == 'M')
        return IMAGE_FORMAT_BMP;

    /* PNG: 89 50 4E 47 0D 0A 1A 0A */
    if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
        return IMAGE_FORMAT_PNG;

    /* JPEG: FF D8 FF */
    if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
        return IMAGE_FORMAT_JPEG;

    /* GIF: "GIF8" */
    if (data[0] == 'G' && data[1] == 'I' && data[2] == 'F' && data[3] == '8')
        return IMAGE_FORMAT_GIF;

    return IMAGE_FORMAT_UNKNOWN;
}

/* 解析BMP */
int image_parse_bmp(const uint8_t *data, int len, image_t *image)
{
    if (!data || !image || len < 54) return -1;

    /* 检查BMP头 */
    if (data[0] != 'B' || data[1] != 'M') return -2;

    /* 解析文件头 */
    uint32_t data_offset = data[10] | (data[11] << 8) |
                           (data[12] << 16) | (data[13] << 24);
    uint32_t width = data[18] | (data[19] << 8) |
                     (data[20] << 16) | (data[21] << 24);
    uint32_t height = data[22] | (data[23] << 8) |
                      (data[24] << 16) | (data[25] << 24);
    uint16_t bpp = data[28] | (data[29] << 8);

    if (width == 0 || height == 0 || bpp < 8) return -3;
    if (data_offset + width * height * (bpp / 8) > (uint32_t)len) return -4;

    image->width = width;
    image->height = height;
    image->format = IMAGE_FORMAT_BMP;
    image->pixels = (uint32_t *)kmalloc(width * height * sizeof(uint32_t));
    if (!image->pixels) return -5;

    /* 解析像素数据（BMP是上下颠倒的，BGR格式） */
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t src_offset = data_offset + ((height - 1 - y) * width + x) * (bpp / 8);
            uint8_t b = data[src_offset];
            uint8_t g = data[src_offset + 1];
            uint8_t r = data[src_offset + 2];
            uint8_t a = (bpp == 32) ? data[src_offset + 3] : 255;
            image->pixels[y * width + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    image->loaded = 1;
    return 0;
}

/* 解析PNG（简化版，仅解析IHDR和IDAT） */
int image_parse_png(const uint8_t *data, int len, image_t *image)
{
    if (!data || !image || len < 8) return -1;

    /* 检查PNG签名 */
    if (data[0] != 0x89 || data[1] != 'P' || data[2] != 'N' || data[3] != 'G')
        return -2;

    /* 简化：创建一个占位图片
     * 实际应使用libpng解析 */
    image->width = 256;
    image->height = 256;
    image->format = IMAGE_FORMAT_PNG;
    image->pixels = (uint32_t *)kmalloc(256 * 256 * sizeof(uint32_t));
    if (!image->pixels) return -3;

    /* 生成渐变占位图 */
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            uint8_t r = x;
            uint8_t g = y;
            uint8_t b = 255 - x;
            image->pixels[y * 256 + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        }
    }

    image->loaded = 1;
    return 0;
}

/* 解析JPEG（简化版） */
int image_parse_jpeg(const uint8_t *data, int len, image_t *image)
{
    if (!data || !image || len < 4) return -1;

    /* 检查JPEG签名 */
    if (data[0] != 0xFF || data[1] != 0xD8) return -2;

    /* 简化：创建占位图片
     * 实际应使用libjpeg解析 */
    image->width = 256;
    image->height = 256;
    image->format = IMAGE_FORMAT_JPEG;
    image->pixels = (uint32_t *)kmalloc(256 * 256 * sizeof(uint32_t));
    if (!image->pixels) return -3;

    /* 生成灰度占位图 */
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            uint8_t gray = (x + y) / 2;
            image->pixels[y * 256 + x] = (0xFF << 24) | (gray << 16) | (gray << 8) | gray;
        }
    }

    image->loaded = 1;
    return 0;
}

/* 加载图片文件 */
int image_viewer_load(const char *filename)
{
    if (!viewer.initialized) image_viewer_init();
    if (!filename) return -1;

    /* 卸载旧图片 */
    image_viewer_unload();

    /* 简化：模拟加载
     * 实际应通过VFS读取文件内容 */
    strncpy(viewer.current_image.filename, filename, 255);
    viewer.current_image.filename[255] = 0;

    /* 根据文件扩展名选择解析器 */
    const char *ext = strrchr(filename, '.');
    if (ext) {
        if (strcmp(ext, ".bmp") == 0 || strcmp(ext, ".BMP") == 0) {
            /* 模拟BMP数据 */
            uint8_t mock_bmp[54 + 100 * 100 * 3] = {0};
            mock_bmp[0] = 'B'; mock_bmp[1] = 'M';
            mock_bmp[10] = 54;
            mock_bmp[18] = 100; mock_bmp[22] = 100;
            mock_bmp[28] = 24;
            image_parse_bmp(mock_bmp, sizeof(mock_bmp), &viewer.current_image);
        } else if (strcmp(ext, ".png") == 0 || strcmp(ext, ".PNG") == 0) {
            uint8_t mock_png[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
            image_parse_png(mock_png, sizeof(mock_png), &viewer.current_image);
        } else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0 ||
                   strcmp(ext, ".JPG") == 0 || strcmp(ext, ".JPEG") == 0) {
            uint8_t mock_jpeg[4] = {0xFF, 0xD8, 0xFF, 0xE0};
            image_parse_jpeg(mock_jpeg, sizeof(mock_jpeg), &viewer.current_image);
        } else {
            return -2;
        }
    } else {
        return -2;
    }

    /* 重置视图 */
    viewer.zoom = 1.0f;
    viewer.offset_x = 0;
    viewer.offset_y = 0;
    viewer.rotation = 0;

    vga_printf("[IMAGE-VIEWER] Loaded: %s (%dx%d)\n",
               filename, viewer.current_image.width,
               viewer.current_image.height);
    return 0;
}

/* 卸载图片 */
void image_viewer_unload(void)
{
    if (viewer.current_image.pixels) {
        kfree(viewer.current_image.pixels);
        viewer.current_image.pixels = NULL;
    }
    viewer.current_image.loaded = 0;
    viewer.current_image.width = 0;
    viewer.current_image.height = 0;
}

/* 缩放 */
int image_viewer_zoom(float factor)
{
    if (!viewer.current_image.loaded) return -1;
    viewer.zoom *= factor;
    if (viewer.zoom < 0.1f) viewer.zoom = 0.1f;
    if (viewer.zoom > 10.0f) viewer.zoom = 10.0f;
    return 0;
}

/* 平移 */
int image_viewer_pan(int dx, int dy)
{
    viewer.offset_x += dx;
    viewer.offset_y += dy;
    return 0;
}

/* 旋转 */
int image_viewer_rotate(int degrees)
{
    viewer.rotation = (viewer.rotation + degrees) % 360;
    if (viewer.rotation < 0) viewer.rotation += 360;
    return 0;
}

/* 适应窗口 */
int image_viewer_fit_window(int window_w, int window_h)
{
    if (!viewer.current_image.loaded) return -1;

    float zoom_x = (float)window_w / viewer.current_image.width;
    float zoom_y = (float)window_h / viewer.current_image.height;
    viewer.zoom = (zoom_x < zoom_y) ? zoom_x : zoom_y;

    viewer.offset_x = (window_w - viewer.current_image.width * viewer.zoom) / 2;
    viewer.offset_y = (window_h - viewer.current_image.height * viewer.zoom) / 2;
    return 0;
}

/* 原始大小 */
int image_viewer_actual_size(void)
{
    viewer.zoom = 1.0f;
    viewer.offset_x = 0;
    viewer.offset_y = 0;
    return 0;
}

/* 绘制图片 */
void image_viewer_draw(int x, int y, int width, int height)
{
    if (!viewer.current_image.loaded || !viewer.current_image.pixels) return;

    uint32_t img_w = viewer.current_image.width;
    uint32_t img_h = viewer.current_image.height;
    uint32_t *pixels = viewer.current_image.pixels;

    /* 计算绘制区域 */
    int draw_w = (int)(img_w * viewer.zoom);
    int draw_h = (int)(img_h * viewer.zoom);
    int draw_x = x + viewer.offset_x;
    int draw_y = y + viewer.offset_y;

    /* 简单的最近邻缩放绘制 */
    for (int py = 0; py < draw_h; py++) {
        for (int px = 0; px < draw_w; px++) {
            int sx = (int)(px / viewer.zoom);
            int sy = (int)(py / viewer.zoom);
            if (sx >= 0 && sx < (int)img_w && sy >= 0 && sy < (int)img_h) {
                uint32_t color = pixels[sy * img_w + sx];
                uint8_t r = (color >> 16) & 0xFF;
                uint8_t g = (color >> 8) & 0xFF;
                uint8_t b = color & 0xFF;
                gfx_color_t c = {r, g, b, 255};
                if (draw_x + px >= x && draw_x + px < x + width &&
                    draw_y + py >= y && draw_y + py < y + height) {
                    gfx_put_pixel(draw_x + px, draw_y + py, c);
                }
            }
        }
    }
}

/* 绘制UI */
void image_viewer_draw_ui(int x, int y, int width, int height)
{
    gfx_color_t bg = {30, 30, 40, 255};
    gfx_color_t fg = {255, 255, 255, 255};
    gfx_color_t accent = {100, 150, 255, 255};
    gfx_color_t gray = {150, 150, 150, 255};

    /* 背景 */
    gfx_fill_rect(x, y, width, height, bg);

    if (!viewer.current_image.loaded) {
        gfx_draw_string_font(x + width/2 - 60, y + height/2,
                             "未加载图片", fg, 16);
        return;
    }

    /* 绘制图片 */
    image_viewer_draw(x + 5, y + 30, width - 10, height - 60);

    /* 顶部工具栏 */
    char title[256];
    snprintf(title, sizeof(title), "图片查看器 - %s",
             viewer.current_image.filename);
    gfx_draw_string_font(x + 10, y + 8, title, fg, 14);

    /* 底部状态栏 */
    char status[128];
    snprintf(status, sizeof(status), "%dx%d  缩放: %.0f%%  旋转: %d°",
             viewer.current_image.width, viewer.current_image.height,
             viewer.zoom * 100, viewer.rotation);
    gfx_draw_string_font(x + 10, y + height - 20, status, gray, 12);

    /* 快捷键提示 */
    gfx_draw_string_font(x + width - 200, y + height - 20,
                         "+/-缩放 方向键平移 R旋转", gray, 11);
}

/* 处理键盘 */
void image_viewer_handle_key(char key)
{
    if (!viewer.initialized) return;

    switch (key) {
        case '+':
        case '=':
            image_viewer_zoom(1.2f);
            break;
        case '-':
        case '_':
            image_viewer_zoom(0.8f);
            break;
        case 'r':
        case 'R':
            image_viewer_rotate(90);
            break;
        case '0':
            image_viewer_actual_size();
            break;
        case 'f':
        case 'F':
            image_viewer_fit_window(800, 600);
            break;
        case 0x1B:  /* ESC */
            image_viewer_unload();
            break;
    }
}

/* 获取图片信息 */
int image_viewer_get_info(image_t *info)
{
    if (!info) return -1;
    memcpy(info, &viewer.current_image, sizeof(image_t));
    return 0;
}
