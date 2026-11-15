/* 32 位 framebuffer 契约 (KERNEL_SCOPE §5-4 / DESKTOP_INTERFACE)
 *
 * 内核内桌面已下线, 桌面逻辑搬到用户态。用户态要画东西, 必须先知道:
 * 显存在哪、多大、一行多少字节、什么格式 —— 这一条 syscall 就答这些。
 *
 * 只回答"在哪", 不做映射。映射走 SYS_MMAP (37), 由桌面自己调。
 * flags 里 flag & FB_FLAG_BACKBUFFER 为 1 表示存在后台缓冲,
 * 为 0 表示此刻是单缓冲直写 —— 后者用户态直接写显存才安全。
 */

#include <stdint.h>

typedef struct {
    uint64_t phys;      /* 显存物理地址 */
    uint32_t width;
    uint32_t height;
    uint32_t pitch;     /* 字节/行 */
    uint32_t bpp;
    uint32_t format;    /* 1 = BGRX8888 */
    uint32_t flags;
} fb_desc32_t;

#define FB_FLAG_BACKBUFFER 0x1

extern int gfx_width;
extern int gfx_height;
extern int gfx_pitch;
extern int gfx_bpp;
extern void *bochs_dispi_fb_phys(void);
extern uint32_t copy_to_user(void *dst, const void *src, uint32_t n);

uint32_t sys_fb_desc(uint32_t user_buf, uint32_t unused1,
                     uint32_t unused2, uint32_t unused3)
{
    (void)unused1; (void)unused2; (void)unused3;
    if (!user_buf) return (uint32_t)-1;

    fb_desc32_t d;
    d.phys   = (uint64_t)(uintptr_t)bochs_dispi_fb_phys();
    d.width  = (uint32_t)gfx_width;
    d.height = (uint32_t)gfx_height;
    d.pitch  = (uint32_t)((gfx_width * gfx_bpp) / 8);
    d.bpp    = (uint32_t)gfx_bpp;
    d.format = (d.bpp == 32) ? 1u : 0u;
    /* 32 位走的是单缓冲直写 LFB, 没有后台缓冲 */
    d.flags  = 0u;

    if (copy_to_user((void *)(uintptr_t)user_buf, &d, sizeof(d)) != 0) {
        return (uint32_t)-1;
    }
    return 0;
}
