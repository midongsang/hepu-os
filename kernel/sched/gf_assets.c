/* ============================================================================
 * 赫普OS - 游戏框架资源管理（从gameframework.c拆分）
 * 纹理/精灵/声音管理
 * ========================================================================== */

#include <stdint.h>
#include "gameframework.h"
#include "vga.h"
#include "string.h"

extern game_framework_t gf;

int gf_create_texture(int game_id, const char *name, int width, int height, int format, void *data)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    game_t *game = &gf.games[game_id];
    for (int i = 0; i < GF_MAX_TEXTURES; i++) {
        if (!game->textures[i].used) {
            memset(&game->textures[i], 0, sizeof(gf_texture_t));
            game->textures[i].used = 1;
            game->textures[i].id = i;
            if (name) strncpy(game->textures[i].name, name, 63);
            game->textures[i].width = width;
            game->textures[i].height = height;
            game->textures[i].format = format;
            game->textures[i].data = data;
            game->textures[i].data_size = width * height * 4;
            game->textures[i].filter_mode = 1;
            game->textures[i].wrap_mode = 0;
            game->texture_count++;
            return i;
        }
    }
    return -1;
}

int gf_destroy_texture(int game_id, int texture_id)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    game_t *game = &gf.games[game_id];
    if (texture_id < 0 || texture_id >= GF_MAX_TEXTURES || !game->textures[texture_id].used) return -1;
    game->textures[texture_id].used = 0;
    game->texture_count--;
    return 0;
}

int gf_get_texture(int game_id, int texture_id, gf_texture_t *tex)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used || !tex) return -1;
    game_t *game = &gf.games[game_id];
    if (texture_id < 0 || texture_id >= GF_MAX_TEXTURES || !game->textures[texture_id].used) return -1;
    memcpy(tex, &game->textures[texture_id], sizeof(gf_texture_t));
    return 0;
}

int gf_create_sprite(int game_id, int texture_id, float x, float y)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    game_t *game = &gf.games[game_id];
    for (int i = 0; i < GF_MAX_SPRITES; i++) {
        if (!game->sprites[i].used) {
            memset(&game->sprites[i], 0, sizeof(gf_sprite_t));
            game->sprites[i].used = 1;
            game->sprites[i].id = i;
            game->sprites[i].texture_id = texture_id;
            game->sprites[i].x = x;
            game->sprites[i].y = y;
            game->sprites[i].scale_x = 1.0f;
            game->sprites[i].scale_y = 1.0f;
            game->sprites[i].visible = 1;
            game->sprites[i].alpha = 1.0f;
            game->sprite_count++;
            return i;
        }
    }
    return -1;
}

int gf_destroy_sprite(int game_id, int sprite_id)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    game_t *game = &gf.games[game_id];
    if (sprite_id < 0 || sprite_id >= GF_MAX_SPRITES || !game->sprites[sprite_id].used) return -1;
    game->sprites[sprite_id].used = 0;
    game->sprite_count--;
    return 0;
}

int gf_sprite_set_position(int game_id, int sprite_id, float x, float y)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    game_t *game = &gf.games[game_id];
    if (sprite_id < 0 || sprite_id >= GF_MAX_SPRITES || !game->sprites[sprite_id].used) return -1;
    game->sprites[sprite_id].x = x;
    game->sprites[sprite_id].y = y;
    return 0;
}

int gf_sprite_set_scale(int game_id, int sprite_id, float sx, float sy)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    game_t *game = &gf.games[game_id];
    if (sprite_id < 0 || sprite_id >= GF_MAX_SPRITES || !game->sprites[sprite_id].used) return -1;
    game->sprites[sprite_id].scale_x = sx;
    game->sprites[sprite_id].scale_y = sy;
    return 0;
}

int gf_sprite_set_rotation(int game_id, int sprite_id, float angle)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    game_t *game = &gf.games[game_id];
    if (sprite_id < 0 || sprite_id >= GF_MAX_SPRITES || !game->sprites[sprite_id].used) return -1;
    game->sprites[sprite_id].rotation = angle;
    return 0;
}

int gf_sprite_set_visible(int game_id, int sprite_id, int visible)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    game_t *game = &gf.games[game_id];
    if (sprite_id < 0 || sprite_id >= GF_MAX_SPRITES || !game->sprites[sprite_id].used) return -1;
    game->sprites[sprite_id].visible = visible ? 1 : 0;
    return 0;
}

