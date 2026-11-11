/* ============================================================================
 * cursor64.c — Xcursor (.xcur) 主题光标加载与渲染
 *
 * 支持 X11 Xcursor 通用格式（file(1) 识别为 "X11 cursor"）：
 *   文件头 16B: magic "Xcur" | header_size | version | ntoc
 *   TOC    12B/项: type | subtype(名义尺寸) | position(块偏移)
 *   image 块: 36B 头(header_size/type/subtype/version/w/h/xhot/yhot/delay)
 *             + w*h 个 ARGB8888(小端) 像素
 *
 * 装载时按期望尺寸(want)选最接近的 subtype，热点直接取文件里的 xhot/yhot。
 * 未装载成功时回退到 gfx64 内置位图，保证任何情况下都有指针可画。
 * ========================================================================== */
#include <stdint.h>

extern void gfx64_put_pixel(int x, int y, uint32_t color);
extern void gfx64_blend_pixel(int x, int y, uint32_t color, int alpha);
extern void gfx64_draw_mouse_cursor(int x, int y, uint32_t color);
extern int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size);

#define CUR_MAX_DIM   64
#define XCUR_IMAGE    0xFFFD0002u
#define XCUR_HDR_SIZE 36u

static uint32_t g_px[CUR_MAX_DIM * CUR_MAX_DIM];
static int g_w, g_h, g_hot_x, g_hot_y;
static int g_ready;
static int g_nominal;          /* 实际选中的名义尺寸，用于诊断 */

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* 解析内存中的 xcur 数据；want = 期望的名义尺寸(像素) */
int cursor64_load_blob(const uint8_t *b, uint32_t len, int want)
{
    if (len < 16) return -1;
    if (!(b[0] == 'X' && b[1] == 'c' && b[2] == 'u' && b[3] == 'r')) return -2;

    uint32_t ntoc = rd32(b + 12);
    if (ntoc == 0 || ntoc > 256) return -3;
    if (16u + ntoc * 12u > len) return -4;

    uint32_t best_d = 0xFFFFFFFFu, best_pos = 0, best_nom = 0;
    for (uint32_t i = 0; i < ntoc; i++) {
        const uint8_t *t = b + 16 + i * 12;
        uint32_t type = rd32(t), sub = rd32(t + 4), pos = rd32(t + 8);
        if (type != XCUR_IMAGE) continue;
        if (pos + XCUR_HDR_SIZE > len) continue;
        uint32_t d = (sub > (uint32_t)want) ? (sub - (uint32_t)want)
                                            : ((uint32_t)want - sub);
        if (d < best_d) { best_d = d; best_pos = pos; best_nom = sub; }
    }
    if (best_nom == 0) return -5;

    const uint8_t *c = b + best_pos;
    uint32_t hs = rd32(c);
    uint32_t w  = rd32(c + 16), h = rd32(c + 20);
    uint32_t xh = rd32(c + 24), yh = rd32(c + 28);

    if (hs != XCUR_HDR_SIZE) return -6;
    if (w == 0 || h == 0 || w > CUR_MAX_DIM || h > CUR_MAX_DIM) return -7;
    if (best_pos + XCUR_HDR_SIZE + w * h * 4u > len) return -8;

    const uint8_t *p = c + XCUR_HDR_SIZE;
    for (uint32_t j = 0; j < h; j++)
        for (uint32_t i = 0; i < w; i++)
            g_px[j * w + i] = rd32(p + 4u * (j * w + i));

    g_w = (int)w; g_h = (int)h;
    g_hot_x = (xh < w) ? (int)xh : (int)(w / 2);
    g_hot_y = (yh < h) ? (int)yh : (int)(h / 2);
    g_nominal = (int)best_nom;
    g_ready = 1;
    return 0;
}

/* 从根文件系统读取 xcur 文件并装载 */
int cursor64_load_file(const char *path, int want)
{
    static uint8_t buf[64 * 1024];
    int n = ext2_64_read_file(path, buf, (uint32_t)sizeof(buf));
    if (n <= 0) return -9;
    return cursor64_load_blob(buf, (uint32_t)n, want);
}

int cursor64_ready(void)      { return g_ready; }
int cursor64_size(void)       { return g_w; }
int cursor64_nominal(void)    { return g_nominal; }
int cursor64_hotspot_x(void)  { return g_hot_x; }
int cursor64_hotspot_y(void)  { return g_hot_y; }

/* 按屏幕分辨率推荐名义尺寸：1024/768→24, 1280/1440→32, 更高→48 */
int cursor64_pick_size(int screen_w, int screen_h)
{
    int m = (screen_w > screen_h) ? screen_h : screen_w;
    if (m >= 2000) return 48;
    if (m >= 1200) return 32;
    return 24;
}

/* 绘制当前光标（热点对齐），未装载时回退内置位图 */
void cursor64_draw(int x, int y)
{
    if (!g_ready) {
        gfx64_draw_mouse_cursor(x, y, 0xE6E1E5);
        return;
    }
    x -= g_hot_x;
    y -= g_hot_y;
    for (int j = 0; j < g_h; j++) {
        for (int i = 0; i < g_w; i++) {
            uint32_t p = g_px[j * g_w + i];
            int a = (int)(p >> 24);
            if (!a) continue;
            uint32_t rgb = p & 0xFFFFFFu;
            if (a >= 255) gfx64_put_pixel(x + i, y + j, rgb);
            else          gfx64_blend_pixel(x + i, y + j, rgb, a);
        }
    }
}
