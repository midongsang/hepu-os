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
#include "cursor_modern.h"

extern void gfx64_put_pixel(int x, int y, uint32_t color);
extern void gfx64_blend_pixel(int x, int y, uint32_t color, int alpha);
extern void gfx64_draw_mouse_cursor(int x, int y, uint32_t color);
extern int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size);

#define CUR_MAX_DIM   64
#define XCUR_IMAGE    0xFFFD0002u
#define XCUR_HDR_SIZE 36u

/* 多形态: 每种形态各留一份已解码位图, 切换形态只切指针, 不重复读盘。
 * 每槽 48x48 上限 —— 实际 want 只可能是 24/32/48(见 cursor64_pick_size),
 * 48*48*4B*4 槽 ≈ 36KB, 对内核体积可接受; 超过 48 的图不装载, 免得撑爆。 */
#define CUR_KIND_MAX  4
#define CUR_SLOT_DIM  48
typedef struct {
    uint32_t px[CUR_SLOT_DIM * CUR_SLOT_DIM];
    int w, h, hot_x, hot_y;
    int ready;
    int nominal;
} cur_slot_t;

static cur_slot_t g_slot[CUR_KIND_MAX];
static int g_active;

/* 主题: 0 = 外部 Xcursor 素材包(经典), 1 = 内置 Modern */
static int g_theme;

/* 内置形态映射: 系统只用到 4 种 kind, 其余形态留在表里供后续扩展 */
static const int g_modern_map[CUR_KIND_MAX] = { 0, 14, 5, 12 };

void cursor64_set_theme(int t) { g_theme = (t == 1) ? 1 : 0; }
int  cursor64_theme(void)      { return g_theme; }

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* 解析内存中的 xcur 数据；want = 期望的名义尺寸(像素) */
int cursor64_load_blob(int kind, const uint8_t *b, uint32_t len, int want)
{
    if (kind < 0 || kind >= CUR_KIND_MAX) return -10;
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
    if (w == 0 || h == 0 || w > CUR_SLOT_DIM || h > CUR_SLOT_DIM) return -7;
    if (best_pos + XCUR_HDR_SIZE + w * h * 4u > len) return -8;

    cur_slot_t *sl = &g_slot[kind];
    const uint8_t *p = c + XCUR_HDR_SIZE;
    for (uint32_t j = 0; j < h; j++)
        for (uint32_t i = 0; i < w; i++)
            sl->px[j * w + i] = rd32(p + 4u * (j * w + i));

    sl->w = (int)w; sl->h = (int)h;
    sl->hot_x = (xh < w) ? (int)xh : (int)(w / 2);
    sl->hot_y = (yh < h) ? (int)yh : (int)(h / 2);
    sl->nominal = (int)best_nom;
    sl->ready = 1;
    return 0;
}

/* 从根文件系统读取 xcur 文件并装载到指定形态槽 */
int cursor64_load_kind(int kind, const char *path, int want)
{
    static uint8_t buf[64 * 1024];
    int n = ext2_64_read_file(path, buf, (uint32_t)sizeof(buf));
    if (n <= 0) return -9;
    return cursor64_load_blob(kind, buf, (uint32_t)n, want);
}

/* 兼容旧调用点: 未指定形态时装到 0 号槽(箭头) */
int cursor64_load_file(const char *path, int want)
{
    return cursor64_load_kind(0, path, want);
}

void cursor64_set_kind(int kind)
{
    if (kind < 0 || kind >= CUR_KIND_MAX) return;
    if (!g_slot[kind].ready) return;      /* 该形态没装: 保持当前, 不闪 */
    g_active = kind;
}
int cursor64_kind(void)       { return g_active; }
int cursor64_ready(void)      { return g_slot[g_active].ready || g_slot[0].ready; }
int cursor64_size(void)       { return g_slot[g_active].ready ? g_slot[g_active].w : g_slot[0].w; }
int cursor64_nominal(void)    { return g_slot[g_active].ready ? g_slot[g_active].nominal : g_slot[0].nominal; }
int cursor64_hotspot_x(void)  { return g_slot[g_active].ready ? g_slot[g_active].hot_x : g_slot[0].hot_x; }
int cursor64_hotspot_y(void)  { return g_slot[g_active].ready ? g_slot[g_active].hot_y : g_slot[0].hot_y; }

