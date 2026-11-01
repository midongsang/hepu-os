/* ============================================================================
 * HepuOS TrueType字体解析器（基础框架）
 * 支持TrueType字体文件解析、字形轮廓提取和光栅化
 * 参考: TrueType Reference Manual (Apple/Microsoft)
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "serial.h"
#include "graphics.h"

/* TrueType表标签（大端序，与tt_read32读取结果一致） */
#define TT_TAG_HEAD  0x68656164  /* "head" */
#define TT_TAG_HHEA  0x68686561  /* "hhea" */
#define TT_TAG_MAXP  0x6D617870  /* "maxp" */
#define TT_TAG_CMAP  0x636D6170  /* "cmap" */
#define TT_TAG_GLYF  0x676C7966  /* "glyf" */
#define TT_TAG_LOCA  0x6C6F6361  /* "loca" */
#define TT_TAG_HMTX  0x686D7478  /* "hmtx" */
#define TT_TAG_NAME  0x6E616D65  /* "name" */
#define TT_TAG_POST  0x706F7374  /* "post" */
#define TT_TAG_OS2   0x4F532F32  /* "OS/2" */

/* TrueType文件头 */
typedef struct {
    uint32_t sfnt_version;    /* 0x00010000 for TrueType */
    uint16_t num_tables;
    uint16_t search_range;
    uint16_t entry_selector;
    uint16_t range_shift;
} __attribute__((packed)) tt_offset_table_t;

/* 表目录条目 */
typedef struct {
    uint32_t tag;
    uint32_t checksum;
    uint32_t offset;
    uint32_t length;
} __attribute__((packed)) tt_table_record_t;

/* head表 */
typedef struct {
    uint32_t version;
    uint32_t font_revision;
    uint32_t checksum_adjustment;
    uint32_t magic_number;     /* 0x5F0F3CF5 */
    uint16_t flags;
    uint16_t units_per_em;
    int64_t  created;
    int64_t  modified;
    int16_t  x_min;
    int16_t  y_min;
    int16_t  x_max;
    int16_t  y_max;
    uint16_t mac_style;
    uint16_t lowest_rec_ppem;
    int16_t  font_direction_hint;
    int16_t  index_to_loc_format;  /* 0=short, 1=long */
    int16_t  glyph_data_format;
} __attribute__((packed)) tt_head_table_t;

/* hhea表 */
typedef struct {
    uint32_t version;
    int16_t  ascent;
    int16_t  descent;
    int16_t  line_gap;
    uint16_t advance_width_max;
    int16_t  min_left_side_bearing;
    int16_t  min_right_side_bearing;
    int16_t  x_max_extent;
    int16_t  caret_slope_rise;
    int16_t  caret_slope_run;
    int16_t  caret_offset;
    int16_t  reserved[4];
    int16_t  metric_data_format;
    uint16_t num_of_long_hor_metrics;
} __attribute__((packed)) tt_hhea_table_t;

/* maxp表 */
typedef struct {
    uint32_t version;
    uint16_t num_glyphs;
    uint16_t max_points;
    uint16_t max_contours;
    uint16_t max_composite_points;
    uint16_t max_composite_contours;
    uint16_t max_zones;
    uint16_t max_twilight_points;
    uint16_t max_storage;
    uint16_t max_function_defs;
    uint16_t max_instruction_defs;
    uint16_t max_stack_elements;
    uint16_t max_size_of_instructions;
    uint16_t max_component_elements;
    uint16_t max_component_depth;
} __attribute__((packed)) tt_maxp_table_t;

/* 字形头部 */
typedef struct {
    int16_t  number_of_contours;  /* 负数=复合字形 */
    int16_t  x_min;
    int16_t  y_min;
    int16_t  x_max;
    int16_t  y_max;
} __attribute__((packed)) tt_glyph_header_t;

/* 简单字形标志位 */
#define TT_FLAG_ON_CURVE   0x01
#define TT_FLAG_X_SHORT    0x02
#define TT_FLAG_Y_SHORT    0x04
#define TT_FLAG_REPEAT     0x08
#define TT_FLAG_X_SAME     0x10
#define TT_FLAG_Y_SAME     0x20

/* 字体解析器状态 */
typedef struct {
    uint8_t  *data;           /* 字体文件数据 */
    uint32_t  data_size;      /* 文件大小 */
    int       loaded;         /* 是否加载 */

    /* 表偏移 */
    uint32_t  head_offset;
    uint32_t  hhea_offset;
    uint32_t  maxp_offset;
    uint32_t  cmap_offset;
    uint32_t  glyf_offset;
    uint32_t  loca_offset;
    uint32_t  hmtx_offset;

    /* 字体信息 */
    uint16_t  units_per_em;
    int16_t   ascent;
    int16_t   descent;
    int16_t   line_gap;
    uint16_t  num_glyphs;
    int16_t   index_to_loc_format;
    uint16_t  num_of_long_hor_metrics;

    /* 缩放因子 */
    float     scale;          /* pixels per em */
} tt_font_t;

static tt_font_t tt_font;
static int tt_initialized = 0;

/* V7.30: Hinting状态（前向声明） */
static int tt_hinting_enabled = 1;  /* 默认启用Hinting */

/* 读取大端16位 */
static inline uint16_t tt_read16(const uint8_t *p) {
    return (p[0] << 8) | p[1];
}

