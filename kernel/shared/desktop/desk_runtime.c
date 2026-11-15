/* ============================================================================
 * desk_runtime.c —— 桌面运行时的启动与驱动
 *
 * 主循环不再直接调用某个具体桌面的渲染函数, 而是:
 *     desk_start(scale)       启动当前选中的桌面(填表面 + 调它的 init)
 *     desk_tick()            画一帧(节流交给调用方, 见下)
 *
 * 为什么节流放在调用方:
 *   32 位主循环挂在 keyboard_getchar 的等待里(有 hlt, 频率由中断决定),
 *   64 位有自己的 16ms 主循环。两边的时间源不同, 在这一层写节流就必须
 *   引入架构分支, 反而破坏"共用"。调用方各按自己的时间源决定何时调。
 *
 * 32/64 共用, 只有一处依赖: desk_surface_get() 里的 gfx64_* 在图元层同形。
 * ========================================================================== */

#include "desk_api.h"

extern int desk_surface_get(desk_surface_t *s, uint32_t scale_pct);
extern int desk_register_hpdesk(void);
extern desk_backend_t *desk_hpdesk_backend(void);

static desk_surface_t g_surf;
static int            g_running = 0;

desk_surface_t *desk_active_surface(void)
{
    return g_running ? &g_surf : 0;
}

/* 启动当前选中的桌面。返回:
 *   0  成功
 *  -1  表面不可用(没有帧缓冲 / 分辨率未知)
 *  -2  注册表里没有桌面
 *  -3  桌面自己的 init 失败
 */
int desk_start(uint32_t scale_pct)
{
    if (g_running) return 0;

    desk_backend_t *d = desk_current();
    if (!d) {
        /* 注册表还是空的 —— 把内置的 hpdesk 注册进去再取一次。
         * 这样上层不必关心"谁先注册", 拿到的一定是能用的桌面。 */
        desk_register_hpdesk();
        d = desk_current();
    }
    if (!d) return -2;

    if (desk_surface_get(&g_surf, scale_pct) != 0) return -1;

    if (d->init && d->init(&g_surf) != 0) return -3;

    g_running = 1;
    return 0;
}

/* 画一帧。返回桌面 frame 的返回值(通常 0), 未启动时返回 -1。 */
int desk_tick(void)
{
    if (!g_running) return -1;
    desk_backend_t *d = desk_current();
    if (!d || !d->frame) return -1;
    return d->frame(&g_surf);
}

/* 把事件交给当前桌面。返回 0 = 已被消费, 非 0 = 桌面不处理(调用方自行处理)。 */
int desk_dispatch(const desk_input_t *ev)
{
    if (!g_running || !ev) return 1;
    desk_backend_t *d = desk_current();
    if (!d || !d->event) return 1;
    return d->event(&g_surf, ev);
}

int desk_is_running(void)
{
    return g_running;
}
