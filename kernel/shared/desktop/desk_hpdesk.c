/* ============================================================================
 * desk_hpdesk.c —— 把 hpdesk 注册成"可切换桌面"里的一个实现
 *
 * hpdesk 本体(hpdesk.c) 仍是老样子: 内部直接调 gfx64_* 图元, 并且每帧自己
 * 轮询鼠标(hpdesk.c 里的 mouse_get_x/y + mouse_get_buttons)。本文件不给它做
 * 大改造, 只是把它包装成 desk_backend_t, 让注册表知道"系统里有一个默认桌面
 * 叫 hpdesk", 并让主循环统一从 desk_current() 取实现来驱动。
 *
 * 这样做的意义:
 *   - "可切换"这句承诺成立: 注册表有内容, desk_select() 有东西可切
 *   - 用户写自己的 desk_backend_t 调 desk_register 之后, 主循环不需要改
 *   - 将来 hpdesk 内部全部改走 desk_surface_t 时, 只动 hpdesk.c 自己,
 *     外面这一层不用改
 *
 * 32/64 共用: hpdesk_init/hpdesk_render 两边同名。
 * ========================================================================== */

#include "desk_api.h"

extern int  hpdesk_init(int width, int height);
extern void hpdesk_render(void);

static int hp_init(desk_surface_t *s)
{
    if (!s) return -1;
    return hpdesk_init(s->width, s->height);
}

static int hp_frame(desk_surface_t *s)
{
    (void)s;
    hpdesk_render();
    return 0;
}

/* hpdesk 自己轮询鼠标和键盘, 不需要外部喂事件。这里如实返回 1
 * (接口约定: 非 0 = 本实现不认这个事件), 让调用方知道"事件不由我处理"。
 * 换成自己处理事件的桌面时, 这里返回 0 并真正消费 ev。 */
static int hp_event(desk_surface_t *s, const desk_input_t *ev)
{
    (void)s; (void)ev;
    return 1;
}

static void hp_fini(desk_surface_t *s)
{
    (void)s;
    /* hpdesk 没有拆解路径; 桌面在系统生命周期内常驻。 */
}

static desk_backend_t g_hpdesk_backend = {
    "hpdesk",              /* 注册名, desk_select("hpdesk") 用这个 */
    "Hepu Desktop",        /* 展示名 */
    hp_init,
    hp_frame,
    hp_event,
    hp_fini
};

desk_backend_t *desk_hpdesk_backend(void)
{
    return &g_hpdesk_backend;
}

/* 幂等注册: 重复调用不会往表里塞两份。 */
int desk_register_hpdesk(void)
{
    for (int i = 0; i < desk_count(); i++) {
        if (desk_at(i) == &g_hpdesk_backend) return 0;
    }
    return desk_register(&g_hpdesk_backend);
}
