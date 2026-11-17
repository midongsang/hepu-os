#ifndef HEPU_DESK_API_H
#define HEPU_DESK_API_H

#include <stdint.h>

/* ============================================================================
 * Hepu OS 桌面接口契约（DESKTOP_INTERFACE）
 *
 * 目标: 一份桌面逻辑, 32 位和 64 位真正共用; 并且允许用户以后自己加桌面。
 *
 * 分层:
 *   桌面实现 (desk_shell.c)  —— 只调 surface, 不碰硬件
 *          ↓  desk_surface_t
 *   后端 (各架构各自提供)    —— 唯一知道"帧缓冲在哪、怎么画"的地方
 *
 * 硬判据: 桌面代码里不允许出现 outb / inb / 0xA0000 / 物理地址 / 寄存器名。
 *        见 docs/architecture/GRAPHICS_BOUNDARY.md。
 *
 * 原语集: 下面这组绘制入口不是"精简版", 而是 64 位 hpdesk.c(281 处调用)
 *         + hs_apps.c(53 处) 用到的全集。少一个都编不过 —— 共用桌面要求
 *         后端必须完整, 不能挑着实现。
 * ========================================================================== */

/* --- 绘制表面: 桌面实现看到的世界 -------------------------------- */

typedef struct desk_surface {
    uint8_t *fb;         /* 帧缓冲基址 (32/64 各位自己给) */
    int      width;
    int      height;
    int      bpp;
    int      pitch;      /* 字节/行 */
    uint32_t scale_pct;  /* UI 缩放百分比, 100 = 1:1 */
    void    *ctx;        /* 后端私有 */

    /* --- 基本 --- */
    void (*put_pixel)(struct desk_surface *s, int x, int y, uint32_t rgb);
    void (*fill_rect)(struct desk_surface *s, int x, int y, int w, int h, uint32_t rgb);
    void (*draw_rect)(struct desk_surface *s, int x, int y, int w, int h, uint32_t rgb);

    /* --- 圆角 (胶囊/卡片/图标底板都要) --- */
    void (*fill_rounded_rect)(struct desk_surface *s, int x, int y, int w, int h,
                              int r, uint32_t rgb);
    void (*draw_rounded_rect)(struct desk_surface *s, int x, int y, int w, int h,
                              int r, uint32_t rgb);

    /* --- 曲线 --- */
    void (*fill_circle)(struct desk_surface *s, int cx, int cy, int r, uint32_t rgb);
    void (*draw_line_aa)(struct desk_surface *s, int x0, int y0, int x1, int y1,
                         uint32_t rgb);

    /* --- 多边形 (箭头/角标) --- */
    void (*fill_polygon)(struct desk_surface *s, const int *pts, int n, uint32_t rgb);

    /* --- 位图混合 (图标/控制中心位图) --- */
    void (*blend_image)(struct desk_surface *s, int x, int y,
                        const uint8_t *src, int sw, int sh, int alpha);

    /* --- 文字 --- */
    void (*draw_text)(struct desk_surface *s, int x, int y, const char *utf8,
                      uint32_t rgb, int px);
    int  (*text_width)(struct desk_surface *s, const char *utf8, int px);

    /* --- 提交 (双缓冲时整帧; 单缓冲可空) --- */
    void (*present)(struct desk_surface *s);

    /* ========================================================================
     * 以下为可选扩展: 后端不支持时指针为 0, 用之前必须判空。
     *
     * 为什么分两层: 上面 12 个是任何桌面的最小集(画矩形/圆角/圆/线/字/提交),
     * 满足就能出一个能看的桌面。而 hpdesk 实际用到 29 种原语 —— 离屏目标切换、
     * 裁剪、模糊、亚像素圆角、分块贴图 —— 这些是实现"窗口层缓存 + 毛玻璃"这类
     * 效果所必需的, 但一个简单桌面用不到。全塞进必须集, 等于要求每个自写桌面
     * 都实现一遍模糊算法, 那不现实。
     * ====================================================================== */

    /* --- 直接访问帧缓冲与像素 (批量处理用; hpdesk 用它画窗口层缓存) --- */
    uint8_t  *(*framebuffer)(struct desk_surface *s);
    uint32_t  (*get_pixel)(struct desk_surface *s, int x, int y);
    void      (*blend_pixel)(struct desk_surface *s, int x, int y, uint32_t rgb, int alpha);

    /* --- 离屏绘制目标: 把后续绘制重定向到 buf(带原点偏移) ---
     * 这是窗口层缓存的基础: 先画到离屏表面, 下次没脏就直接贴回来。 */
    void (*target_begin)(struct desk_surface *s, void *buf, int w, int h, int ox, int oy);
    void (*target_end)(struct desk_surface *s);
    void (*set_target_front)(struct desk_surface *s);
    void (*set_target_back)(struct desk_surface *s);
    void (*present_region)(struct desk_surface *s, int x, int y, int w, int h);

    /* --- 更多几何 --- */
    void (*draw_circle)(struct desk_surface *s, int cx, int cy, int r, uint32_t rgb);
    void (*draw_arc)(struct desk_surface *s, int cx, int cy, int r,
                     int a0, int a1, uint32_t rgb);
    void (*fill_circle_gradient)(struct desk_surface *s, int cx, int cy, int r,
                                 uint32_t c1, uint32_t c2);
    void (*fill_round_rect_aa)(struct desk_surface *s, int x, int y, int w, int h,
                               int r, uint32_t rgb);
    /* 直接往指定 buf 填圆角矩形(argb 预乘); corners 是四角位掩码 */
    void (*fill_round_rect_argb)(struct desk_surface *s, void *buf, int bw, int bh,
                                 int x, int y, int w, int h, int r,
                                 uint32_t argb, int corners);

    /* --- 分块贴图(图标/岛/通知卡的局部合成都要) --- */
    void (*blend_image_sub)(struct desk_surface *s, void *src, int sw, int sh,
                            int sx, int sy, int w, int h, int dx, int dy, int opacity);

    /* --- 效果 --- */
    void (*blur_region)(struct desk_surface *s, int x, int y, int w, int h,
                        int radius, uint32_t overlay, int overlay_alpha);

    /* --- 裁剪 --- */
    void (*clip_set)(struct desk_surface *s, int x, int y, int w, int h);
    void (*clip_disable)(struct desk_surface *s);
} desk_surface_t;

