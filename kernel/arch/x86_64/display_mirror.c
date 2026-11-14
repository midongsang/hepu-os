/* ============================================================================
 * Hepu OS — 第二个显示后端：RAM 镜像（mirror）
 *
 * 它是干什么的：`display_backend.h` 刚建立时只有一个 lfb 实现，"接口可换"
 * 这件事没有被第二个实现检验过。这里补一个**完全不同的写屏目标** ——
 * 不写显存，写一块自己分配的 RAM 缓冲：
 *   1. 无显示环境（-display none / headless）也能跑完整桌面，帧落在内存里；
 *   2. 用它和 lfb 做**等价性自检**：同一帧分别经两个后端提交，读回比对 hash。
 *      这是这层抽象唯一的硬判据 —— 换后端必须画对。
 *
 * 注意它不是"硬件第二后端"。virtio-gpu / VBE 的移植是另一件事
 * （旧树 kernel/gfx/gfx_virtio.c 有 927 行可参考，但依赖旧头文件、未编进）。
 * 之所以先做 mirror：成本低、判据硬、不引入设备依赖，能先把接口的毛病照出来。
 * ========================================================================== */
#include <stdint.h>
#include "display_backend.h"

extern uint64_t pmm64_alloc_pages(uint64_t n);
extern uint8_t *gfx64_back_buffer(void);
extern uint32_t gfx64_width_px(void);
extern uint32_t gfx64_height_px(void);
extern uint32_t gfx64_pitch_bytes(void);

static uint8_t *m_buf = 0;
static uint32_t m_w = 0;
static uint32_t m_h = 0;
static uint32_t m_pitch = 0;

uint8_t *display_mirror_buffer(void) { return m_buf; }

static int mir_init(int w, int h, int pitch, int bpp, uint8_t *fb)
{
    (void)bpp; (void)fb;
    if (m_buf) return 0;
    if (w <= 0 || h <= 0) return -1;
    uint32_t st = (pitch > 0) ? (uint32_t)pitch : (uint32_t)w * 4u;
    uint64_t bytes = (uint64_t)st * (uint64_t)h;
    uint64_t pages = (bytes + 4095) / 4096;
    uint64_t phys = pmm64_alloc_pages(pages);
    if (!phys) return -1;
    m_buf = (uint8_t *)(uintptr_t)phys;   /* 恒等映射，可直接当虚拟地址用 */
    m_w = (uint32_t)w; m_h = (uint32_t)h; m_pitch = st;
    for (uint64_t i = 0; i < bytes; i++) m_buf[i] = 0;
    return 0;
}

static void mir_copy_region(int x, int y, int w, int h)
{
    uint8_t *src = gfx64_back_buffer();
    uint32_t sp = gfx64_pitch_bytes();
    if (!src || !m_buf || !sp || !m_pitch) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;
    if ((uint32_t)(x + w) > m_w) w = (int)m_w - x;
    if ((uint32_t)(y + h) > m_h) h = (int)m_h - y;
    for (int j = 0; j < h; j++) {
        uint8_t *d = m_buf + (uint64_t)(y + j) * m_pitch + (uint64_t)x * 4u;
        uint8_t *s = src + (uint64_t)(y + j) * sp + (uint64_t)x * 4u;
        for (int i = 0; i < w * 4; i++) d[i] = s[i];
    }
}

static void mir_present(void)
{
    mir_copy_region(0, 0, (int)m_w, (int)m_h);
}

static void mir_present_region(int x, int y, int w, int h)
{
    mir_copy_region(x, y, w, h);
}

const display_backend_t display_mirror_backend = {
    "mirror", mir_init, mir_present, mir_present_region
};
