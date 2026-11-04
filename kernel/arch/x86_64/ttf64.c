/* ============================================================================
 * 赫普OS 64位 - TrueType字体解析器（简化版）
 * 支持：TrueType文件解析、简单字形光栅化、ASCII字符渲染
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

extern void vga_puts(const char *s);
extern void vga_put_hex(uint64_t val);

/* TrueType表标签 */
#define TT_TAG_HEAD  0x68656164
#define TT_TAG_HHEA  0x68686561
#define TT_TAG_MAXP  0x6D617870
#define TT_TAG_CMAP  0x636D6170
#define TT_TAG_GLYF  0x676C7966
#define TT_TAG_LOCA  0x6C6F6361
#define TT_TAG_HMTX  0x686D7478

/* 大端序读取 */
static inline uint16_t tt_read16(const uint8_t *p) {
    return (p[0] << 8) | p[1];
}
static inline uint32_t tt_read32(const uint8_t *p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}
static inline int16_t tt_read16s(const uint8_t *p) {
    return (int16_t)((p[0] << 8) | p[1]);
}

/* 表记录 */
typedef struct {
    uint32_t tag;
    uint32_t offset;
    uint32_t length;
} tt_table_t;

/* TrueType字体上下文 */
typedef struct {
    const uint8_t *data;
    uint32_t data_size;
    tt_table_t tables[32];
    int num_tables;

    /* head表 */
    uint16_t units_per_em;
    int16_t x_min, y_min, x_max, y_max;
    int16_t index_to_loc_format;

    /* hhea表 */
    int16_t ascent;
    int16_t descent;
    int16_t line_gap;
    uint16_t num_hmtx;

    /* maxp表 */
    uint16_t num_glyphs;

    /* 表指针 */
    const uint8_t *head;
    const uint8_t *hhea;
    const uint8_t *maxp;
    const uint8_t *cmap;
    const uint8_t *glyf;
    const uint8_t *loca;
    const uint8_t *hmtx;

    int loaded;
} ttf_font_t;

static ttf_font_t g_font;
static ttf_font_t g_cjk_font;  /* 中文字体 */

/* 查找表 */
static const tt_table_t *ttf_find_table(ttf_font_t *f, uint32_t tag)
{
    for (int i = 0; i < f->num_tables; i++) {
        if (f->tables[i].tag == tag) return &f->tables[i];
    }
    return NULL;
}

/* 解析cmap表，获取字符到字形的映射（format 4或format 0） */
static uint16_t ttf_char_to_glyph(ttf_font_t *f, uint32_t ch)
{
    if (!f->cmap) return 0;
    const uint8_t *cmap = f->cmap;

    uint16_t num_subtables = tt_read16(cmap + 2);
    for (int i = 0; i < num_subtables; i++) {
        const uint8_t *sub = cmap + 4 + i * 8;
        uint16_t platform = tt_read16(sub);
        uint16_t encoding = tt_read16(sub + 2);
        uint32_t offset = tt_read32(sub + 4);
        const uint8_t *table = cmap + offset;
        uint16_t format = tt_read16(table);

        /* 只处理Unicode映射 (platform 0,3) */
        if (platform != 0 && platform != 3) continue;

        if (format == 4) {
            /* Format 4: 分段映射 */
            uint16_t seg_count = tt_read16(table + 6) / 2;
            const uint8_t *end_codes = table + 14;
            const uint8_t *start_codes = end_codes + seg_count * 2 + 2;
            const uint8_t *id_deltas = start_codes + seg_count * 2;
            const uint8_t *id_range_offsets = id_deltas + seg_count * 2;

            for (int s = 0; s < seg_count; s++) {
                uint16_t end = tt_read16(end_codes + s * 2);
                uint16_t start = tt_read16(start_codes + s * 2);
                if (ch >= start && ch <= end) {
                    uint16_t delta = tt_read16(id_deltas + s * 2);
                    uint16_t range_off = tt_read16(id_range_offsets + s * 2);
                    if (range_off == 0) {
                        return (ch + delta) & 0xFFFF;
                    } else {
                        const uint8_t *glyph_ptr = id_range_offsets + s * 2 +
                            range_off + (ch - start) * 2;
                        uint16_t glyph = tt_read16(glyph_ptr);
                        if (glyph) return (glyph + delta) & 0xFFFF;
                        return 0;
                    }
                }
            }
            return 0;
        } else if (format == 0) {
            /* Format 0: 字节映射 */
            if (ch < 256) {
                return table[6 + ch];
            }
            return 0;
        }
    }
    return 0;
}

