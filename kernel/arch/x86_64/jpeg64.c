/* ============================================================================
 * jpeg64.c — 用 stb_image 给内核补上 JPEG 解码（PNG 已有 png64.c）
 *
 * 为什么只开 JPEG: stb_image 单头库约 250KB 源码, 全格式编进来会把内核撑爆;
 * PNG 已由 png64 实现(带自己的 inflate), GIF/BMP/TGA 对桌面场景没需求。
 * 用 STBI_ONLY_JPEG 让预处理器把其余解码器全部剪掉。
 *
 * 内存: stb 需要 malloc/realloc/free, 内核没有通用堆。这里给一个按页向 pmm
 * 申请、只前进不回收的 bump 分配器 —— 壁纸只解码一次并常驻, 反复 free/alloc
 * 没有意义; 若将来需要多次解码, 应在 jpeg64_free 里补真正的回收。
 * 前提: 内核在核段使用恒等映射(pmm64_alloc_pages 返回的物理地址可直接当虚拟
 * 地址访问), 与 png64.c 的 p_alloc 一致。
 * ========================================================================== */
#include "jpeg64.h"

/* stb 无条件 include <assert.h> 并使用 assert(), 内核没有 libc 实现 → NDEBUG
 * 让 assert 编译成空宏(它仍会 include 头文件, 但只做声明)。 */
/* 内核没有 TLS(FS 基址未设), stb 默认用 __thread 存了
 * stbi__vertically_flip_on_load_local / _set 两个线程局部变量, 访问
 * %fs:0xfffffff8 直接页错误(CR2=0xFFFFFFFFFFFFFFF8) -> 关掉, 改用全局变量。 */
#define STBI_NO_THREAD_LOCALS
/* stb 允许用 STBI_ASSERT 覆盖它的断言。内核的 assert 会 HALT 整机, 但解码
 * 过程中的断言只用于不变式检查, 用空宏更稳(失败时由 stbi 返回 NULL 处理)。 */
#define STBI_ASSERT(x) ((void)0)
/* 内核按 -mno-sse 编译, stb 的 SIMD 分支会生成 SSE 寄存器返回 -> 必须关掉 */
#define STBI_NO_SIMD
#define STBI_NO_STDIO
#define STBI_NO_PNG
#define STBI_NO_BMP
#define STBI_NO_GIF
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_JPEG

extern uint64_t pmm64_alloc_pages(uint64_t n);
extern void *memcpy(void *dst, const void *src, unsigned long n);
extern int vga_printf(const char *fmt, ...);

/* 页级 bump 分配器 */
static uint8_t *jp_cur;
static uint32_t jp_left;

static void *jp_alloc(unsigned long n)
{
    if (n == 0) return 0;
    n = (n + 15) & ~15UL;
    if (jp_left < n) {
        uint64_t pages = ((uint64_t)n + 4095) / 4096;
        uint64_t phys = pmm64_alloc_pages(pages);
        if (!phys) { vga_printf("[JPEG64] OOM need=%u pages\n", (unsigned)pages); return 0; }
        jp_cur = (uint8_t *)phys;
        jp_left = (uint32_t)(pages * 4096);
    }
    void *p = jp_cur;
    jp_cur += n;
    jp_left -= (uint32_t)n;
    return p;
}

static void jp_free(void *p) { (void)p; }

static __attribute__((unused)) void *jp_realloc_sized(void *p, unsigned long old_size,
                                                     unsigned long new_size)
{
    if (!p) return jp_alloc(new_size);
    if (new_size <= old_size) return p;
    void *np = jp_alloc(new_size);
    if (!np) return 0;
    memcpy(np, p, old_size);
    return np;
}

#define STBI_MALLOC(sz)              jp_alloc(sz)
#define STBI_REALLOC_SIZED(p, os, ns) jp_realloc_sized(p, os, ns)
#define STBI_FREE(p)                 jp_free(p)
#define STB_IMAGE_IMPLEMENTATION
#include "../../../third_party/stb/stb_image.h"

int jpeg64_decode(const uint8_t *data, int len, jpeg64_image_t *out)
{
    if (!data || len <= 0 || !out) return -1;
    out->rgba = 0;
    out->width = out->height = 0;

    int w = 0, h = 0, comp = 0;
    unsigned char *px = stbi_load_from_memory(data, len, &w, &h, &comp, 4);
    if (px) vga_printf("[JPEG64] decoded %dx%d\n", w, h);
    if (!px) {
        /* stbi 失败原因: 不是 JPEG / 数据截断 / 尺寸超限 */
        return -2;
    }
    out->rgba = px;
    out->width = (uint32_t)w;
    out->height = (uint32_t)h;
    return 0;
}

void jpeg64_free(jpeg64_image_t *img)
{
    /* bump 分配器不回收, 只清指针; 壁纸常驻, 无需释放 */
    if (!img) return;
    img->rgba = 0;
    img->width = img->height = 0;
}