/* 读取大端32位 */
static inline uint32_t tt_read32(const uint8_t *p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

/* 读取大端16位有符号 */
static inline int16_t tt_read16s(const uint8_t *p) {
    return (int16_t)((p[0] << 8) | p[1]);
}

/* ============================================================================
 * 字体加载和解析
 * ========================================================================== */

int tt_font_load(uint8_t *data, uint32_t size)
{
    if (!data || size < sizeof(tt_offset_table_t)) {
        serial_puts(0x3F8, "[TTF] invalid font data\n");
        return -1;
    }

    memset(&tt_font, 0, sizeof(tt_font_t));
    tt_font.data = data;
    tt_font.data_size = size;

    /* 解析偏移表 */
    tt_offset_table_t *offset = (tt_offset_table_t *)data;
    uint32_t sfnt = tt_read32((uint8_t*)&offset->sfnt_version);

    if (sfnt != 0x00010000) {
        serial_puts(0x3F8, "[TTF] not a TrueType font (sfnt=0x");
        extern void serial_puthex(uint16_t, uint32_t);
        serial_puthex(0x3F8, sfnt);
        serial_puts(0x3F8, ")\n");
        return -1;
    }

    uint16_t num_tables = tt_read16((uint8_t*)&offset->num_tables);
    serial_puts(0x3F8, "[TTF] loading font, tables=");
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, num_tables);
    serial_puts(0x3F8, "\n");

    /* 解析表目录 */
    tt_table_record_t *tables = (tt_table_record_t *)(data + sizeof(tt_offset_table_t));
    for (int i = 0; i < num_tables; i++) {
        uint32_t tag = tt_read32((uint8_t*)&tables[i].tag);
        uint32_t offset_val = tt_read32((uint8_t*)&tables[i].offset);
        uint32_t length = tt_read32((uint8_t*)&tables[i].length);

        switch (tag) {
            case TT_TAG_HEAD:
                tt_font.head_offset = offset_val;
                break;
            case TT_TAG_HHEA:
                tt_font.hhea_offset = offset_val;
                break;
            case TT_TAG_MAXP:
                tt_font.maxp_offset = offset_val;
                break;
            case TT_TAG_CMAP:
                tt_font.cmap_offset = offset_val;
                break;
            case TT_TAG_GLYF:
                tt_font.glyf_offset = offset_val;
                break;
            case TT_TAG_LOCA:
                tt_font.loca_offset = offset_val;
                break;
            case TT_TAG_HMTX:
                tt_font.hmtx_offset = offset_val;
                break;
        }
    }

    /* 解析head表 */
    if (tt_font.head_offset) {
        tt_head_table_t *head = (tt_head_table_t *)(data + tt_font.head_offset);
        tt_font.units_per_em = tt_read16((uint8_t*)&head->units_per_em);
        tt_font.index_to_loc_format = tt_read16s((uint8_t*)&head->index_to_loc_format);
    }

    /* 解析hhea表 */
    if (tt_font.hhea_offset) {
        tt_hhea_table_t *hhea = (tt_hhea_table_t *)(data + tt_font.hhea_offset);
        tt_font.ascent = tt_read16s((uint8_t*)&hhea->ascent);
        tt_font.descent = tt_read16s((uint8_t*)&hhea->descent);
        tt_font.line_gap = tt_read16s((uint8_t*)&hhea->line_gap);
        tt_font.num_of_long_hor_metrics = tt_read16((uint8_t*)&hhea->num_of_long_hor_metrics);
    }

    /* 解析maxp表 */
    if (tt_font.maxp_offset) {
        tt_maxp_table_t *maxp = (tt_maxp_table_t *)(data + tt_font.maxp_offset);
        tt_font.num_glyphs = tt_read16((uint8_t*)&maxp->num_glyphs);
    }

    tt_font.loaded = 1;
    tt_initialized = 1;

    serial_puts(0x3F8, "[TTF] font loaded: ");
    serial_puthex(0x3F8, tt_font.units_per_em);
    serial_puts(0x3F8, " units/em, ");
    serial_puthex(0x3F8, tt_font.num_glyphs);
    serial_puts(0x3F8, " glyphs, ascent=");
    serial_puthex(0x3F8, tt_font.ascent);
    serial_puts(0x3F8, " descent=");
    serial_puthex(0x3F8, tt_font.descent);
    serial_puts(0x3F8, "\n");

    return 0;
}

/* ============================================================================
 * 字符到字形索引映射（简化版，支持格式4 cmap）
 * ========================================================================== */

uint16_t tt_char_to_glyph(uint32_t codepoint)
{
    if (!tt_font.loaded || !tt_font.cmap_offset) return 0;

    uint8_t *cmap = tt_font.data + tt_font.cmap_offset;
    uint16_t version = tt_read16(cmap);
    uint16_t num_tables = tt_read16(cmap + 2);

    /* 查找platform=3, encoding=1的cmap子表（Microsoft Unicode BMP） */
    for (int i = 0; i < num_tables; i++) {
        uint8_t *entry = cmap + 4 + i * 8;
        uint16_t platform = tt_read16(entry);
        uint16_t encoding = tt_read16(entry + 2);
        uint32_t offset = tt_read32(entry + 4);

        if (platform == 3 && encoding == 1) {
            uint8_t *subtable = cmap + offset;
            uint16_t format = tt_read16(subtable);

            if (format == 4) {
                /* 格式4：段映射 */
                uint16_t seg_count = tt_read16(subtable + 6) / 2;
                uint16_t *end_codes = (uint16_t *)(subtable + 14);
                uint16_t *start_codes = (uint16_t *)(subtable + 16 + seg_count * 2);
                int16_t *id_deltas = (int16_t *)(subtable + 16 + seg_count * 4);
                uint16_t *id_range_offsets = (uint16_t *)(subtable + 16 + seg_count * 6);

                for (int s = 0; s < seg_count; s++) {
                    uint16_t start = tt_read16((uint8_t*)&start_codes[s]);
                    uint16_t end = tt_read16((uint8_t*)&end_codes[s]);

                    if (codepoint >= start && codepoint <= end) {
                        uint16_t range_offset = tt_read16((uint8_t*)&id_range_offsets[s]);
                        int16_t delta = tt_read16s((uint8_t*)&id_deltas[s]);

                        if (range_offset == 0) {
                            /* V7.91: 检查glyph有效性，避免cmap格式4对未匹配字符返回错误glyph
                             * 格式4最后一段通常是[0xFFFF,0xFFFF], delta=1, 未匹配字符返回(codepoint+1)&0xFFFF
                             * 这可能是一个大于num_glyphs的无效值，导致渲染错误字形 */
                            uint16_t glyph = (codepoint + delta) & 0xFFFF;
                            if (glyph >= tt_font.num_glyphs) return 0;
                            return glyph;
                        } else {
                            uint16_t *glyph_index = (uint16_t *)(subtable + 16 + seg_count * 6 +
                                s * 2 + range_offset + (codepoint - start) * 2);
                            uint16_t glyph = tt_read16((uint8_t*)glyph_index);
                            if (glyph != 0) {
                                uint16_t result = (glyph + delta) & 0xFFFF;
                                /* V7.91: 检查glyph有效性 */
                                if (result >= tt_font.num_glyphs) return 0;
                                return result;
                            }
                            return 0;
                        }
                    }
                }
            }
        }
    }

    return 0;  /* .notdef */
}