/* 获取字形偏移 */
static uint32_t ttf_get_glyph_offset(ttf_font_t *f, uint16_t glyph)
{
    if (!f->loca || !f->glyf) return 0;
    if (glyph >= f->num_glyphs) return 0;

    uint32_t offset;
    if (f->index_to_loc_format == 0) {
        /* short format */
        offset = tt_read16(f->loca + glyph * 2) * 2;
    } else {
        /* long format */
        offset = tt_read32(f->loca + glyph * 4);
    }
    return offset;
}

/* 获取字形宽度 */
static uint16_t ttf_get_glyph_width(ttf_font_t *f, uint16_t glyph)
{
    if (!f->hmtx) return f->units_per_em / 2;
    if (glyph < f->num_hmtx) {
        return tt_read16(f->hmtx + glyph * 4);
    }
    /* 超出范围使用最后一个宽度 */
    return tt_read16(f->hmtx + (f->num_hmtx - 1) * 4);
}

/* 简单字形光栅化（扫描线填充） */
static int ttf_render_simple_glyph(ttf_font_t *f, const uint8_t *glyph_data,
                                    int pixel_size, uint8_t *bitmap,
                                    int *out_w, int *out_h, int *out_x, int *out_y)
{
    int16_t num_contours = tt_read16s(glyph_data);
    if (num_contours <= 0 || num_contours > 64) return -1;

    int16_t x_min = tt_read16s(glyph_data + 2);
    int16_t y_min = tt_read16s(glyph_data + 4);
    int16_t x_max = tt_read16s(glyph_data + 6);
    int16_t y_max = tt_read16s(glyph_data + 8);

    /* 计算像素尺寸 */
    float scale = (float)pixel_size / f->units_per_em;
    int gw = (int)((x_max - x_min) * scale) + 1;
    int gh = (int)((y_max - y_min) * scale) + 1;
    if (gw <= 0 || gh <= 0 || gw > 256 || gh > 256) return -1;

    *out_w = gw;
    *out_h = gh;
    *out_x = (int)(x_min * scale);
    *out_y = (int)(y_max * scale);

    /* 清零位图 */
    for (int i = 0; i < gw * gh; i++) bitmap[i] = 0;

    /* 读取轮廓端点 */
    const uint8_t *end_pts = glyph_data + 10;
    int total_points = tt_read16(end_pts + (num_contours - 1) * 2) + 1;
    if (total_points <= 0 || total_points > 512) return -1;

    /* 跳过指令区域：endPts[] + instructionLength(2) + instructions[] */
    uint16_t instruction_length = tt_read16(end_pts + num_contours * 2);
    if (instruction_length > 2048) return -1;
    const uint8_t *flags_ptr = end_pts + num_contours * 2 + 2 + instruction_length;

    /* 使用静态数组避免栈溢出（512*5=2560字节） */
    static uint8_t flags[512];
    static int16_t xs[512], ys[512];

    /* 读取flags（带边界保护） */
    int fp = 0;
    for (int i = 0; i < total_points; i++) {
        if (fp > 4096) return -1;  /* 安全上限 */
        flags[i] = flags_ptr[fp++];
        if (flags[i] & 0x08) {
            if (fp > 4096) return -1;
            int repeat = flags_ptr[fp++];
            if (repeat > total_points) repeat = total_points;
            for (int j = 1; j <= repeat && i + j < total_points; j++) {
                flags[i + j] = flags[i];
            }
            i += repeat;
        }
    }

    /* 读取x坐标（带边界保护） */
    int xp = 0;
    const uint8_t *x_ptr = flags_ptr + fp;
    for (int i = 0; i < total_points; i++) {
        if (flags[i] & 0x02) {
            if (xp > 8192) return -1;
            uint8_t v = x_ptr[xp++];
            if (flags[i] & 0x10) xs[i] = (i > 0 ? xs[i-1] : 0) + v;
            else xs[i] = (i > 0 ? xs[i-1] : 0) - v;
        } else if (!(flags[i] & 0x10)) {
            if (xp > 8192) return -1;
            int16_t delta = tt_read16s(x_ptr + xp); xp += 2;
            xs[i] = (i > 0 ? xs[i-1] : 0) + delta;
        } else {
            xs[i] = (i > 0 ? xs[i-1] : 0);
        }
    }

    /* 读取y坐标（带边界保护） */
    int yp = 0;
    const uint8_t *y_ptr = x_ptr + xp;
    for (int i = 0; i < total_points; i++) {
        if (flags[i] & 0x04) {
            if (yp > 8192) return -1;
            uint8_t v = y_ptr[yp++];
            if (flags[i] & 0x20) ys[i] = (i > 0 ? ys[i-1] : 0) + v;
            else ys[i] = (i > 0 ? ys[i-1] : 0) - v;
        } else if (!(flags[i] & 0x20)) {
            if (yp > 8192) return -1;
            int16_t delta = tt_read16s(y_ptr + yp); yp += 2;
            ys[i] = (i > 0 ? ys[i-1] : 0) + delta;
        } else {
            ys[i] = (i > 0 ? ys[i-1] : 0);
        }
    }

    /* 扫描线填充（2x2超采样抗锯齿，带除零保护） */
    for (int py = 0; py < gh; py++) {
        for (int px = 0; px < gw; px++) {
            int coverage = 0;
            /* 4个子采样点 */
            float sub_offsets[4][2] = {
                {0.25f, 0.25f}, {0.75f, 0.25f},
                {0.25f, 0.75f}, {0.75f, 0.75f}
            };
            for (int s = 0; s < 4; s++) {
                float fy = y_max - (py + sub_offsets[s][1]) / scale;
                float fx = x_min + (px + sub_offsets[s][0]) / scale;
                int inside = 0;
                int contour_start = 0;
                for (int c = 0; c < num_contours; c++) {
                    int contour_end = tt_read16(end_pts + c * 2);
                    if (contour_end >= total_points) contour_end = total_points - 1;
                    for (int i = contour_start; i <= contour_end; i++) {
                        int j = (i == contour_end) ? contour_start : i + 1;
                        float yi = ys[i], yj = ys[j];
                        float xi = xs[i], xj = xs[j];
                        float dy = yj - yi;
                        if (((yi > fy) != (yj > fy)) && (dy > 0.001f || dy < -0.001f) &&
                            (fx < (xj - xi) * (fy - yi) / dy + xi)) {
                            inside = !inside;
                        }
                    }
                    contour_start = contour_end + 1;
                }
                if (inside) coverage++;
            }
            bitmap[py * gw + px] = (uint8_t)(coverage * 255 / 4);
        }
    }

    return 0;
}

