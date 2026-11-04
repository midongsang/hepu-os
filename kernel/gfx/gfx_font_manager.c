/* ============================================================================
 * HepuOS 可插拔图形子系统 - 字体管理器
 * 支持多种字体格式：8x16位图、抗锯齿位图、TrueType（基础框架）
 * 统一字体渲染接口，支持字号缩放和字体缓存
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "graphics.h"
#include "serial.h"
#include "font8x16.h"

/* V7.34: 内置字体数据（编译到内核，确保启动即可用） */
#include "fonts/font_default.h"
#include "fonts/font_cjk.h"

/* 字体类型 */
#define FONT_TYPE_BITMAP_8x16    0
#define FONT_TYPE_ANTIALIASED    1
#define FONT_TYPE_TRUETYPE       2

/* V7.47: 当前TrueType解析器加载的字体索引，避免重复加载 */
static int current_ttf_font_index = -1;

/* 字体缓存条目 */
#define FONT_CACHE_SIZE  64  /* V7.91: 减少缓存大小，每个条目独立位图 */
typedef struct {
    uint32_t codepoint;      /* Unicode码点 */
    uint8_t  type;           /* 字体类型 */
    uint16_t size;           /* 字号 */
    uint8_t  bitmap[64 * 64]; /* V7.91: 独立位图缓冲区，避免共享缓冲区被覆盖 */
    uint16_t width;          /* 字形宽度 */
    uint16_t height;         /* 字形高度 */
    int16_t  advance;        /* 水平步进 */
    int16_t  bearing_x;      /* 左轴承 */
    int16_t  bearing_y;      /* 上轴承 */
    uint8_t  valid;          /* 是否有效 */
} font_cache_entry_t;

/* 字体管理器状态 */
static int font_manager_initialized = 0;
static font_cache_entry_t font_cache[FONT_CACHE_SIZE];
static int font_cache_count = 0;
static uint16_t current_font_size = 16;
static uint8_t current_font_type = FONT_TYPE_ANTIALIASED;

/* V7.23: 多字体管理 */
#define MAX_FONTS 8
typedef struct {
    uint8_t  *data;           /* 字体文件数据 */
    uint32_t  size;           /* 文件大小 */
    char      name[32];       /* 字体名称 */
    int       loaded;         /* 是否已加载 */
    int       is_default;     /* 是否默认字体 */
} font_entry_t;

static font_entry_t font_registry[MAX_FONTS];
static int font_count = 0;
static int current_font_index = -1;  /* 当前使用的TrueType字体索引 */

/* ============================================================================
 * 8x16位图字体渲染
 * ========================================================================== */

static int font_render_bitmap_8x16(uint32_t codepoint, uint8_t *output,
                                    uint16_t *width, uint16_t *height,
                                    int16_t *advance, int16_t *bearing_x, int16_t *bearing_y)
{
    if (codepoint > 127) return -1;

    const uint8_t *glyph = &hepu_font8x16[codepoint * 16];

    *width = 8;
    *height = 16;
    *advance = 8;
    *bearing_x = 0;
    *bearing_y = 0;

    if (output) {
        for (int row = 0; row < 16; row++) {
            uint8_t line = glyph[row];
            for (int col = 0; col < 8; col++) {
                output[row * 8 + col] = (line & (0x80 >> col)) ? 255 : 0;
            }
        }
    }

    return 0;
}

/* ============================================================================
 * 抗锯齿字体渲染（基于8x16位图的边缘Alpha计算）
 * ========================================================================== */

static uint8_t aa_calc_alpha(uint32_t codepoint, int col, int row)
{
    if (codepoint > 127) return 0;

    const uint8_t *glyph = &hepu_font8x16[codepoint * 16];
    uint8_t line = glyph[row];
    int pixel_set = (line & (0x80 >> col)) ? 1 : 0;

    if (pixel_set) {
        /* 检查是否是边缘 */
        int neighbors = 0;
        if (col > 0 && (line & (0x80 >> (col - 1)))) neighbors++;
        if (col < 7 && (line & (0x80 >> (col + 1)))) neighbors++;
        if (row > 0 && (glyph[row - 1] & (0x80 >> col))) neighbors++;
        if (row < 15 && (glyph[row + 1] & (0x80 >> col))) neighbors++;

        if (neighbors == 4) return 255;  /* 内部像素 */
        return 200;  /* 边缘像素 */
    } else {
        /* 检查是否是边缘（相邻像素有设置） */
        int neighbors = 0;
        if (col > 0 && (line & (0x80 >> (col - 1)))) neighbors++;
        if (col < 7 && (line & (0x80 >> (col + 1)))) neighbors++;
        if (row > 0 && (glyph[row - 1] & (0x80 >> col))) neighbors++;
        if (row < 15 && (glyph[row + 1] & (0x80 >> col))) neighbors++;

        if (neighbors > 0) {
            return 55 + neighbors * 25;  /* 边缘外像素 */
        }
        return 0;
    }
}

