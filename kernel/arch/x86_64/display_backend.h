/* ============================================================================
 * 64 位侧显示后端声明。
 *
 * 契约本身(display_backend_t / fb_desc_t)已移到 shared/gfx/display_backend.h,
 * 因为 32 位桥接需要同一份定义。这个文件只保留 64 位专属的三项。
 * ========================================================================== */
#ifndef DISPLAY_BACKEND_H
#define DISPLAY_BACKEND_H

#include "../../shared/gfx/display_backend.h"

/* 返回当前帧缓冲描述；未初始化返回 0。 */
const fb_desc_t *gfx64_fb_desc(void);

/* 第二个实现：RAM 镜像后端（display_mirror.c）。
 * 用途是检验这层抽象 —— 同一帧经它和 lfb 提交必须逐像素一致。 */
extern const display_backend_t display_mirror_backend;

/* 运行时切换后端（抽象的一部分：换后端不该要求重编上层）。 */
void gfx64_set_display_backend(const display_backend_t *b);

#endif /* DISPLAY_BACKEND_H */