int gf_sprite_play_animation(int game_id, int sprite_id, int frames, float duration, int loop)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    game_t *game = &gf.games[game_id];
    if (sprite_id < 0 || sprite_id >= GF_MAX_SPRITES || !game->sprites[sprite_id].used) return -1;
    game->sprites[sprite_id].animating = 1;
    game->sprites[sprite_id].frame_count = frames;
    game->sprites[sprite_id].frame_duration = duration / frames;
    game->sprites[sprite_id].loop = loop;
    game->sprites[sprite_id].current_frame = 0;
    return 0;
}

int gf_create_sound(int game_id, const char *name, int sample_rate, int channels, void *data, uint32_t size)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    game_t *game = &gf.games[game_id];
    for (int i = 0; i < GF_MAX_SOUNDS; i++) {
        if (!game->sounds[i].used) {
            memset(&game->sounds[i], 0, sizeof(gf_sound_t));
            game->sounds[i].used = 1;
            game->sounds[i].id = i;
            if (name) strncpy(game->sounds[i].name, name, 63);
            game->sounds[i].sample_rate = sample_rate;
            game->sounds[i].channels = channels;
            game->sounds[i].data = data;
            game->sounds[i].data_size = size;
            game->sounds[i].volume = 1.0f;
            game->sounds[i].pitch = 1.0f;
            game->sound_count++;
            return i;
        }
    }
    return -1;
}

int gf_play_sound(int game_id, int sound_id)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    game_t *game = &gf.games[game_id];
    if (sound_id < 0 || sound_id >= GF_MAX_SOUNDS || !game->sounds[sound_id].used) return -1;
    return 0;
}

int gf_stop_sound(int game_id, int sound_id)
{
    (void)game_id; (void)sound_id;
    return 0;
}

int gf_set_sound_volume(int game_id, int sound_id, float volume)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    game_t *game = &gf.games[game_id];
    if (sound_id < 0 || sound_id >= GF_MAX_SOUNDS || !game->sounds[sound_id].used) return -1;
    game->sounds[sound_id].volume = volume;
    return 0;
}

int gf_create_emitter(int game_id, float x, float y)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    game_t *game = &gf.games[game_id];
    for (int i = 0; i < 16; i++) {
        if (!game->emitters[i].used) {
            memset(&game->emitters[i], 0, sizeof(gf_particle_emitter_t));
            game->emitters[i].used = 1;
            game->emitters[i].id = i;
            game->emitters[i].x = x;
            game->emitters[i].y = y;
            game->emitters[i].emission_rate = 10;
            game->emitters[i].particle_life = 1.0f;
            game->emitters[i].particle_size = 4.0f;
            game->emitters[i].particle_color = 0xFFFFFF;
            game->emitters[i].spread = 360.0f;
            game->emitters[i].speed = 50.0f;
            game->emitter_count++;
            return i;
        }
    }
    return -1;
}

int gf_emitter_start(int game_id, int emitter_id)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    game_t *game = &gf.games[game_id];
    if (emitter_id < 0 || emitter_id >= 16 || !game->emitters[emitter_id].used) return -1;
    game->emitters[emitter_id].active = 1;
    return 0;
}

int gf_emitter_stop(int game_id, int emitter_id)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    game_t *game = &gf.games[game_id];
    if (emitter_id < 0 || emitter_id >= 16 || !game->emitters[emitter_id].used) return -1;
    game->emitters[emitter_id].active = 0;
    return 0;
}

int gf_update_particles(int game_id, float dt)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    game_t *game = &gf.games[game_id];
    for (int i = 0; i < GF_MAX_PARTICLES; i++) {
        if (!game->particles[i].used) continue;
        game->particles[i].x += game->particles[i].vx * dt;
        game->particles[i].y += game->particles[i].vy * dt;
        game->particles[i].vy += 9.8f * dt;
        game->particles[i].life -= dt;
        game->particles[i].alpha = game->particles[i].life / game->particles[i].max_life;
        if (game->particles[i].life <= 0) game->particles[i].used = 0;
    }
    return 0;
}

int gf_set_camera(int game_id, float x, float y, float zoom)
{
    if (game_id < 0 || game_id >= GF_MAX_GAMES || !gf.games[game_id].used) return -1;
    gf.games[game_id].camera_x = x;
    gf.games[game_id].camera_y = y;
    gf.games[game_id].camera_zoom = zoom;
    return 0;
}

int gf_graphics_init(void) { return 0; }

int gf_graphics_begin_frame(void) { return 0; }

int gf_graphics_end_frame(void) { return 0; }