static int font_render_antialiased(uint32_t codepoint, uint8_t *output,
                                    uint16_t *width, uint16_t *height,
                                    int16_t *advance, int16_t *bearing_x, int16_t *bearing_y)
{
    if (codepoint > 127) return -1;

    *width = 8;
    *height = 16;
    *advance = 8;
    *bearing_x = 0;
    *bearing_y = 0;

    if (output) {
        for (int row = 0; row < 16; row++) {
            for (int col = 0; col < 8; col++) {
                output[row * 8 + col] = aa_calc_alpha(codepoint, col, row);
            }
        }
    }

    return 0;
}

/* ============================================================================
 * 字号缩放（最近邻插值，简化版）
 * ========================================================================== */

static int font_scale_bitmap(const uint8_t *src, uint16_t src_w, uint16_t src_h,
                              uint8_t *dst, uint16_t dst_w, uint16_t dst_h)
{
    if (!src || !dst || dst_w == 0 || dst_h == 0) return -1;

    for (uint16_t y = 0; y < dst_h; y++) {
        uint16_t src_y = (y * src_h) / dst_h;
        for (uint16_t x = 0; x < dst_w; x++) {
            uint16_t src_x = (x * src_w) / dst_w;
            dst[y * dst_w + x] = src[src_y * src_w + src_x];
        }
    }

    return 0;
}

/* ============================================================================
 * 字体缓存管理
 * ========================================================================== */

static font_cache_entry_t* font_cache_find(uint32_t codepoint, uint8_t type, uint16_t size)
{
    for (int i = 0; i < FONT_CACHE_SIZE; i++) {
        if (font_cache[i].valid &&
            font_cache[i].codepoint == codepoint &&
            font_cache[i].type == type &&
            font_cache[i].size == size) {
            return &font_cache[i];
        }
    }
    return NULL;
}

static font_cache_entry_t* font_cache_alloc(void)
{
    /* 查找空槽 */
    for (int i = 0; i < FONT_CACHE_SIZE; i++) {
        if (!font_cache[i].valid) {
            memset(&font_cache[i], 0, sizeof(font_cache_entry_t));
            font_cache[i].valid = 1;
            return &font_cache[i];
        }
    }

    /* 缓存满，替换第一个（FIFO） */
    static int cache_replace_idx = 0;
    font_cache_entry_t *entry = &font_cache[cache_replace_idx];
    if (entry->bitmap) {
        /* 静态分配的位图不需要释放 */
    }
    memset(entry, 0, sizeof(font_cache_entry_t));
    entry->valid = 1;
    cache_replace_idx = (cache_replace_idx + 1) % FONT_CACHE_SIZE;
    return entry;
}

/* ============================================================================
 * 字形渲染（统一接口）
 * ========================================================================== */

static uint8_t glyph_bitmap_buffer[64 * 64];  /* 最大64x64字形 */