/* 初始化TrueType字体 */
int ttf64_init(const uint8_t *data, uint32_t size)
{
    if (!data || size < 12) return -1;

    g_font.data = data;
    g_font.data_size = size;
    g_font.loaded = 0;

    /* 检查sfnt版本 */
    uint32_t sfnt = tt_read32(data);
    if (sfnt != 0x00010000 && sfnt != 0x74727565) {
        vga_puts("[TTF64] Not a TrueType font (sfnt=");
        vga_put_hex(sfnt);
        vga_puts(")\n");
        return -1;
    }

    uint16_t num_tables = tt_read16(data + 4);
    if (num_tables > 32) num_tables = 32;
    g_font.num_tables = num_tables;

    /* 解析表目录 */
    const uint8_t *dir = data + 12;
    for (int i = 0; i < num_tables; i++) {
        g_font.tables[i].tag = tt_read32(dir + i * 16);
        g_font.tables[i].offset = tt_read32(dir + i * 16 + 8);
        g_font.tables[i].length = tt_read32(dir + i * 16 + 12);
    }

    /* 获取表指针 */
    const tt_table_t *t;
    #define GET_TABLE(tag, ptr) do { \
        t = ttf_find_table(&g_font, tag); \
        if (t) ptr = data + t->offset; else ptr = NULL; \
    } while(0)

    GET_TABLE(TT_TAG_HEAD, g_font.head);
    GET_TABLE(TT_TAG_HHEA, g_font.hhea);
    GET_TABLE(TT_TAG_MAXP, g_font.maxp);
    GET_TABLE(TT_TAG_CMAP, g_font.cmap);
    GET_TABLE(TT_TAG_GLYF, g_font.glyf);
    GET_TABLE(TT_TAG_LOCA, g_font.loca);
    GET_TABLE(TT_TAG_HMTX, g_font.hmtx);

    if (!g_font.head || !g_font.maxp || !g_font.cmap || !g_font.glyf || !g_font.loca) {
        vga_puts("[TTF64] Missing required tables\n");
        return -1;
    }

    /* 解析head表 */
    g_font.units_per_em = tt_read16(g_font.head + 18);
    g_font.x_min = tt_read16s(g_font.head + 36);
    g_font.y_min = tt_read16s(g_font.head + 38);
    g_font.x_max = tt_read16s(g_font.head + 40);
    g_font.y_max = tt_read16s(g_font.head + 42);
    g_font.index_to_loc_format = tt_read16s(g_font.head + 50);

    /* 解析hhea表 */
    if (g_font.hhea) {
        g_font.ascent = tt_read16s(g_font.hhea + 4);
        g_font.descent = tt_read16s(g_font.hhea + 6);
        g_font.line_gap = tt_read16s(g_font.hhea + 8);
        g_font.num_hmtx = tt_read16(g_font.hhea + 34);
    }

    /* 解析maxp表 */
    g_font.num_glyphs = tt_read16(g_font.maxp + 4);

    g_font.loaded = 1;

    vga_puts("[TTF64] Font loaded: ");
    vga_put_hex(g_font.num_glyphs);
    vga_puts(" glyphs, ");
    vga_put_hex(g_font.units_per_em);
    vga_puts(" units/em\n");

    return 0;
}

