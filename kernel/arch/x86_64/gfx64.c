/* ============================================================================
 * 赫普OS - 64位图形子系统（简化版）
 * 支持VBE线性帧缓冲，基本绘图和字体渲染
 * V0.10.6: 初始实现
 * V0.10.95: P3-1图形共享实现，引用shared/gfx/gfx_shared.h
 * V0.13.1: 矢量图元扩展（直线/圆/弧/椭圆/多边形/贝塞尔/抗锯齿/alpha混合）
 * ========================================================================== */

#include <stdint.h>
#include "spinlock.h"
#include <stddef.h>
#include "../../shared/gfx/gfx_shared.h"
#include "math64.h"
#include "vg64.h"   /* P19.2: 图元统一到矢量管线 */

/* V0.55.1: 以下两个函数在文件内先被调用、后定义, 在此提前原型以消除隐式声明 */
void gfx64_blit_background(uint8_t *bg, int x, int y, int w, int h);
void gfx64_blit_to(uint8_t *dstfb, uint8_t *bg, int x, int y, int w, int h);
void gfx64_blend_pixel(int x, int y, uint32_t fg, int alpha);

/* 图形模式信息（使用共享gfx_framebuffer_t结构） */
static gfx_framebuffer_t gfx_fb_info = {0};

/* 兼容旧代码的宏（逐步迁移到gfx_fb_info） */
#define gfx_width       (gfx_fb_info.width)
#define gfx_height      (gfx_fb_info.height)
#define gfx_pitch       (gfx_fb_info.pitch)
#define gfx_bpp         (gfx_fb_info.bpp)
#define gfx_fb          ((uint8_t*)gfx_fb_info.framebuffer)
#define gfx_initialized (gfx_fb_info.initialized)

/* P9: 双缓冲 — 后台缓冲运行时 pmm 动态分配 (避免 3MB 静态 BSS 未映射崩溃) */
#define GFX_MAX_BUF (1024 * 768 * 4)
static uint8_t *back_buffer = 0;
static int back_buf_alloc_ok = 0;
static uint8_t *front_buffer = (uint8_t *)0;
static uint8_t *gfx_target = (uint8_t *)0;   /* 当前渲染目标（默认=帧缓冲） */

/* SB-15: 帧提交锁。
 * 当前只有 BSP 桌面主循环一个写者，并发窗口尚未打开（AP 只跑 idle）。
 * 这把锁是为 SB-25「放开 AP 调度」预埋：一旦有第二个绘制者，
 * gfx_target 的切换与 8MB 帧拷贝必须互斥，否则整帧会落到错误缓冲。
 * 帧内容绘制（fill_rect / put_string / 面板 render）一律不加锁 ——
 * 它们写的是后台缓冲，只要目标不被抢走就是独占的。 */
static spinlock_t g_gfx_lock;

/* 设置渲染目标（用于分层渲染：静态层渲染到背景缓冲） */
void gfx64_set_target(uint8_t *fb)
{
    uint64_t gfx_f = spinlock_lock_irqsave(&g_gfx_lock);
    gfx_target = fb;
    spinlock_unlock_irqrestore(&g_gfx_lock, gfx_f);
}

/* 恢复渲染目标为前缓冲 */
void gfx64_set_target_front(void)
{
    uint64_t gfx_f = spinlock_lock_irqsave(&g_gfx_lock);
    gfx_target = front_buffer;
    spinlock_unlock_irqrestore(&g_gfx_lock, gfx_f);
}

/* P9: 双缓冲 — 渲染目标切到后台缓冲 (首次调用时分配) */
extern uint64_t pmm64_alloc_pages(uint64_t n);
void gfx64_set_target_back(void)
{
    uint64_t gfx_f = spinlock_lock_irqsave(&g_gfx_lock);
    if (!back_buffer && !back_buf_alloc_ok) {
        uint64_t phys = pmm64_alloc_pages((GFX_MAX_BUF + 4095) / 4096);
        if (phys) {
            back_buffer = (uint8_t *)(uintptr_t)phys;
        }
        back_buf_alloc_ok = 1;
    }
    if (back_buffer) gfx_target = back_buffer;
    spinlock_unlock_irqrestore(&g_gfx_lock, gfx_f);
}

/* P9: 帧交换 — 将后台完整帧一次性拷贝到前台 (原子帧, 消除撕裂/中途帧)
 * GFX-02: 旧实现靠「临时改全局 gfx_target 再改回来」把目标指向前台，
 * 那段 8MB 拷贝期间任何别的绘制都会画到前台去（窗口期最长可达数毫秒）。
 * 这里改成显式传目标：blit 内部函数接受 dst，present 期间全局 gfx_target
 * 完全不动。 */
void gfx64_present(void)
{
    if (!gfx_initialized) return;
    if (!back_buffer) return;
    uint64_t gfx_f = spinlock_lock_irqsave(&g_gfx_lock);
    gfx64_blit_to(front_buffer, back_buffer, 0, 0, (int)gfx_width, (int)gfx_height);
    spinlock_unlock_irqrestore(&g_gfx_lock, gfx_f);
}


/* Surface 雏形: 从后台缓冲抓一块矩形到调用方缓冲(窗口拖动快照),
 * 以及把这块快照贴回后台缓冲。目标始终是后台缓冲 —— 前台只在 present 时更新。 */
int gfx64_back_snapshot(void *dst, int x, int y, int w, int h)
{
    if (!back_buffer || !dst || w <= 0 || h <= 0) return 0;
    if (gfx_bpp != 32) return 0;
    int pitch32 = gfx_pitch / 4;
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if ((uint32_t)x1 > gfx_width) x1 = (int)gfx_width;
    if ((uint32_t)y1 > gfx_height) y1 = (int)gfx_height;
    if (x0 >= x1 || y0 >= y1) return 0;
    uint32_t *src = (uint32_t *)back_buffer;
    uint32_t *out = (uint32_t *)dst;
    for (int j = y0; j < y1; j++) {
        uint32_t *srow = src + j * pitch32 + x0;
        uint32_t *drow = out + (j - y) * w + (x0 - x);
        for (int i = 0; i < x1 - x0; i++) drow[i] = srow[i];
    }
    return 1;
}

int gfx64_back_restore(void *src, int x, int y, int w, int h)
{
    if (!back_buffer || !src || w <= 0 || h <= 0) return 0;
    if (gfx_bpp != 32) return 0;
    uint64_t gfx_f = spinlock_lock_irqsave(&g_gfx_lock);
    int pitch32 = gfx_pitch / 4;
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if ((uint32_t)x1 > gfx_width) x1 = (int)gfx_width;
    if ((uint32_t)y1 > gfx_height) y1 = (int)gfx_height;
    uint32_t *dst = (uint32_t *)back_buffer;
    uint32_t *in = (uint32_t *)src;
    if (x0 < x1 && y0 < y1) {
        for (int j = y0; j < y1; j++) {
            uint32_t *drow = dst + j * pitch32 + x0;
            uint32_t *srow = in + (j - y) * w + (x0 - x);
            for (int i = 0; i < x1 - x0; i++) drow[i] = srow[i];
        }
    }
    spinlock_unlock_irqrestore(&g_gfx_lock, gfx_f);
    return 1;
}

/* P0 脏区: 只把后台缓冲的一块矩形提交到前台。
 * 光标每移动一次原实现都会整帧重绘 + 整帧 8MB 拷贝, 实际只为了搬一个 64x64 的光标;
 * 这里给光标合成提供"只拷两块"的路径。加锁与 gfx64_present 一致, 避免和整帧提交互斥。 */
void gfx64_present_region(int x, int y, int w, int h)
{
    if (!gfx_initialized || !back_buffer) return;
    if (w <= 0 || h <= 0) return;
    uint64_t gfx_f = spinlock_lock_irqsave(&g_gfx_lock);
    gfx64_blit_to(front_buffer, back_buffer, x, y, w, h);
    spinlock_unlock_irqrestore(&g_gfx_lock, gfx_f);
}

/* 复制背景缓冲区域到帧缓冲（整行64位复制，分层渲染用） */
void gfx64_blit_to(uint8_t *dstfb, uint8_t *bg, int x, int y, int w, int h)
{
    if (!gfx_initialized || !bg || !dstfb) return;
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if ((uint32_t)x1 > gfx_width) x1 = (int)gfx_width;
    if ((uint32_t)y1 > gfx_height) y1 = (int)gfx_height;
    if (x0 >= x1 || y0 >= y1) return;
    if (gfx_bpp == 32) {
        uint64_t *dst = (uint64_t *)dstfb;
        uint64_t *src = (uint64_t *)bg;
        int pitch64 = gfx_pitch / 8;
        /* x0 为偶数像素时整行按 64 位复制；奇数像素回退 32 位 */
        if ((x0 & 1) == 0) {
            for (int j = y0; j < y1; j++) {
                int n = (x1 - x0);
                uint64_t *srow = src + j * pitch64 + x0 / 2;
                uint64_t *drow = dst + j * pitch64 + x0 / 2;
                int cnt = n / 2;
                for (int i = 0; i < cnt; i++) drow[i] = srow[i];
                if (n & 1) {
                    uint32_t *ds = (uint32_t *)dstfb;
                    uint32_t *ss = (uint32_t *)bg;
                    int pitch32 = gfx_pitch / 4;
                    ds[j * pitch32 + x0 + n - 1] = ss[j * pitch32 + x0 + n - 1];
                }
            }
        } else {
            uint32_t *ds = (uint32_t *)dstfb;
            uint32_t *ss = (uint32_t *)bg;
            int pitch32 = gfx_pitch / 4;
            for (int j = y0; j < y1; j++) {
                int n = (x1 - x0);
                uint32_t *srow = ss + j * pitch32 + x0;
                uint32_t *drow = ds + j * pitch32 + x0;
                for (int i = 0; i < n; i++) drow[i] = srow[i];
            }
        }
    } else if (gfx_bpp == 24) {
        uint8_t *dst = dstfb;
        uint8_t *src = bg;
        for (int j = y0; j < y1; j++) {
            int n = (x1 - x0) * 3;
            uint8_t *srow = src + j * gfx_pitch + x0 * 3;
            uint8_t *drow = dst + j * gfx_pitch + x0 * 3;
            for (int i = 0; i < n; i++) drow[i] = srow[i];
        }
    }
}

/* 兼容旧调用点：目标取当前渲染目标 */
void gfx64_blit_background(uint8_t *bg, int x, int y, int w, int h)
{
    uint64_t gfx_f = spinlock_lock_irqsave(&g_gfx_lock);
    uint8_t *dst = gfx_target ? gfx_target : front_buffer;
    spinlock_unlock_irqrestore(&g_gfx_lock, gfx_f);
    gfx64_blit_to(dst, bg, x, y, w, h);
}

/* 外部函数 */
extern void vga_puts(const char *str);
extern void vga_put_hex(uint64_t val);
extern void *memset(void *dst, int val, size_t n);
extern void *memcpy(void *dst, const void *src, size_t n);

/* TrueType字体支持 */
extern int ttf64_render_char(uint32_t ch, int pixel_size, uint8_t *bitmap,
                              int *w, int *h, int *x, int *y);
extern int ttf64_is_loaded(void);
extern void ttf64_get_metrics(int pixel_size, int *ascent, int *descent, int *line_gap);
extern int ttf64_init_cjk(const uint8_t *data, uint32_t size);
extern int ttf64_render_char_cjk(uint32_t ch, int pixel_size, uint8_t *bitmap,
                                 int *w, int *h, int *x, int *y);
extern int ttf64_cjk_is_loaded(void);
static int gfx_ttf_size = 16;  /* TrueType字体大小 */
static int gfx_ttf_ascent = 14; /* 缓存的字体ascent，避免每次渲染都调用浮点函数 */
static int gfx_use_ttf = 0;     /* 是否使用TrueType字体 */