int font_render_glyph(uint32_t codepoint, uint8_t type, uint16_t size,
                       const uint8_t **bitmap, uint16_t *width, uint16_t *height,
                       int16_t *advance, int16_t *bearing_x, int16_t *bearing_y)
{
    /* 查找缓存 */
    font_cache_entry_t *cached = font_cache_find(codepoint, type, size);
    if (cached) {
        *bitmap = cached->bitmap;  /* V7.91: 返回独立位图缓冲区 */
        *width = cached->width;
        *height = cached->height;
        *advance = cached->advance;
        *bearing_x = cached->bearing_x;
        *bearing_y = cached->bearing_y;
        return 0;
    }

    /* 渲染原始字形（16px） */
    /* V7.54: 增大缓冲区从8*16到64*64，支持TrueType字形 */
    uint8_t src_bitmap[64 * 64];
    uint16_t src_w, src_h;
    int16_t src_advance, src_bx, src_by;
    int ret;

    /* V7.22: CJK字符自动使用TrueType字体 */
    if (codepoint > 127 && type != FONT_TYPE_TRUETYPE) {
        extern int tt_font_is_loaded(void);
        if (tt_font_is_loaded()) {
            /* 自动切换到TrueType字体渲染CJK字符 */
            type = FONT_TYPE_TRUETYPE;
        } else {
            /* TrueType未加载，显示CJK占位符（方框） */
            *width = size;
            *height = size;
            *advance = size;
            *bearing_x = 0;
            *bearing_y = 0;
            if (bitmap) {
                /* 绘制一个空心方框作为占位符 */
                for (int y = 0; y < size && y < 16; y++) {
                    for (int x = 0; x < size && x < 8; x++) {
                        int is_edge = (x == 0 || x == size - 1 || y == 0 || y == size - 1);
                        src_bitmap[y * 8 + x] = is_edge ? 128 : 0;
                    }
                }
                *bitmap = src_bitmap;
            }
            return 0;
        }
    }

    switch (type) {
        case FONT_TYPE_BITMAP_8x16:
            ret = font_render_bitmap_8x16(codepoint, src_bitmap, &src_w, &src_h,
                                           &src_advance, &src_bx, &src_by);
            break;
        case FONT_TYPE_ANTIALIASED:
            ret = font_render_antialiased(codepoint, src_bitmap, &src_w, &src_h,
                                           &src_advance, &src_bx, &src_by);
            break;
        case FONT_TYPE_TRUETYPE: {
            /* V7.18: TrueType字体渲染 */
            extern int tt_font_is_loaded(void);
            extern uint16_t tt_char_to_glyph(uint32_t);
            extern int tt_get_glyph_metrics(uint16_t, int16_t*, int16_t*);
            extern int tt_render_glyph(uint16_t, uint8_t*, uint16_t, uint16_t, int*, int*);
            extern int tt_font_load(uint8_t*, uint32_t);
            extern void tt_set_pixel_size(uint16_t);

            if (!tt_font_is_loaded()) {
                /* TrueType字体未加载，回退到抗锯齿位图 */
                ret = font_render_antialiased(codepoint, src_bitmap, &src_w, &src_h,
                                               &src_advance, &src_bx, &src_by);
                break;
            }

            /* V7.39: 设置目标字号，确保scale正确
             * TrueType的size指em-square，实际字符高度约为size的60-70%
             * 放大1.5倍确保实际字符高度符合用户预期
             */
            uint16_t tt_size = (uint16_t)(size * 3 / 2);
            if (tt_size > 64) tt_size = 64;
            tt_set_pixel_size(tt_size);

            /* V7.36: 字体回退机制 - 当前字体不含该字符时，切换到包含该字符的字体 */
            /* V7.91: 对于CJK字符（>127），glyph初始化为0，不先尝试Default字体
             * 避免Default字体cmap格式4对未匹配字符返回错误的非0glyph */
            uint16_t glyph = (codepoint > 127) ? 0 : tt_char_to_glyph(codepoint);
            int font_switched = 0;
            int original_font = current_font_index;

            /* V7.47: 对于中文字符（>127），直接尝试CJK字体，避免Default字体的错误cmap映射 */
            if (codepoint > 127) {
                for (int i = 0; i < font_count; i++) {
                    if (i == current_font_index) continue;
                    if (!font_registry[i].loaded) continue;

                    /* 查找名称包含"CJK"的字体 */
                    const char *name = font_registry[i].name;
                    int is_cjk = 0;
                    for (int j = 0; name[j] && j < 32; j++) {
                        if (name[j] == 'C' && name[j+1] == 'J' && name[j+2] == 'K') {
                            is_cjk = 1;
                            break;
                        }
                    }
                    if (!is_cjk) continue;

                    /* 加载CJK字体 */
                    if (current_ttf_font_index != i) {
                        if (tt_font_load(font_registry[i].data, font_registry[i].size) == 0) {
                            current_ttf_font_index = i;
                            tt_set_pixel_size(tt_size);
                        } else {
                            continue;
                        }
                    }
                    uint16_t test_glyph = tt_char_to_glyph(codepoint);
                    if (test_glyph > 0) {
                        glyph = test_glyph;
                        font_switched = 1;
                        break;
                    }
                }
            }

            /* 如果glyph为0（.notdef），尝试其他已注册字体
             * V7.91: 对于CJK字符（>127），不回退到其他字体，避免Default字体cmap返回错误glyph
             * CJK字符只使用CJK字体，缺失时显示.notdef（方框） */
            if (glyph == 0 && codepoint <= 127) {
                for (int i = 0; i < font_count; i++) {
                    if (i == current_font_index) continue;
                    if (!font_registry[i].loaded) continue;

                    if (current_ttf_font_index != i) {
                        if (tt_font_load(font_registry[i].data, font_registry[i].size) == 0) {
                            current_ttf_font_index = i;
                            tt_set_pixel_size(tt_size);
                        } else {
                            continue;
                        }
                    }
                    uint16_t test_glyph = tt_char_to_glyph(codepoint);
                    if (test_glyph > 0) {
                        glyph = test_glyph;
                        font_switched = 1;
                        break;
                    }
                }
            }

            int16_t advance, lsb;
            tt_get_glyph_metrics(glyph, &advance, &lsb);

            /* V7.33: 使用实际units_per_em计算advance，修复字符重叠 */
            extern uint16_t tt_get_units_per_em(void);
            uint16_t units_em = tt_get_units_per_em();
            if (units_em == 0) units_em = 2048;  /* 默认值 */

            /* 渲染TrueType字形到临时缓冲区（已经是目标字号，不需要再缩放） */
            static uint8_t ttf_bitmap[64 * 64];
            /* V7.91: 渲染前清零缓冲区，避免之前字形残留导致重影 */
            memset(ttf_bitmap, 0, sizeof(ttf_bitmap));
            int offset_x, offset_y;
            int ttf_w = tt_render_glyph(glyph, ttf_bitmap, 64, 64, &offset_x, &offset_y);

            /* V7.36: 渲染完成后恢复原字体 */
            if (font_switched && original_font >= 0) {
                /* V7.47: 只有当原字体不是当前加载的字体时才加载 */
                if (current_ttf_font_index != original_font) {
                    tt_font_load(font_registry[original_font].data,
                                 font_registry[original_font].size);
                    current_ttf_font_index = original_font;
                    tt_set_pixel_size(tt_size);
                }
            }

            if (ttf_w > 0 && advance > 0) {
                /* V7.93: tt_render_glyph统一使用bitmap_w(64)作为行宽
                 * 所以src_w和src_h都使用64，确保像素访问正确
                 * advance仍使用实际字形宽度，保证字符间距正确 */
                memcpy(src_bitmap, ttf_bitmap, sizeof(src_bitmap));
                src_w = 64;  /* V7.93: 位图行宽=64 */
                src_h = 64;  /* V7.93: 位图高度=64 */
                src_advance = (int16_t)((advance * tt_size) / units_em);
                src_bx = (int16_t)((lsb * tt_size) / units_em);
                src_by = 0;
                ret = 0;
            } else {
                /* 渲染失败或advance无效，回退到抗锯齿位图 */
                ret = font_render_antialiased(codepoint, src_bitmap, &src_w, &src_h,
                                               &src_advance, &src_bx, &src_by);
            }
            break;
        }
        default:
            return -1;
    }

    if (ret != 0) return -1;

    /* V7.37: 缩放到目标字号
     * TrueType: tt_render_glyph已根据scale缩放到目标字号，不需要再缩放
     * 位图字体: 从16px缩放到目标字号
     */
    uint16_t dst_w, dst_h;
    if (type == FONT_TYPE_TRUETYPE) {
        /* TrueType字形已经是目标字号，直接使用 */
        dst_w = src_w;
        dst_h = src_h;
        if (dst_w > 64) dst_w = 64;
        if (dst_h > 64) dst_h = 64;
        memcpy(glyph_bitmap_buffer, src_bitmap, dst_w * dst_h);
    } else {
        /* 位图字体从16px缩放到目标字号 */
        dst_w = (src_w * size) / 16;
        dst_h = size;
        if (dst_w == 0) dst_w = 1;
        if (dst_w > 64) dst_w = 64;
        if (dst_h > 64) dst_h = 64;

        if (size != 16) {
            font_scale_bitmap(src_bitmap, src_w, src_h, glyph_bitmap_buffer, dst_w, dst_h);
        } else {
            memcpy(glyph_bitmap_buffer, src_bitmap, src_w * src_h);
        }
    }

    /* 存入缓存 */
    font_cache_entry_t *entry = font_cache_alloc();
    if (entry) {
        entry->codepoint = codepoint;
        entry->type = type;
        entry->size = size;
        /* V7.91: 复制位图到独立缓冲区，避免共享缓冲区被覆盖 */
        memcpy(entry->bitmap, glyph_bitmap_buffer, dst_w * dst_h);
        entry->width = dst_w;
        entry->height = dst_h;
        /* V7.37: TrueType的advance/bearing已经是目标字号值，位图字体需要缩放 */
        if (type == FONT_TYPE_TRUETYPE) {
            entry->advance = src_advance;
            entry->bearing_x = src_bx;
            entry->bearing_y = src_by;
        } else {
            entry->advance = (src_advance * size) / 16;
            entry->bearing_x = (src_bx * size) / 16;
            entry->bearing_y = (src_by * size) / 16;
        }
    }

    *bitmap = glyph_bitmap_buffer;
    *width = dst_w;
    *height = dst_h;
    /* V7.37: TrueType的advance/bearing已经是目标字号值 */
    if (type == FONT_TYPE_TRUETYPE) {
        *advance = src_advance;
        *bearing_x = src_bx;
        *bearing_y = src_by;
    } else {
        *advance = (src_advance * size) / 16;
        *bearing_x = (src_bx * size) / 16;
        *bearing_y = (src_by * size) / 16;
    }

    return 0;
}