/* 初始化中文字体 */
int ttf64_init_cjk(const uint8_t *data, uint32_t size)
{
    if (!data || size < 12) return -1;

    g_cjk_font.data = data;
    g_cjk_font.data_size = size;
    g_cjk_font.loaded = 0;

    uint32_t sfnt = tt_read32(data);
    if (sfnt != 0x00010000 && sfnt != 0x74727565) return -1;

    uint16_t num_tables = tt_read16(data + 4);
    if (num_tables > 32) num_tables = 32;
    g_cjk_font.num_tables = num_tables;

    const uint8_t *dir = data + 12;
    for (int i = 0; i < num_tables; i++) {
        g_cjk_font.tables[i].tag = tt_read32(dir + i * 16);
        g_cjk_font.tables[i].offset = tt_read32(dir + i * 16 + 8);
        g_cjk_font.tables[i].length = tt_read32(dir + i * 16 + 12);
    }

    #define GET_TABLE_CJK(tag, field) do { \
        const tt_table_t *t = ttf_find_table(&g_cjk_font, tag); \
        g_cjk_font.field = t ? data + t->offset : 0; \
    } while(0)

    GET_TABLE_CJK(TT_TAG_HEAD, head);
    GET_TABLE_CJK(TT_TAG_HHEA, hhea);
    GET_TABLE_CJK(TT_TAG_MAXP, maxp);
    GET_TABLE_CJK(TT_TAG_CMAP, cmap);
    GET_TABLE_CJK(TT_TAG_GLYF, glyf);
    GET_TABLE_CJK(TT_TAG_LOCA, loca);
    GET_TABLE_CJK(TT_TAG_HMTX, hmtx);
    #undef GET_TABLE_CJK

    if (!g_cjk_font.head || !g_cjk_font.maxp || !g_cjk_font.cmap ||
        !g_cjk_font.glyf || !g_cjk_font.loca) return -1;

    g_cjk_font.units_per_em = tt_read16(g_cjk_font.head + 18);
    g_cjk_font.x_min = tt_read16s(g_cjk_font.head + 36);
    g_cjk_font.y_min = tt_read16s(g_cjk_font.head + 38);
    g_cjk_font.x_max = tt_read16s(g_cjk_font.head + 40);
    g_cjk_font.y_max = tt_read16s(g_cjk_font.head + 42);
    g_cjk_font.index_to_loc_format = tt_read16s(g_cjk_font.head + 50);

    if (g_cjk_font.hhea) {
        g_cjk_font.ascent = tt_read16s(g_cjk_font.hhea + 4);
        g_cjk_font.descent = tt_read16s(g_cjk_font.hhea + 6);
        g_cjk_font.line_gap = tt_read16s(g_cjk_font.hhea + 8);
        g_cjk_font.num_hmtx = tt_read16(g_cjk_font.hhea + 34);
    }
    g_cjk_font.num_glyphs = tt_read16(g_cjk_font.maxp + 4);
    g_cjk_font.loaded = 1;

    vga_puts("[TTF64] CJK font loaded: ");
    vga_put_hex(g_cjk_font.num_glyphs);
    vga_puts(" glyphs\n");
    return 0;
}