/* ============================================================================
 * 字形度量
 * ========================================================================== */

/* V7.33: 获取units_per_em */
uint16_t tt_get_units_per_em(void)
{
    return tt_font.units_per_em;
}

/* V7.33: 获取字形数量 */
uint16_t tt_get_num_glyphs(void)
{
    return tt_font.num_glyphs;
}

int tt_get_glyph_metrics(uint16_t glyph_index, int16_t *advance_width,
                          int16_t *left_side_bearing)
{
    if (!tt_font.loaded || !tt_font.hmtx_offset) return -1;

    uint8_t *hmtx = tt_font.data + tt_font.hmtx_offset;

    if (glyph_index < tt_font.num_of_long_hor_metrics) {
        /* 长度量格式 */
        uint8_t *entry = hmtx + glyph_index * 4;
        *advance_width = tt_read16(entry);
        *left_side_bearing = tt_read16s(entry + 2);
    } else {
        /* 短度量格式（只有left_side_bearing） */
        uint16_t last_advance = tt_read16(hmtx + (tt_font.num_of_long_hor_metrics - 1) * 4);
        uint8_t *entry = hmtx + tt_font.num_of_long_hor_metrics * 4 +
                         (glyph_index - tt_font.num_of_long_hor_metrics) * 2;
        *advance_width = last_advance;
        *left_side_bearing = tt_read16s(entry);
    }

    return 0;
}

/* ============================================================================
 * 字体缩放和渲染设置
 * ========================================================================== */

void tt_set_pixel_size(uint16_t pixels)
{
    if (tt_font.units_per_em > 0) {
        tt_font.scale = (float)pixels / (float)tt_font.units_per_em;
    } else {
        tt_font.scale = 1.0f;
    }
}

int tt_get_pixel_height(void)
{
    if (!tt_font.loaded) return 16;
    return (int)((tt_font.ascent - tt_font.descent + tt_font.line_gap) * tt_font.scale);
}

/* ============================================================================
 * TrueType完整光栅化（V7.20）
 * 支持：端点解析、二次贝塞尔曲线细分、扫描线填充、抗锯齿超采样
 * ========================================================================== */

#define TT_MAX_POINTS   1024  /* V7.65: 从512增大到1024，支持复杂CJK字形 */
#define TT_MAX_CONTOURS 128   /* V7.65: 从64增大到128 */
#define TT_SUBSAMPLE    4  /* 4x4超采样 */

typedef struct {
    float x, y;
} tt_point_t;

/* 二次贝塞尔曲线细分（V7.44: 优化阈值，曲线更平滑） */
static void tt_subdivide_quadratic(tt_point_t p0, tt_point_t p1, tt_point_t p2,
                                    tt_point_t *out, int *count, int max_count)
{
    /* 递归细分，直到曲线足够平 */
    float dx = p2.x - p0.x;
    float dy = p2.y - p0.y;
    float dist = dx * dx + dy * dy;

    /* V7.44: 降低阈值从0.5到0.25，曲线更平滑 */
    if (dist < 0.25f || *count >= max_count - 2) {
        out[(*count)++] = p2;
        return;
    }

    /* 中点细分 */
    tt_point_t m01 = {(p0.x + p1.x) / 2, (p0.y + p1.y) / 2};
    tt_point_t m12 = {(p1.x + p2.x) / 2, (p1.y + p2.y) / 2};
    tt_point_t m012 = {(m01.x + m12.x) / 2, (m01.y + m12.y) / 2};

    tt_subdivide_quadratic(p0, m01, m012, out, count, max_count);
    tt_subdivide_quadratic(m012, m12, p2, out, count, max_count);
}

/* 扫描线填充（非零环绕规则） */
static void tt_scanline_fill(tt_point_t *points, int num_points,
                              uint8_t *bitmap, int bmp_w, int bmp_h,
                              int offset_x, int offset_y)
{
    if (num_points < 3) return;

    /* V7.48: 非零环绕规则（TrueType标准），替代奇偶规则
     * 中文字符有多个不同方向的轮廓，奇偶规则会导致填充错误
     */
    typedef struct {
        float x;
        int dir;  /* +1=向上, -1=向下 */
    } intersect_t;

    /* 对每条扫描线 */
    for (int y = 0; y < bmp_h * TT_SUBSAMPLE; y++) {
        float scan_y = (float)y / TT_SUBSAMPLE + 0.5f;
        static intersect_t intersections[2048];  /* V7.65: 从1024增大到2048 */
        int num_intersections = 0;

        /* 计算与所有边的交点，记录方向 */
        for (int i = 0; i < num_points; i++) {
            int j = (i + 1) % num_points;
            float y1 = points[i].y - offset_y;
            float y2 = points[j].y - offset_y;

            if (y1 < scan_y && y2 >= scan_y) {
                /* 向上边 */
                float t = (scan_y - y1) / (y2 - y1);
                float x = points[i].x - offset_x + t * (points[j].x - points[i].x);
                if (num_intersections < 1024) {
                    intersections[num_intersections].x = x;
                    intersections[num_intersections].dir = 1;
                    num_intersections++;
                }
            } else if (y2 < scan_y && y1 >= scan_y) {
                /* 向下边 */
                float t = (scan_y - y1) / (y2 - y1);
                float x = points[i].x - offset_x + t * (points[j].x - points[i].x);
                if (num_intersections < 1024) {
                    intersections[num_intersections].x = x;
                    intersections[num_intersections].dir = -1;
                    num_intersections++;
                }
            }
        }

        /* 按x坐标排序交点 */
        for (int i = 0; i < num_intersections - 1; i++) {
            for (int j = i + 1; j < num_intersections; j++) {
                if (intersections[i].x > intersections[j].x) {
                    intersect_t tmp = intersections[i];
                    intersections[i] = intersections[j];
                    intersections[j] = tmp;
                }
            }
        }

        /* 非零环绕规则：累加方向，非零区域填充 */
        int winding = 0;
        for (int i = 0; i < num_intersections; i++) {
            int old_winding = winding;
            winding += intersections[i].dir;

            /* 如果当前区域环绕数非零，填充从当前交点到下一个交点 */
            if (winding != 0 && i + 1 < num_intersections) {
                int x_start = (int)(intersections[i].x * TT_SUBSAMPLE);
                int x_end = (int)(intersections[i + 1].x * TT_SUBSAMPLE);

                for (int x = x_start; x < x_end; x++) {
                    int px = x / TT_SUBSAMPLE;
                    int py = y / TT_SUBSAMPLE;
                    if (px >= 0 && px < bmp_w && py >= 0 && py < bmp_h) {
                        int val = bitmap[py * bmp_w + px] + 255 / (TT_SUBSAMPLE * TT_SUBSAMPLE);
                        if (val > 255) val = 255;
                        bitmap[py * bmp_w + px] = (uint8_t)val;
                    }
                }
            }
        }
    }
}

