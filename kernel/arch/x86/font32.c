/* ============================================================================
 * 32 位桌面字体: 点阵 + 按字号缩放 + 抗锯齿
 *
 * 为什么不是 TrueType:
 *   点阵字体的价值是"零依赖的确定性" —— 不碰文件系统、不碰 FPU、不碰
 *   字形缓存, 启动早期和回退文本模式时它是唯一能画字的字体。64 位桌面
 *   的结构本身就是 "TTF 已加载 -> TrueType, 否则 -> 点阵", 点阵是兜底层。
 *   32 位原先的问题不是"用了点阵", 而是只保留了最原始那一档:
 *   scale 固定 1、无抗锯齿、size 参数被 (void) 丢掉。
 *
 * 本文件把 32 位拉回和 64 位同一档的兜底:
 *   scale = size/16 (clamp 1..4), scale>=2 且 2 倍缩放时做 2x2 超采样抗锯齿。
 *   算法与 64 位 arch/x86_64/font64.c 的 font_render_glyph/aa_calc_alpha 同构,
 *   但函数名加 f32_ 前缀, 避免与 kernel/font/ 里的 font_render_glyph 冲突
 *   (重名会让编译器报 conflicting types, 本轮已经踩过一次)。
 * ==========================================================================*/
#include <stdint.h>
#include <stddef.h>
#include "font8x16.h"

extern uint16_t gfx_width;
extern uint16_t gfx_height;
extern int gfx_pitch;
extern int gfx_bpp;
extern uint8_t *framebuffer;
extern void gfx_putpixel_rgb(int x, int y, uint32_t rgb);

#define F32_MAX_SCALE 4

/* 取字形某一位。hepu_font8x16 是 32 位那套一维表(ch*16 + row),
 * 每行高位在左(与 64 位 font8x16[c][r] 的位序一致)。 */
static inline int f32_bit(uint32_t cp, int col, int row)
{
    if (cp >= 128 || col < 0 || col >= 8 || row < 0 || row >= 16) return 0;
    const uint8_t *g = &hepu_font8x16[cp * 16 + row];
    return (*g >> (7 - col)) & 1;
}

/* 2x2 超采样覆盖率。只在 scale==2 时用(与 64 位一致: 更大倍数做了也救不回来) */
static uint8_t f32_alpha(uint32_t cp, int col, int row)
{
    int sc = col >> 1, sr = row >> 1;
    int count = 0;
    for (int dy = 0; dy < 2; dy++)
        for (int dx = 0; dx < 2; dx++)
            count += f32_bit(cp, sc + dx, sr + dy);
    return (uint8_t)((count * 255) / 4);
}

static void f32_draw_cp(int x, int y, uint32_t cp, uint32_t color, int scale)
{
    int aa = (scale == 2);   /* 与 64 位一致: 只有 2 倍走抗锯齿 */
    int gw = 8 * scale, gh = 16 * scale;

    for (int row = 0; row < gh; row++) {
        int py = y + row;
        if (py < 0 || py >= (int)gfx_height) continue;
        for (int col = 0; col < gw; col++) {
            int px = x + col;
            if (px < 0 || px >= (int)gfx_width) continue;

            uint8_t a = aa ? f32_alpha(cp, col, row)
                           : (f32_bit(cp, col / scale, row / scale) ? 255 : 0);
            if (a == 0) continue;

            if (a == 255) { gfx_putpixel_rgb(px, py, color); continue; }

            /* 半透明像素: 与目标缓冲做 alpha 混合。
             * 32bpp 下每像素 4 字节, 行跨距用 gfx_pitch(已含 bpp)。 */
            if (gfx_bpp != 32 || !framebuffer) continue;
            int pitch_px = gfx_pitch / 4;
            uint32_t *buf = (uint32_t *)framebuffer;
            uint32_t bg = buf[py * pitch_px + px];
            int r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
            int br = (bg >> 16) & 0xFF, bg_ = (bg >> 8) & 0xFF, bb = bg & 0xFF;
            int nr = (r * a + br * (255 - a)) / 255;
            int ng = (g * a + bg_ * (255 - a)) / 255;
            int nb = (b * a + bb * (255 - a)) / 255;
            gfx_putpixel_rgb(px, py, ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | nb);
        }
    }
}


/* ============================================================================
 * TrueType 路径
 *
 * 结构对齐 64 位: TTF 已加载 -> 走 TTF; 否则 -> 上面的点阵兜底。
 * 64 位用的是 42 KB 的 DejaVuSans-Subset(kernel/arch/x86_64/font_data.c),
 * 不是目录里那份 757 KB 的完整版 —— 后者塞不进内核。32 位沿用同一份数据
 * (font_data32.c 就是它的拷贝), 这样两边字形来源完全一致。
 *
 * 光栅化在 ttf32.c(ttf64.c 的拷贝): 输出平台无关的 alpha 位图, 只有两处
 * extern(vga_puts/vga_put_hex)用于溢出告警。它内部用 float 做贝塞尔细分,
 * 所以 kernel_init 里补了 fninit + 清 CR0.EM/TS。
 * ==========================================================================*/

