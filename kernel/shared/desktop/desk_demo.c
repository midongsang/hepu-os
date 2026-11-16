/* ============================================================================
 * desk_demo.c —— 第二套桌面, 存在的唯一目的是证明"可切换"是真的
 *
 * 为什么需要它: 注册表里只注册过 hpdesk 一个后端时, "可切换桌面"无从验证 ——
 * 没有第二个实现, select 永远选到同一个东西。这份极简桌面把承诺变成可执行的
 * 事实: 它只用 desk_surface_t 的核心 12 个原语(一个可选扩展都没用), 完全不碰
 * gfx64_*, 也不碰任何架构相关的东西。
 *
 * 它同时也是"用户自己加桌面"的模板: 实现 desk_backend_t 四个函数, 调一次
 * desk_register 就完事, 不需要理解 gfx64、不需要知道 32/64 的差别。
 *
 * 打开方式: 编译时加 -DHEPU_DESK_DEMO, desk_start 会优先选它(见 desk_runtime.c)。
 * 平时不选, 所以默认启动看到的仍是 hpdesk。
 * ========================================================================== */

#include "desk_api.h"

static int  g_frame_no;
static int  g_tick;

static int demo_init(desk_surface_t *s)
{
    (void)s;
    g_frame_no = 0;
    g_tick = 0;
    return 0;
}

/* 把 "HH:MM" 从秒数拼出来 —— 只用整数, 不依赖 libc/格式化输出 */
static void demo_clock(char *out, int ticks)
{
    int total = ticks / 60;           /* 假定 PIT/主循环约 60 帧/秒 */
    int hh = (total / 3600 + 9) % 24;  /* 偏移只为让画面里时间看起来合理 */
    int mm = (total / 60) % 60;
    out[0] = (char)('0' + hh / 10);
    out[1] = (char)('0' + hh % 10);
    out[2] = ':';
    out[3] = (char)('0' + mm / 10);
    out[4] = (char)('0' + mm % 10);
    out[5] = 0;
}

static void demo_triangle(desk_surface_t *s, int cx, int cy, int r, uint32_t c)
{
    /* 用交错数组表达三角形, 正好演示 fill_polygon 的接口形态 */
    int pts[6];
    pts[0] = cx;      pts[1] = cy - r;
    pts[2] = cx - r;  pts[3] = cy + r;
    pts[4] = cx + r;  pts[5] = cy + r;
    s->fill_polygon(s, pts, 3, c);
}

static int demo_frame(desk_surface_t *s)
{
    int w = s->width, h = s->height;
    char buf[8];
    int i;

    g_frame_no++;
    g_tick++;

    /* 0. 先清掉上一个桌面留下的裁剪区 —— 桌面是切换来的, 不是从干净状态开始。
     *    少了这一步, 铺底只填到上一个桌面最后设的裁剪框, 屏上会剩半块黑。
     *    (实测第一次跑就是这样: 上半深紫, 下半纯黑, 因为 hpdesk 退出时
     *     clip 还停在它的某个面板矩形里。) */
    s->clip_disable(s);

    /* 1. 铺底: 一块和 hpdesk 完全不同的深紫, 一眼就能区分两套桌面 */
    s->fill_rect(s, 0, 0, w, h, 0x2A1B3D);

    /* 2. 顶部一条横带, 证明 fill_rect 在整屏尺度上正常 */
    s->fill_rect(s, 0, 0, w, 6, 0xD0BCFF);

    /* 3. 中间大号时钟 */
    demo_clock(buf, g_tick);
    s->draw_text(s, w / 2 - 90, h / 2 - 70, buf, 0xF2E9FF, 96);

    /* 4. 一行说明文字 */
    s->draw_text(s, w / 2 - 330, h / 2 + 60,
                 "desk_demo — 第二套桌面, 只用了核心 12 个原语",
                 0xB9A6D4, 24);

    /* 5. 一个会走的小三角, 证明这一帧确实在重画(不是死帧) */
    {
        int x = (g_tick * 4) % (w - 200) + 100;
        demo_triangle(s, x, h - 180, 28, 0xD0BCFF);
    }

    /* 6. 底部进度条: 每 600 帧走一格, 用圆角矩形 + 直角矩形各一次 */
    s->fill_rounded_rect(s, w / 2 - 260, h - 60, 520, 14, 7, 0x4A3560);
    {
        int pct = (g_frame_no % 600);
        int pw = 520 * pct / 600;
        if (pw > 0)
            s->fill_rounded_rect(s, w / 2 - 260, h - 60, pw, 14, 7, 0xD0BCFF);
    }

    /* 7. 四角画圆, 顺便验证 fill_circle 与 draw_line_aa */
    s->fill_circle(s, 80, h - 80, 24, 0x7C5BA6);
    s->fill_circle(s, w - 80, h - 80, 24, 0x7C5BA6);
    for (i = 0; i < 3; i++)
        s->draw_line_aa(s, 40 + i * 18, 40, 40 + i * 18, h / 3, 0x6A4C93);

    /* 8. 提交 */
    s->present(s);
    return 0;
}

static desk_backend_t g_demo_backend = {
    "demo",
    "Desk Demo",
    demo_init,
    demo_frame,
    0,          /* event: 不处理输入, 主循环自己轮询鼠标 */
    0,          /* fini */
};

int desk_register_demo(void)
{
    return desk_register(&g_demo_backend);
}
