/* ============================================================================
 * 赫普OS 64位 - 窗口管理器
 * 支持窗口创建/移动/缩放/关闭、Z轴顺序、鼠标交互
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

#define MAX_WINDOWS 16
#define WINDOW_TITLE_HEIGHT 24
#define WINDOW_BORDER_WIDTH 2

typedef struct {
    int      x, y;
    int      width, height;
    char     title[64];
    uint32_t title_color;
    uint32_t bg_color;
    uint32_t border_color;
    int      visible;
    int      z_order;
    int      has_focus;
    void     (*draw_content)(void *fb, int fb_w, int fb_h,
                             int x, int y, int w, int h);
} window_t;

static window_t windows[MAX_WINDOWS];
static int window_count = 0;
static int focus_window = -1;

extern void font64_draw_string(void *fb, int fb_w, int fb_h,
                               int x, int y, const char *str,
                               uint32_t color, uint16_t size);

/* 绘制矩形 */
static void draw_rect(void *fb, int fb_w, int fb_h,
                      int x, int y, int w, int h, uint32_t color)
{
    if (!fb) return;
    uint32_t *framebuffer = (uint32_t *)fb;
    for (int row = 0; row < h; row++) {
        if (y + row < 0 || y + row >= fb_h) continue;
        for (int col = 0; col < w; col++) {
            if (x + col < 0 || x + col >= fb_w) continue;
            framebuffer[(y + row) * fb_w + (x + col)] = color;
        }
    }
}

/* 初始化窗口管理器 */
void window64_init(void)
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        windows[i].visible = 0;
        windows[i].z_order = 0;
    }
    window_count = 0;
    focus_window = -1;
}

/* 创建窗口 */
int window64_create(const char *title, int x, int y, int w, int h)
{
    if (window_count >= MAX_WINDOWS) return -1;

    int id = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].visible) { id = i; break; }
    }
    if (id < 0) return -1;

    windows[id].x = x;
    windows[id].y = y;
    windows[id].width = w;
    windows[id].height = h;
    windows[id].title_color = 0x2C5F8D;
    windows[id].bg_color = 0xE8E8E8;
    windows[id].border_color = 0x888888;
    windows[id].visible = 1;
    windows[id].z_order = window_count;
    windows[id].has_focus = 0;
    windows[id].draw_content = NULL;

    /* 复制标题 */
    int i = 0;
    while (title && title[i] && i < 63) {
        windows[id].title[i] = title[i];
        i++;
    }
    windows[id].title[i] = 0;

    window_count++;
    window64_set_focus(id);
    return id;
}

/* 销毁窗口 */
void window64_destroy(int id)
{
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].visible) return;
    windows[id].visible = 0;
    window_count--;

    if (focus_window == id) {
        focus_window = -1;
        /* 找下一个窗口获得焦点 */
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (windows[i].visible) {
                window64_set_focus(i);
                break;
            }
        }
    }
}

/* 设置窗口焦点 */
void window64_set_focus(int id)
{
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].visible) return;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        windows[i].has_focus = 0;
    }
    windows[id].has_focus = 1;
    windows[id].z_order = window_count;
    focus_window = id;

    /* 重新排序Z轴 */
    int z = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].visible && i != id) {
            windows[i].z_order = z++;
        }
    }
}

/* 移动窗口 */
void window64_move(int id, int dx, int dy)
{
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].visible) return;
    windows[id].x += dx;
    windows[id].y += dy;
}

/* 缩放窗口 */
void window64_resize(int id, int dw, int dh)
{
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].visible) return;
    if (windows[id].width + dw > 100) windows[id].width += dw;
    if (windows[id].height + dh > 50) windows[id].height += dh;
}