extern int  ttf64_init(const uint8_t *data, uint32_t size);
extern int  ttf64_render_char(uint32_t ch, int pixel_size, uint8_t *bitmap,
                              int *w, int *h, int *x, int *y);
extern int  ttf64_get_char_advance(uint32_t ch, int pixel_size);
extern void ttf64_get_metrics(int pixel_size, int *ascent, int *descent, int *line_gap);

static int g_f32_ttf_loaded = 0;
static uint16_t g_f32_ttf_size = 16;
static int g_f32_ttf_ascent = 12;

/* 单字形位图暂存。最大字号 64px 也是 64x64, 256x256 足够且不必动态分配。 */
static uint8_t g_f32_ttf_bm[256 * 256];

int f32_ttf_init(const uint8_t *data, uint32_t size)
{
    if (ttf64_init(data, size) != 0) return -1;
    g_f32_ttf_loaded = 1;
    return 0;
}

int f32_ttf_is_loaded(void) { return g_f32_ttf_loaded; }

/* 绘制单个码位, 返回 advance(像素, 含左右侧承)。
 * 对比度增强与 64 位 gfx64_put_ttf 一致: 核心笔画(>=192)加实、边缘(<=64)弱化、
 * 中段拉伸 —— 消除小字号"发灰、笔画淡"的粗糙感。 */
static int f32_put_ttf(int x, int y, uint32_t cp, uint32_t color)
{
    int w = 0, h = 0, bx = 0, by = 0;
    if (ttf64_render_char(cp, g_f32_ttf_size, g_f32_ttf_bm, &w, &h, &bx, &by) != 0)
        return g_f32_ttf_size / 2;
    if (w <= 0 || h <= 0 || w > 256 || h > 256) return g_f32_ttf_size / 2;

    int base_y = y + g_f32_ttf_ascent;
    int base_x = x + bx;

    int r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    int pitch_px = gfx_pitch / 4;
    uint32_t *buf = (uint32_t *)framebuffer;

    /* 垂直偏移必须是 baseline - by + row。by 是字形的 yMin(相对基线,
     * 通常为负), 漏掉 -by 会让整行字整体上移, 压到上一行 —— 上一版
     * 就是因为这个把 Settings 的行间挤成了重叠。 */
    for (int row = 0; row < h; row++) {
        int py = base_y - by + row;
        if (py < 0 || py >= (int)gfx_height) continue;
        for (int col = 0; col < w; col++) {
            int px = base_x + col;
            if (px < 0 || px >= (int)gfx_width) continue;

            uint8_t a = g_f32_ttf_bm[row * w + col];
            if (a == 0) continue;
            /* 对比度增强(与 64 位同式) */
            if (a <= 64) a = (uint8_t)(a * 3 / 4);
            else if (a >= 192) a = 255;
            else a = (uint8_t)(128 + (a - 64) * 127 / 128);

            if (a == 255) { gfx_putpixel_rgb(px, py, color); continue; }
            if (gfx_bpp != 32 || !framebuffer) continue;

            uint32_t bg = buf[py * pitch_px + px];
            int br = (bg >> 16) & 0xFF, bg_ = (bg >> 8) & 0xFF, bb = bg & 0xFF;
            int nr = (r * a + br * (255 - a)) / 255;
            int ng = (g * a + bg_ * (255 - a)) / 255;
            int nb = (b * a + bb * (255 - a)) / 255;
            gfx_putpixel_rgb(px, py, ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | nb);
        }
    }

    int adv = ttf64_get_char_advance(cp, g_f32_ttf_size);
    /* 与渲染定位同一取整规则: advance 至少要覆盖字形右缘(bx 可能为负),
     * 否则小字号下窄字会互相压住。 */
    int right_edge = bx + w + 1;
    if (adv < right_edge) adv = right_edge;
    return adv > 0 ? adv : w;
}

/* UTF-8 解码 + 逐字推进。非 ASCII(cp>=128) 当前没有字形, 跳过但留出宽度,
 * 避免汉字把后面的排版挤乱(与原先 16px 的推进量保持兼容)。 */