/* 复合字形组件flags */
#define TT_ARG_1_AND_2_ARE_WORDS  0x0001
#define TT_ARGS_ARE_XY_VALUES     0x0002
#define TT_ROUND_XY_TO_GRID       0x0004
#define TT_WE_HAVE_A_SCALE        0x0008
#define TT_MORE_COMPONENTS        0x0020
#define TT_WE_HAVE_XY_SCALE       0x0040
#define TT_WE_HAVE_TWO_BY_TWO     0x0080
#define TT_WE_HAVE_INSTRUCTIONS   0x0100

/* 渲染简单字形（内部函数，供复合字形调用） */
static int tt_render_simple_internal(uint8_t *glyph_data,
                                      uint8_t *bitmap, uint16_t bitmap_w, uint16_t bitmap_h,
                                      int offset_x, int offset_y,
                                      float scale_x, float scale_y)
{
    /* 简化：直接调用主渲染逻辑的简单字形部分
     * 这里复用tt_render_glyph的简单字形处理
     */
    return 0;  /* 占位，实际渲染在主函数中完成 */
}

/* 前向声明 */
int tt_render_glyph(uint16_t glyph_index, uint8_t *bitmap,
                     uint16_t bitmap_w, uint16_t bitmap_h,
                     int *offset_x, int *offset_y);

