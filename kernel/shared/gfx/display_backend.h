/* ============================================================================
 * Hepu OS — 显示后端抽象（通用契约，32/64 位共用）
 *
 * 这一层只收"写屏"这一件事(后台缓冲 -> 前台), 共 3 个入口:
 *   init            绑定线性帧缓冲
 *   present         整帧提交
 *   present_region  局部提交
 *
 * 注意: set_target(front/back/离屏 RT 的切换) 故意不在这里 —— 它不是
 * 后端能力, 而是合成层的"当前渲染目标", 混进来会让后端承担合成语义。
 *
 * 从 arch/x86_64/display_backend.h 拆出来（2026-11-16）: 结构本身与
 * 架构无关, 32 位一侧要桥接自己的探测结果也需要同一份契约。64 位专属的
 * 声明(gfx64_fb_desc / display_mirror_backend / gfx64_set_display_backend)
 * 留在原头文件里。
 * ========================================================================== */
#ifndef HEPU_DISPLAY_BACKEND_COMMON_H
#define HEPU_DISPLAY_BACKEND_COMMON_H

#include <stdint.h>

typedef struct display_backend {
    const char *name;
    /* 绑定线性帧缓冲；返回 0 表示成功 */
    int  (*init)(int w, int h, int pitch, int bpp, uint8_t *fb);
    /* 后台缓冲 -> 前台，整帧 */
    void (*present)(void);
    /* 后台缓冲 -> 前台，局部矩形 */
    void (*present_region)(int x, int y, int w, int h);
} display_backend_t;

/* ---------------------------------------------------------------------------
 * framebuffer 契约（KERNEL_SCOPE §5-4 第 1 步）
 *
 * §5-4 要的是 "map + blit + flip" 三个最小原语，让合成以后能搬到 ring3。
 * 现有 3 入口已经覆盖 blit/present：`present` = flip，`present_region` = blit。
 * 缺的是 **map** —— 用户态要知道"显存在哪、多大、什么格式"，才能自己去映射。
 *
 * 所以契约的第一块砖是**可映射描述**：内核把帧缓冲的物理基址和布局交出去，
 * 用户态据此自己映射（映射本身仍要内核批，但"信息可得"是前提）。
 * 这条信息不属于任何单个后端 —— 两个后端共享同一块物理显存，
 * mirror 只是替换了"提交到哪"。所以它是 gfx64 的全局属性，不进 vtable，
 * 以免破坏这层刻意保留的 3 入口最小面。
 * ------------------------------------------------------------------------- */
typedef struct fb_desc {
    uint64_t phys;     /* 帧缓冲物理基址（可映射） */
    uint32_t width;    /* 像素宽 */
    uint32_t height;   /* 像素高 */
    uint32_t pitch;    /* 每扫描行字节数 */
    uint32_t bpp;      /* 每像素位数 */
    uint32_t format;   /* 1 = BGRX8888（当前唯一格式） */
    uint32_t flags;    /* bit0: 后台缓冲可用 */
} fb_desc_t;

#define FB_DESC_FLAG_DOUBLE_BUFFERED 0x1u

#endif /* HEPU_DISPLAY_BACKEND_COMMON_H */
