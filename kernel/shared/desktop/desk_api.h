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
} desk_surface_t;

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
