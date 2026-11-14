/* ============================================================================
 * 赫普OS 64位 - 字体管理器
 * 支持8x16位图字体、缩放、抗锯齿渲染
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* 8x16 ASCII字体（在gfx64.c中定义） */
extern const uint8_t font8x16[128][16];

#define FONT_CACHE_SIZE 16
#define FONT_MAX_SCALE 6

typedef struct {
    uint32_t codepoint;
    uint8_t  scale;
    uint8_t  antialiased;
    uint8_t  valid;
    uint8_t  bitmap[16 * FONT_MAX_SCALE * 16 * FONT_MAX_SCALE];
} font_cache_entry_t;

static font_cache_entry_t font_cache[FONT_CACHE_SIZE];
static uint16_t current_size = 16;
static uint8_t  current_aa = 0;

/* 抗锯齿alpha计算（基于2x2超采样） */
static uint8_t aa_calc_alpha(uint32_t codepoint, int col, int row)
{
    if (codepoint >= 128) return 0;
    const uint8_t *glyph = font8x16[codepoint];

    int src_col = col / 2;
    int src_row = row / 2;
    if (src_col >= 8 || src_row >= 16) return 0;

    int count = 0;
    for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
            int sc = src_col + dx;
            int sr = src_row + dy;
            if (sc < 8 && sr < 16) {
                if (glyph[sr] & (0x80 >> sc)) count++;
            }
        }
    }
    return (count * 255) / 4;
}

/* 渲染字形到位图 */
static int font_render_glyph(uint32_t codepoint, uint8_t scale,
                             uint8_t antialiased, uint8_t *output)
{
    if (codepoint >= 128) return -1;
    const uint8_t *glyph = font8x16[codepoint];

    int out_w = 8 * scale;
    int out_h = 16 * scale;

    if (antialiased && scale == 2) {
        for (int row = 0; row < out_h; row++) {
            for (int col = 0; col < out_w; col++) {
                output[row * out_w + col] = aa_calc_alpha(codepoint, col, row);
            }
        }
    } else {
        for (int row = 0; row < out_h; row++) {
            int src_row = row / scale;
            for (int col = 0; col < out_w; col++) {
                int src_col = col / scale;
                if (src_row < 16 && src_col < 8 &&
                    (glyph[src_row] & (0x80 >> src_col))) {
                    output[row * out_w + col] = 255;
                } else {
                    output[row * out_w + col] = 0;
                }
            }
        }
    }
    return 0;
}

/* 查找或创建缓存条目 */
static font_cache_entry_t* font_cache_get(uint32_t codepoint, uint8_t scale,
                                          uint8_t antialiased)
{
    for (int i = 0; i < FONT_CACHE_SIZE; i++) {
        if (font_cache[i].valid &&
            font_cache[i].codepoint == codepoint &&
            font_cache[i].scale == scale &&
            font_cache[i].antialiased == antialiased) {
            return &font_cache[i];
        }
    }

    for (int i = 0; i < FONT_CACHE_SIZE; i++) {
        if (!font_cache[i].valid) {
            font_cache[i].codepoint = codepoint;
            font_cache[i].scale = scale;
            font_cache[i].antialiased = antialiased;
            font_cache[i].valid = 1;
            if (font_render_glyph(codepoint, scale, antialiased,
                                  font_cache[i].bitmap) != 0) {
                font_cache[i].valid = 0;
                return NULL;
            }
            return &font_cache[i];
        }
    }

    font_cache[0].codepoint = codepoint;
    font_cache[0].scale = scale;
    font_cache[0].antialiased = antialiased;
    font_cache[0].valid = 1;
    font_render_glyph(codepoint, scale, antialiased, font_cache[0].bitmap);
    return &font_cache[0];
}