/* ============================================================================
 * 统一API：绘制字符
 * ========================================================================== */

void gfx_draw_char_font(uint32_t x, uint32_t y, char c, gfx_color_t fg, uint16_t size)
{
    const uint8_t *bitmap;
    uint16_t width, height;
    int16_t advance, bx, by;

    if (font_render_glyph((uint8_t)c, current_font_type, size,
                          &bitmap, &width, &height, &advance, &bx, &by) != 0) {
        return;
    }

    for (uint16_t row = 0; row < height; row++) {
        for (uint16_t col = 0; col < width; col++) {
            uint8_t alpha = bitmap[row * width + col];
            if (alpha > 0) {
                if (alpha == 255) {
                    gfx_put_pixel(x + col + bx, y + row + by, fg);
                } else {
                    /* Alpha混合 */
                    gfx_color_t bg = gfx_get_pixel(x + col + bx, y + row + by);
                    uint8_t fr = (fg >> 16) & 0xFF;
                    uint8_t fg_ = (fg >> 8) & 0xFF;
                    uint8_t fb = fg & 0xFF;
                    uint8_t br = (bg >> 16) & 0xFF;
                    uint8_t bg_ = (bg >> 8) & 0xFF;
                    uint8_t bb = bg & 0xFF;

                    uint8_t r = (fr * alpha + br * (255 - alpha)) / 255;
                    uint8_t g = (fg_ * alpha + bg_ * (255 - alpha)) / 255;
                    uint8_t b = (fb * alpha + bb * (255 - alpha)) / 255;

                    gfx_put_pixel(x + col + bx, y + row + by, (r << 16) | (g << 8) | b);
                }
            }
        }
    }
}

