/* ============================================================================
 * 赫普OS - 游戏控制器管理（从gameframework.c拆分）
 * ========================================================================== */

#include <stdint.h>
#include "gameframework.h"
#include "vga.h"
#include "string.h"

extern game_framework_t gf;

int gf_controller_count(void) { return gf.controller_count; }

int gf_get_controller(int controller_id, controller_t *ctrl)
{
    if (controller_id < 0 || controller_id >= GF_MAX_CONTROLLERS || !gf.controllers[controller_id].used || !ctrl) return -1;
    memcpy(ctrl, &gf.controllers[controller_id], sizeof(controller_t));
    return 0;
}

int gf_is_button_pressed(int controller_id, controller_button_t button)
{
    if (controller_id < 0 || controller_id >= GF_MAX_CONTROLLERS || !gf.controllers[controller_id].used) return 0;
    if (button < 0 || button >= BTN_MAX) return 0;
    return gf.controllers[controller_id].buttons[button];
}

int gf_is_button_just_pressed(int controller_id, controller_button_t button)
{
    if (controller_id < 0 || controller_id >= GF_MAX_CONTROLLERS || !gf.controllers[controller_id].used) return 0;
    if (button < 0 || button >= BTN_MAX) return 0;
    return gf.controllers[controller_id].buttons[button] && !gf.controllers[controller_id].buttons_prev[button];
}

float gf_get_axis(int controller_id, int axis)
{
    if (controller_id < 0 || controller_id >= GF_MAX_CONTROLLERS || !gf.controllers[controller_id].used) return 0;
    switch (axis) {
        case 0: return gf.controllers[controller_id].axis_left_x;
        case 1: return gf.controllers[controller_id].axis_left_y;
        case 2: return gf.controllers[controller_id].axis_right_x;
        case 3: return gf.controllers[controller_id].axis_right_y;
        case 4: return gf.controllers[controller_id].trigger_left;
        case 5: return gf.controllers[controller_id].trigger_right;
        default: return 0;
    }
}

int gf_set_rumble(int controller_id, float left, float right)
{
    if (controller_id < 0 || controller_id >= GF_MAX_CONTROLLERS || !gf.controllers[controller_id].used) return -1;
    if (!gf.controllers[controller_id].rumble_support) return -1;
    gf.controllers[controller_id].rumble_left = left;
    gf.controllers[controller_id].rumble_right = right;
    return 0;
}

int gf_vibrate_controller(int controller_id, int duration_ms)
{
    (void)duration_ms;
    return gf_set_rumble(controller_id, 0.5f, 0.5f);
}