/* V7.21: 复合字形渲染 */
static int tt_render_composite_glyph(uint16_t glyph_index,
                                      uint8_t *bitmap, uint16_t bitmap_w, uint16_t bitmap_h,
                                      int *offset_x, int *offset_y)
{
    if (!tt_font.loaded || !tt_font.glyf_offset || !tt_font.loca_offset) {
        return -1;
    }

    /* 获取字形偏移 */
    uint32_t glyph_offset;
    if (tt_font.index_to_loc_format == 0) {
        uint16_t off1 = tt_read16(tt_font.data + tt_font.loca_offset + glyph_index * 2);
        uint16_t off2 = tt_read16(tt_font.data + tt_font.loca_offset + (glyph_index + 1) * 2);
        if (off1 == off2) return -1;
        glyph_offset = tt_font.glyf_offset + off1 * 2;
    } else {
        uint32_t off1 = tt_read32(tt_font.data + tt_font.loca_offset + glyph_index * 4);
        uint32_t off2 = tt_read32(tt_font.data + tt_font.loca_offset + (glyph_index + 1) * 4);
        if (off1 == off2) return -1;
        glyph_offset = tt_font.glyf_offset + off1;
    }

    uint8_t *p = tt_font.data + glyph_offset;
    int16_t num_contours = tt_read16s(p);
    int16_t x_min = tt_read16s(p + 2);
    int16_t y_min = tt_read16s(p + 4);
    int16_t x_max = tt_read16s(p + 6);
    int16_t y_max = tt_read16s(p + 8);
    p += 10;

    if (num_contours >= 0) return -1;  /* 不是复合字形 */

    /* 计算字形尺寸 */
    int glyph_w = (int)((x_max - x_min) * tt_font.scale) + 1;
    int glyph_h = (int)((y_max - y_min) * tt_font.scale) + 1;
    if (glyph_w <= 0 || glyph_h <= 0) return -1;
    if (glyph_w > bitmap_w) glyph_w = bitmap_w;
    if (glyph_h > bitmap_h) glyph_h = bitmap_h;

    *offset_x = (int)(x_min * tt_font.scale);
    *offset_y = (int)(y_max * tt_font.scale);

    /* 清空位图 */
    memset(bitmap, 0, bitmap_w * bitmap_h);

    /* 解析复合字形组件 */
    int has_more = 1;
    int component_count = 0;

    while (has_more && component_count < 32) {
        uint16_t flags = tt_read16(p);
        p += 2;
        uint16_t comp_glyph = tt_read16(p);
        p += 2;

        /* 解析参数（平移） */
        int16_t arg1, arg2;
        if (flags & TT_ARG_1_AND_2_ARE_WORDS) {
            arg1 = tt_read16s(p);
            arg2 = tt_read16s(p + 2);
            p += 4;
        } else {
            arg1 = (int8_t)*p++;
            arg2 = (int8_t)*p++;
        }

        /* 解析缩放/变换 */
        float scale_x = 1.0f, scale_y = 1.0f;
        float scale_01 = 0.0f, scale_10 = 0.0f;

        if (flags & TT_WE_HAVE_A_SCALE) {
            /* 2.14格式 */
            int16_t s = tt_read16s(p);
            p += 2;
            scale_x = scale_y = s / 16384.0f;
        } else if (flags & TT_WE_HAVE_XY_SCALE) {
            int16_t sx = tt_read16s(p);
            int16_t sy = tt_read16s(p + 2);
            p += 4;
            scale_x = sx / 16384.0f;
            scale_y = sy / 16384.0f;
        } else if (flags & TT_WE_HAVE_TWO_BY_TWO) {
            int16_t a = tt_read16s(p);
            int16_t b = tt_read16s(p + 2);
            int16_t c = tt_read16s(p + 4);
            int16_t d = tt_read16s(p + 6);
            p += 8;
            scale_x = a / 16384.0f;
            scale_01 = b / 16384.0f;
            scale_10 = c / 16384.0f;
            scale_y = d / 16384.0f;
        }

        /* 计算组件偏移（像素，字体坐标系y轴向上） */
        int comp_offset_x = (int)(arg1 * tt_font.scale);
        int comp_offset_y = (int)(arg2 * tt_font.scale);

        /* 递归渲染组件字形（简单字形） */
        static uint8_t comp_bitmap[128 * 128];
        int comp_off_x, comp_off_y;
        int comp_w = tt_render_glyph(comp_glyph, comp_bitmap, 128, 128,
                                      &comp_off_x, &comp_off_y);

        if (comp_w > 0) {
            /* 将组件位图合并到主位图
             * V7.92: 修复复合字形渲染bug
             * - 正确处理y轴方向（位图y向下，字体y向上）
             * - 组件高度使用bitmap_h(128)，避免假设正方形导致底部像素丢失 */
            for (int cy = 0; cy < 128; cy++) {
                for (int cx = 0; cx < comp_w && cx < 128; cx++) {
                    uint8_t alpha = comp_bitmap[cy * 128 + cx];
                    if (alpha == 0) continue;

                    /* 位图坐标 -> 字体坐标（y轴向上） */
                    float glyph_x = cx + comp_off_x;
                    float glyph_y = comp_off_y - cy;

                    /* 应用2x2变换矩阵（字体坐标系） */
                    float src_x = glyph_x * scale_x + glyph_y * scale_10;
                    float src_y = glyph_x * scale_01 + glyph_y * scale_y;

                    /* 应用组件平移偏移，得到最终字体坐标 */
                    float final_x = src_x + comp_offset_x;
                    float final_y = src_y + comp_offset_y;

                    /* 字体坐标 -> 主位图坐标（y轴向下）
                     * 主位图(0,0)对应字体坐标(*offset_x, *offset_y)，其中*offset_y=y_max*scale */
                    int dst_x = (int)final_x - *offset_x;
                    int dst_y = *offset_y - (int)final_y;

                    if (dst_x >= 0 && dst_x < glyph_w &&
                        dst_y >= 0 && dst_y < glyph_h) {
                        /* Alpha混合（取最大值） */
                        if (alpha > bitmap[dst_y * bitmap_w + dst_x]) {
                            bitmap[dst_y * bitmap_w + dst_x] = alpha;
                        }
                    }
                }
            }
        }

        has_more = (flags & TT_MORE_COMPONENTS) ? 1 : 0;
        component_count++;

        /* 跳过指令（如果有） */
        if (!has_more && (flags & TT_WE_HAVE_INSTRUCTIONS)) {
            uint16_t instr_len = tt_read16(p);
            p += 2 + instr_len;
        }
    }

    serial_puts(0x3F8, "[TTF] composite glyph rendered, components=");
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puthex(0x3F8, component_count);
    serial_puts(0x3F8, "\n");

    return glyph_w;
}

