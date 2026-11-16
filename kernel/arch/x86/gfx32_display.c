/* 32 位 -> display_backend_t 桥接层。
 *
 * 为什么需要这一层（而不是把 32 位改成薄接口）:
 *   两边的后端不是同一种东西。32 位的 gfx_backend_t 是厚接口(11 字段),
 *   它提供三项薄接口给不了、但降级链非要不可的能力:
 *       has_double_buffer  —— 决定 present 走"拷贝"还是"无事可做"
 *       get_fb_paddr       —— 填 fb_desc_t，将来给 ring3 映射
 *       fill_rect/put_pixel—— 该路径下可加速的绘制实现(如 virtio 可下发)
 *   64 位的 display_backend_t 是薄接口, 只管"写屏"一件事, 因为它只有
 *   一个 lfb 后端, 不需要能力协商。
 *
 *   所以正确做法不是二选一, 而是让两者各做自己擅长的:
 *       探测与能力声明  -> gfx_backend_t（厚）
 *       提交契约        -> display_backend_t（薄）
 *   这个文件就是那条缝: 把 32 位探测出来的提交能力, 包成一份薄接口,
 *   让上游(桌面/合成层)只看见 3 个入口, 不必知道背后是 VBE 还是别的。
 *
 * 现状: 32 位唯一在管提交的是 VBE(其余路径按单缓冲直写), 所以先只桥
 * 接 VBE; 将来 virtio/GOP 要接进来, 在这里加一个 backend 即可, 上游
 * 代码不用动 —— 这正是这层存在的意义。
 */

#include "../../shared/gfx/display_backend.h"

extern uint32_t gfx_vbe_lfb_phys(void);
extern int gfx_backend_present(void);
extern int gfx_backend_present_region(int, int, int, int);

/* init: 32 位的缓冲绑定在 gfx_vbe_sync_mode / kernel_init 里已经完成,
 * 这里不需要重复设置。保留入口只为满足契约(让上层可以假装"绑定过一次")。 */
static int b32_init(int w, int h, int pitch, int bpp, uint8_t *fb)
{
    (void)w; (void)h; (void)pitch; (void)bpp;
    return fb ? 0 : -1;
}

static void b32_present(void)
{
    (void)gfx_backend_present();
}

static void b32_present_region(int x, int y, int w, int h)
{
    (void)gfx_backend_present_region(x, y, w, h);
}

static const display_backend_t vbe32_backend = {
    "vbe32", b32_init, b32_present, b32_present_region
};

static const display_backend_t *g_display32 = 0;

/* 绑定当前可用后端。返回 0 表示没有后端能接管提交(此时调用方应退回
 * 直写路径, 即 framebuffer 就是 LFB 的单缓冲模式)。 */
const display_backend_t *gfx32_display_bind(void)
{
    if (gfx_vbe_lfb_phys() < 0x10000000u) return 0;
    g_display32 = &vbe32_backend;
    return g_display32;
}

const display_backend_t *gfx32_display(void)
{
    return g_display32;
}