/* ============================================================================
 * 统一API：绘制字符串
 * ========================================================================== */

void gfx_draw_string_font(uint32_t x, uint32_t y, const char *str, gfx_color_t fg, uint16_t size)
{
    if (!str) return;

    uint32_t cur_x = x;
    while (*str) {
        const uint8_t *bitmap;
        uint16_t width, height;
        int16_t advance, bx, by;

        /* V7.35: UTF-8解码，支持多字节中文字符 */
        uint32_t codepoint = 0;
        uint8_t c = (uint8_t)*str;

        if (c < 0x80) {
            /* 单字节ASCII */
            codepoint = c;
            str++;
        } else if ((c & 0xE0) == 0xC0) {
            /* 双字节UTF-8 */
            if ((str[1] & 0xC0) == 0x80) {
                codepoint = ((c & 0x1F) << 6) | (str[1] & 0x3F);
                str += 2;
            } else {
                codepoint = c;
                str++;
            }
        } else if ((c & 0xF0) == 0xE0) {
            /* 三字节UTF-8（中文） */
            if ((str[1] & 0xC0) == 0x80 && (str[2] & 0xC0) == 0x80) {
                codepoint = ((c & 0x0F) << 12) | ((str[1] & 0x3F) << 6) | (str[2] & 0x3F);
                str += 3;
            } else {
                codepoint = c;
                str++;
            }
        } else if ((c & 0xF8) == 0xF0) {
            /* 四字节UTF-8 */
            if ((str[1] & 0xC0) == 0x80 && (str[2] & 0xC0) == 0x80 && (str[3] & 0xC0) == 0x80) {
                codepoint = ((c & 0x07) << 18) | ((str[1] & 0x3F) << 12) | ((str[2] & 0x3F) << 6) | (str[3] & 0x3F);
                str += 4;
            } else {
                codepoint = c;
                str++;
            }
        } else {
            /* 无效UTF-8，跳过 */
            codepoint = c;
            str++;
        }

        if (font_render_glyph(codepoint, current_font_type, size,
                              &bitmap, &width, &height, &advance, &bx, &by) == 0) {
            for (uint16_t row = 0; row < height; row++) {
                for (uint16_t col = 0; col < width; col++) {
                    uint8_t alpha = bitmap[row * width + col];
                    if (alpha > 0) {
                        if (alpha == 255) {
                            gfx_put_pixel(cur_x + col + bx, y + row + by, fg);
                        } else {
                            gfx_color_t bg = gfx_get_pixel(cur_x + col + bx, y + row + by);
                            uint8_t fr = (fg >> 16) & 0xFF;
                            uint8_t fg_ = (fg >> 8) & 0xFF;
                            uint8_t fb = fg & 0xFF;
                            uint8_t br = (bg >> 16) & 0xFF;
                            uint8_t bg_ = (bg >> 8) & 0xFF;
                            uint8_t bb = bg & 0xFF;

                            uint8_t r = (fr * alpha + br * (255 - alpha)) / 255;
                            uint8_t g = (fg_ * alpha + bg_ * (255 - alpha)) / 255;
                            uint8_t b = (fb * alpha + bb * (255 - alpha)) / 255;

                            gfx_put_pixel(cur_x + col + bx, y + row + by, (r << 16) | (g << 8) | b);
                        }
                    }
                }
            }
            cur_x += advance;
        }
    }
}

