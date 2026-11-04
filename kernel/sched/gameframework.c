/* ============================================================================
 * 赫普OS - 游戏框架
 * 游戏引擎 + 手柄支持 + 性能模式 + 图形API + 粒子系统 + 存档
 * ========================================================================== */

#include <stdint.h>
#include "gameframework.h"
#include "vga.h"
#include "string.h"

game_framework_t gf;

void gameframework_init(void)
{
    memset(&gf, 0, sizeof(game_framework_t));
    gf.initialized = 1;
    gf.active_game = -1;
    gf.game_mode_enabled = 0;
    gf.do_not_disturb = 0;
    gf.performance_lock = 0;
    gf.screen_record = 0;
    gf.fps = 60;
    gf.frame_time_ms = 16.67f;

    /* 图形API初始化 */
    gf.graphics.initialized = 1;
    gf.graphics.api_version = 100;
    gf.graphics.max_texture_size = 4096;
    gf.graphics.max_anisotropy = 16;
    gf.graphics.support_depth_buffer = 1;
    gf.graphics.support_msaa = 1;
    gf.graphics.msaa_level = 4;
    gf.graphics.vsync_enabled = 1;
    gf.graphics.framebuffer_width = 320;
    gf.graphics.framebuffer_height = 200;

    /* 模拟2个手柄 */
    for (int i = 0; i < 2; i++) {
        gf.controllers[i].used = 1;
        gf.controllers[i].id = i;
        gf.controllers[i].type = CTRL_TYPE_GAMEPAD;
        strncpy(gf.controllers[i].name, i == 0 ? "Hepu Gamepad 1" : "Hepu Gamepad 2", 63);
        gf.controllers[i].connected = 1;
        gf.controllers[i].battery_level = 80;
        gf.controllers[i].rumble_support = 1;
        gf.controller_count++;
    }

    vga_puts("[GF] game framework init done \n");
    vga_printf(" gamepad: %d, graphics API v%d, MSAA: %dx\n",
               gf.controller_count, gf.graphics.api_version, gf.graphics.msaa_level);
}

int gf_create_game(const char *name, const char *package)
{
    if (!name || !gf.initialized) return -1;
    for (int i = 0; i < GF_MAX_GAMES; i++) {
        if (!gf.games[i].used) {
            memset(&gf.games[i], 0, sizeof(game_t));
            gf.games[i].used = 1;
            gf.games[i].id = i;
            strncpy(gf.games[i].name, name, 127);
            if (package) strncpy(gf.games[i].package, package, 63);
            gf.games[i].state = GAME_STATE_UNLOADED;
            gf.games[i].perf_mode = PERF_MODE_BALANCED;
            gf.games[i].target_fps = 60;
            gf.games[i].current_fps = 60;
            gf.games[i].scene_width = 320;
            gf.games[i].scene_height = 200;
            gf.games[i].camera_zoom = 1.0f;
            gf.games[i].save_slot_count = 3;
            gf.game_count++;
            vga_printf("[GF] create game: %s\n", name);
            return i;
        }
    }
    return -1;
}

int gf_destroy_game(int game_id)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    if (gf.games[game_id].on_destroy) gf.games[game_id].on_destroy();
    gf.games[game_id].used = 0;
    gf.game_count--;
    if (gf.active_game == game_id) gf.active_game = -1;
    return 0;
}

int gf_start_game(int game_id)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    gf.games[game_id].state = GAME_STATE_RUNNING;
    gf.active_game = game_id;
    if (gf.games[game_id].on_init) gf.games[game_id].on_init();
    vga_printf("[GF] start game: %s\n", gf.games[game_id].name);
    return 0;
}

int gf_pause_game(int game_id)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    gf.games[game_id].state = GAME_STATE_PAUSED;
    if (gf.games[game_id].on_pause) gf.games[game_id].on_pause();
    return 0;
}

int gf_resume_game(int game_id)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    gf.games[game_id].state = GAME_STATE_RUNNING;
    if (gf.games[game_id].on_resume) gf.games[game_id].on_resume();
    return 0;
}

int gf_stop_game(int game_id)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    gf.games[game_id].state = GAME_STATE_READY;
    return 0;
}