/* 当前活动绘制表面（桌面宿主注册后返回; 未注册时返回 NULL）。
 * 必须在这里声明: 原先 hpdesk.c 直接调 desk_active_surface() 而没有可见原型,
 * 被隐式当作 int, 64 位下指针被截成 32 位高半丢失 (-Wint-conversion)。
 * 内核当前帧缓冲地址在 4GB 以下所以没立刻出事, 但这是定时炸弹。 */
desk_surface_t *desk_active_surface(void);

/* --- 输入事件 ------------------------------------------------------ */

typedef enum {
    DESK_EV_NONE = 0,
    DESK_EV_MOUSE_MOVE,
    DESK_EV_MOUSE_DOWN,
    DESK_EV_MOUSE_UP,
    DESK_EV_KEY_DOWN,
    DESK_EV_KEY_UP
} desk_ev_type_t;

typedef struct desk_input {
    desk_ev_type_t type;
    int x, y;
    int button;
    int key;
    char ch;
} desk_input_t;

/* --- 一个桌面实现 -------------------------------------------------- */

typedef struct desk_backend {
    const char *name;
    const char *display_name;

    int  (*init)(desk_surface_t *s);
    int  (*frame)(desk_surface_t *s);
    int  (*event)(desk_surface_t *s, const desk_input_t *ev);
    void (*fini)(desk_surface_t *s);
} desk_backend_t;

/* --- 注册表: "可切换"就是这张表 ------------------------------------ */

#define DESK_MAX_BACKENDS 8

int             desk_register(desk_backend_t *d);
int             desk_select(const char *name);
desk_backend_t *desk_current(void);
desk_backend_t *desk_at(int index);
int             desk_count(void);
int             desk_list(const char **out, int max);

#endif /* HEPU_DESK_API_H */