int tt_render_glyph(uint16_t glyph_index, uint8_t *bitmap,
                     uint16_t bitmap_w, uint16_t bitmap_h,
                     int *offset_x, int *offset_y)
{
    if (!tt_font.loaded || !tt_font.glyf_offset || !tt_font.loca_offset) {
        return -1;
    }

    /* 获取字形偏移 */
    uint32_t glyph_offset;
    if (tt_font.index_to_loc_format == 0) {
        uint16_t off1 = tt_read16(tt_font.data + tt_font.loca_offset + glyph_index * 2);
        uint16_t off2 = tt_read16(tt_font.data + tt_font.loca_offset + (glyph_index + 1) * 2);
        if (off1 == off2) return -1;
        glyph_offset = tt_font.glyf_offset + off1 * 2;
    } else {
        uint32_t off1 = tt_read32(tt_font.data + tt_font.loca_offset + glyph_index * 4);
        uint32_t off2 = tt_read32(tt_font.data + tt_font.loca_offset + (glyph_index + 1) * 4);
        if (off1 == off2) return -1;
        glyph_offset = tt_font.glyf_offset + off1;
    }

    /* 解析字形头 */
    uint8_t *p = tt_font.data + glyph_offset;
    int16_t num_contours = tt_read16s(p);
    int16_t x_min = tt_read16s(p + 2);
    int16_t y_min = tt_read16s(p + 4);
    int16_t x_max = tt_read16s(p + 6);
    int16_t y_max = tt_read16s(p + 8);
    p += 10;

    if (num_contours < 0) {
        /* V7.21: 复合字形支持 */
        return tt_render_composite_glyph(glyph_index, bitmap, bitmap_w, bitmap_h,
                                          offset_x, offset_y);
    }
    if (num_contours == 0) return 0;  /* 空字形 */

    /* V7.93: 清零位图，使用bitmap_w作为行宽 */
    memset(bitmap, 0, bitmap_w * bitmap_h);

    if (num_contours > TT_MAX_CONTOURS) num_contours = TT_MAX_CONTOURS;

    /* 解析轮廓端点 */
    uint16_t end_pts[TT_MAX_CONTOURS];
    int total_points = 0;
    for (int i = 0; i < num_contours; i++) {
        end_pts[i] = tt_read16(p);
        p += 2;
        if (end_pts[i] + 1 > total_points) total_points = end_pts[i] + 1;
    }

    if (total_points > TT_MAX_POINTS) total_points = TT_MAX_POINTS;

    /* V7.30: 读取并执行字形Hinting指令 */
    uint16_t instruction_length = tt_read16(p);
    p += 2;
    uint8_t *instruction_ptr = p;
    p += instruction_length;  /* 指令指针移到坐标数据前 */

    /* 解析标志和坐标 */
    static uint8_t flags[TT_MAX_POINTS];
    static int16_t coords_x[TT_MAX_POINTS];
    static int16_t coords_y[TT_MAX_POINTS];

    int idx = 0;
    while (idx < total_points) {
        uint8_t flag = *p++;
        flags[idx] = flag;
        int repeat = 1;

        if (flag & TT_FLAG_REPEAT) {
            repeat = *p++;
            if (repeat == 0) repeat = 1;
        }

        for (int r = 0; r < repeat && idx < total_points; r++) {
            if (r > 0) flags[idx] = flag;
            idx++;
        }
    }

    /* 解析x坐标 */
    int16_t x = 0;
    for (int i = 0; i < total_points; i++) {
        if (flags[i] & TT_FLAG_X_SHORT) {
            uint8_t dx = *p++;
            if (flags[i] & TT_FLAG_X_SAME) x += dx;
            else x -= dx;
        } else {
            if (!(flags[i] & TT_FLAG_X_SAME)) {
                x += tt_read16s(p);
                p += 2;
            }
        }
        coords_x[i] = x;
    }

    /* 解析y坐标 */
    int16_t y = 0;
    for (int i = 0; i < total_points; i++) {
        if (flags[i] & TT_FLAG_Y_SHORT) {
            uint8_t dy = *p++;
            if (flags[i] & TT_FLAG_Y_SAME) y += dy;
            else y -= dy;
        } else {
            if (!(flags[i] & TT_FLAG_Y_SAME)) {
                y += tt_read16s(p);
                p += 2;
            }
        }
        coords_y[i] = y;
    }

    /* V7.30: 执行字形Hinting指令 */
    if (tt_hinting_enabled && instruction_length > 0) {
        tt_hint_execute(instruction_ptr, instruction_length,
                        coords_x, coords_y, flags, total_points,
                        num_contours, end_pts);
    }

    /* 计算字形尺寸 */
    int glyph_w = (int)((x_max - x_min) * tt_font.scale) + 1;
    int glyph_h = (int)((y_max - y_min) * tt_font.scale) + 1;
    if (glyph_w <= 0 || glyph_h <= 0) return -1;
    if (glyph_w > bitmap_w) glyph_w = bitmap_w;
    if (glyph_h > bitmap_h) glyph_h = bitmap_h;

    *offset_x = (int)(x_min * tt_font.scale);
    *offset_y = (int)(y_max * tt_font.scale);

    /* 清空位图 */
    memset(bitmap, 0, bitmap_w * bitmap_h);

    /* 对每个轮廓进行光栅化 */
    int start_pt = 0;
    for (int c = 0; c < num_contours; c++) {
        int end_pt = end_pts[c];
        int contour_pts = end_pt - start_pt + 1;

        if (contour_pts < 2) {
            start_pt = end_pt + 1;
            continue;
        }

        /* 将轮廓点转换为像素坐标并处理贝塞尔曲线 */
        static tt_point_t outline[TT_MAX_POINTS * 4];
        int outline_count = 0;

        for (int i = 0; i < contour_pts; i++) {
            int pt_idx = start_pt + i;
            /* V7.40: 减去x_min确保字形从位图左上角开始 */
            outline[outline_count].x = (coords_x[pt_idx] - x_min) * tt_font.scale;
            outline[outline_count].y = (y_max - coords_y[pt_idx]) * tt_font.scale;
            outline_count++;
        }

        /* V7.49: 重写轮廓展开逻辑，确保第一个点被正确添加
         * 问题：当第一个点是OFF_CURVE时，p0（最后一个点）未被添加，导致轮廓不完整
         * 修复：先找到第一个ON_CURVE点作为起点，从那里开始遍历
         */
        /* V7.50: 重写轮廓展开逻辑，确保第一个点被正确添加 */
        static tt_point_t flattened[TT_MAX_POINTS * 8];
        int flat_count = 0;

        /* 找到第一个ON_CURVE点作为起点 */
        int first_on = -1;
        for (int i = 0; i < outline_count; i++) {
            if (flags[start_pt + i] & TT_FLAG_ON_CURVE) {
                first_on = i;
                break;
            }
        }
        if (first_on < 0) first_on = 0;

        /* 添加起点 */
        flattened[flat_count++] = outline[first_on];

        /* 从起点的下一个点开始遍历，直到回到起点 */
        int idx = (first_on + 1) % outline_count;
        int iterations = 0;
        while (idx != first_on && iterations < outline_count * 2) {
            iterations++;
            if (flags[start_pt + idx] & TT_FLAG_ON_CURVE) {
                flattened[flat_count++] = outline[idx];
                idx = (idx + 1) % outline_count;
            } else {
                tt_point_t p1 = outline[idx];
                int next_on = (idx + 1) % outline_count;
                int search = 0;
                while (!(flags[start_pt + next_on] & TT_FLAG_ON_CURVE) && search < outline_count) {
                    next_on = (next_on + 1) % outline_count;
                    search++;
                }

                tt_point_t p0 = flattened[flat_count - 1];
                tt_point_t p2 = outline[next_on];

                if (!(flags[start_pt + next_on] & TT_FLAG_ON_CURVE)) {
                    p2.x = (outline[idx].x + outline[next_on].x) / 2;
                    p2.y = (outline[idx].y + outline[next_on].y) / 2;
                }

                tt_subdivide_quadratic(p0, p1, p2, flattened, &flat_count, TT_MAX_POINTS * 8);

                if (next_on == first_on) break;

                if (flags[start_pt + next_on] & TT_FLAG_ON_CURVE) {
                    idx = (next_on + 1) % outline_count;
                } else {
                    idx = next_on;
                }
            }
        }

        /* V7.53: 确保轮廓闭合 */
        if (flat_count > 0) {
            tt_point_t last = flattened[flat_count - 1];
            tt_point_t first = outline[first_on];
            if (last.x != first.x || last.y != first.y) {
                flattened[flat_count++] = first;
            }
        }

        /* 扫描线填充
         * V7.93: 统一使用bitmap_w作为行宽，避免调用者行宽不一致导致的像素错位
         * 之前使用glyph_w作为行宽，复合字形中用128访问导致渲染错误 */
        if (flat_count >= 3) {
            tt_scanline_fill(flattened, flat_count, bitmap, bitmap_w, bitmap_h, 0, 0);
        }

        start_pt = end_pt + 1;
    }

    return glyph_w;
}