/* ============================================================================
 * 字体管理器初始化
 * ========================================================================== */

void font_manager_init(void)
{
    if (font_manager_initialized) return;

    memset(font_cache, 0, sizeof(font_cache));
    font_cache_count = 0;
    current_font_size = 16;
    current_font_type = FONT_TYPE_ANTIALIASED;
    font_manager_initialized = 1;

    serial_puts(0x3F8, "[GFX-FONT] font manager initialized\n");
    serial_puts(0x3F8, "[GFX-FONT] default: antialiased 16px\n");
}

/* 设置当前字号 */
void font_set_size(uint16_t size)
{
    if (size < 8) size = 8;
    if (size > 64) size = 64;
    current_font_size = size;
}

/* 设置当前字体类型 */
void font_set_type(uint8_t type)
{
    if (type <= FONT_TYPE_TRUETYPE) {
        current_font_type = type;
    }
}

/* 获取当前字号 */
uint16_t font_get_size(void)
{
    return current_font_size;
}

/* 获取字符串宽度 */
uint32_t font_get_string_width(const char *str, uint16_t size)
{
    if (!str) return 0;

    uint32_t width = 0;
    while (*str) {
        const uint8_t *bitmap;
        uint16_t w, h;
        int16_t advance, bx, by;
        if (font_render_glyph((uint8_t)*str, current_font_type, size,
                              &bitmap, &w, &h, &advance, &bx, &by) == 0) {
            width += advance;
        }
        str++;
    }
    return width;
}

/* ============================================================================
 * TrueType字体加载和切换（V7.18）
 * ========================================================================== */

/* 加载TrueType字体数据并切换到TrueType字体 */
int font_load_truetype(uint8_t *data, uint32_t size)
{
    extern int tt_font_load(uint8_t*, uint32_t);
    extern void tt_set_pixel_size(uint16_t);

    if (!data || size == 0) {
        serial_puts(0x3F8, "[GFX-FONT] invalid TrueType font data\n");
        return -1;
    }

    /* 加载TrueType字体 */
    if (tt_font_load(data, size) != 0) {
        serial_puts(0x3F8, "[GFX-FONT] failed to load TrueType font\n");
        return -1;
    }

    /* V7.33: 验证字体有效性，失败则回退到抗锯齿字体 */
    extern uint16_t tt_get_units_per_em(void);
    extern uint16_t tt_get_num_glyphs(void);
    uint16_t units_em = tt_get_units_per_em();
    uint16_t num_glyphs = tt_get_num_glyphs();
    if (units_em == 0 || num_glyphs == 0) {
        serial_puts(0x3F8, "[GFX-FONT] TrueType font invalid (units_em=");
        extern void serial_puthex(uint16_t, uint32_t);
        serial_puthex(0x3F8, units_em);
        serial_puts(0x3F8, ", glyphs=");
        serial_puthex(0x3F8, num_glyphs);
        serial_puts(0x3F8, "), fallback to antialiased\n");
        current_font_type = FONT_TYPE_ANTIALIASED;
        return -1;
    }

    /* 设置像素大小 */
    tt_set_pixel_size(current_font_size);

    /* 切换当前字体类型为TrueType */
    current_font_type = FONT_TYPE_TRUETYPE;

    /* 清空字体缓存（因为字体类型变了） */
    for (int i = 0; i < FONT_CACHE_SIZE; i++) {
        font_cache[i].valid = 0;
    }

    serial_puts(0x3F8, "[GFX-FONT] switched to TrueType font\n");
    return 0;
}

/* 切换到位图字体 */
void font_use_bitmap(void)
{
    current_font_type = FONT_TYPE_BITMAP_8x16;
    /* 清空字体缓存 */
    for (int i = 0; i < FONT_CACHE_SIZE; i++) {
        font_cache[i].valid = 0;
    }
    serial_puts(0x3F8, "[GFX-FONT] switched to bitmap font\n");
}

