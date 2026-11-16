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

/* UTF-8 解码 + 逐字推进。非 ASCII(cp>=128) 当前没有字形, 跳过但留出宽度,
 * 避免汉字把后面的排版挤乱(与原先 16px 的推进量保持兼容)。 */
void f32_draw_string(int x, int y, const char *str, uint32_t color, uint16_t size)
{
    if (!str || gfx_bpp != 32) return;

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
