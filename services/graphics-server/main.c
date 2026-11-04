/* ============================================================================
 * 赫普OS - 图形合成服务 (用户态)
 * 负责窗口管理、图形合成、自由窗口、后台冻结
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "hepu_service.h"

#define SERVICE_NAME "graphics-server"

#define MAX_WINDOWS 64
#define WINDOW_TITLE_LEN 64

/* 窗口状态 */
typedef enum {
    WINDOW_NORMAL = 0,
    WINDOW_MINIMIZED,
    WINDOW_MAXIMIZED,
    WINDOW_FROZEN,   /* 后台冻结：零耗电 */
    WINDOW_FULLSCREEN
} window_state_t;

/* 窗口结构 */
typedef struct {
    int      in_use;
    uint32_t window_id;
    char     title[WINDOW_TITLE_LEN];
    uint32_t x, y, width, height;
    uint32_t z_order;
    window_state_t state;
    uint32_t owner_pid;
    uint32_t framebuffer_addr;
} window_t;

static window_t windows[MAX_WINDOWS];
static uint32_t next_window_id = 1;
static uint32_t screen_width = 1080;
static uint32_t screen_height = 1920;

static int graphics_server_init(void)
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        windows[i].in_use = 0;
    }
    hepu_log(SERVICE_NAME, "图形合成服务初始化完成");
    hepu_log(SERVICE_NAME, "弹性自由桌面模式已启用");
    hepu_log(SERVICE_NAME, "后台窗口冻结机制已启用");
    return 0;
}

/* 创建窗口 */
static uint32_t create_window(const char *title, uint32_t w, uint32_t h,
                              uint32_t owner)
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].in_use) {
            windows[i].in_use = 1;
            windows[i].window_id = next_window_id++;
            windows[i].x = 100;
            windows[i].y = 100;
            windows[i].width = w;
            windows[i].height = h;
            windows[i].state = WINDOW_NORMAL;
            windows[i].owner_pid = owner;
            windows[i].z_order = i;
            for (int j = 0; j < WINDOW_TITLE_LEN - 1 && title[j]; j++) {
                windows[i].title[j] = title[j];
            }
            windows[i].title[WINDOW_TITLE_LEN - 1] = '\0';
            return windows[i].window_id;
        }
    }
    return 0;
}

/* 冻结窗口（后台零耗电） */
static int freeze_window(uint32_t win_id)
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].in_use && windows[i].window_id == win_id) {
            windows[i].state = WINDOW_FROZEN;
            hepu_log(SERVICE_NAME, "窗口已冻结 (零耗电)");
            return 0;
        }
    }
    return -1;
}

static void graphics_server_handle_msg(ipc_message_t *msg)
{
    switch (msg->type) {
        case 0x3001: /* 创建窗口 */
            break;
        case 0x3002: /* 移动窗口 */
            break;
        case 0x3003: /* 缩放窗口 */
            break;
        case 0x3004: /* 冻结窗口 */
            break;
        case 0x3005: /* 窗口重绘请求 */
            break;
        default:
            break;
    }
}

int hepu_service_main(int argc, char **argv)
{
    if (graphics_server_init() != 0) return -1;

    while (1) {
        ipc_message_t msg;
        if (hepu_ipc_receive(&msg) == 0) {
            graphics_server_handle_msg(&msg);
        }
        hepu_yield();
    }
    return 0;
}