/* 渲染中文字符到位图 */
int ttf64_render_char_cjk(uint32_t ch, int pixel_size, uint8_t *bitmap,
                          int *w, int *h, int *x, int *y)
{
    if (!g_cjk_font.loaded) return -1;

    uint16_t glyph = ttf_char_to_glyph(&g_cjk_font, ch);
    if (glyph == 0) return -1;

    uint32_t offset = ttf_get_glyph_offset(&g_cjk_font, glyph);
    if (offset == 0) {
        *w = ttf_get_glyph_width(&g_cjk_font, glyph) * pixel_size / g_cjk_font.units_per_em;
        *h = 0; *x = 0; *y = 0;
        return 0;
    }

    const uint8_t *glyph_data = g_cjk_font.glyf + offset;
    int16_t num_contours = tt_read16s(glyph_data);
    if (num_contours > 0) {
        return ttf_render_simple_glyph(&g_cjk_font, glyph_data, pixel_size,
                                       bitmap, w, h, x, y);
    }
    *w = ttf_get_glyph_width(&g_cjk_font, glyph) * pixel_size / g_cjk_font.units_per_em;
    *h = 0;
    return 0;
}

int ttf64_cjk_is_loaded(void) { return g_cjk_font.loaded; }
int ttf64_render_char(uint32_t ch, int pixel_size, uint8_t *bitmap,
                       int *w, int *h, int *x, int *y)
{
    if (!g_font.loaded) return -1;

    uint16_t glyph = ttf_char_to_glyph(&g_font, ch);
    if (glyph == 0) {
        *w = pixel_size / 2;
        *h = pixel_size;
        *x = 0;
        *y = 0;
        return 0;
    }

    uint32_t offset = ttf_get_glyph_offset(&g_font, glyph);
    if (offset == 0) {
        *w = ttf_get_glyph_width(&g_font, glyph) * pixel_size / g_font.units_per_em;
        *h = pixel_size;
        return 0;
    }

    const uint8_t *glyph_data = g_font.glyf + offset;
    int16_t num_contours = tt_read16s(glyph_data);

    if (num_contours > 0) {
        /* 简单字形 */
        return ttf_render_simple_glyph(&g_font, glyph_data, pixel_size,
                                        bitmap, w, h, x, y);
    } else if (num_contours < 0) {
        /* 复合字形：简化处理，返回空格宽度 */
        *w = ttf_get_glyph_width(&g_font, glyph) * pixel_size / g_font.units_per_em;
        *h = pixel_size;
        return 0;
    }

    *w = pixel_size / 2;
    *h = pixel_size;
    return 0;
}

/* 获取字体度量 */
void ttf64_get_metrics(int pixel_size, int *ascent, int *descent, int *line_gap)
{
    if (!g_font.loaded) {
        *ascent = pixel_size;
        *descent = 0;
        *line_gap = 0;
        return;
    }
    float scale = (float)pixel_size / g_font.units_per_em;
    *ascent = (int)(g_font.ascent * scale);
    *descent = (int)(-g_font.descent * scale);
    *line_gap = (int)(g_font.line_gap * scale);
}

int ttf64_is_loaded(void) { return g_font.loaded; }

/* V0.13.8 P7: 获取字符水平 advance（像素）— 修复字母间距不均匀/窄字粘连
 * 使用 hmtx 表的真实 advanceWidth（含左右 side bearing），替代"位图宽+1"的粗略值 */
int ttf64_get_char_advance(uint32_t ch, int pixel_size)
{
    if (!g_font.loaded) return (pixel_size * 6) / 10 + 1;
    uint16_t glyph = ttf_char_to_glyph(&g_font, ch);
    uint16_t adv = ttf_get_glyph_width(&g_font, glyph);
    /* 四舍五入 (避免向下取整把窄字 advance 截小导致粘连) */
    int px = (adv * pixel_size + g_font.units_per_em / 2) / g_font.units_per_em;
    if (px < 2) px = 2;
    return px;
}

int ttf64_get_cjk_advance(uint32_t ch, int pixel_size)
{
    (void)ch;
    if (!g_cjk_font.loaded) return pixel_size + 1;
    uint16_t glyph = ttf_char_to_glyph(&g_cjk_font, ch);
    uint16_t adv = ttf_get_glyph_width(&g_cjk_font, glyph);
    int px = (adv * pixel_size + g_cjk_font.units_per_em / 2) / g_cjk_font.units_per_em;
    if (px < pixel_size) px = pixel_size;  /* CJK 至少全宽 */
    return px + 1;
}