/* 绘制单个窗口 */
static void draw_window(void *fb, int fb_w, int fb_h, window_t *win)
{
    if (!win || !win->visible) return;

    /* 绘制边框 */
    draw_rect(fb, fb_w, fb_h, win->x, win->y,
              win->width, win->height, win->border_color);

    /* 绘制标题栏 */
    uint32_t title_color = win->has_focus ? win->title_color : 0x666666;
    draw_rect(fb, fb_w, fb_h,
              win->x + WINDOW_BORDER_WIDTH,
              win->y + WINDOW_BORDER_WIDTH,
              win->width - WINDOW_BORDER_WIDTH * 2,
              WINDOW_TITLE_HEIGHT, title_color);

    /* 绘制标题文字 */
    font64_draw_string(fb, fb_w, fb_h,
                       win->x + 8, win->y + 4,
                       win->title, 0xFFFFFF, 16);

    /* 绘制关闭按钮 */
    int btn_x = win->x + win->width - WINDOW_TITLE_HEIGHT - 4;
    int btn_y = win->y + 4;
    draw_rect(fb, fb_w, fb_h, btn_x, btn_y, 16, 16, 0xFF4444);
    font64_draw_string(fb, fb_w, fb_h, btn_x + 4, btn_y, "X", 0xFFFFFF, 16);

    /* 绘制内容区背景 */
    draw_rect(fb, fb_w, fb_h,
              win->x + WINDOW_BORDER_WIDTH,
              win->y + WINDOW_TITLE_HEIGHT + WINDOW_BORDER_WIDTH,
              win->width - WINDOW_BORDER_WIDTH * 2,
              win->height - WINDOW_TITLE_HEIGHT - WINDOW_BORDER_WIDTH * 2,
              win->bg_color);

    /* 绘制自定义内容 */
    if (win->draw_content) {
        win->draw_content(fb, fb_w, fb_h,
                          win->x + WINDOW_BORDER_WIDTH,
                          win->y + WINDOW_TITLE_HEIGHT + WINDOW_BORDER_WIDTH,
                          win->width - WINDOW_BORDER_WIDTH * 2,
                          win->height - WINDOW_TITLE_HEIGHT - WINDOW_BORDER_WIDTH * 2);
    }
}

/* 绘制所有窗口（按Z轴顺序） */
void window64_draw_all(void *fb, int fb_w, int fb_h)
{
    /* 按Z轴顺序绘制 */
    for (int z = 0; z <= window_count; z++) {
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (windows[i].visible && windows[i].z_order == z) {
                draw_window(fb, fb_w, fb_h, &windows[i]);
            }
        }
    }
}

/* 点击测试：返回点击的窗口ID和区域 */
int window64_hit_test(int x, int y, int *region)
{
    /* 从最上层开始检查 */
    for (int z = window_count; z >= 0; z--) {
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (!windows[i].visible || windows[i].z_order != z) continue;

            window_t *win = &windows[i];
            if (x >= win->x && x < win->x + win->width &&
                y >= win->y && y < win->y + win->height) {

                if (region) {
                    /* 检查是否点击关闭按钮 */
                    int btn_x = win->x + win->width - WINDOW_TITLE_HEIGHT - 4;
                    int btn_y = win->y + 4;
                    if (x >= btn_x && x < btn_x + 16 &&
                        y >= btn_y && y < btn_y + 16) {
                        *region = 3; /* 关闭按钮 */
                    } else if (y < win->y + WINDOW_TITLE_HEIGHT + WINDOW_BORDER_WIDTH) {
                        *region = 1; /* 标题栏（可拖拽） */
                    } else {
                        *region = 2; /* 内容区 */
                    }
                }
                return i;
            }
        }
    }
    return -1;
}

/* 获取窗口数量 */
int window64_get_count(void)
{
    return window_count;
}

/* 获取窗口信息 */
int window64_get_info(int id, int *x, int *y, int *w, int *h)
{
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].visible) return -1;
    if (x) *x = windows[id].x;
    if (y) *y = windows[id].y;
    if (w) *w = windows[id].width;
    if (h) *h = windows[id].height;
    return 0;
}

/* 设置窗口内容绘制回调 */
void window64_set_content_callback(int id,
    void (*callback)(void *fb, int fb_w, int fb_h, int x, int y, int w, int h))
{
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].visible) return;
    windows[id].draw_content = callback;
}