/* ============================================================================
 * 初始化
 * ========================================================================== */

void tt_font_init(void)
{
    if (tt_initialized) return;

    memset(&tt_font, 0, sizeof(tt_font_t));
    tt_font.scale = 1.0f;
    tt_initialized = 1;

    serial_puts(0x3F8, "[TTF] TrueType font parser initialized\n");
    serial_puts(0x3F8, "[TTF] Note: load font data with tt_font_load()\n");
}

int tt_font_is_loaded(void)
{
    return tt_font.loaded;
}

/* ============================================================================
 * V7.30: TrueType指令解释器（Hinting）
 * 简化版虚拟机，支持最常用的字形提示指令
 * ========================================================================== */

#define TT_HINT_STACK_SIZE  64
#define TT_HINT_MAX_FUNCS   32
#define TT_HINT_MAX_STORE   32

/* TrueType虚拟机状态 */
typedef struct {
    int32_t  stack[TT_HINT_STACK_SIZE];
    int      sp;                    /* 栈指针 */
    int32_t  storage[TT_HINT_MAX_STORE]; /* 存储区 */
    uint8_t *ip;                    /* 指令指针 */
    uint8_t *end;                   /* 指令结束 */
    int      error;                 /* 错误状态 */
    int      loop;                  /* 循环计数器 */
    int      call_depth;            /* 调用深度 */

    /* 字形数据（执行指令时需要） */
    int16_t *coords_x;
    int16_t *coords_y;
    uint8_t *flags;
    int      num_points;
    int      num_contours;
    uint16_t *end_pts;

    /* 控制值表（简化） */
    int16_t  cvt[64];
    int      cvt_count;

    /* 函数定义 */
    uint8_t *funcs[TT_HINT_MAX_FUNCS];
    int      func_count;
} tt_hint_vm_t;

static tt_hint_vm_t tt_vm;

/* 栈操作 */
static inline void hint_push(int32_t val)
{
    if (tt_vm.sp < TT_HINT_STACK_SIZE) {
        tt_vm.stack[tt_vm.sp++] = val;
    } else {
        tt_vm.error = 1;  /* 栈溢出 */
    }
}

static inline int32_t hint_pop(void)
{
    if (tt_vm.sp > 0) {
        return tt_vm.stack[--tt_vm.sp];
    }
    tt_vm.error = 1;  /* 栈下溢 */
    return 0;
}

/* 执行SVTCA指令（设置向量为坐标轴） */
static void hint_svtca(uint8_t opcode)
{
    /* opcode: 0x00-0x05
     * 0x00: SVTCA[0] (X轴)
     * 0x01: SVTCA[1] (Y轴)
     * 简化：忽略向量设置，因为我们不做完整的网格拟合
     */
    (void)opcode;
}

/* 执行IUP指令（更新点） */
static void hint_iup(uint8_t opcode)
{
    /* opcode: 0x30 (IUP[0]), 0x31 (IUP[1])
     * 简化：不做点更新，保持原始坐标
     */
    (void)opcode;
}

/* 执行MDAP指令（移动直接绝对点） */
static void hint_mdap(uint8_t opcode)
{
    /* opcode: 0x2E (MDAP[0]), 0x2F (MDAP[1])
     * 弹出一个点索引，移动到网格位置
     * 简化：不做实际移动
     */
    hint_pop();  /* 弹出点索引 */
    (void)opcode;
}

/* 执行MIRP指令（移动间接相对点） */
static void hint_mirp(uint8_t opcode)
{
    /* opcode: 0xE0-0xE1 (MIRP[0]), 0xE2-0xE3 (MIRP[1])
     * 弹出两个点索引和一个距离
     * 简化：不做实际移动
     */
    hint_pop();  /* 距离 */
    hint_pop();  /* 点索引2 */
    hint_pop();  /* 点索引1 */
    (void)opcode;
}

/* 执行MIAP指令（移动间接绝对点） */
static void hint_miap(uint8_t opcode)
{
    /* opcode: 0x3E (MIAP[0]), 0x3F (MIAP[1])
     * 弹出一个点索引和一个CVT索引
     * 简化：不做实际移动
     */
    hint_pop();  /* CVT索引 */
    hint_pop();  /* 点索引 */
    (void)opcode;
}

/* 执行PUSHB指令（压入字节） */
static void hint_pushb(uint8_t count)
{
    for (int i = 0; i < count && tt_vm.ip < tt_vm.end; i++) {
        hint_push((int32_t)(int8_t)*tt_vm.ip++);
    }
}

/* 执行PUSHW指令（压入字） */
static void hint_pushw(uint8_t count)
{
    for (int i = 0; i < count && tt_vm.ip + 1 < tt_vm.end; i++) {
        int16_t val = (int16_t)((*tt_vm.ip << 8) | *(tt_vm.ip + 1));
        tt_vm.ip += 2;
        hint_push((int32_t)val);
    }
}