/* 8x16 ASCII字体（简化版，前128字符） */
const uint8_t font8x16[128][16] = {
    [0x20] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},  /* space */
    [0x21] = {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x18,0x18,0x00,0x00},  /* ! */
    [0x22] = {0x6C,0x6C,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* " */
    [0x23] = {0x6C,0x6C,0x6C,0xFE,0x6C,0x6C,0x6C,0xFE,0x6C,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00},  /* # */
    [0x24] = {0x18,0x7E,0xC0,0x7C,0x06,0x7C,0xC0,0x7E,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* $ */
    [0x25] = {0x00,0xC6,0xC6,0x0C,0x18,0x30,0x60,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* % */
    [0x26] = {0x38,0x6C,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* & */
    [0x27] = {0x30,0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* ' */
    [0x28] = {0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* ( */
    [0x29] = {0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* ) */
    [0x2A] = {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* * */
    [0x2B] = {0x00,0x18,0x18,0x18,0x7E,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* + */
    [0x2C] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},  /* , */
    [0x2D] = {0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* - */
    [0x2E] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* . */
    [0x2F] = {0x06,0x0C,0x0C,0x18,0x18,0x30,0x30,0x60,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* / */
    [0x30] = {0x7C,0xC6,0xC6,0xCE,0xD6,0xE6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* 0 */
    [0x31] = {0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* 1 */
    [0x32] = {0x7C,0xC6,0x06,0x0C,0x18,0x30,0x60,0xC6,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* 2 */
    [0x33] = {0x7C,0xC6,0x06,0x06,0x3C,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* 3 */
    [0x34] = {0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x1E,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* 4 */
    [0x35] = {0xFE,0xC0,0xC0,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* 5 */
    [0x36] = {0x38,0x60,0xC0,0xC0,0xFC,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* 6 */
    [0x37] = {0xFE,0xC6,0x06,0x0C,0x18,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* 7 */
    [0x38] = {0x7C,0xC6,0xC6,0xC6,0x7C,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* 8 */
    [0x39] = {0x7C,0xC6,0xC6,0xC6,0x7E,0x06,0x06,0x0C,0x78,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* 9 */
    [0x3A] = {0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* : */
    [0x3B] = {0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* ; */
    [0x3C] = {0x06,0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* < */
    [0x3D] = {0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* = */
    [0x3E] = {0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* > */
    [0x3F] = {0x7C,0xC6,0xC6,0x0C,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* ? */
    [0x40] = {0x7C,0xC6,0xC6,0xDE,0xDE,0xDE,0xC0,0xC0,0x7C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* @ */
    [0x41] = {0x38,0x6C,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* A */
    [0x42] = {0xFC,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0xFC,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* B */
    [0x43] = {0x3C,0x66,0xC2,0xC0,0xC0,0xC0,0xC2,0x66,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* C */
    [0x44] = {0xF8,0x6C,0x66,0x66,0x66,0x66,0x66,0x6C,0xF8,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* D */
    [0x45] = {0xFE,0x66,0x62,0x68,0x78,0x68,0x62,0x66,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* E */
    [0x46] = {0xFE,0x66,0x62,0x68,0x78,0x68,0x60,0x60,0xF0,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* F */
    [0x47] = {0x3C,0x66,0xC2,0xC0,0xC0,0xDE,0xC6,0x66,0x3A,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* G */
    [0x48] = {0xC6,0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* H */
    [0x49] = {0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* I */
    [0x4A] = {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* J */
    [0x4B] = {0xE6,0x66,0x6C,0x6C,0x78,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* K */
    [0x4C] = {0xF0,0x60,0x60,0x60,0x60,0x60,0x62,0x66,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* L */
    [0x4D] = {0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* M */
    [0x4E] = {0xC6,0xE6,0xF6,0xFE,0xDE,0xCE,0xC6,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* N */
    [0x4F] = {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* O */
    [0x50] = {0xFC,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* P */
    [0x51] = {0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0xD6,0xDE,0x7C,0x06,0x00,0x00,0x00,0x00,0x00,0x00},  /* Q */
    [0x52] = {0xFC,0x66,0x66,0x66,0x7C,0x6C,0x66,0x66,0xE6,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* R */
    [0x53] = {0x7C,0xC6,0x60,0x60,0x38,0x0C,0x0C,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* S */
    [0x54] = {0x7E,0x7E,0x5A,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* T */
    [0x55] = {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* U */
    [0x56] = {0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* V */
    [0x57] = {0xC6,0xC6,0xC6,0xC6,0xD6,0xD6,0xFE,0xEE,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* W */
    [0x58] = {0xC6,0xC6,0x6C,0x7C,0x38,0x7C,0x6C,0xC6,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* X */
    [0x59] = {0x66,0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* Y */
    [0x5A] = {0xFE,0xC6,0x86,0x0C,0x18,0x30,0x60,0xC6,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* Z */
    [0x5B] = {0x3C,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* [ */
    [0x5C] = {0x60,0x60,0x30,0x30,0x18,0x18,0x0C,0x0C,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* \ */
    [0x5D] = {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* ] */
    [0x5E] = {0x18,0x3C,0x66,0xC3,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* ^ */
    [0x5F] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00,0x00,0x00,0x00,0x00,0x00},  /* _ */
    [0x61] = {0x00,0x00,0x00,0x78,0x0C,0x7C,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* a */
    [0x62] = {0xE0,0x60,0x60,0x78,0x6C,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* b */
    [0x63] = {0x00,0x00,0x00,0x7C,0xC6,0xC0,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* c */
    [0x64] = {0x1C,0x0C,0x0C,0x3C,0x6C,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* d */
    [0x65] = {0x00,0x00,0x00,0x7C,0xC6,0xFE,0xC0,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* e */
    [0x66] = {0x38,0x6C,0x6C,0x60,0xF0,0x60,0x60,0x60,0xF0,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* f */
    [0x67] = {0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0x7C,0x0C,0xCC,0x78,0x00,0x00,0x00,0x00,0x00},  /* g */
    [0x68] = {0xE0,0x60,0x60,0x78,0x6C,0x66,0x66,0x66,0xE6,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* h */
    [0x69] = {0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* i */
    [0x6A] = {0x06,0x06,0x00,0x0E,0x06,0x06,0x06,0x06,0x06,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},  /* j */
    [0x6B] = {0xE0,0x60,0x60,0x66,0x6C,0x78,0x78,0x6C,0xE6,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* k */
    [0x6C] = {0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* l */
    [0x6D] = {0x00,0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xD6,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* m */
    [0x6E] = {0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* n */
    [0x6F] = {0x00,0x00,0x00,0x7C,0xC6,0xC6,0xC6,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* o */
    [0x70] = {0x00,0x00,0x00,0xDC,0x66,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00,0x00,0x00,0x00,0x00},  /* p */
    [0x71] = {0x00,0x00,0x00,0x76,0xCC,0xCC,0xCC,0x7C,0x0C,0x0C,0x1E,0x00,0x00,0x00,0x00,0x00},  /* q */
    [0x72] = {0x00,0x00,0x00,0xDC,0x76,0x66,0x60,0x60,0xF0,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* r */
    [0x73] = {0x00,0x00,0x00,0x7C,0xC0,0x7C,0x06,0xC6,0x7C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* s */
    [0x74] = {0x30,0x30,0xFC,0x30,0x30,0x30,0x36,0x36,0x1C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* t */
    [0x75] = {0x00,0x00,0x00,0xCC,0xCC,0xCC,0xCC,0xCC,0x76,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* u */
    [0x76] = {0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* v */
    [0x77] = {0x00,0x00,0x00,0xC6,0xC6,0xD6,0xD6,0xFE,0x6C,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* w */
    [0x78] = {0x00,0x00,0x00,0xC6,0x6C,0x38,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* x */
    [0x79] = {0x00,0x00,0x00,0xC6,0xC6,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00,0x00,0x00,0x00,0x00},  /* y */
    [0x7A] = {0x00,0x00,0x00,0xFE,0x0C,0x18,0x30,0x60,0xFE,0x00,0x00,0x00,0x00,0x00,0x00,0x00},  /* z */
};

/* 设置图形模式信息（由bootloader或内核调用） */
void gfx64_set_mode(int width, int height, int pitch, int bpp, uint8_t *fb)
{
    uint64_t gfx_f = spinlock_lock_irqsave(&g_gfx_lock);
    gfx_fb_info.width = width;
    gfx_fb_info.height = height;
    gfx_fb_info.pitch = pitch;
    gfx_fb_info.bpp = bpp;
    gfx_fb_info.framebuffer = fb;
    front_buffer = fb;

    vga_puts("[GFX64] Mode set: ");
    vga_put_hex(width);
    vga_puts("x");
    vga_put_hex(height);
    vga_puts("x");
    vga_put_hex(bpp);
    vga_puts(" pitch=");
    vga_put_hex(pitch);
    vga_puts(" fb=");
    vga_put_hex((uint64_t)fb);
    vga_puts("\n");
    spinlock_unlock_irqrestore(&g_gfx_lock, gfx_f);

    gfx_initialized = 1;
    /* V0.13.60: 图形模式接管后关闭早期文本镜像, 避免覆盖桌面 */
    extern void early_fb_text_disable(void);
    early_fb_text_disable();
}

/* 写入像素到当前渲染目标（默认前缓冲/显存） */
void gfx64_put_pixel(int x, int y, uint32_t color)
{
    if (!gfx_initialized) return;
    if (x < 0 || y < 0 || (uint32_t)x >= gfx_width || (uint32_t)y >= gfx_height) return;
    uint8_t *dst = gfx_target ? gfx_target : front_buffer;

    if (gfx_bpp == 32) {
        uint32_t *p = (uint32_t *)(dst + y * gfx_pitch + x * 4);
        *p = color;
    } else if (gfx_bpp == 24) {
        uint8_t *p = dst + y * gfx_pitch + x * 3;
        p[0] = color & 0xFF;
        p[1] = (color >> 8) & 0xFF;
        p[2] = (color >> 16) & 0xFF;
    }
}

/* 读取像素（当前渲染目标） */
uint32_t gfx64_get_pixel(int x, int y)
{
    if (!gfx_initialized) return 0;
    if (x < 0 || y < 0 || (uint32_t)x >= gfx_width || (uint32_t)y >= gfx_height) return 0;
    uint8_t *dst = gfx_target ? gfx_target : front_buffer;
    if (gfx_bpp == 32) {
        uint32_t *p = (uint32_t *)(dst + y * gfx_pitch + x * 4);
        return *p;
    } else if (gfx_bpp == 24) {
        uint8_t *p = dst + y * gfx_pitch + x * 3;
        return p[0] | (p[1] << 8) | (p[2] << 16);
    }
    return 0;
}

/* 填充矩形（整行填充，避免逐像素函数调用开销；V0.13.3 渲染加速） */
void gfx64_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    if (!gfx_initialized) return;
    int x0 = x, x1 = x + w;
    int y0 = y, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if ((uint32_t)x1 > gfx_width) x1 = (int)gfx_width;
    if ((uint32_t)y1 > gfx_height) y1 = (int)gfx_height;
    if (x0 >= x1 || y0 >= y1) return;
    uint8_t *dst = gfx_target ? gfx_target : front_buffer;

    if (gfx_bpp == 32) {
        uint32_t *base = (uint32_t *)dst;
        int pitch32 = gfx_pitch / 4;
        for (int j = y0; j < y1; j++) {
            uint32_t *row = base + j * pitch32 + x0;
            uint32_t *end = row + (x1 - x0);
            while (row < end) *row++ = color;
        }
    } else if (gfx_bpp == 24) {
        uint8_t *base = dst;
        uint8_t r = color & 0xFF, g = (color >> 8) & 0xFF, b = (color >> 16) & 0xFF;
        for (int j = y0; j < y1; j++) {
            uint8_t *row = base + j * gfx_pitch + x0 * 3;
            for (int i = x0; i < x1; i++) {
                row[0] = r; row[1] = g; row[2] = b;
                row += 3;
            }
        }
    }
}

/* 绘制字符（支持TrueType和8x16位图），返回字符宽度 */
/* TTF字符位图缓存：避免每帧重新栅格化字形（V0.13.3 渲染加速）
 * V0.13.4: 缓存key升为uint32, 支持CJK码点
 * V0.13.6: 缓存扩容至64并支持锁定槽(桌面大时钟48px字形锁定, 防止环形替换
 *          挤出后 miss 重光栅化带来的时序竞态 -- 间歇性"时钟消失"根因) */
/* V0.58.3: 24 槽对一屏上百个 (字符,字号) 组合几乎必然全未命中 —— 缓存等于没有,
 * 每次重绘都要重新光栅化, 单串文本绘制耗时被放大到数百毫秒级(见 §1.93)。
 * 扩到 256 槽, 同时把单槽 128x128 收到 64x64: 256*64*64 = 1 MB, 与原来 24 槽
 * 的 384 KB 同量级, 但命中率覆盖整屏。超宽字形仍由 ttf_canary 兜底不缓存。 */
#define TTF_CACHE_ITEMS 256
#define TTF_CACHE_W 64
#define TTF_CACHE_H 64
typedef struct {
    uint32_t c;
    uint16_t size;
    uint8_t valid;
    uint8_t locked;  /* V0.13.6: 锁定槽不被环形替换 */
    uint8_t bitmap[TTF_CACHE_W * TTF_CACHE_H];
    int w, h, bx, by;
} ttf_cache_entry_t;
static ttf_cache_entry_t ttf_cache[TTF_CACHE_ITEMS];
static int ttf_cache_next = 0;
/* SB-15: 字形缓存锁。两核同时缺同一个字形会各自 find_slot 到同一槽、
 * 互相覆盖 bitmap，缓存里出现两个字形混在一起的结果。
 * 只锁槽位查找与填充记账，字形光栅化（慢）留在锁外。 */
static spinlock_t g_ttf_lock;



/* V0.13.6: 时钟字形锁定判定 (48px ASCII 数字/冒号) */
static int ttf_cache_should_lock(uint32_t cp, int size)
{
    if (size != 48) return 0;
    if (cp >= '0' && cp <= '9') return 1;
    if (cp == ':') return 1;
    return 0;
}

/* V0.13.6: 查找可替换槽 (优先非锁定; 全锁定则沿用旧槽) */
static int ttf_cache_find_slot(uint32_t cp, int size)
{
    uint64_t tf = spinlock_lock_irqsave(&g_ttf_lock);
    int result = -1;   /* 单出口：所有分支只设结果，锁在末尾统一释放 */
    int victim = -1;
    for (int k = 0; k < TTF_CACHE_ITEMS; k++) {
        int ci = (ttf_cache_next + k) % TTF_CACHE_ITEMS;
        if (!ttf_cache[ci].valid) {
            ttf_cache_next = (ci + 1) % TTF_CACHE_ITEMS;
            result = ci;
            goto out;
        }
        if (!ttf_cache[ci].locked && victim < 0) victim = ci;
    }
    if (victim >= 0) {
        ttf_cache_next = (victim + 1) % TTF_CACHE_ITEMS;
        result = victim;
        goto out;
    }
    /* 全锁定: 逐字搜索同key槽 */
    for (int ci = 0; ci < TTF_CACHE_ITEMS; ci++) {
        if (ttf_cache[ci].valid && ttf_cache[ci].c == cp && ttf_cache[ci].size == size) {
            result = ci;
            break;
        }
    }
out:
    spinlock_unlock_irqrestore(&g_ttf_lock, tf);
    return result;
}

static int gfx64_put_ttf(int x, int y, uint32_t cp, uint32_t color);

int gfx64_put_char(int x, int y, char c, uint32_t color)
{
    if (!gfx_initialized) return 8;

    /* 如果TrueType字体可用，使用TrueType渲染（带alpha混合） */
    if (gfx_use_ttf && ttf64_is_loaded()) {
        return gfx64_put_ttf(x, y, (uint32_t)(uint8_t)c, color);
    }
    /* 回退到8x16位图字体 */
    /* V0.55.1: c 是 char(最大 127), "c < 128" 恒真, 只保留可打印判定 */
    if (c > 0) {
        const uint8_t *glyph = font8x16[(uint8_t)c];
        for (int row = 0; row < 16; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (0x80 >> col)) {
                    if ((uint32_t)(x + col) < gfx_width && (uint32_t)(y + row) < gfx_height)
                        gfx64_put_pixel(x + col, y + row, color);
                }
            }
        }
    }
    return 8;
}

/* ---------- TTF 字符渲染（ASCII 与 CJK 统一入口, 带缓存） ---------- */
static int gfx64_put_ttf(int x, int y, uint32_t cp, uint32_t color)
{
    int w, h, bx, by;
    uint8_t *bm = 0;
    for (int ci = 0; ci < TTF_CACHE_ITEMS; ci++) {
        if (ttf_cache[ci].valid && ttf_cache[ci].c == cp &&
            ttf_cache[ci].size == gfx_ttf_size) {
            bm = ttf_cache[ci].bitmap;
            w = ttf_cache[ci].w; h = ttf_cache[ci].h;
            bx = ttf_cache[ci].bx; by = ttf_cache[ci].by;
            break;
        }
    }
    if (!bm) {
        static uint8_t ttf_bitmap[TTF_CACHE_W * TTF_CACHE_H + 256];
        static volatile uint32_t ttf_canary = 0xDEADBEEF;
        int render_ok;
        if (cp < 128) {
            render_ok = ttf64_render_char(cp, gfx_ttf_size, ttf_bitmap, &w, &h, &bx, &by);
        } else {
            render_ok = ttf64_render_char_cjk(cp, gfx_ttf_size, ttf_bitmap, &w, &h, &bx, &by);
        }
        if (ttf_canary != 0xDEADBEEF) {
            extern void vga_puts(const char *s);
            extern void vga_put_hex(uint64_t val);
            vga_puts("[GFX64] TTF bitmap overflow! cp=");
            vga_put_hex(cp);
            vga_puts(" w="); vga_put_hex(w);
            vga_puts(" h="); vga_put_hex(h);
            vga_puts("\n");
            ttf_canary = 0xDEADBEEF;
        }
        if (render_ok == 0 && w > 0 && w <= TTF_CACHE_W && h > 0 && h <= TTF_CACHE_H) {
            int slot = ttf_cache_find_slot(cp, gfx_ttf_size);
            if (slot >= 0) {
                ttf_cache_entry_t *e = &ttf_cache[slot];
                e->c = cp;
                e->size = gfx_ttf_size;
                e->w = w; e->h = h; e->bx = bx; e->by = by;
                e->locked = ttf_cache_should_lock(cp, gfx_ttf_size);
                for (int r = 0; r < h; r++) {
                    for (int cc = 0; cc < w; cc++) e->bitmap[r * TTF_CACHE_W + cc] = ttf_bitmap[r * w + cc];
                }
                e->valid = 1;
                bm = e->bitmap;
            }
        }
    }
    if (bm && w > 0 && w <= TTF_CACHE_W && h > 0 && h <= TTF_CACHE_H) {
        int baseline_y = y + gfx_ttf_ascent;
        for (int row = 0; row < h; row++) {
            for (int col = 0; col < w; col++) {
                uint8_t alpha = bm[row * TTF_CACHE_W + col];
                /* V0.13.8 S1: 软渲染 TTF 对比度增强 — 核心笔画(>=192)加实,
                 * 边缘(<=64)弱化, 中段拉伸: 消除小字号"发灰/笔画淡"的粗糙感 */
                if (alpha <= 64) alpha = (uint8_t)(alpha * 3 / 4);
                else if (alpha >= 192) alpha = 255;
                else alpha = (uint8_t)(128 + (alpha - 64) * 127 / 128);
                if (alpha > 0) {
                    int px = x + bx + col;
                    int py = baseline_y - by + row;
                    if (px >= 0 && py >= 0 && (uint32_t)px < gfx_width && (uint32_t)py < gfx_height) {
                        if (alpha >= 240) {
                            gfx64_put_pixel(px, py, color);
                        } else {
                            uint32_t bg = gfx64_get_pixel(px, py);
                            uint8_t fr = (color >> 16) & 0xFF;
                            uint8_t fg = (color >> 8) & 0xFF;
                            uint8_t fb = color & 0xFF;
                            uint8_t br = (bg >> 16) & 0xFF;
                            uint8_t bg_ = (bg >> 8) & 0xFF;
                            uint8_t bb = bg & 0xFF;
                            uint8_t r = fr * alpha / 255 + br * (255 - alpha) / 255;
                            uint8_t g = fg * alpha / 255 + bg_ * (255 - alpha) / 255;
                            uint8_t b = fb * alpha / 255 + bb * (255 - alpha) / 255;
                            gfx64_put_pixel(px, py, (r << 16) | (g << 8) | b);
                        }
                    }
                }
            }
        }
        /* V0.13.8 P7: 用 hmtx advance 宽度（真实字距），替代位图宽+1。
         * 一致性保护: advance 至少覆盖字形右边缘+1px (bx可能为负, 用右缘= x+bx+w+1),
         * 与渲染定位同一取整规则, 避免小字号下字形重叠/间隙抖动 */
        extern int ttf64_get_char_advance(uint32_t ch, int pixel_size);
        extern int ttf64_get_cjk_advance(uint32_t ch, int pixel_size);
        int adv = (cp < 128) ? ttf64_get_char_advance(cp, gfx_ttf_size)
                             : ttf64_get_cjk_advance(cp, gfx_ttf_size);
        int right_edge = bx + w + 1;
        if (adv < right_edge) adv = right_edge;
        return adv;
    }
    /* CJK/ASCII 失败时的宽度兜底 */
    return (cp < 128) ? 8 : (gfx_ttf_size + 1);
}

/* UTF-8 解码：返回码点并推进指针 */
static uint32_t gfx64_utf8_next(const char **p)
{
    const uint8_t *s = (const uint8_t *)*p;
    uint32_t cp = s[0];
    if (cp < 0x80) { *p += 1; return cp; }
    if ((cp & 0xE0) == 0xC0 && s[1]) {
        cp = ((cp & 0x1F) << 6) | (s[1] & 0x3F);
        *p += 2; return cp;
    }
    if ((cp & 0xF0) == 0xE0 && s[1] && s[2]) {
        cp = ((cp & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *p += 3; return cp;
    }
    if ((cp & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
        cp = ((cp & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        *p += 4; return cp;
    }
    *p += 1;
    return cp;
}

/* 设置TrueType字体大小 */
void gfx64_set_font_size(int size)
{
    if (size >= 8 && size <= 72) {
        gfx_ttf_size = size;
        /* 缓存ascent，避免渲染时重复调用浮点函数 */
        int descent, line_gap;
        ttf64_get_metrics(size, &gfx_ttf_ascent, &descent, &line_gap);
    }
}

/* 启用/禁用TrueType字体 */
void gfx64_enable_ttf(int enable)
{
    gfx_use_ttf = enable && ttf64_is_loaded();
}

/* 查询TrueType字体是否启用 */
int gfx64_is_ttf_enabled_probe_counter;
int gfx64_is_ttf_enabled(void)
{
    return gfx_use_ttf;
}

/* 绘制字符串 */
void gfx64_put_string(int x, int y, const char *str, uint32_t color)
{
    if (!gfx_initialized || !str) return;

    /* 保存中断状态并禁用中断，防止FPU状态被中断破坏
       使用pushfq/popfq而非简单cli/sti，避免嵌套时外层cli被内层sti破坏 */
    uint64_t rflags_save;
    __asm__ volatile("pushfq; popq %0" : "=r"(rflags_save));
    __asm__ volatile("cli");

    int cx = x;
    int line_height = gfx_use_ttf ? gfx_ttf_size + 2 : 16;
    while (*str) {
        if (*str == '\n') {
            cx = x;
            y += line_height;
            str++;
        } else if (*str == ' ') {
            /* 空格：直接移动半角宽度 */
            cx += (gfx_use_ttf && ttf64_is_loaded()) ? gfx_ttf_size / 2 : 8;
            str++;
        } else {
            /* UTF-8 解码（支持 CJK 3 字节序列） */
            uint32_t cp = gfx64_utf8_next(&str);
            int w;
            if (cp < 128) {
                w = gfx64_put_char(cx, y, (char)cp, color);
            } else if (gfx_use_ttf && ttf64_is_loaded()) {
                w = gfx64_put_ttf(cx, y, cp, color);
            } else {
                /* 无TTF时 CJK 显示为占位框 */
                w = 8;
            }
            cx += w;
        }
    }

    /* 只在之前中断启用时才恢复sti */
    if (rflags_save & (1ULL << 9)) {
        __asm__ volatile("sti");
    }
}

/* 清屏 */
void gfx64_clear(uint32_t color)
{
    if (!gfx_initialized) return;
    gfx64_fill_rect(0, 0, gfx_width, gfx_height, color);
}

/* 获取图形信息 */
void gfx64_get_info(int *width, int *height, int *bpp)
{
    if (width) *width = gfx_width;
    if (height) *height = gfx_height;
    if (bpp) *bpp = gfx_bpp;
}

/* 获取宽度 */
int gfx64_get_width(void)
{
    return gfx_width;
}

/* 获取高度 */
int gfx64_get_height(void)
{
    return gfx_height;
}

/* 获取当前渲染目标指针（静态层渲染时为背景缓冲） */
uint8_t *gfx64_get_framebuffer(void)
{
    return gfx_target ? gfx_target : front_buffer;
}

/* 绘制矩形边框 */
void gfx64_draw_rect(int x, int y, int w, int h, uint32_t color)
{
    if (!gfx_initialized) return;
    /* 上边 */
    gfx64_fill_rect(x, y, w, 1, color);
    /* 下边 */
    gfx64_fill_rect(x, y + h - 1, w, 1, color);
    /* 左边 */
    gfx64_fill_rect(x, y, 1, h, color);
    /* 右边 */
    gfx64_fill_rect(x + w - 1, y, 1, h, color);
}

/* 圆角 AA 辅助：圆心(cx,cy)半径r，角区域 x∈[ax0,ax1] y∈[ay0,ay1]
 * fill_mode=1: 实心+外缘1-2px alpha混合；=0: 仅环带(描边)+边缘混合 */
static void rounded_corner_aa(int cx, int cy, int ax0, int ax1, int ay0, int ay1,
                              int r, uint32_t color, int fill_mode)
{
    /* V0.13.8 P5.1: SDF式圆角AA — 内部 (r-2)..r 与外部 r..(r+2) 对称渐变,
     * alpha = clamp(255 - |dist-r| * 128):
     *   - 内部 2px 由背景平滑过渡到纯色 (消除纯色到渐变的"方切")
     *   - 外部 2px 内衰减到 0 (消除旧 96 系数在 r+1..r+2 残留的半透明"灰突边")
     *   - 小半径(r=2~6)下过渡带更匀称, 低分辨率/放大观看不再显锯齿 */
    int r2 = r * r;
    /* V0.13.8 P5.1: 小半径自适应过渡带 — 小圆角(r<=6, 按钮/勾选/高亮块)
     * 在低分辨率/放大观看时加宽到 r+3 且系数 96 更柔, 避免"方切/突角";
     * 大圆角(r>=7, 窗口/卡片)用紧凑 r+2 + 128, 边缘干净 */
    int r2o = (r <= 6) ? (r + 3) * (r + 3) : (r + 2) * (r + 2);
    int r2i = (r > 2) ? (r - 2) * (r - 2) : 0;
    int ak = (r <= 6) ? 96 : 128;
    for (int dy = ay0; dy <= ay1; dy++) {
        for (int dx = ax0; dx <= ax1; dx++) {
            int d2 = dx * dx + dy * dy;
            if (fill_mode) {
                if (d2 <= r2i) { gfx64_put_pixel(cx + dx, cy + dy, color); continue; }
                if (d2 > r2o) continue;
            } else {
                if (d2 < r2i || d2 > r2o) continue;
                if (d2 <= r2) { gfx64_put_pixel(cx + dx, cy + dy, color); continue; }
            }
            /* 边缘 alpha：距圆边越远越透明 (内+外平滑, 128系数 2px 内收敛) */
            uint32_t dist = math64_isqrt((uint32_t)d2);
            int delta = (int)dist - r;
            int alpha = 255 - (delta < 0 ? -delta : delta) * ak;
            if (alpha < 0) alpha = 0;
            if (alpha > 255) alpha = 255;
            gfx64_blend_pixel(cx + dx, cy + dy, color, alpha);
        }
    }
}

/* 填充圆角矩形（V0.13.3 AA角） */
void gfx64_fill_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color)
{
    /* P19.2: 统一到 vg64 矢量管线 (圆角路径 + 扫描线抗锯齿) */
    if (!gfx_initialized) return;
    vg_fill_round_rect(x, y, w, h, radius, color);
}

/* OBS-03: 圆角填充（圆方程 + 边缘 AA）
 * 与 gfx64_draw_rounded_rect 共用 rounded_corner_aa，填充与描边因此是同一条
 * 曲线 —— 窗口背景和它自己的边框不会各走各的。 */
void gfx64_fill_round_rect_aa(int x, int y, int w, int h, int radius, uint32_t color)
{
    if (!gfx_initialized) return;
    if (w <= 0 || h <= 0) return;
    if (radius < 0) radius = 0;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    if (radius == 0) { gfx64_fill_rect(x, y, w, h, color); return; }

    /* 中间竖条 + 左右两条 */
    gfx64_fill_rect(x + radius, y, w - 2 * radius, h, color);
    gfx64_fill_rect(x, y + radius, radius, h - 2 * radius, color);
    gfx64_fill_rect(x + w - radius, y + radius, radius, h - 2 * radius, color);

    /* 四角（以末像素坐标定位圆心，与描边保持一致） */
    rounded_corner_aa(x + radius,         y + radius,         -radius - 2, 0, -radius - 2, 0, radius, color, 1);
    rounded_corner_aa(x + w - 1 - radius, y + radius,         0, radius + 2, -radius - 2, 0, radius, color, 1);
    rounded_corner_aa(x + radius,         y + h - 1 - radius, -radius - 2, 0, 0, radius + 2, radius, color, 1);
    rounded_corner_aa(x + w - 1 - radius, y + h - 1 - radius, 0, radius + 2, 0, radius + 2, radius, color, 1);
}

/* 绘制圆角矩形边框（V0.13.3 AA角） */
void gfx64_draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color)
{
    if (!gfx_initialized) return;
    if (radius < 0) radius = 0;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;

    /* 四条边 —— 矩形范围按 [x..x+W]、[y..y+H] 计（W/H 为末像素坐标），
     * 四角圆弧的圆心也落在同一套坐标上。之前直边用 h-1 而弧按 h 展开，
     * 底部两角会多出 1px 的直角凸起（表现为"弧外还留着一小块方角"），
     * 顶部两角则是接缝处出现缺口 —— 统一坐标后弧与直边首尾相接。 */
    int W = w - 1, H = h - 1;
    gfx64_fill_rect(x + radius, y,     W - 2 * radius, 1, color);
    gfx64_fill_rect(x + radius, y + H, W - 2 * radius, 1, color);
    gfx64_fill_rect(x,          y + radius, 1, H - 2 * radius, color);
    gfx64_fill_rect(x + W,      y + radius, 1, H - 2 * radius, color);

    /* 四角 AA 环带 */
    rounded_corner_aa(x + radius,     y + radius,     -radius - 2, 0, -radius - 2, 0, radius, color, 0);
    rounded_corner_aa(x + W - radius, y + radius,     0, radius + 2, -radius - 2, 0, radius, color, 0);
    rounded_corner_aa(x + radius,     y + H - radius, -radius - 2, 0, 0, radius + 2, radius, color, 0);
    rounded_corner_aa(x + W - radius, y + H - radius, 0, radius + 2, 0, radius + 2, radius, color, 0);
}

/* 毛玻璃效果：模糊指定区域并叠加半透明颜色 */
void gfx64_blur_region(int x, int y, int w, int h, int blur_radius, uint32_t overlay_color, int overlay_alpha)
{
    if (!gfx_initialized) return;
    if (blur_radius < 1) blur_radius = 1;

    /* 盒式模糊：对每个像素取周围blur_radius范围内的平均 */
    for (int py = y; py < y + h && (uint32_t)py < gfx_height; py++) {
        for (int px = x; px < x + w && (uint32_t)px < gfx_width; px++) {
            int r = 0, g = 0, b = 0, count = 0;
            for (int dy = -blur_radius; dy <= blur_radius; dy++) {
                for (int dx = -blur_radius; dx <= blur_radius; dx++) {
                    int sx = px + dx;
                    int sy = py + dy;
                    if (sx >= 0 && sy >= 0 && (uint32_t)sx < gfx_width && (uint32_t)sy < gfx_height) {
                        uint32_t c = gfx64_get_pixel(sx, sy);
                        r += (c >> 16) & 0xFF;
                        g += (c >> 8) & 0xFF;
                        b += c & 0xFF;
                        count++;
                    }
                }
            }
            if (count > 0) {
                r /= count; g /= count; b /= count;
            }
            /* 叠加半透明颜色 */
            if (overlay_alpha > 0) {
                int or_ = (overlay_color >> 16) & 0xFF;
                int og = (overlay_color >> 8) & 0xFF;
                int ob = overlay_color & 0xFF;
                r = (r * (255 - overlay_alpha) + or_ * overlay_alpha) / 255;
                g = (g * (255 - overlay_alpha) + og * overlay_alpha) / 255;
                b = (b * (255 - overlay_alpha) + ob * overlay_alpha) / 255;
            }
            gfx64_put_pixel(px, py, (r << 16) | (g << 8) | b);
        }
    }
}

/* 从bootloader传递的固定地址读取VBE信息 */
/* V0.13.60: 早期帧缓冲文本镜像激活(UEFI GOP 下 0xB8000 不可见, 启动日志画到 fb)
 * 在 vmm64_init 之后调用: 读 0x6000(GOP fb 信息, UEFI引导器写入) -> 映射 fb -> 激活 */
void early_fb_text_init_from_bootloader(void)
{
    /* VBE/GOP信息存储在0x6000: w(2)h(2)pitch(2)bpp(1) + fb(4) + fb_hi(4) */
    volatile uint16_t *v = (volatile uint16_t *)0x6000;
    uint16_t w = v[0], h = v[1], pitch = v[2];
    uint8_t bpp = *(volatile uint8_t *)(0x6000 + 6);
    uint64_t fb = (uint64_t)*(volatile uint32_t *)(0x6000 + 8)
                | ((uint64_t)*(volatile uint32_t *)(0x6000 + 12) << 32);
    if (w == 0 || h == 0 || fb == 0) {
        vga_puts("[GFX64] early fb: no info\n");
        return;
    }
    #define PAGE_PRESENT 0x1
    #define PAGE_WRITE   0x2
    /* 映射帧缓冲到内核页表(identity, 可能 >256MB) */
    uint64_t fb_size = (uint64_t)h * pitch;
    uint64_t pml4_addr;
    asm volatile("mov %%cr3, %0" : "=r"(pml4_addr));
    extern int vmm64_map_page(uint64_t pml4_addr, uint64_t virt, uint64_t phys, uint64_t flags);
    for (uint64_t off = 0; off < fb_size; off += 4096) {
        vmm64_map_page(pml4_addr, (uint64_t)fb + off, (uint64_t)fb + off,
                       PAGE_PRESENT | PAGE_WRITE);
    }
    extern void early_fb_text_init(uint8_t *fb, int w, int h, int pitch, int bpp);
    early_fb_text_init((uint8_t *)(uint64_t)fb, w, h, pitch, bpp);
    vga_puts("[GFX64] early fb text mirror active: ");
    vga_put_hex(w);
    vga_puts("x");
    vga_put_hex(h);
    vga_puts("\n");
}

int gfx64_init_from_bootloader(void)
{
    /* VBE/GOP信息存储在0x6000: w(2)h(2)pitch(2)bpp(1) + fb(4) + fb_hi(4) */
    volatile uint16_t *vbe_info = (volatile uint16_t *)0x6000;
    uint16_t width = vbe_info[0];
    uint16_t height = vbe_info[1];
    uint16_t pitch = vbe_info[2];
    uint8_t bpp = *(volatile uint8_t *)(0x6000 + 6);
    uint64_t fb = (uint64_t)*(volatile uint32_t *)(0x6000 + 8)
                | ((uint64_t)*(volatile uint32_t *)(0x6000 + 12) << 32);

    if (width == 0 || height == 0 || fb == 0) {
        vga_puts("[GFX64] No VBE info from bootloader\n");
        return -1;
    }

    /* 映射帧缓冲到内核页表（VMM重建页表后stage2的映射丢失） */
    uint64_t fb_size = (uint64_t)height * pitch;
    uint64_t pml4_addr;
    asm volatile("mov %%cr3, %0" : "=r"(pml4_addr));
    extern int vmm64_map_page(uint64_t pml4_addr, uint64_t virt, uint64_t phys, uint64_t flags);
    #define PAGE_PRESENT 0x1
    #define PAGE_WRITE   0x2
    for (uint64_t off = 0; off < fb_size; off += 4096) {
        vmm64_map_page(pml4_addr, (uint64_t)fb + off, (uint64_t)fb + off,
                       PAGE_PRESENT | PAGE_WRITE);
    }

    gfx64_set_mode(width, height, pitch, bpp, (uint8_t *)(uint64_t)fb);
    return gfx64_init();
}

/* 初始化图形子系统（使用VBE信息） */
int gfx64_init(void)
{
    spinlock_init(&g_gfx_lock);
    spinlock_init(&g_ttf_lock);
    vga_puts("[GFX64] Initializing graphics subsystem...\n");

    /* 默认使用VBE 1024x768x32（由bootloader设置） */
    /* 如果bootloader没有设置，使用VGA文本模式 */
    if (!gfx_initialized) {
        vga_puts("[GFX64] No VBE mode set, using text mode\n");
        return -1;
    }

    /* 初始化TrueType字体 */
    extern const unsigned char dejavu_sans_font_data[];
    extern const unsigned int dejavu_sans_font_data_len;
    extern int ttf64_init(const uint8_t *data, uint32_t size);
    if (ttf64_init(dejavu_sans_font_data, dejavu_sans_font_data_len) == 0) {
        vga_puts("[GFX64] TrueType font loaded (DejaVuSans subset)\n");
        gfx_use_ttf = 1;
    } else {
        vga_puts("[GFX64] TrueType font load failed, using bitmap\n");
    }

    /* V0.13.4: 加载中文字形（NotoSansCJK 子集, 已编入内核） */
    extern const uint8_t notosanscjk_font_data[];
    extern const uint32_t notosanscjk_font_data_len;
    extern int ttf64_init_cjk(const uint8_t *data, uint32_t size);
    if (ttf64_init_cjk(notosanscjk_font_data, notosanscjk_font_data_len) == 0) {
        vga_puts("[GFX64] CJK font loaded (NotoSansCJK subset)\n");
    } else {
        vga_puts("[GFX64] CJK font load failed\n");
    }

    /* 清屏为深蓝色 */
    gfx64_clear(0x000080);

    /* 绘制标题 */
    gfx64_put_string(10, 10, "HepuOS 64-bit Graphics", 0xFFFFFF);
    gfx64_put_string(10, 30, "VBE Linear Framebuffer", 0x00FF00);

    vga_puts("[OK] Graphics initialized\n");
    return 0;
}

/* ============================================================================
 * 矢量图元扩展（V0.13.1）
 * 直线 / 圆 / 弧 / 椭圆 / 多边形 / 贝塞尔 / 抗锯齿 / alpha 混合
 * 全部使用整数与定点运算，不依赖 FPU
 * ========================================================================== */

/* ---------- 像素 alpha 混合（source-over） ---------- */
void gfx64_blend_pixel(int x, int y, uint32_t fg, int alpha)
{
    if (!gfx_initialized) return;
    if (x < 0 || y < 0 || (uint32_t)x >= gfx_width || (uint32_t)y >= gfx_height) return;
    if (alpha <= 0) return;
    if (alpha >= 255) { gfx64_put_pixel(x, y, fg); return; }

    uint32_t bg = gfx64_get_pixel(x, y);
    int fa = alpha;
    int ba = 255 - alpha;
    uint8_t r = (uint8_t)((((fg >> 16) & 0xFF) * fa + ((bg >> 16) & 0xFF) * ba) >> 8);
    uint8_t g = (uint8_t)((((fg >> 8) & 0xFF) * fa + ((bg >> 8) & 0xFF) * ba) >> 8);
    uint8_t b = (uint8_t)(((fg & 0xFF) * fa + (bg & 0xFF) * ba) >> 8);
    gfx64_put_pixel(x, y, (r << 16) | (g << 8) | b);
}

/* ---------- 半透明矩形 ---------- */
void gfx64_fill_rect_alpha(int x, int y, int w, int h, uint32_t color, int alpha)
{
    if (!gfx_initialized) return;
    for (int j = y; j < y + h && (uint32_t)j < gfx_height; j++) {
        for (int i = x; i < x + w && (uint32_t)i < gfx_width; i++) {
            gfx64_blend_pixel(i, j, color, alpha);
        }
    }
}

/* ---------- 直线（Bresenham） ---------- */
void gfx64_draw_line(int x0, int y0, int x1, int y1, uint32_t color)
{
    if (!gfx_initialized) return;
    int dx = math64_abs(x1 - x0);
    int dy = -math64_abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        gfx64_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* ---------- 抗锯齿直线（Wu 算法） ---------- */
static void plot_aa(int x, int y, uint32_t color, int alpha)
{
    if (alpha <= 0) return;
    if (alpha > 255) alpha = 255;
    gfx64_blend_pixel(x, y, color, alpha);
}

void gfx64_draw_line_aa(int x0, int y0, int x1, int y1, uint32_t color)
{
    if (!gfx_initialized) return;
    int steep = math64_abs(y1 - y0) > math64_abs(x1 - x0);
    if (steep) {
        int t; t = x0; x0 = y0; y0 = t;
        t = x1; x1 = y1; y1 = t;
    }
    if (x0 > x1) {
        int t; t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }
    int dx = x1 - x0;
    int dy = y1 - y0;
    if (dx == 0) dx = 1;
    int gradient = (dy << 16) / dx;  /* 16.16 斜率 */
    int y = y0;
    for (int x = x0; x <= x1; x++) {
        int y_int = y >> 16;
        int frac = y & 0xFFFF;
        int alpha = (frac >> 8) & 0xFF;
        int ialpha = 255 - alpha;
        if (steep) {
            plot_aa(y_int, x, color, ialpha);
            plot_aa(y_int + 1, x, color, alpha);
        } else {
            plot_aa(x, y_int, color, ialpha);
            plot_aa(x, y_int + 1, color, alpha);
        }
        y += gradient;
    }
}

/* ---------- 圆（中点算法） ---------- */
void gfx64_draw_circle(int cx, int cy, int r, uint32_t color)
{
    if (!gfx_initialized || r < 0) return;
    int x = r, y = 0;
    int err = 1 - r;
    while (x >= y) {
        gfx64_put_pixel(cx + x, cy + y, color);
        gfx64_put_pixel(cx - x, cy + y, color);
        gfx64_put_pixel(cx + x, cy - y, color);
        gfx64_put_pixel(cx - x, cy - y, color);
        gfx64_put_pixel(cx + y, cy + x, color);
        gfx64_put_pixel(cx - y, cy + x, color);
        gfx64_put_pixel(cx + y, cy - x, color);
        gfx64_put_pixel(cx - y, cy - x, color);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

/* ---------- 实心圆（水平线填充） ---------- */
void gfx64_fill_circle(int cx, int cy, int r, uint32_t color)
{
    if (!gfx_initialized || r < 0) return;
    for (int dy = -r; dy <= r; dy++) {
        int dx = math64_isqrt((uint32_t)(r * r - dy * dy));
        gfx64_draw_line(cx - dx, cy + dy, cx + dx, cy + dy, color);
    }
}

/* ---------- 椭圆（参数法，整数定点） ---------- */
void gfx64_draw_ellipse(int cx, int cy, int rx, int ry, uint32_t color)
{
    if (!gfx_initialized || rx < 1 || ry < 1) return;
    int n = 96;  /* 分段数 */
    int prev_x = cx + rx, prev_y = cy;
    for (int i = 1; i <= n; i++) {
        int deg = i * 360 / n;
        fixed_t c = math64_cos_deg(deg);
        fixed_t s = math64_sin_deg(deg);
        int nx = cx + FIX_TO_INT(FIX_MUL(INT_TO_FIX(rx), c));
        int ny = cy + FIX_TO_INT(FIX_MUL(INT_TO_FIX(ry), s));
        gfx64_draw_line(prev_x, prev_y, nx, ny, color);
        prev_x = nx; prev_y = ny;
    }
}

/* ---------- 圆弧（角度整数度，逆时针 0°=3点钟方向） ---------- */
void gfx64_draw_arc(int cx, int cy, int r, int a0, int a1, uint32_t color)
{
    if (!gfx_initialized || r < 0) return;
    /* 归一角度 */
    a0 = ((a0 % 360) + 360) % 360;
    a1 = ((a1 % 360) + 360) % 360;
    /* 步长随半径自适应，保证弧线平滑且不过度采样 */
    int step = 1;
    if (r > 40) step = 2;
    if (r > 100) step = 3;
    int prev_x = 0, prev_y = 0, have_prev = 0;
    int ang = a0;
    while (1) {
        fixed_t c = math64_cos_deg(ang);
        fixed_t s = math64_sin_deg(ang);
        int nx = cx + FIX_TO_INT(FIX_MUL(INT_TO_FIX(r), c));
        int ny = cy - FIX_TO_INT(FIX_MUL(INT_TO_FIX(r), s));
        if (have_prev) gfx64_draw_line(prev_x, prev_y, nx, ny, color);
        prev_x = nx; prev_y = ny; have_prev = 1;
        if (ang == a1) break;
        ang = (ang + step) % 360;
        /* 跨过终点 */
        if (a0 < a1) { if (ang > a1 || ang < a0) ang = a1; }
        else { if (ang > a1 && ang < a0) ang = a1; }
    }
}

/* ---------- 多边形填充（扫描线，偶数-奇数规则） ---------- */
void gfx64_fill_polygon(const int *xs, const int *ys, int n, uint32_t color)
{
    /* P19.2: 统一到 vg64 矢量管线 */
    if (!gfx_initialized || n < 3) return;
    vg_fill_polygon(xs, ys, n, color);
    return;
#if 0
    int ymin = ys[0], ymax = ys[0];
    for (int i = 1; i < n; i++) {
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
    }
    if (ymin < 0) ymin = 0;
    if (ymax >= gfx_height) ymax = gfx_height - 1;

    for (int y = ymin; y <= ymax; y++) {
        int xsect[32];
        int cnt = 0;
        for (int i = 0; i < n && cnt < 32; i++) {
            int j = (i + 1) % n;
            int yi = ys[i], yj = ys[j];
            if ((yi <= y && yj > y) || (yj <= y && yi > y)) {
                /* 线段与扫描线的交点 x（整数除法，避免浮点） */
                int num = (y - yi) * (xs[j] - xs[i]);
                int den = yj - yi;
                xsect[cnt++] = xs[i] + num / den;
            }
        }
        /* 排序交点 */
        for (int a = 0; a < cnt - 1; a++) {
            for (int b = a + 1; b < cnt; b++) {
                if (xsect[b] < xsect[a]) {
                    int t = xsect[a]; xsect[a] = xsect[b]; xsect[b] = t;
                }
            }
        }
        /* 成对填充 */
        for (int a = 0; a + 1 < cnt; a += 2) {
            int x0 = xsect[a], x1 = xsect[a + 1];
            gfx64_draw_line(x0, y, x1, y, color);
        }
    }
#endif
}

/* ---------- 三次贝塞尔曲线 ---------- */
void gfx64_draw_bezier3(int x0, int y0, int x1, int y1,
                        int x2, int y2, int x3, int y3, uint32_t color)
{
    if (!gfx_initialized) return;
    int steps = 48;
    int prev_x = x0, prev_y = y0;
    for (int i = 1; i <= steps; i++) {
        fixed_t t = FIX_DIV(INT_TO_FIX(i), INT_TO_FIX(steps));
        fixed_t mt = FIX_ONE - t;
        fixed_t a = FIX_MUL(FIX_MUL(mt, mt), mt);
        fixed_t b = FIX_MUL(INT_TO_FIX(3), FIX_MUL(FIX_MUL(mt, mt), t));
        fixed_t c = FIX_MUL(INT_TO_FIX(3), FIX_MUL(mt, FIX_MUL(t, t)));
        fixed_t d = FIX_MUL(FIX_MUL(t, t), t);
        int nx = FIX_TO_INT(FIX_MUL(INT_TO_FIX(x0), a) + FIX_MUL(INT_TO_FIX(x1), b) +
                            FIX_MUL(INT_TO_FIX(x2), c) + FIX_MUL(INT_TO_FIX(x3), d));
        int ny = FIX_TO_INT(FIX_MUL(INT_TO_FIX(y0), a) + FIX_MUL(INT_TO_FIX(y1), b) +
                            FIX_MUL(INT_TO_FIX(y2), c) + FIX_MUL(INT_TO_FIX(y3), d));
        gfx64_draw_line(prev_x, prev_y, nx, ny, color);
        prev_x = nx; prev_y = ny;
    }
}

/* ============================================================================
 * V0.13.2: 图形库完善 - 抗锯齿圆/弧/实心圆 + 线性渐变
 * 参考: 现代桌面(Windows/macOS)图标边缘柔和处理, 全整数定点, 无浮点
 * ==========================================================================*/

/* ---------- 抗锯齿圆环（距离采样, 边缘 alpha 混合） ---------- */
void gfx64_draw_circle_aa(int cx, int cy, int r, uint32_t color)
{
    if (!gfx_initialized || r < 0) return;
    int x0 = cx - r - 1, x1 = cx + r + 1;
    int y0 = cy - r - 1, y1 = cy + r + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if ((uint32_t)x1 >= gfx_width) x1 = (int)gfx_width - 1;
    if ((uint32_t)y1 >= gfx_height) y1 = (int)gfx_height - 1;
    int r2_in  = (r - 1) * (r - 1);
    int r2_out = (r + 1) * (r + 1);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            int dx = x - cx, dy = y - cy;
            int d2 = dx * dx + dy * dy;
            if (d2 < r2_in || d2 > r2_out) continue;
            uint32_t dist = math64_isqrt((uint32_t)d2);
            int delta = (dist > (uint32_t)r) ? (int)(dist - r) : (int)(r - dist);
            int alpha = 255 - delta * 128;
            if (alpha < 0) alpha = 0;
            if (alpha > 255) alpha = 255;
            gfx64_blend_pixel(x, y, color, alpha);
        }
    }
}

/* ---------- 抗锯齿实心圆（统一 vg64 矢量管线） ---------- */
void gfx64_fill_circle_aa(int cx, int cy, int r, uint32_t color)
{
    if (!gfx_initialized || r < 0) return;
    vg_fill_circle(cx, cy, r, color);
    return;
#if 0
    int r2 = r * r;
    int r2_out = (r + 1) * (r + 1);
    for (int dy = -r - 1; dy <= r + 1; dy++) {
        for (int dx = -r - 1; dx <= r + 1; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 < r2 || d2 > r2_out) continue;
            uint32_t dist = math64_isqrt((uint32_t)d2);
            int delta = (int)dist - r;
            int alpha = 255 - delta * 128;
            if (alpha < 0) alpha = 0;
            if (alpha > 255) alpha = 255;
            gfx64_blend_pixel(cx + dx, cy + dy, color, alpha);
        }
    }
#endif
}

/* ---------- 抗锯齿圆弧 V3: 1° 采样点 + Wu 抗锯齿线段连接
 * 每像素弧长约 1 段, AA 平滑且无角度步进锯齿; 每段一次除法, 性能远优于逐像素 atan2 ---------- */
void gfx64_draw_arc_aa(int cx, int cy, int r, int a0, int a1, uint32_t color)
{
    if (!gfx_initialized || r < 0) return;
    int a0n = ((a0 % 360) + 360) % 360;
    int a1n = ((a1 % 360) + 360) % 360;
    int prev_x = 0x7FFF0000, prev_y = 0x7FFF0000;
    int ang = a0n;
    int guard = 0;
    while (guard++ < 720) {
        fixed_t c = math64_cos_deg(ang);
        fixed_t s = math64_sin_deg(ang);
        int px = cx + FIX_TO_INT(FIX_MUL(INT_TO_FIX(r), c));
        int py = cy - FIX_TO_INT(FIX_MUL(INT_TO_FIX(r), s));
        if (prev_x != 0x7FFF0000 && (px != prev_x || py != prev_y)) {
            gfx64_draw_line_aa(prev_x, prev_y, px, py, color);
        }
        prev_x = px; prev_y = py;
        if (ang == a1n) break;
        ang = (ang + 1) % 360;
        if (a0n <= a1n) { if (ang > a1n || ang < a0n) ang = a1n; }
        else            { if (ang > a1n && ang < a0n) ang = a1n; }
    }
}

/* ---------- 线性渐变填充（vertical=1 垂直, 0 水平, 2 对角左上→右下; c1→c2） ---------- */
void gfx64_fill_rect_gradient(int x, int y, int w, int h,
                              uint32_t c1, uint32_t c2, int vertical)
{
    if (!gfx_initialized || w <= 0 || h <= 0) return;
    int r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    int r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    int den = vertical ? (h - 1) : (w - 1);
    if (den <= 0) den = 1;
    for (int j = y; j < y + h && (uint32_t)j < gfx_height; j++) {
        for (int i = x; i < x + w && (uint32_t)i < gfx_width; i++) {
            int t;
            if (vertical == 2) {        /* 对角：左上→右下 */
                t = (j - y) + (i - x);
                den = (h - 1) + (w - 1);
                if (den <= 0) den = 1;
            } else {
                t = vertical ? (j - y) : (i - x);
            }
            int a = (t * 255) / den;
            int ia = 255 - a;
            uint8_t r = (uint8_t)((r1 * ia + r2 * a) >> 8);
            uint8_t g = (uint8_t)((g1 * ia + g2 * a) >> 8);
            uint8_t b = (uint8_t)((b1 * ia + b2 * a) >> 8);
            gfx64_put_pixel(i, j, (r << 16) | (g << 8) | b);
        }
    }
}

/* ---------- 径向渐变圆（中心 c1 → 边缘 c2, 距离插值） ---------- */
void gfx64_fill_circle_gradient(int cx, int cy, int r, uint32_t c1, uint32_t c2)
{
    if (!gfx_initialized || r <= 0) return;
    int r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    int r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    int r2i = r * r;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 > r2i) continue;
            uint32_t dist = math64_isqrt((uint32_t)d2);
            int t = (int)(dist * 255 / (uint32_t)r);
            int ia = 255 - t;
            uint8_t rr = (uint8_t)((r1 * ia + r2 * t) >> 8);
            uint8_t gg = (uint8_t)((g1 * ia + g2 * t) >> 8);
            uint8_t bb = (uint8_t)((b1 * ia + b2 * t) >> 8);
            gfx64_put_pixel(cx + dx, cy + dy, (rr << 16) | (gg << 8) | bb);
        }
    }
    /* 边缘 AA 混合 */
    gfx64_fill_circle_aa(cx, cy, r, c2);
}

/* ============================================================================
 * V0.13.3: 鼠标光标 AA 渲染（标准箭头, 黑色描边 + 白色主体 + 边缘柔和）
 * 参考 Windows/macOS 指针：硬边靠1px描边+背景混合消除锯齿观感
 * ==========================================================================*/

/* 标准箭头模板：1=主体, 2=描边, 0=透明（12x18） */
void gfx64_draw_mouse_cursor(int x, int y, uint32_t color)
{
    if (!gfx_initialized) return;
    /* V0.54.12: 经典 "^" 形箭头指针 — 对称尖顶 + 短竖杆, 描边黑/填充浅色
     * 模板 12x18: 0=空 1=填充 2=描边。尖顶在 (5,0), 热点左移 5px 对齐尖端 */
    x -= 5;
    static const uint8_t ARROW[18][12] = {
        {0,0,0,0,0,2,0,0,0,0,0,0},
        {0,0,0,0,2,1,2,0,0,0,0,0},
        {0,0,0,2,1,1,1,2,0,0,0,0},
        {0,0,2,1,1,1,1,1,2,0,0,0},
        {0,2,1,1,1,1,1,1,1,2,0,0},
        {2,1,1,1,1,1,1,1,1,1,2,0},
        {2,1,1,1,1,1,1,1,1,1,1,2},
        {2,1,1,1,1,1,1,1,1,1,1,2},
        {2,2,1,1,1,1,1,1,1,1,2,2},
        {0,2,1,1,1,1,1,1,1,2,0,0},
        {0,0,2,1,1,1,1,1,2,0,0,0},
        {0,0,0,2,1,1,1,2,0,0,0,0},
        {0,0,0,2,1,1,1,2,0,0,0,0},
        {0,0,0,2,1,1,1,2,0,0,0,0},
        {0,0,0,2,1,1,1,2,0,0,0,0},
        {0,0,0,2,1,1,1,2,0,0,0,0},
        {0,0,0,2,1,1,1,2,0,0,0,0},
        {0,0,0,2,2,2,2,2,0,0,0,0},
    };
    for (int j = 0; j < 18; j++) {
        for (int i = 0; i < 12; i++) {
            uint8_t v = ARROW[j][i];
            if (v == 0) continue;
            if (v == 2) {
                /* 描边：黑色 */
                gfx64_put_pixel(x + i, y + j, 0x000000);
            } else {
                gfx64_put_pixel(x + i, y + j, color);
            }
        }
    }
    /* 边缘 AA：对模板外1px的4邻域像素做 96 alpha 混合, 柔化描边 */
    int dxs[4] = {1, -1, 0, 0};
    int dys[4] = {0, 0, 1, -1};
    for (int j = 0; j < 18; j++) {
        for (int i = 0; i < 12; i++) {
            if (ARROW[j][i] == 0) {
                for (int k = 0; k < 4; k++) {
                    int nx = i + dxs[k], ny = j + dys[k];
                    if (nx >= 0 && nx < 12 && ny >= 0 && ny < 18 && ARROW[ny][nx] != 0) {
                        gfx64_blend_pixel(x + i, y + j, 0x000000, 96);
                        break;
                    }
                }
            }
        }
    }
}

/* ============================================================================
 * V0.13.3: 图标库标准化 API（参考 Windows/GTK 图标库思路）
 * 通用矢量图标：WIFI / BATTERY / VOLUME / SETTINGS / POWER
 * 任何 UI（顶栏/控制中心/设置/启动器）复用同一绘制路径, 尺寸自适应
 * ==========================================================================*/
#define GFX64_ICON_WIFI      0
#define GFX64_ICON_BATTERY   1
#define GFX64_ICON_VOLUME    2
#define GFX64_ICON_SETTINGS  3
#define GFX64_ICON_POWER     4
#define GFX64_ICON_FOLDER    5   /* V0.13.7 P4: 文件夹 */
#define GFX64_ICON_FILE      6   /* V0.13.7 P4: 文件 */
/* V0.13.9 P11.1: 文件类型图标 */
#define GFX64_ICON_TEXT      7   /* 文本文档 */
#define GFX64_ICON_IMAGE     8   /* 图片 */
#define GFX64_ICON_AUDIO     9   /* 音频 */
#define GFX64_ICON_VIDEO     10  /* 视频 */
#define GFX64_ICON_ARCHIVE   11  /* 压缩包 */
#define GFX64_ICON_PDF       12  /* PDF 文档 */
#define GFX64_ICON_EXE       13  /* 可执行/程序 */
#define GFX64_ICON_CODE      14  /* 代码 */
#define GFX64_ICON_SHEET     15  /* 表格 */
#define GFX64_ICON_FONT      16  /* 字体 */
/* V0.13.9 P11.1: 系统状态/工具图标 */
#define GFX64_ICON_SEARCH    17  /* 放大镜 */
#define GFX64_ICON_REFRESH   18  /* 刷新 */
#define GFX64_ICON_HOME      19  /* 主目录 */
#define GFX64_ICON_UPLOAD    20  /* 上传 */
#define GFX64_ICON_DOWNLOAD  21  /* 下载 */
#define GFX64_ICON_SHARE     22  /* 共享 */
#define GFX64_ICON_TRASH     23  /* 回收站 */
#define GFX64_ICON_INFO      24  /* 信息 */
#define GFX64_ICON_WARNING   25  /* 警告 */
#define GFX64_ICON_ERROR     26  /* 错误 */
#define GFX64_ICON_CLOCK     27  /* 时钟 */
#define GFX64_ICON_CALENDAR  28  /* 日历 */
#define GFX64_ICON_LOCATION  29  /* GPS 定位 */
#define GFX64_ICON_WEATHER   30  /* 天气 */
#define GFX64_ICON_GLOBE     31  /* 多语言/地球 */
#define GFX64_ICON_NETWORK   32  /* 网络节点 */

/* P11.3: 图标辅助 — 2px 粗线 (小尺寸图标可辨的关键) */
static void icon_line2(int x1, int y1, int x2, int y2, uint32_t c)
{
    gfx64_draw_line_aa(x1, y1, x2, y2, c);
    gfx64_draw_line_aa(x1 + 1, y1, x2 + 1, y2, c);
}
static void icon_h2(int x1, int x2, int y, uint32_t c)
{
    gfx64_fill_rect(x1, y, x2 - x1 + 1, 2, c);
}
static void icon_v2(int x, int y1, int y2, uint32_t c)
{
    gfx64_fill_rect(x, y1, 2, y2 - y1 + 1, c);
}

void gfx64_icon_draw(int id, int x, int y, int size, uint32_t fg, uint32_t hole_bg, int param)
{
    if (!gfx_initialized || size < 8) return;
    switch (id) {
    case GFX64_ICON_WIFI: {
        /* V0.13.8 S4: WiFi 标准图标 — 底部圆点 + 3 条 2px 加粗同心弧,
         * 弧间距均匀(参考 Material/Windows 状态栏), 20px 下清晰可辨 */
        int cx = x + size / 2;
        int by = y + size - 2;
        gfx64_fill_circle_aa(cx, by - 1, 2, fg);
        for (int ring = 1; ring <= 3; ring++) {
            int r = ring * (size / 4) - 1;
            if (r < 4) r = 4;
            /* 2px 线宽: 双半径 AA 弧并排 (真距离 AA, 边缘平滑不糊) */
            gfx64_draw_arc_aa(cx, by, r, 20, 160, fg);
            gfx64_draw_arc_aa(cx, by, r + 1, 20, 160, fg);
        }
        break;
    }
    case GFX64_ICON_BATTERY: {
        int w = size - 3;
        int h = size / 2 + 2;
        int by = y + (size - h) / 2;
        gfx64_draw_rounded_rect(x, by, w, h, 2, fg);
        gfx64_fill_rect(x + w, by + h / 4, 2, h / 2, fg);
        int percent = (param >= 0 && param <= 100) ? param : 75;
        int fill_w = (w - 3) * percent / 100;
        if (fill_w > 0) {
            uint32_t fill_color = (percent > 50) ? 0x52C41A :
                                  (percent > 25) ? 0xFAAD14 : 0xEA6668;
            gfx64_fill_rounded_rect(x + 1, by + 1, fill_w, h - 2, 1, fill_color);
        }
        break;
    }
    case GFX64_ICON_VOLUME: {
        int cy = y + size / 2;
        int bx = x + 1;
        int bw = size / 3;
        int px[4] = { bx, bx, bx + bw, bx + bw };
        int py[4] = { cy - 1, cy + 1, cy - 4, cy + 4 };
        gfx64_fill_polygon(px, py, 4, fg);
        int qx[4] = { bx + bw, bx + bw, bx + bw + size / 4, bx + bw + size / 4 };
        int qy[4] = { cy - 3, cy + 3, cy - 5, cy + 5 };
        gfx64_fill_polygon(qx, qy, 4, fg);
        int arc_cx = bx + bw + size / 4;
        for (int i = 1; i <= 2; i++) {
            int r = size / 4 + i * (size / 6);
            gfx64_draw_arc_aa(arc_cx, cy, r, 310, 50, fg);
        }
        break;
    }
    case GFX64_ICON_SETTINGS: {
        int cx = x + size / 2;
        int cy = y + size / 2;
        int r = size / 2 - 2;
        gfx64_draw_circle_aa(cx, cy, r, fg);
        for (int i = 0; i < 8; i++) {
            int deg = i * 45;
            fixed_t c = math64_cos_deg(deg);
            fixed_t s = math64_sin_deg(deg);
            int x1 = cx + FIX_TO_INT(FIX_MUL(INT_TO_FIX(r - 1), c));
            int y1 = cy - FIX_TO_INT(FIX_MUL(INT_TO_FIX(r - 1), s));
            int x2 = cx + FIX_TO_INT(FIX_MUL(INT_TO_FIX(r + 1), c));
            int y2 = cy - FIX_TO_INT(FIX_MUL(INT_TO_FIX(r + 1), s));
            gfx64_draw_line_aa(x1, y1, x2, y2, fg);
        }
        gfx64_fill_circle_aa(cx, cy, r / 3, hole_bg);
        gfx64_draw_circle_aa(cx, cy, r / 3, fg);
        break;
    }
    case GFX64_ICON_POWER: {
        int cx = x + size / 2;
        int cy = y + size / 2 + 1;
        int r = size / 2 - 2;
        gfx64_draw_arc_aa(cx, cy, r, 105, 75, fg);
        gfx64_draw_line_aa(cx, cy - r - 1, cx, cy - r - 2 - r / 4, fg);
        break;
    }
    case GFX64_ICON_FOLDER: {
        /* 文件夹: 底部矩形 + 顶部小折角 */
        int w = size - 2, h = size - 3;
        gfx64_draw_rounded_rect(x, y + h / 3, w, h - h / 3, 2, fg);
        gfx64_draw_line_aa(x, y + h / 3, x + size / 3, y, fg);
        gfx64_draw_line_aa(x + size / 3, y, x + size / 2, y + h / 3, fg);
        break;
    }
    case GFX64_ICON_FILE: {
        /* 文件: 矩形 + 折角 */
        int w = size - 4, h = size - 3;
        gfx64_draw_rect(x + 1, y, w, h, fg);
        gfx64_draw_line_aa(x + w - h / 2, y, x + w - 1, y + h / 2 - 1, fg);
        gfx64_draw_line_aa(x + w - 1, y + h / 2 - 1, x + w - h / 2, y + h / 2 - 1, fg);
        break;
    }
    case GFX64_ICON_TEXT: {
        /* 文本文档: 蓝底徽章 + 白页 + 蓝横线 */
        int s = size;
        gfx64_fill_rounded_rect(x, y, s, s, s / 4, 0x4A90D9);
        gfx64_fill_rect(x + s / 4, y + s / 5, s / 2, s * 3 / 5, 0xFFFFFF);
        icon_h2(x + s / 4 + 1, x + s / 4 + s / 2 - 2, y + s / 3, 0x4A90D9);
        icon_h2(x + s / 4 + 1, x + s / 4 + s / 2 - 2, y + s / 2, 0x4A90D9);
        break;
    }
    case GFX64_ICON_IMAGE: {
        /* 图片: 绿底徽章 + 白太阳 + 白山 */
        int s = size;
        gfx64_fill_rounded_rect(x, y, s, s, s / 4, 0x5CB85C);
        gfx64_fill_circle_aa(x + s - s / 3, y + s / 3, s / 6, 0xFFFFFF);
        icon_line2(x + s / 5, y + s * 3 / 5, x + s / 3, y + s * 2 / 5, 0xFFFFFF);
        icon_line2(x + s / 3, y + s * 2 / 5, x + s / 2, y + s * 3 / 5, 0xFFFFFF);
        icon_line2(x + s / 2, y + s * 3 / 5, x + s - s / 5, y + s * 3 / 5, 0xFFFFFF);
        break;
    }
    case GFX64_ICON_AUDIO: {
        /* 音频: 橙底徽章 + 白音符 */
        int s = size;
        gfx64_fill_rounded_rect(x, y, s, s, s / 4, 0xE8A33D);
        gfx64_fill_circle_aa(x + s / 3, y + s * 3 / 5, s / 5, 0xFFFFFF);
        icon_v2(x + s / 3, y + s * 3 / 5, y + s / 4, 0xFFFFFF);
        icon_line2(x + s / 3, y + s / 4, x + s * 2 / 3, y + s / 5, 0xFFFFFF);
        icon_v2(x + s * 2 / 3, y + s / 5, y + s * 2 / 5, 0xFFFFFF);
        break;
    }
    case GFX64_ICON_VIDEO: {
        /* 视频: 红底徽章 + 白播放三角 */
        int s = size;
        gfx64_fill_rounded_rect(x, y, s, s, s / 4, 0xD9534F);
        int tx[3] = { x + s / 3, x + s / 3, x + s * 2 / 3 };
        int ty[3] = { y + s / 4, y + s * 3 / 4, y + s / 2 };
        gfx64_fill_polygon(tx, ty, 3, 0xFFFFFF);
        break;
    }
    case GFX64_ICON_ARCHIVE: {
        /* 压缩包: 棕底徽章 + 白盒 + 拉链 */
        int s = size;
        gfx64_fill_rounded_rect(x, y, s, s, s / 4, 0xA67C52);
        gfx64_draw_rounded_rect(x + s / 4, y + s / 4, s / 2, s / 2, 2, 0xFFFFFF);
        icon_h2(x + s / 4, x + s * 3 / 4, y + s / 4, 0xFFFFFF);
        icon_v2(x + s / 3, y + s / 4, y + s / 2, 0xFFFFFF);
        icon_v2(x + s * 2 / 3, y + s / 4, y + s / 2, 0xFFFFFF);
        icon_h2(x + s / 4, x + s * 3 / 4, y + s / 2, 0xFFFFFF);
        break;
    }
    case GFX64_ICON_PDF: {
        /* PDF: 红底徽章 + 白页 + 红 P */
        int s = size;
        gfx64_fill_rounded_rect(x, y, s, s, s / 4, 0xE05252);
        gfx64_fill_rect(x + s / 4, y + s / 5, s / 2, s * 3 / 5, 0xFFFFFF);
        icon_h2(x + s / 4 + 1, x + s / 4 + s / 2 - 2, y + s / 3, 0xE05252);
        icon_h2(x + s / 4 + 1, x + s / 4 + s / 2 - 2, y + s / 2, 0xE05252);
        break;
    }
    case GFX64_ICON_EXE: {
        /* 可执行: 灰底徽章 + 白齿轮 */
        int s = size;
        gfx64_fill_rounded_rect(x, y, s, s, s / 4, 0x7F8C8D);
        int cx = x + s / 2, cy = y + s / 2;
        gfx64_fill_circle_aa(cx, cy, s / 2 - 2, 0xFFFFFF);
        gfx64_fill_circle_aa(cx, cy, s / 2 - 4, 0x7F8C8D);
        for (int i = 0; i < 8; i++) {
            int deg = i * 45;
            fixed_t cc = math64_cos_deg(deg), sn = math64_sin_deg(deg);
            int x1 = cx + FIX_TO_INT(FIX_MUL(INT_TO_FIX(s / 2 - 2), cc));
            int y1 = cy - FIX_TO_INT(FIX_MUL(INT_TO_FIX(s / 2 - 2), sn));
            int x2 = cx + FIX_TO_INT(FIX_MUL(INT_TO_FIX(s / 2 - 1), cc));
            int y2 = cy - FIX_TO_INT(FIX_MUL(INT_TO_FIX(s / 2 - 1), sn));
            icon_line2(x1, y1, x2, y2, 0xFFFFFF);
        }
        gfx64_fill_circle_aa(cx, cy, s / 6, 0xFFFFFF);
        break;
    }
    case GFX64_ICON_CODE: {
        /* 代码: 青底徽章 + 白尖括号 */
        int s = size;
        gfx64_fill_rounded_rect(x, y, s, s, s / 4, 0x3F9E9E);
        int cy = y + s / 2;
        icon_line2(x + s / 5, cy - s / 4, x + s / 2, cy + s / 4, 0xFFFFFF);
        icon_line2(x + s / 5, cy + s / 4, x + s / 2, cy - s / 4, 0xFFFFFF);
        icon_line2(x + s * 4 / 5, cy - s / 4, x + s / 2, cy + s / 4, 0xFFFFFF);
        icon_line2(x + s * 4 / 5, cy + s / 4, x + s / 2, cy - s / 4, 0xFFFFFF);
        break;
    }
    case GFX64_ICON_SHEET: {
        /* 表格: 绿底徽章 + 白网格 */
        int s = size;
        gfx64_fill_rounded_rect(x, y, s, s, s / 4, 0x52A25C);
        gfx64_draw_rounded_rect(x + s / 4, y + s / 4, s / 2, s / 2, 1, 0xFFFFFF);
        icon_v2(x + s / 3, y + s / 4, y + s * 3 / 4, 0xFFFFFF);
        icon_v2(x + s * 2 / 3, y + s / 4, y + s * 3 / 4, 0xFFFFFF);
        icon_h2(x + s / 4, x + s * 3 / 4, y + s / 2, 0xFFFFFF);
        break;
    }
    case GFX64_ICON_FONT: {
        /* 字体: 紫底徽章 + 白 A */
        int s = size;
        gfx64_fill_rounded_rect(x, y, s, s, s / 4, 0x9B6BBF);
        int cy = y + s * 3 / 4;
        icon_line2(x + s / 4, cy, x + s / 2, y + s / 4, 0xFFFFFF);
        icon_line2(x + s * 3 / 4, cy, x + s / 2, y + s / 4, 0xFFFFFF);
        icon_h2(x + s / 2 - s / 6, x + s / 2 + s / 6, y + s / 2, 0xFFFFFF);
        break;
    }
    case GFX64_ICON_SEARCH: {
        /* 放大镜: 粗圆 + 粗手柄 */
        int s = size;
        gfx64_fill_circle_aa(x + s / 2 - 1, y + s / 2 - 1, s / 3, fg);
        gfx64_fill_circle_aa(x + s / 2 - 1, y + s / 2 - 1, s / 3 - 2, hole_bg);
        icon_line2(x + s / 2 + s / 6, y + s / 2 + s / 6, x + s - 2, y + s - 2, fg);
        break;
    }
    case GFX64_ICON_REFRESH: {
        /* 刷新: 粗弧 + 粗箭头 */
        int s = size;
        int cx = x + s / 2, cy = y + s / 2, r = s / 3;
        gfx64_draw_arc_aa(cx, cy, r, 40, 280, fg);
        gfx64_draw_arc_aa(cx, cy, r + 1, 40, 280, fg);
        int ex = cx + FIX_TO_INT(FIX_MUL(INT_TO_FIX(r), math64_cos_deg(40)));
        int ey = cy - FIX_TO_INT(FIX_MUL(INT_TO_FIX(r), math64_sin_deg(40)));
        gfx64_fill_polygon((int[3]){ ex, ex - 4, ex - 1 }, (int[3]){ ey, ey + 3, ey + 5 }, 3, fg);
        break;
    }
    case GFX64_ICON_HOME: {
        /* 主页: 粗屋顶 + 填充房身 */
        int s = size;
        gfx64_fill_rect(x + s / 5, y + s / 2, s * 3 / 5, s * 2 / 5, fg);
        icon_line2(x + s / 5, y + s / 2, x + s / 2, y + s / 6, fg);
        icon_line2(x + s * 4 / 5, y + s / 2, x + s / 2, y + s / 6, fg);
        break;
    }
    case GFX64_ICON_UPLOAD: {
        int s = size;
        int cy = y + s / 2;
        icon_v2(x + s / 2, y + s / 6, cy + s / 4, fg);
        gfx64_fill_polygon((int[3]){ x + s / 2 - s / 6, y + s / 6, x + s / 2 + s / 6 }, (int[3]){ y + s / 6, y + s / 6, y + s / 6 }, 3, fg);
        icon_line2(x + s / 2 - s / 6, cy, x + s / 2, y + s / 6, fg);
        icon_line2(x + s / 2 + s / 6, cy, x + s / 2, y + s / 6, fg);
        icon_h2(x + s / 6, x + s * 5 / 6, y + s - 2, fg);
        break;
    }
    case GFX64_ICON_DOWNLOAD: {
        int s = size;
        int cy = y + s / 2;
        icon_v2(x + s / 2, y + s / 6, cy + s / 4, fg);
        icon_line2(x + s / 2 - s / 6, cy - s / 4, x + s / 2, cy + s / 4, fg);
        icon_line2(x + s / 2 + s / 6, cy - s / 4, x + s / 2, cy + s / 4, fg);
        icon_h2(x + s / 6, x + s * 5 / 6, y + s - 2, fg);
        break;
    }
    case GFX64_ICON_SHARE: {
        int s = size;
        gfx64_fill_circle_aa(x + s / 6, y + s / 6, s / 8, fg);
        gfx64_fill_circle_aa(x + s * 5 / 6, y + s / 6, s / 8, fg);
        gfx64_fill_circle_aa(x + s / 2, y + s * 5 / 6, s / 8, fg);
        icon_line2(x + s / 6, y + s / 6, x + s * 5 / 6, y + s / 6, fg);
        icon_line2(x + s / 6, y + s / 6, x + s / 2, y + s * 5 / 6, fg);
        icon_line2(x + s * 5 / 6, y + s / 6, x + s / 2, y + s * 5 / 6, fg);
        break;
    }
    case GFX64_ICON_TRASH: {
        int s = size;
        int w = s - s / 3, h = s - s / 3;
        gfx64_fill_rounded_rect(x + s / 3, y + s / 6, s / 3, s / 10, 2, fg);
        gfx64_fill_rounded_rect(x + s / 6, y + s / 4, w, h, 2, fg);
        gfx64_fill_rect(x + s / 3, y + s / 4, s / 3, h, hole_bg);
        icon_v2(x + s / 3, y + s / 4, y + s - s / 6, fg);
        icon_v2(x + s / 2, y + s / 4, y + s - s / 6, fg);
        break;
    }
    case GFX64_ICON_INFO: {
        int s = size;
        gfx64_fill_circle_aa(x + s / 2, y + s / 2, s / 2 - 2, fg);
        gfx64_fill_circle_aa(x + s / 2, y + s / 2, s / 2 - 4, hole_bg);
        gfx64_fill_circle_aa(x + s / 2, y + s / 2 - s / 6, s / 10, hole_bg);
        icon_v2(x + s / 2, y + s / 2 - s / 12, y + s * 3 / 5, hole_bg);
        break;
    }
    case GFX64_ICON_WARNING: {
        int s = size;
        int cx = x + s / 2;
        int px[3] = { cx, x + s / 6, x + s * 5 / 6 };
        int py[3] = { y + s / 6, y + s * 5 / 6, y + s * 5 / 6 };
        gfx64_fill_polygon(px, py, 3, fg);
        gfx64_fill_rect(cx - s / 16, y + s / 3, s / 8, s / 6, hole_bg);
        gfx64_fill_circle_aa(cx, y + s * 2 / 3, s / 12, hole_bg);
        break;
    }
    case GFX64_ICON_ERROR: {
        int s = size;
        gfx64_fill_circle_aa(x + s / 2, y + s / 2, s / 2 - 2, fg);
        gfx64_fill_circle_aa(x + s / 2, y + s / 2, s / 2 - 4, hole_bg);
        icon_line2(x + s / 2 - s / 6, y + s / 2 - s / 6, x + s / 2 + s / 6, y + s / 2 + s / 6, hole_bg);
        icon_line2(x + s / 2 - s / 6, y + s / 2 + s / 6, x + s / 2 + s / 6, y + s / 2 - s / 6, hole_bg);
        break;
    }
    case GFX64_ICON_CLOCK: {
        int s = size;
        int cx = x + s / 2, cy = y + s / 2;
        gfx64_fill_circle_aa(cx, cy, s / 2 - 2, fg);
        gfx64_fill_circle_aa(cx, cy, s / 2 - 4, hole_bg);
        icon_v2(cx, cy - s / 6, cy - s / 4, hole_bg);
        icon_line2(cx, cy, cx + s / 5, cy + s / 6, hole_bg);
        break;
    }
    case GFX64_ICON_CALENDAR: {
        int s = size;
        gfx64_fill_rounded_rect(x + s / 6, y + s / 4, s * 2 / 3, s * 2 / 3, 2, fg);
        gfx64_fill_rect(x + s / 3, y + s / 4, s / 3, s / 6, hole_bg);
        gfx64_fill_rect(x + s / 3, y + s / 8, s / 12, s / 6, hole_bg);
        gfx64_fill_rect(x + s * 7 / 12, y + s / 8, s / 12, s / 6, hole_bg);
        break;
    }
    case GFX64_ICON_LOCATION: {
        int s = size;
        int cx = x + s / 2;
        int cy = y + s / 3;
        gfx64_fill_circle_aa(cx, cy, s / 4, fg);
        gfx64_fill_circle_aa(cx, cy, s / 4 - 2, hole_bg);
        gfx64_fill_polygon((int[3]){ cx - s / 4, cx + s / 4, cx }, (int[3]){ cy + s / 4, cy + s / 4, y + s - 2 }, 3, fg);
        break;
    }
    case GFX64_ICON_WEATHER: {
        /* 天气: 填充云 + 白太阳 */
        int s = size;
        int cx = x + s * 2 / 3, cy = y + s * 3 / 5;
        gfx64_fill_circle_aa(cx - s / 5, cy, s / 5, fg);
        gfx64_fill_circle_aa(cx + s / 6, cy - s / 10, s / 4, fg);
        gfx64_fill_rect(cx - s / 3, cy - s / 16, s * 2 / 3, s / 4, fg);
        int sx = x + s / 3, sy = y + s / 3;
        gfx64_fill_circle_aa(sx, sy, s / 7, fg);
        for (int i = 0; i < 6; i++) {
            int deg = i * 60;
            fixed_t cc = math64_cos_deg(deg), sn = math64_sin_deg(deg);
            int r1 = s / 7 + 1, r2 = s / 7 + 3;
            int x1 = sx + FIX_TO_INT(FIX_MUL(INT_TO_FIX(r1), cc));
            int y1 = sy - FIX_TO_INT(FIX_MUL(INT_TO_FIX(r1), sn));
            int x2 = sx + FIX_TO_INT(FIX_MUL(INT_TO_FIX(r2), cc));
            int y2 = sy - FIX_TO_INT(FIX_MUL(INT_TO_FIX(r2), sn));
            icon_line2(x1, y1, x2, y2, fg);
        }
        break;
    }
    case GFX64_ICON_GLOBE: {
        int s = size;
        int cx = x + s / 2, cy = y + s / 2, r = s / 2 - 2;
        gfx64_draw_circle_aa(cx, cy, r, fg);
        gfx64_draw_circle_aa(cx, cy, r + 1, fg);
        icon_h2(x + s / 4, x + s * 3 / 4, cy, fg);
        gfx64_draw_arc_aa(cx, cy, r / 2, 90, 270, fg);
        gfx64_draw_arc_aa(cx, cy, r / 2, 270, 90, fg);
        break;
    }
    case GFX64_ICON_NETWORK: {
        int s = size;
        int cy = y + s / 2;
        gfx64_fill_circle_aa(x + s / 6, cy, s / 8, fg);
        gfx64_fill_circle_aa(x + s * 5 / 6, cy, s / 8, fg);
        gfx64_fill_circle_aa(x + s / 2, y + s / 6, s / 8, fg);
        icon_line2(x + s / 6, cy, x + s / 2, y + s / 6, fg);
        icon_line2(x + s / 2, y + s / 6, x + s * 5 / 6, cy, fg);
        icon_h2(x + s / 6, x + s * 5 / 6, cy, fg);
        break;
    }
    default:
        break;
    }
}

/* P13: 位图图标渲染 — RGBA 源缩放至 (x,y,w,h), alpha 混合到当前 target
 * (Windows 11 风格图标由外部工具生成, 48x48 RGBA 数组嵌入) */
/* P16: 单色位掩码图标 blit (Win11 任务栏风格托盘图标, 最近邻缩放, 只写符号像素) */
void gfx64_blit_mask(int x, int y, int w, int h,
                     const uint32_t *rows, uint32_t color)
{
    if (!gfx_initialized || !rows || w <= 0 || h <= 0) return;
    uint8_t *fb = gfx64_get_framebuffer();
    int W = gfx_width, H = gfx_height;
    for (int dy = 0; dy < h; dy++) {
        int sy = dy * 32 / h;
        if (sy > 31) sy = 31;
        int yy = y + dy;
        if (yy < 0 || yy >= H) continue;
        uint32_t bits = rows[sy];
        for (int dx = 0; dx < w; dx++) {
            int sx = dx * 32 / w;
            if (sx > 31) sx = 31;
            if (!(bits & (1u << (31 - sx)))) continue;
            int xx = x + dx;
            if (xx < 0 || xx >= W) continue;
            uint32_t *dst = (uint32_t *)fb + yy * W + xx;
            *dst = color;
        }
    }
}

/* P18.1: 64x64 掩码 blit (每行 2×uint32: hi=左半, lo=右半; bit63=最左) */
void gfx64_blit_mask64(int x, int y, int w, int h,
                       const uint32_t *rows, uint32_t color)
{
    if (!gfx_initialized || !rows || w <= 0 || h <= 0) return;
    uint8_t *fb = gfx64_get_framebuffer();
    int W = gfx_width, H = gfx_height;
    for (int dy = 0; dy < h; dy++) {
        int sy = dy * 64 / h;
        if (sy > 63) sy = 63;
        int yy = y + dy;
        if (yy < 0 || yy >= H) continue;
        const uint32_t *r = &rows[sy * 2];
        for (int dx = 0; dx < w; dx++) {
            int sx = dx * 64 / w;
            if (sx > 63) sx = 63;
            uint32_t bits = (sx < 32) ? r[0] : r[1];
            if (!(bits & (1u << (31 - (sx & 31))))) continue;
            int xx = x + dx;
            if (xx < 0 || xx >= W) continue;
            uint32_t *dst = (uint32_t *)fb + yy * W + xx;
            *dst = color;
        }
    }
}

/* P19.5: 64x64 掩码 → 任意 w×h 抗锯齿混合 (4×4 子采样面积覆盖率 → alpha 混合) */
void gfx64_blit_mask64_aa(int x, int y, int w, int h,
                          const uint32_t *rows, uint32_t color)
{
    if (!gfx_initialized || !rows || w <= 0 || h <= 0) return;
    uint8_t *fb = gfx64_get_framebuffer();
    int W = gfx_width, H = gfx_height;
    uint8_t sr = (color >> 16) & 0xFF, sg = (color >> 8) & 0xFF, sb = color & 0xFF;
    for (int dy = 0; dy < h; dy++) {
        int yy = y + dy;
        if (yy < 0 || yy >= H) continue;
        for (int dx = 0; dx < w; dx++) {
            int xx = x + dx;
            if (xx < 0 || xx >= W) continue;
            int cov = 0;
            for (int j = 0; j < 4; j++) {
                int sy = (dy * 64 + j * 16) / h;
                if (sy > 63) sy = 63;
                const uint32_t *r = &rows[sy * 2];
                for (int i = 0; i < 4; i++) {
                    int sx = (dx * 64 + i * 16) / w;
                    if (sx > 63) sx = 63;
                    uint32_t bits = (sx < 32) ? r[0] : r[1];
                    if (bits & (1u << (31 - (sx & 31)))) cov++;
                }
            }
            if (cov == 0) continue;
            uint32_t *dst = (uint32_t *)fb + yy * W + xx;
            if (cov == 16) { *dst = color; continue; }
            uint32_t bg = *dst;
            uint32_t a = cov * 16;
            uint8_t dr = (uint8_t)((sr * a + ((bg >> 16) & 0xFF) * (255 - a)) >> 8);
            uint8_t dg = (uint8_t)((sg * a + ((bg >> 8) & 0xFF) * (255 - a)) >> 8);
            uint8_t db = (uint8_t)((sb * a + (bg & 0xFF) * (255 - a)) >> 8);
            *dst = (dr << 16) | (dg << 8) | db;
        }
    }
}

/* P18.2: 128x128 掩码 blit (每行 128bit = 4×uint32; word0=最左32bit, bit127=最左) */
void gfx64_blit_mask128(int x, int y, int w, int h,
                        const uint32_t *rows, uint32_t color)
{
    if (!gfx_initialized || !rows || w <= 0 || h <= 0) return;
    uint8_t *fb = gfx64_get_framebuffer();
    int W = gfx_width, H = gfx_height;
    for (int dy = 0; dy < h; dy++) {
        int sy = dy * 128 / h;
        if (sy > 127) sy = 127;
        int yy = y + dy;
        if (yy < 0 || yy >= H) continue;
        const uint32_t *r = &rows[sy * 4];
        for (int dx = 0; dx < w; dx++) {
            int sx = dx * 128 / w;
            if (sx > 127) sx = 127;
            uint32_t bits = r[sx >> 5];
            if (!(bits & (1u << (31 - (sx & 31))))) continue;
            int xx = x + dx;
            if (xx < 0 || xx >= W) continue;
            uint32_t *dst = (uint32_t *)fb + yy * W + xx;
            *dst = color;
        }
    }
}

/* P19.5: 128x128 掩码 → 任意 w×h 抗锯齿混合 (4×4 子采样覆盖率 → alpha 混合)
 * 位格式同 blit_mask128: 每行 4×uint32; sx>>5=word, bit31-(sx&31) 由左到右 */
void gfx64_blit_mask128_aa(int x, int y, int w, int h,
                           const uint32_t *rows, uint32_t color)
{
    if (!gfx_initialized || !rows || w <= 0 || h <= 0) return;
    uint8_t *fb = gfx64_get_framebuffer();
    int W = gfx_width, H = gfx_height;
    uint8_t sr = (color >> 16) & 0xFF, sg = (color >> 8) & 0xFF, sb = color & 0xFF;
    for (int dy = 0; dy < h; dy++) {
        int yy = y + dy;
        if (yy < 0 || yy >= H) continue;
        for (int dx = 0; dx < w; dx++) {
            int xx = x + dx;
            if (xx < 0 || xx >= W) continue;
            int cov = 0;
            for (int j = 0; j < 4; j++) {
                int sy = (dy * 128 + j * 32) / h;
                if (sy > 127) sy = 127;
                const uint32_t *r = &rows[sy * 4];
                for (int i = 0; i < 4; i++) {
                    int sx = (dx * 128 + i * 32) / w;
                    if (sx > 127) sx = 127;
                    if (r[sx >> 5] & (1u << (31 - (sx & 31)))) cov++;
                }
            }
            if (cov == 0) continue;
            uint32_t *dst = (uint32_t *)fb + yy * W + xx;
            if (cov == 16) { *dst = color; continue; }
            uint32_t bg = *dst;
            uint32_t a = cov * 16;
            uint8_t dr = (uint8_t)((sr * a + ((bg >> 16) & 0xFF) * (255 - a)) >> 8);
            uint8_t dg = (uint8_t)((sg * a + ((bg >> 8) & 0xFF) * (255 - a)) >> 8);
            uint8_t db = (uint8_t)((sb * a + (bg & 0xFF) * (255 - a)) >> 8);
            *dst = (dr << 16) | (dg << 8) | db;
        }
    }
}

/* P18.5: 24x24 抗锯齿 alpha blit (0=透明 255=不透明; 目标色 alpha 混合 → 平滑边缘) */
void gfx64_blit_alpha24(int x, int y, int w, int h,
                        const uint8_t *alpha, uint32_t color)
{
    if (!gfx_initialized || !alpha || w <= 0 || h <= 0) return;
    uint8_t *fb = gfx64_get_framebuffer();
    int W = gfx_width, H = gfx_height;
    uint8_t cr = (color >> 16) & 0xFF, cg = (color >> 8) & 0xFF, cb = color & 0xFF;
    for (int dy = 0; dy < h; dy++) {
        int sy = dy * 24 / h;
        if (sy > 23) sy = 23;
        int yy = y + dy;
        if (yy < 0 || yy >= H) continue;
        const uint8_t *row = &alpha[sy * 24];
        for (int dx = 0; dx < w; dx++) {
            int sx = dx * 24 / w;
            if (sx > 23) sx = 23;
            int a = row[sx];
            if (!a) continue;
            int xx = x + dx;
            if (xx < 0 || xx >= W) continue;
            uint32_t *dst = (uint32_t *)fb + yy * W + xx;
            if (a == 255) { *dst = color; continue; }
            uint32_t cur = *dst;
            uint8_t dr = (cur >> 16) & 0xFF, dg = (cur >> 8) & 0xFF, db = cur & 0xFF;
            uint8_t nr = (uint8_t)((dr * (255 - a) + cr * a) / 255);
            uint8_t ng = (uint8_t)((dg * (255 - a) + cg * a) / 255);
            uint8_t nb = (uint8_t)((db * (255 - a) + cb * a) / 255);
            *dst = ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | nb;
        }
    }
}

/* 不透明快速路径: 忽略 alpha 直接覆盖写。
 * 壁纸这类全屏不透明图每像素做 alpha 混合 + 除法在 QEMU 上慢到画不完一帧,
 * 表现为截图里的水平条纹(只刷了部分行)。 */
void gfx64_blit_rgba_opaque(int x, int y, int w, int h,
                            const uint8_t *rgba, int sw, int sh)
{
    if (!gfx_initialized || !rgba || w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return;
    uint32_t *fb = (uint32_t *)gfx64_get_framebuffer();
    int W = gfx_width, H = gfx_height;
    for (int dy = 0; dy < h; dy++) {
        int sy = dy * sh / h;
        if (sy >= sh) sy = sh - 1;
        int yy = y + dy;
        if (yy < 0 || yy >= H) continue;
        const uint8_t *row = rgba + (uint32_t)sy * sw * 4;
        uint32_t *dstrow = fb + (uint32_t)yy * W;
        for (int dx = 0; dx < w; dx++) {
            int sx = dx * sw / w;
            if (sx >= sw) sx = sw - 1;
            int xx = x + dx;
            if (xx < 0 || xx >= W) continue;
            const uint8_t *p = row + (uint32_t)sx * 4;
            dstrow[xx] = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        }
    }
}

void gfx64_blit_rgba(int x, int y, int w, int h,
                     const uint8_t *rgba, int sw, int sh)
{
    if (!gfx_initialized || !rgba || w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return;
    uint8_t *fb = gfx64_get_framebuffer();
    int W = gfx_width, H = gfx_height;
    for (int dy = 0; dy < h; dy++) {
        int sy = dy * sh / h;
        if (sy >= sh) sy = sh - 1;
        int yy = y + dy;
        if (yy < 0 || yy >= H) continue;
        for (int dx = 0; dx < w; dx++) {
            int sx = dx * sw / w;
            if (sx >= sw) sx = sw - 1;
            int xx = x + dx;
            if (xx < 0 || xx >= W) continue;
            const uint8_t *p = rgba + (sy * sw + sx) * 4;
            uint8_t a = p[3];
            if (a == 0) continue;
            uint32_t *dst = (uint32_t *)fb + yy * W + xx;
            uint32_t dr = (*dst >> 16) & 0xFF, dg = (*dst >> 8) & 0xFF, db = *dst & 0xFF;
            uint32_t sr = p[0], sg = p[1], sb = p[2];
            uint32_t r = (sr * a + dr * (255 - a)) / 255;
            uint32_t g = (sg * a + dg * (255 - a)) / 255;
            uint32_t b = (sb * a + db * (255 - a)) / 255;
            *dst = (r << 16) | (g << 8) | b;
        }
    }
}

/* ============================================================================
 * V0.13.5 P2: 帧同步 / 垂直同步抽象
 * 参考 Linux DRM vblank 机制: 真机可挂到显示 IRQ/查询帧缓冲状态
 * QEMU display-none 下无 vblank 硬件, 提供空实现保证接口兼容
 * ==========================================================================*/
static int g_vsync_enabled = 0;

/* 启用/禁用垂直同步等待 */
void gfx64_vsync_enable(int enable)
{
    g_vsync_enabled = enable ? 1 : 0;
}
int gfx64_vsync_is_enabled(void) { return g_vsync_enabled; }

/* 等待垂直消隐期。真机: 等待显示控制器 VSYNC 位/IRQ;
 * QEMU(无真实硬件): 空转一小段让出, 防止紧循环占用 CPU */
void gfx64_vsync_wait(void)
{
    if (!g_vsync_enabled) return;
    /* 占位: 真机挂接显示 IRQ 或轮询 MMIO 的 VSYNC 状态位 */
    __asm__ volatile("pause");
}

/* 帧节流: 限制渲染频率, 返回 1=应渲染 0=跳过 (自适应省CPU) */
static uint64_t g_last_frame_ticks = 0;
int gfx64_frame_throttle(void)
{
    extern uint64_t pit64_get_ticks(void);
    uint64_t now = pit64_get_ticks();
    /* 目标帧间隔: 16ms (60fps); PIT 频率由内核决定 */
    if (now - g_last_frame_ticks < 2) return 0;
    g_last_frame_ticks = now;
    return 1;
}