/* 绘制字符（带alpha混合） */
void font64_draw_char(void *framebuffer, int fb_width, int fb_height,
                      int x, int y, char c, uint32_t color, uint16_t size)
{
    /* V0.59.11: 处于窗口 surface 渲染上下文时, 忽略调用方传的屏幕 framebuffer,
     * 改写 surface 缓冲 + 自身宽高, 并把屏幕坐标平移进缓冲。调用点无需改动。 */
    extern int gfx64_in_surface(void);
    extern void *gfx64_surface_buffer(void);
    extern int gfx64_surface_pitch(void);
    extern int gfx64_surface_height(void);
    extern int gfx64_surface_origin_x(void);
    extern int gfx64_surface_origin_y(void);
    if (gfx64_in_surface()) {
        x -= gfx64_surface_origin_x();
        y -= gfx64_surface_origin_y();
        framebuffer = gfx64_surface_buffer();
        fb_width = gfx64_surface_pitch();
        fb_height = gfx64_surface_height();
    }

    if (!framebuffer || x < 0 || y < 0) return;
    if ((uint8_t)c >= 128) c = '?';

    uint8_t scale = size / 16;
    if (scale < 1) scale = 1;
    if (scale > FONT_MAX_SCALE) scale = FONT_MAX_SCALE;

    uint8_t use_aa = (scale >= 2 && current_aa) ? 1 : 0;

    font_cache_entry_t *entry = font_cache_get((uint8_t)c, scale, use_aa);
    if (!entry) return;

    int glyph_w = 8 * scale;
    int glyph_h = 16 * scale;

    uint32_t *fb = (uint32_t *)framebuffer;
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    for (int row = 0; row < glyph_h && y + row < fb_height; row++) {
        for (int col = 0; col < glyph_w && x + col < fb_width; col++) {
            uint8_t alpha = entry->bitmap[row * glyph_w + col];
            if (alpha == 0) continue;

            int idx = (y + row) * fb_width + (x + col);
            if (alpha == 255) {
                fb[idx] = color;
            } else {
                uint32_t bg = fb[idx];
                uint8_t br = (bg >> 16) & 0xFF;
                uint8_t bg_ = (bg >> 8) & 0xFF;
                uint8_t bb = bg & 0xFF;
                uint8_t nr = (r * alpha + br * (255 - alpha)) / 255;
                uint8_t ng = (g * alpha + bg_ * (255 - alpha)) / 255;
                uint8_t nb = (b * alpha + bb * (255 - alpha)) / 255;
                fb[idx] = (nr << 16) | (ng << 8) | nb;
            }
        }
    }
}

/* 绘制字符串 */
void font64_draw_string(void *framebuffer, int fb_width, int fb_height,
                        int x, int y, const char *str, uint32_t color,
                        uint16_t size)
{
    if (!str) return;

    /* 如果TrueType字体可用，使用TrueType渲染（更平滑） */
    extern int gfx64_is_ttf_enabled(void);
    extern int ttf64_is_loaded(void);
    extern void gfx64_set_font_size(int size);
    extern void gfx64_put_string(int x, int y, const char *str, uint32_t color);
    if (gfx64_is_ttf_enabled() && ttf64_is_loaded()) {
        /* gfx64_put_string内部已禁中断保护FPU状态 */
        gfx64_set_font_size(size);
        gfx64_put_string(x, y, str, color);
        return;
    }

    int cur_x = x;
    uint8_t scale = size / 16;
    if (scale < 1) scale = 1;
    int char_w = 8 * scale;

    while (*str) {
        if (*str == '\n') {
            y += 16 * scale;
            cur_x = x;
            str++;
            continue;
        }
        if (*str == ' ') {
            /* 空格直接移动光标，不调用TTF渲染 */
            cur_x += char_w;
            str++;
            continue;
        }
        font64_draw_char(framebuffer, fb_width, fb_height,
                         cur_x, y, *str, color, size);
        cur_x += char_w;
        str++;
    }
}

/* 获取字符串宽度 */
uint32_t font64_get_string_width(const char *str, uint16_t size)
{
    if (!str) return 0;
    uint8_t scale = size / 16;
    if (scale < 1) scale = 1;
    uint32_t len = 0;
    while (*str++) len++;
    return len * 8 * scale;
}

/* 初始化字体管理器 */
void font64_init(void)
{
    for (int i = 0; i < FONT_CACHE_SIZE; i++) {
        font_cache[i].valid = 0;
    }
    current_size = 16;
    current_aa = 1;
}

void font64_set_size(uint16_t size) { current_size = size; }
void font64_set_antialiased(uint8_t enable) { current_aa = enable; }
uint16_t font64_get_size(void) { return current_size; }