/* 切换到抗锯齿位图字体 */
void font_use_antialiased(void)
{
    current_font_type = FONT_TYPE_ANTIALIASED;
    /* 清空字体缓存 */
    for (int i = 0; i < FONT_CACHE_SIZE; i++) {
        font_cache[i].valid = 0;
    }
    serial_puts(0x3F8, "[GFX-FONT] switched to antialiased font\n");
}

/* 获取当前字体类型名称 */
const char* font_get_type_name(void)
{
    switch (current_font_type) {
        case FONT_TYPE_BITMAP_8x16: return "Bitmap 8x16";
        case FONT_TYPE_ANTIALIASED: return "Antialiased";
        case FONT_TYPE_TRUETYPE: return "TrueType";
        default: return "Unknown";
    }
}

/* ============================================================================
 * V7.23: 多字体管理
 * ========================================================================== */

/* 注册字体 */
int font_register(uint8_t *data, uint32_t size, const char *name)
{
    if (font_count >= MAX_FONTS) {
        serial_puts(0x3F8, "[GFX-FONT] font registry full\n");
        return -1;
    }

    font_entry_t *entry = &font_registry[font_count];
    entry->data = data;
    entry->size = size;
    strncpy(entry->name, name ? name : "unknown", sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
    entry->loaded = 1;
    entry->is_default = 0;

    serial_puts(0x3F8, "[GFX-FONT] registered font: ");
    serial_puts(0x3F8, entry->name);
    serial_puts(0x3F8, " (");
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, size);
    serial_puts(0x3F8, " bytes)\n");

    return font_count++;
}

/* 选择字体 */
int font_select(int index)
{
    if (index < 0 || index >= font_count || !font_registry[index].loaded) {
        return -1;
    }

    /* 加载选中的TrueType字体 */
    extern int tt_font_load(uint8_t*, uint32_t);
    extern void tt_set_pixel_size(uint16_t);

    /* V7.47: 恢复原始逻辑，每次都加载字体 */
    if (tt_font_load(font_registry[index].data, font_registry[index].size) != 0) {
        return -1;
    }
    current_ttf_font_index = index;

    /* V7.33: 验证字体有效性，失败则回退到抗锯齿字体 */
    extern uint16_t tt_get_units_per_em(void);
    extern uint16_t tt_get_num_glyphs(void);
    uint16_t units_em = tt_get_units_per_em();
    uint16_t num_glyphs = tt_get_num_glyphs();
    if (units_em == 0 || num_glyphs == 0) {
        serial_puts(0x3F8, "[GFX-FONT] font invalid, fallback to antialiased\n");
        current_font_type = FONT_TYPE_ANTIALIASED;
        return -1;
    }
    tt_set_pixel_size(current_font_size);
    current_font_index = index;
    current_font_type = FONT_TYPE_TRUETYPE;

    /* 清空字体缓存 */
    for (int i = 0; i < FONT_CACHE_SIZE; i++) {
        font_cache[i].valid = 0;
    }

    serial_puts(0x3F8, "[GFX-FONT] selected font: ");
    serial_puts(0x3F8, font_registry[index].name);
    serial_puts(0x3F8, "\n");
    return 0;
}

/* 获取字体数量 */
int font_get_count(void)
{
    return font_count;
}

/* 获取字体名称 */
const char* font_get_name(int index)
{
    if (index < 0 || index >= font_count) return NULL;
    return font_registry[index].name;
}

/* 获取当前字体索引 */
int font_get_current(void)
{
    return current_font_index;
}

/* 设置默认字体 */
int font_set_default(int index)
{
    if (index < 0 || index >= font_count) return -1;

    for (int i = 0; i < font_count; i++) {
        font_registry[i].is_default = 0;
    }
    font_registry[index].is_default = 1;
    return 0;
}

/* 字体回退：查找包含指定字符的字体 */
int font_fallback(uint32_t codepoint)
{
    extern uint16_t tt_char_to_glyph(uint32_t);

    /* 先尝试当前字体 */
    if (current_font_index >= 0) {
        uint16_t glyph = tt_char_to_glyph(codepoint);
        if (glyph > 0) return current_font_index;
    }

    /* 遍历所有字体，查找包含该字符的字体 */
    for (int i = 0; i < font_count; i++) {
        if (i == current_font_index) continue;
        if (!font_registry[i].loaded) continue;

        /* 临时加载该字体并检查字符 */
        extern int tt_font_load(uint8_t*, uint32_t);
        if (tt_font_load(font_registry[i].data, font_registry[i].size) == 0) {
            uint16_t glyph = tt_char_to_glyph(codepoint);
            if (glyph > 0) {
                /* 找到包含该字符的字体，切换到该字体 */
                return font_select(i);
            }
        }
    }

    /* 恢复当前字体 */
    if (current_font_index >= 0) {
        font_select(current_font_index);
    }

    return -1;  /* 没有字体包含该字符 */
}