/* 按屏幕分辨率推荐名义尺寸：1024/768→24, 1280/1440→32, 更高→48 */
int cursor64_pick_size(int screen_w, int screen_h)
{
    int m = (screen_w > screen_h) ? screen_h : screen_w;
    if (m >= 2000) return 48;
    if (m >= 1200) return 32;
    return 24;
}

/* 光标尺寸跟随 UI 缩放档(100/125/150/200): 指针是 DIP 尺寸, 高 DPI 下要同步放大 */
static int g_scale_pct = 100;
void cursor64_set_scale(int pct) { g_scale_pct = (pct >= 100 && pct <= 250) ? pct : 100; }
int  cursor64_scale(void)        { return g_scale_pct; }

/* Modern 主题: 位图 48px, 按缩放档最近邻放大后逐像素 alpha 混合。
 * 最近邻而不是双线性 —— 线稿放大后要保持硬边, 插值会把描边糊成灰边。 */
static void cur_modern_draw(int x, int y)
{
    int k = g_active;
    if (k < 0 || k >= CUR_KIND_MAX) k = 0;
    int idx = g_modern_map[k];
    const cur_modern_t *b = 0;
    for (int i = 0; i < g_cur_modern_count; i++) {
        if (g_cur_modern[i].id == idx) { b = &g_cur_modern[i]; break; }
    }
    if (!b) b = &g_cur_modern[0];

    int tw = b->w * g_scale_pct / 100;
    int th = b->h * g_scale_pct / 100;
    if (tw < 8) tw = 8;
    if (th < 8) th = 8;
    if (tw > 128) tw = 128;
    if (th > 128) th = 128;
    int hx = b->hx * g_scale_pct / 100;      /* 热点同比缩放, 否则放大后落点偏 */
    int hy = b->hy * g_scale_pct / 100;
    int ox = x - hx, oy = y - hy;

    for (int j = 0; j < th; j++) {
        int sj = j * b->h / th;
        const uint8_t *arow = b->alpha + sj * b->w;
        int brow = sj * b->w;
        for (int i = 0; i < tw; i++) {
            int si = i * b->w / tw;
            int a = arow[si];
            if (!a) continue;
            int off = brow + si;
            uint32_t c = (b->bits[off >> 3] >> (off & 7)) & 1 ? b->fill : b->stroke;
            if (a >= 200) gfx64_put_pixel(ox + i, oy + j, c);
            else          gfx64_blend_pixel(ox + i, oy + j, c, a);
        }
    }
}

/* 绘制当前光标（热点对齐），未装载时回退内置位图 */
void cursor64_draw(int x, int y)
{
    if (g_theme == 1) { cur_modern_draw(x, y); return; }
    const cur_slot_t *sl = &g_slot[g_active];
    if (!sl->ready) sl = &g_slot[0];     /* 回退箭头 */
    if (!sl->ready) {
        gfx64_draw_mouse_cursor(x, y, 0xE6E1E5);
        return;
    }
    x -= sl->hot_x;
    y -= sl->hot_y;
    for (int j = 0; j < sl->h; j++) {
        for (int i = 0; i < sl->w; i++) {
            uint32_t p = sl->px[j * sl->w + i];
            int a = (int)(p >> 24);
            if (!a) continue;
            uint32_t rgb = p & 0xFFFFFFu;
            if (a >= 255) gfx64_put_pixel(x + i, y + j, rgb);
            else          gfx64_blend_pixel(x + i, y + j, rgb, a);
        }
    }
}