int gf_get_game(int game_id, game_t *game)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used || !game) return -1;
    memcpy(game, &gf.games[game_id], sizeof(game_t));
    return 0;
}

int gf_get_active_game(void) { return gf.active_game; }

int gf_set_perf_mode(int game_id, perf_mode_t mode)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    gf.games[game_id].perf_mode = mode;
    vga_printf("[GF] performance mode: %d\n", mode);
    return 0;
}

perf_mode_t gf_get_perf_mode(int game_id)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return PERF_MODE_BALANCED;
    return gf.games[game_id].perf_mode;
}

int gf_set_target_fps(int game_id, int fps)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    gf.games[game_id].target_fps = fps;
    return 0;
}

int gf_get_current_fps(int game_id)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return 0;
    return gf.games[game_id].current_fps;
}

int gf_graphics_clear(int color)
{
    (void)color;
    return 0;
}

int gf_graphics_draw_texture(int texture_id, float x, float y, float w, float h)
{
    (void)texture_id; (void)x; (void)y; (void)w; (void)h;
    return 0;
}

int gf_graphics_draw_rect(float x, float y, float w, float h, int color)
{
    (void)x; (void)y; (void)w; (void)h; (void)color;
    return 0;
}

int gf_graphics_draw_line(float x1, float y1, float x2, float y2, int color)
{
    (void)x1; (void)y1; (void)x2; (void)y2; (void)color;
    return 0;
}

int gf_graphics_set_vsync(int enable)
{
    gf.graphics.vsync_enabled = enable ? 1 : 0;
    return 0;
}

int gf_game_mode_enable(int enable)
{
    gf.game_mode_enabled = enable ? 1 : 0;
    if (enable) {
        gf.do_not_disturb = 1;
        gf.performance_lock = 1;
        vga_puts("[GF] game mode on \n");
    } else {
        gf.do_not_disturb = 0;
        gf.performance_lock = 0;
        vga_puts("[GF] game mode close \n");
    }
    return 0;
}

int gf_game_mode_is_enabled(void) { return gf.game_mode_enabled; }

int gf_set_do_not_disturb(int enable)
{
    gf.do_not_disturb = enable ? 1 : 0;
    return 0;
}

int gf_start_screen_record(void)
{
    gf.screen_record = 1;
    vga_puts("[GF] start screen rec \n");
    return 0;
}

int gf_stop_screen_record(void)
{
    gf.screen_record = 0;
    vga_puts("[GF] stop screen rec \n");
    return 0;
}

int gf_get_perf_stats(int *fps, float *frame_time, float *cpu, float *gpu, int *mem_mb, int *temp)
{
    if (fps) *fps = gf.fps;
    if (frame_time) *frame_time = gf.frame_time_ms;
    if (cpu) *cpu = 45.0f;
    if (gpu) *gpu = 60.0f;
    if (mem_mb) *mem_mb = 128;
    if (temp) *temp = 42;
    return 0;
}

int gf_save_game(int game_id, int slot, void *data, int size)
{
    (void)game_id; (void)slot; (void)data; (void)size;
    return 0;
}

int gf_load_game(int game_id, int slot, void *data, int max_size)
{
    (void)game_id; (void)slot; (void)data; (void)max_size;
    return 0;
}

void gf_tick(void)
{
    /* 更新手柄状态 */
    for (int i = 0; i < GF_MAX_CONTROLLERS; i++) {
        if (!gf.controllers[i].used) continue;
        memcpy(gf.controllers[i].buttons_prev, gf.controllers[i].buttons, sizeof(int) * BTN_MAX);
    }
    /* 更新活动游戏 */
    if (gf.active_game >= 0 && gf.games[gf.active_game].state == GAME_STATE_RUNNING) {
        gf.games[gf.active_game].frame_count++;
        gf.games[gf.active_game].play_time += 16;
        if (gf.games[gf.active_game].on_update) gf.games[gf.active_game].on_update(0.016f);
        gf_update_particles(gf.active_game, 0.016f);
    }
}

int gf_render(void)
{
    if (gf.active_game < 0) return -1;
    if (gf.games[gf.active_game].on_render) gf.games[gf.active_game].on_render();
    return 0;
}