/* ============================================================================
 * V7.24: 字体配置文件加载
 * ========================================================================== */

/* 尝试加载默认字体文件（使用ide_ext2） */
static int font_try_load(const char *dirpath, const char *filename, const char *name)
{
    extern int ide_ext2_mount(void);
    extern int ide_ext2_read_file(const char*, const char*, char*, int);
    extern int ide_ext2_get_file_size(const char*, const char*);
    extern int fs_mounted;
    extern void* kmalloc(uint32_t);

    /* 确保ide_ext2已挂载 */
    if (!fs_mounted) {
        if (ide_ext2_mount() != 0) {
            return -1;
        }
    }

    /* V7.47: 先获取文件大小，再分配精确大小缓冲区，避免2MB大内存分配失败 */
    int file_size = ide_ext2_get_file_size(dirpath, filename);
    if (file_size <= 0) {
        return -1;
    }

    /* 分配精确大小的缓冲区（+1用于null终止） */
    uint8_t *data = (uint8_t*)kmalloc((uint32_t)file_size + 1);
    if (!data) {
        serial_puts(0x3F8, "[GFX-FONT] failed to allocate font buffer\n");
        return -1;
    }

    /* 使用ide_ext2读取文件 */
    int read_size = ide_ext2_read_file(dirpath, filename, (char*)data, file_size + 1);
    if (read_size <= 0) {
        extern void kfree(void*);
        kfree(data);
        return -1;
    }

    /* 注册字体 */
    int index = font_register(data, (uint32_t)read_size, name);
    if (index >= 0) {
        /* 自动选择第一个加载的字体 */
        if (current_font_index < 0) {
            font_select(index);
        }
    }

    return index;
}

/* V7.34: 从内置数据加载字体 */
static int font_load_builtin(const uint8_t *data, uint32_t size, const char *name)
{
    if (!data || size == 0) return -1;

    /* 注册字体（直接使用内置数据指针，不需要复制） */
    int index = font_register((uint8_t*)data, size, name);
    if (index >= 0) {
        /* 自动选择第一个加载的字体 */
        if (current_font_index < 0) {
            font_select(index);
        }
    }
    return index;
}

/* 加载默认字体配置 */
void font_load_default_config(void)
{
    serial_puts(0x3F8, "[GFX-FONT] loading default font config...\n");

    int loaded = 0;

    /* V7.34: 优先加载内置字体（确保启动即可用，不依赖文件系统） */
    if (font_load_builtin(default_font_data, default_font_size, "Default (Built-in)") >= 0) {
        loaded++;
        serial_puts(0x3F8, "[GFX-FONT] loaded built-in Default font\n");
    }
    if (font_load_builtin(cjk_font_data_data, cjk_font_data_size, "CJK (Built-in)") >= 0) {
        loaded++;
        serial_puts(0x3F8, "[GFX-FONT] loaded built-in CJK font\n");
    }

    /* 然后尝试从文件系统加载字体（可替换内置字体） */
    static const char *font_paths[][3] = {
        {"/fonts", "default.ttf", "Default"},
        {"/fonts", "DejaVuSans.ttf", "DejaVu Sans"},
        {"/fonts", "arial.ttf", "Arial"},
        {"/fonts", "cjk.ttf", "CJK"},
        {"/system/fonts", "default.ttf", "System Default"},
        {NULL, NULL, NULL}
    };

    /* V7.47: 暂时禁用文件系统字体加载，避免旧字体干扰 */
    if (0) {
    for (int i = 0; font_paths[i][0] != NULL; i++) {
        if (font_try_load(font_paths[i][0], font_paths[i][1], font_paths[i][2]) >= 0) {
            loaded++;
        }
    }
    }

    if (loaded > 0) {
        serial_puts(0x3F8, "[GFX-FONT] loaded ");
        extern void serial_puthex(uint16_t, uint32_t);
        serial_puthex(0x3F8, loaded);
        serial_puts(0x3F8, " font(s) from config\n");
        /* V7.47: 默认使用Default字体，中文通过字体回退机制使用CJK字体 */
    } else {
        serial_puts(0x3F8, "[GFX-FONT] no default fonts found, using built-in bitmap\n");
    }
}
