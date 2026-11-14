/* ============================================================================
 * Hepu OS — 显示后端抽象（三层模型的第一层）
 *
 * 为什么有这一层：`GRAPHICS_STACK.md` 记的现状是"第一层至今是空的" ——
 * gfx64 有 front/back/target 三个缓冲，但**直接操作 LFB**，没有"后端"这个
 * 可替换的边界。结果是：想加 VBE / virtio / 多显示器后端，或者想把
 * framebuffer 映射给用户态，都必须改 gfx64 内部。
 *
 * 这一层只收"写屏"这一件事（后台缓冲 -> 前台），共 3 个入口：
 *   init            绑定线性帧缓冲
 *   present         整帧提交
 *   present_region  局部提交
 *
 * 注意：`set_target`（front/back/离屏 RT 的切换）**故意不在这里** ——
 * 它不是后端能力，而是合成层的"当前渲染目标"，混进来会让后端承担合成语义。
 *
 * 当前只有一个后端 `lfb`（把原来直接写 front_buffer 的两段原样搬进来），
 * 所以行为必须与改造前逐像素一致。
 * ========================================================================== */
#ifndef DISPLAY_BACKEND_H
#define DISPLAY_BACKEND_H

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

/* 第二个实现：RAM 镜像后端（display_mirror.c）。
 * 用途是检验这层抽象 —— 同一帧经它和 lfb 提交必须逐像素一致。 */
extern const display_backend_t display_mirror_backend;

/* 运行时切换后端（抽象的一部分：换后端不该要求重编上层）。 */
void gfx64_set_display_backend(const display_backend_t *b);

#endif /* DISPLAY_BACKEND_H */
