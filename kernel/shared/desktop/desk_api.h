#ifndef HEPU_DESK_API_H
#define HEPU_DESK_API_H

#include <stdint.h>

/* ============================================================================
 * Hepu OS 桌面接口契约（DESKTOP_INTERFACE）
 *
 * 目标: 一份桌面逻辑, 32 位和 64 位都能用; 并且允许用户以后自己加桌面。
 *
 * 分层:
 *
 *   桌面实现 (desk_xxx.c)  —— 只调 surface, 不碰硬件
 *          ↓  desk_surface_t
 *   后端 (各架构各自提供)  —— 唯一知道"帧缓冲在哪、怎么画"的地方
 *
 * 所以: 桌面代码里不允许出现 outb / inb / 0xA0000 / 物理地址 / 寄存器名。
 * 边界判据见 docs/architecture/GRAPHICS_BOUNDARY.md。
 * ========================================================================== */

/* --- 绘制表面: 桌面实现看到的世界 -------------------------------- */

typedef struct desk_surface {
    int      width;      /* 逻辑宽 (像素) */
    int      height;
    int      bpp;
    int      pitch;      /* 字节/行 */
    uint32_t scale_pct;  /* UI 缩放百分比, 100 = 1:1 */
    void    *ctx;        /* 后端私有 (用户态: mmap 后的基址; 内核: LFB) */

    /* 全部以颜色为 0x00RRGGBB 的语义传入 */
    void (*put_pixel)(struct desk_surface *s, int x, int y, uint32_t rgb);
    void (*fill_rect)(struct desk_surface *s, int x, int y, int w, int h, uint32_t rgb);
    void (*hline)(struct desk_surface *s, int x, int y, int w, uint32_t rgb);
    void (*draw_text)(struct desk_surface *s, int x, int y, const char *utf8,
                      uint32_t rgb, int px);
    int  (*text_width)(struct desk_surface *s, const char *utf8, int px);
    void (*present)(struct desk_surface *s);   /* 双缓冲时整帧提交; 单缓冲可空 */
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
    int x, y;        /* 鼠标坐标 */
    int button;      /* 0=左 1=右 */
    int key;         /* 键码 */
    char ch;         /* 可打印字符, 0 表示无 */
} desk_input_t;

/* --- 一个桌面实现 -------------------------------------------------- */

typedef struct desk_backend {
    const char *name;                  /* 唯一名, 用于切换, 如 "shell" */
    const char *display_name;          /* 菜单里显示的名字, 如 "极简桌面" */

    /* 首次进入: 拿 surface 算布局。返回 0 成功。 */
    int  (*init)(desk_surface_t *s);
    /* 画一帧。返回 0 成功。 */
    int  (*frame)(desk_surface_t *s);
    /* 处理一个输入事件; 返回 1 表示"已消费", 0 表示不关心。可空。 */
    int  (*event)(desk_surface_t *s, const desk_input_t *ev);
    /* 退出/切换走之前清理。可空。 */
    void (*fini)(desk_surface_t *s);
} desk_backend_t;

/* --- 注册表: 这就是"可切换"的实现 --------------------------------- */

#define DESK_MAX_BACKENDS 8

int             desk_register(desk_backend_t *d);          /* 0=成功, -1=满, -2=重名 */
int             desk_select(const char *name);              /* 0=成功, -1=找不到 */
desk_backend_t *desk_current(void);
desk_backend_t *desk_at(int index);                         /* 遍历用, 越界返回 NULL */
int             desk_count(void);
/* 给 UI 用的"桌面选择"数据源: 返回名字列表, 写进 out, 最多 max 个 */
int             desk_list(const char **out, int max);

#endif /* HEPU_DESK_API_H */