void f32_draw_string(int x, int y, const char *str, uint32_t color, uint16_t size)
{
    if (!str || gfx_bpp != 32) return;

    /* TTF 已加载 -> 走 TTF(字号真实生效、边缘由光栅化器决定);
     * 否则走点阵兜底。与 64 位 font64_draw_string 的分支逻辑一致。 */
    if (g_f32_ttf_loaded && size > 0) {
        g_f32_ttf_size = size;
        int asc, desc, gap;
        ttf64_get_metrics(size, &asc, &desc, &gap);
        if (asc > 0) g_f32_ttf_ascent = asc;

        int cx = x;
        while (*str) {
            uint32_t cp;
            const unsigned char *p8 = (const unsigned char *)str;
            if (p8[0] < 0x80) { cp = p8[0]; str += 1; }
            else if ((p8[0] & 0xE0) == 0xC0 && p8[1]) {
                cp = ((uint32_t)(p8[0] & 0x1F) << 6) | (p8[1] & 0x3F); str += 2;
            } else if ((p8[0] & 0xF0) == 0xE0 && p8[1] && p8[2]) {
                cp = ((uint32_t)(p8[0] & 0x0F) << 12) |
                     ((uint32_t)(p8[1] & 0x3F) << 6) | (p8[2] & 0x3F); str += 3;
            } else if ((p8[0] & 0xF8) == 0xF0 && p8[1] && p8[2] && p8[3]) {
                cp = ((uint32_t)(p8[0] & 0x07) << 18) |
                     ((uint32_t)(p8[1] & 0x3F) << 12) |
                     ((uint32_t)(p8[2] & 0x3F) << 6) | (p8[3] & 0x3F); str += 4;
            } else { cp = p8[0]; str += 1; }

            if (cp == ' ') { cx += g_f32_ttf_size / 2; continue; }
            cx += f32_put_ttf(cx, y, cp, color);
        }
        return;
    }

    int scale = size / 16;
    if (scale < 1) scale = 1;
    if (scale > F32_MAX_SCALE) scale = F32_MAX_SCALE;

    int cur_x = x;
    while (*str) {
        uint32_t cp;
        const unsigned char *p8 = (const unsigned char *)str;
        if (p8[0] < 0x80) { cp = p8[0]; str += 1; }
        else if ((p8[0] & 0xE0) == 0xC0 && p8[1]) {
            cp = ((uint32_t)(p8[0] & 0x1F) << 6) | (p8[1] & 0x3F); str += 2;
        } else if ((p8[0] & 0xF0) == 0xE0 && p8[1] && p8[2]) {
            cp = ((uint32_t)(p8[0] & 0x0F) << 12) |
                 ((uint32_t)(p8[1] & 0x3F) << 6) | (p8[2] & 0x3F); str += 3;
        } else if ((p8[0] & 0xF8) == 0xF0 && p8[1] && p8[2] && p8[3]) {
            cp = ((uint32_t)(p8[0] & 0x07) << 18) |
                 ((uint32_t)(p8[1] & 0x3F) << 12) |
                 ((uint32_t)(p8[2] & 0x3F) << 6) | (p8[3] & 0x3F); str += 4;
        } else { cp = p8[0]; str += 1; }

        if (cp < 128) {
            f32_draw_cp(cur_x, y, cp, color, scale);
            cur_x += 8 * scale;
        } else {
            /* 无 CJK 字形: 留一格宽度, 不画 */
            cur_x += 16;
        }
    }
}

/* 与 draw 的推进量严格一致, 否则居中/右对齐会偏。 */
int f32_string_width(const char *str, uint16_t size)
{
    if (!str) return 0;
    if (g_f32_ttf_loaded && size > 0) {
        int w = 0;
        const unsigned char *p = (const unsigned char *)str;
        while (*p) {
            uint32_t cp;
            if (p[0] < 0x80) { cp = p[0]; p += 1; }
            else if ((p[0] & 0xE0) == 0xC0 && p[1]) { cp = ((uint32_t)(p[0]&0x1F)<<6)|(p[1]&0x3F); p += 2; }
            else if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) {
                cp = ((uint32_t)(p[0]&0x0F)<<12)|((uint32_t)(p[1]&0x3F)<<6)|(p[2]&0x3F); p += 3;
            }
            else if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) { p += 4; w += size / 2; continue; }
            else { w += size / 2; p += 1; continue; }

            if (cp == ' ') { w += size / 2; continue; }
            int adv = ttf64_get_char_advance(cp, size);
            w += adv > 0 ? adv : size;
        }
        return w;
    }
    int scale = size / 16;
    if (scale < 1) scale = 1;
    if (scale > F32_MAX_SCALE) scale = F32_MAX_SCALE;

    int w = 0;
    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        if (p[0] < 0x80)               { w += 8 * scale; p += 1; }
        else if ((p[0] & 0xE0) == 0xC0 && p[1]) { w += 16; p += 2; }
        else if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) { w += 16; p += 3; }
        else if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) { w += 16; p += 4; }
        else                           { w += 16; p += 1; }
    }
    return w;
}