/* 执行TrueType指令流 */
int tt_hint_execute(uint8_t *instructions, uint16_t length,
                     int16_t *coords_x, int16_t *coords_y,
                     uint8_t *flags, int num_points,
                     int num_contours, uint16_t *end_pts)
{
    if (!instructions || length == 0) return 0;

    /* 初始化虚拟机 */
    memset(&tt_vm, 0, sizeof(tt_hint_vm_t));
    tt_vm.ip = instructions;
    tt_vm.end = instructions + length;
    tt_vm.coords_x = coords_x;
    tt_vm.coords_y = coords_y;
    tt_vm.flags = flags;
    tt_vm.num_points = num_points;
    tt_vm.num_contours = num_contours;
    tt_vm.end_pts = end_pts;

    /* 指令执行循环 */
    int max_iterations = length * 4;  /* 防止无限循环 */
    int iterations = 0;

    while (tt_vm.ip < tt_vm.end && !tt_vm.error && iterations < max_iterations) {
        iterations++;
        uint8_t opcode = *tt_vm.ip++;

        switch (opcode) {
            /* SVTCA系列 */
            case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:
                hint_svtca(opcode);
                break;

            /* 控制流 */
            case 0x1B:  /* IF */
            case 0x1C:  /* IF...ELSE...EIF的简化处理 */
            case 0x1D:  /* ELSE */
            case 0x1E:  /* EIF */
                /* 简化：不做条件分支，直接跳过 */
                break;

            /* 函数定义 */
            case 0x2C:  /* FDEF */
                /* 简化：跳过函数定义 */
                while (tt_vm.ip < tt_vm.end && *tt_vm.ip != 0x2D) {
                    tt_vm.ip++;
                }
                if (tt_vm.ip < tt_vm.end) tt_vm.ip++;  /* 跳过ENDF */
                break;
            case 0x2D:  /* ENDF */
                break;

            /* 点操作 */
            case 0x2E: case 0x2F:  /* MDAP */
                hint_mdap(opcode);
                break;
            case 0x30: case 0x31:  /* IUP */
                hint_iup(opcode);
                break;
            case 0x3E: case 0x3F:  /* MIAP */
                hint_miap(opcode);
                break;

            /* MIRP系列 */
            case 0xE0: case 0xE1: case 0xE2: case 0xE3:
                hint_mirp(opcode);
                break;

            /* 压入数据 */
            case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4:
            case 0xB5: case 0xB6: case 0xB7: case 0xB8: case 0xB9:
            case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE:
            case 0xBF:  /* PUSHB[0-15] */
                hint_pushb(opcode - 0xB0 + 1);
                break;
            case 0x40:  /* PUSHB[ ] */
                if (tt_vm.ip < tt_vm.end) {
                    uint8_t count = *tt_vm.ip++;
                    hint_pushb(count);
                }
                break;

            case 0x41:  /* PUSHW[ ] */
                if (tt_vm.ip < tt_vm.end) {
                    uint8_t count = *tt_vm.ip++;
                    hint_pushw(count);
                }
                break;

            /* 算术运算 */
            case 0x60:  /* ADD */
                hint_push(hint_pop() + hint_pop());
                break;
            case 0x61:  /* SUB */
                { int32_t b = hint_pop(); hint_push(hint_pop() - b); }
                break;
            case 0x62:  /* MUL */
                hint_push(hint_pop() * hint_pop());
                break;
            case 0x63:  /* DIV */
                { int32_t b = hint_pop(); int32_t a = hint_pop();
                  hint_push(b != 0 ? a / b : 0); }
                break;
            case 0x64:  /* ABS */
                { int32_t a = hint_pop(); hint_push(a < 0 ? -a : a); }
                break;
            case 0x65:  /* NEG */
                hint_push(-hint_pop());
                break;
            case 0x66:  /* FLOOR */
                hint_push(hint_pop());  /* 整数已经是floor */
                break;
            case 0x67:  /* CEILING */
                hint_push(hint_pop());  /* 整数已经是ceiling */
                break;

            /* 存储区操作 */
            case 0x42:  /* RS */
                { int32_t idx = hint_pop();
                  if (idx >= 0 && idx < TT_HINT_MAX_STORE)
                      hint_push(tt_vm.storage[idx]); }
                break;
            case 0x43:  /* WS */
                { int32_t val = hint_pop(); int32_t idx = hint_pop();
                  if (idx >= 0 && idx < TT_HINT_MAX_STORE)
                      tt_vm.storage[idx] = val; }
                break;

            /* CVT操作 */
            case 0x8E:  /* RCVT */
                hint_pop();  /* 弹出CVT索引，返回0 */
                hint_push(0);
                break;

            /* 其他常用指令（简化处理） */
            case 0x7C:  /* ROUND */
            case 0x7D:  /* ROUND */
            case 0x7E:  /* ROUND */
            case 0x7F:  /* ROUND */
                hint_push(hint_pop());  /* 整数不需要round */
                break;

            case 0x89:  /* MIN */
                { int32_t a = hint_pop(); int32_t b = hint_pop();
                  hint_push(a < b ? a : b); }
                break;
            case 0x8A:  /* MAX */
                { int32_t a = hint_pop(); int32_t b = hint_pop();
                  hint_push(a > b ? a : b); }
                break;

            case 0x59:  /* INSTCTRL */
                tt_vm.ip += 2;  /* 跳过两个参数 */
                break;

            case 0x1A:  /* DEBUG */
                break;

            case 0x1F:  /* LOOPCALL */
                { int32_t count = hint_pop();
                  int32_t func = hint_pop();
                  (void)func; (void)count; }  /* 简化：不执行循环 */
                break;

            case 0x2A:  /* CALL */
                hint_pop();  /* 弹出函数号，简化：不调用 */
                break;

            case 0x8B:  /* SCANCTRL */
                hint_pop();
                break;

            case 0x8C:  /* SCANTYPE */
                hint_pop();
                break;

            case 0x85:  /* SDPVTL */
            case 0x86:  /* SDPVTL */
                hint_pop(); hint_pop(); hint_pop();
                break;

            /* 未知指令：跳过 */
            default:
                /* 大多数未知指令不需要操作数，直接继续 */
                break;
        }
    }

    if (tt_vm.error) {
        return -1;
    }

    return 0;
}

/* 启用/禁用Hinting */
void tt_set_hinting(int enable)
{
    tt_hinting_enabled = enable ? 1 : 0;
    serial_puts(0x3F8, "[TTF] hinting ");
    serial_puts(0x3F8, tt_hinting_enabled ? "enabled\n" : "disabled\n");
}

/* 获取Hinting状态 */
int tt_get_hinting(void)
{
    return tt_hinting_enabled;
}
