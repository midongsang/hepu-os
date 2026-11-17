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
#include <process.h>
/* 统一源码树: framebuffer 可映射描述 32/64 共用同一份契约（KERNEL_SCOPE §5-4）。
 * 32 位曾在此文件内自带一份 fb_desc32_t, 字段与共享版逐字一致, 属纯重复。 */
#include "gfx/display_backend.h"

#define FB_FLAG_BACKBUFFER FB_DESC_FLAG_DOUBLE_BUFFERED

extern int gfx_width;
extern int gfx_height;
extern int gfx_pitch;
extern int gfx_bpp;
extern void *bochs_dispi_fb_phys(void);
extern uint32_t copy_to_user(void *dst, const void *src, uint32_t n);

/* --------------------------------------------------------------------------
 * §5-4 附: 前台所有权
 *
 * acquire 做两件事: ① 把显存物理地址经 sys_mmap 的 MAP_PHYS 路径映射进调用者
 * 的地址空间, 返回可写虚拟地址; ② 把"前台提交权"交给调用进程, 内核同时停止
 * 提交(hpdesk 的 hs_present 会看到 owner 已存在而直接返回)。
 *
 * 冲突返回 -EBUSY 而**不排队**: 前台只可能有一个, 排队等于让旧 owner 无限期
 * 占着屏幕。调用方拿到 -EBUSY 应当自己决定是等下一次还是放弃。
 *
 * 为什么必须先把映射做成: 如果只给所有权不给映射, 调用者拿着一个"我拥有前台"
 * 的凭证却写不进去任何像素 —— 那正是这个仓库反复出现过的假接口。
 * ------------------------------------------------------------------------ */
extern uint32_t sys_mmap(uint32_t, uint32_t, uint32_t, uint32_t);
extern int      desk_fb_owner_acquire(int);
extern void     desk_fb_owner_release(int);
extern int      desk_fb_owner_pid(void);
extern process_t *current;

uint32_t sys_fb_acquire(uint32_t hint_len, uint32_t unused1,
                        uint32_t unused2, uint32_t unused3)
{
    (void)unused1; (void)unused2; (void)unused3;

    if (!current) return 0;                      /* 内核上下文不参与前台所有权 */
    if (desk_fb_owner_pid() >= 0) return (uint32_t)-16;   /* -EBUSY, 不排队 */

    uint32_t total = (uint32_t)(gfx_width * gfx_height * (gfx_bpp / 8));
    if (hint_len && hint_len < total) total = hint_len;

    uint32_t phys = (uint32_t)(uintptr_t)bochs_dispi_fb_phys();
    if (!phys) return 0;                         /* 显存还没出现(boot 早期) */

    uint32_t virt = sys_mmap(phys, total, 0, 0x1000u /* MAP_PHYS */);
    if (!virt) return 0;                         /* 映射失败就不给所有权 */

    if (desk_fb_owner_acquire((int)current->pid) != 0) {
        return (uint32_t)-16;
    }

    {
        extern void serial_puts(uint16_t, const char *);
        serial_puts(0x3F8, "[FB] owner acquired by pid, kernel present paused\n");
    }
    return virt;
}

uint32_t sys_fb_release(uint32_t unused1, uint32_t unused2,
                        uint32_t unused3, uint32_t unused4)
{
    (void)unused1; (void)unused2; (void)unused3; (void)unused4;
    if (!current) return 0;
    desk_fb_owner_release((int)current->pid);
    {
        extern void serial_puts(uint16_t, const char *);
        serial_puts(0x3F8, "[FB] owner released, kernel present resumed\n");
    }
    return 0;
}

uint32_t sys_fb_desc(uint32_t user_buf, uint32_t unused1,
                     uint32_t unused2, uint32_t unused3)
{
    (void)unused1; (void)unused2; (void)unused3;
    if (!user_buf) return (uint32_t)-1;

    fb_desc_t d;
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
