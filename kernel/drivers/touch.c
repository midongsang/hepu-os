/* ============================================================================
 * 赫普OS - 触屏驱动
 * 支持电容/电阻触屏，多点触摸框架
 * ========================================================================== */

#include <stdint.h>
#include "touch.h"
#include "vga.h"
#include "irq.h"
#include "string.h"

static touch_point_t points[TOUCH_MAX_POINTS];
static touch_callback_t callback = NULL;
static int last_x = 0, last_y = 0;
static int pressed = 0;
static int touch_initialized = 0;

/* 触屏校准数据 */
static struct {
    int min_x, max_x;
    int min_y, max_y;
    int width, height;
} calib = {0, 4095, 0, 4095, 320, 200};

void touch_init(void)
{
    /* V4.35-E80: 必须先初始化硬件层 (touch_hw_state) —— 否则模拟注入静默失败 */
    extern int touch_hw_init(void);
    touch_hw_init();
    for (int i = 0; i < TOUCH_MAX_POINTS; i++) {
        points[i].active = 0;
        points[i].id = i;
    }
    pressed = 0;
    last_x = last_y = 0;
    touch_initialized = 1;

    vga_puts("[TOUCH] touch driver init done \n");
    vga_printf(" calib: X[%d-%d] Y[%d-%d] -> %dx%d\n",
               calib.min_x, calib.max_x, calib.min_y, calib.max_y,
               calib.width, calib.height);
}

/* 原始坐标转换为屏幕坐标 */
static void transform_coords(int raw_x, int raw_y, int *sx, int *sy)
{
    int range_x = calib.max_x - calib.min_x;
    int range_y = calib.max_y - calib.min_y;
    if (range_x <= 0) range_x = 1;
    if (range_y <= 0) range_y = 1;
    *sx = (raw_x - calib.min_x) * calib.width / range_x;
    *sy = (raw_y - calib.min_y) * calib.height / range_y;
    if (*sx < 0) *sx = 0;
    if (*sx >= calib.width) *sx = calib.width - 1;
    if (*sy < 0) *sy = 0;
    if (*sy >= calib.height) *sy = calib.height - 1;
}

void touch_handle_irq(void)
{
    if (!touch_initialized) return;

    /* 原型阶段: 模拟触屏数据读取
     * 实际硬件: 从I2C/SPI控制器读取触摸数据
     */
    static int sim_state = 0;
    sim_state++;

    if (sim_state % 100 == 0) {
        /* 模拟触摸事件 */
        int raw_x = calib.min_x + (sim_state * 7) % (calib.max_x - calib.min_x);
        int raw_y = calib.min_y + (sim_state * 11) % (calib.max_y - calib.min_y);
        int sx, sy;
        transform_coords(raw_x, raw_y, &sx, &sy);

        touch_event_t evt;
        if (!pressed) {
            evt.type = TOUCH_DOWN;
            pressed = 1;
            points[0].active = 1;
        } else if (sim_state % 300 == 0) {
            evt.type = TOUCH_UP;
            pressed = 0;
            points[0].active = 0;
        } else {
            evt.type = TOUCH_MOVE;
        }

        evt.point_id = 0;
        evt.x = sx;
        evt.y = sy;
        evt.pressure = pressed ? 200 : 0;

        last_x = sx;
        last_y = sy;
        points[0].x = sx;
        points[0].y = sy;
        points[0].pressure = evt.pressure;

        if (callback) callback(&evt);
    }
}

int touch_get_points(touch_point_t *out_points, int max)
{
    if (!out_points || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < TOUCH_MAX_POINTS && count < max; i++) {
        if (points[i].active) {
            memcpy(&out_points[count], &points[i], sizeof(touch_point_t));
            count++;
        }
    }
    return count;
}

void touch_register_callback(touch_callback_t cb)
{
    callback = cb;
}

int touch_last_x(void) { return last_x; }
int touch_last_y(void) { return last_y; }
int touch_is_pressed(void) { return pressed; }

void touch_calibrate(void)
{
    vga_puts("[TOUCH] touch calib ...\n");
    /* 原型阶段: 使用默认校准值 */
    vga_puts("[TOUCH] calib done(using defaults)\n");
}
