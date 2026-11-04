#ifndef GAMEFRAMEWORK_H
#define GAMEFRAMEWORK_H

#include <stdint.h>

#define GF_MAX_GAMES 16
#define GF_MAX_CONTROLLERS 8
#define GF_MAX_TEXTURES 128
#define GF_MAX_SPRITES 256
#define GF_MAX_SOUNDS 64
#define GF_MAX_PARTICLES 1024

/* 游戏状态 */
typedef enum {
    GAME_STATE_UNLOADED = 0,
    GAME_STATE_LOADING,
    GAME_STATE_READY,
    GAME_STATE_RUNNING,
    GAME_STATE_PAUSED,
    GAME_STATE_SAVING,
    GAME_STATE_ERROR,
} game_state_t;

/* 性能模式 */
typedef enum {
    PERF_MODE_POWER_SAVER = 0,
    PERF_MODE_BALANCED,
    PERF_MODE_PERFORMANCE,
    PERF_MODE_GAME,
    PERF_MODE_BOOST,
} perf_mode_t;

/* 手柄类型 */
typedef enum {
    CTRL_TYPE_UNKNOWN = 0,
    CTRL_TYPE_GAMEPAD,
    CTRL_TYPE_JOYSTICK,
    CTRL_TYPE_WHEEL,
    CTRL_TYPE_FLIGHTSTICK,
    CTRL_TYPE_DANCEPAD,
    CTRL_TYPE_GUITAR,
    CTRL_TYPE_DRUM,
} controller_type_t;

/* 手柄按钮 */
typedef enum {
    BTN_A = 0, BTN_B, BTN_X, BTN_Y,
    BTN_L1, BTN_R1, BTN_L2, BTN_R2,
    BTN_L3, BTN_R3,
    BTN_START, BTN_SELECT, BTN_HOME,
    BTN_DPAD_UP, BTN_DPAD_DOWN, BTN_DPAD_LEFT, BTN_DPAD_RIGHT,
    BTN_TOUCHPAD,
    BTN_MAX,
} controller_button_t;

/* 手柄状态 */
typedef struct {
    int used;
    int id;
    controller_type_t type;
    char name[64];
    int connected;
    int buttons[BTN_MAX];
    int buttons_prev[BTN_MAX];
    float axis_left_x, axis_left_y;
    float axis_right_x, axis_right_y;
    float trigger_left, trigger_right;
    int battery_level;
    int rumble_support;
    float rumble_left, rumble_right;
    uint64_t last_packet;
} controller_t;

/* 纹理 */
typedef struct {
    int used;
    int id;
    char name[64];
    int width, height;
    int format;  /* 0=RGBA8888, 1=RGB565, 2=Alpha8 */
    void *data;
    uint32_t data_size;
    int gpu_handle;
    int filter_mode;  /* 0=nearest, 1=linear */
    int wrap_mode;    /* 0=clamp, 1=repeat */
} gf_texture_t;

/* 精灵 */
typedef struct {
    int used;
    int id;
    int texture_id;
    float x, y;
    float width, height;
    float scale_x, scale_y;
    float rotation;
    float pivot_x, pivot_y;
    int visible;
    int layer;
    float alpha;
    int color_tint;
    /* 动画 */
    int animating;
    int current_frame;
    int frame_count;
    float frame_time;
    float frame_duration;
    int loop;
} gf_sprite_t;

/* 声音 */
typedef struct {
    int used;
    int id;
    char name[64];
    int format;  /* 0=PCM16, 1=PCM8, 2=ADPCM */
    int sample_rate;
    int channels;
    void *data;
    uint32_t data_size;
    float volume;
    int looping;
    float pitch;
} gf_sound_t;

/* 粒子 */
typedef struct {
    int used;
    float x, y;
    float vx, vy;
    float life;
    float max_life;
    float size;
    int color;
    float alpha;
} gf_particle_t;

/* 粒子发射器 */
typedef struct {
    int used;
    int id;
    float x, y;
    int emission_rate;
    float particle_life;
    float particle_size;
    int particle_color;
    float spread;
    float speed;
    int gravity;
    int active;
} gf_particle_emitter_t;

/* 游戏实例 */
typedef struct {
    int used;
    int id;
    char name[128];
    char package[64];
    game_state_t state;
    perf_mode_t perf_mode;
    int target_fps;
    int current_fps;
    float delta_time;
    uint64_t frame_count;
    uint64_t play_time;
    /* 资源 */
    gf_texture_t textures[GF_MAX_TEXTURES];
    int texture_count;
    gf_sprite_t sprites[GF_MAX_SPRITES];
    int sprite_count;
    gf_sound_t sounds[GF_MAX_SOUNDS];
    int sound_count;
    gf_particle_t particles[GF_MAX_PARTICLES];
    int particle_count;
    gf_particle_emitter_t emitters[16];
    int emitter_count;
    /* 场景 */
    int scene_width, scene_height;
    float camera_x, camera_y;
    float camera_zoom;
    /* 回调 */
    void (*on_init)(void);
    void (*on_update)(float dt);
    void (*on_render)(void);
    void (*on_pause)(void);
    void (*on_resume)(void);
    void (*on_destroy)(void);
    /* 存档 */
    int save_slot_count;
    uint64_t last_save_time;
} game_t;

/* 图形API (Vulkan式简化) */
typedef struct {
    int initialized;
    int api_version;
    int max_texture_size;
    int max_anisotropy;
    int support_depth_buffer;
    int support_msaa;
    int msaa_level;
    int vsync_enabled;
    int framebuffer_width, framebuffer_height;
    void *framebuffer;
} gf_graphics_t;

/* 游戏框架状态 */
typedef struct {
    int initialized;
    game_t games[GF_MAX_GAMES];
    int game_count;
    int active_game;
    controller_t controllers[GF_MAX_CONTROLLERS];
    int controller_count;
    gf_graphics_t graphics;
    /* 性能监控 */
    int fps;
    float frame_time_ms;
    float cpu_usage;
    float gpu_usage;
    int memory_usage_mb;
    int temperature;
    /* 游戏模式 */
    int game_mode_enabled;
    int do_not_disturb;
    int performance_lock;
    int screen_record;
} game_framework_t;

/* 初始化游戏框架 */
void gameframework_init(void);

/* 游戏管理 */
int gf_create_game(const char *name, const char *package);
int gf_destroy_game(int game_id);
int gf_start_game(int game_id);
int gf_pause_game(int game_id);
int gf_resume_game(int game_id);
int gf_stop_game(int game_id);
int gf_get_game(int game_id, game_t *game);
int gf_get_active_game(void);

/* 性能模式 */
int gf_set_perf_mode(int game_id, perf_mode_t mode);
perf_mode_t gf_get_perf_mode(int game_id);
int gf_set_target_fps(int game_id, int fps);
int gf_get_current_fps(int game_id);

/* 手柄 */
int gf_controller_count(void);
int gf_get_controller(int controller_id, controller_t *ctrl);
int gf_is_button_pressed(int controller_id, controller_button_t button);
int gf_is_button_just_pressed(int controller_id, controller_button_t button);
float gf_get_axis(int controller_id, int axis);
int gf_set_rumble(int controller_id, float left, float right);
int gf_vibrate_controller(int controller_id, int duration_ms);

/* 纹理 */
int gf_create_texture(int game_id, const char *name, int width, int height, int format, void *data);
int gf_destroy_texture(int game_id, int texture_id);
int gf_get_texture(int game_id, int texture_id, gf_texture_t *tex);

/* 精灵 */
int gf_create_sprite(int game_id, int texture_id, float x, float y);
int gf_destroy_sprite(int game_id, int sprite_id);
int gf_sprite_set_position(int game_id, int sprite_id, float x, float y);
int gf_sprite_set_scale(int game_id, int sprite_id, float sx, float sy);
int gf_sprite_set_rotation(int game_id, int sprite_id, float angle);
int gf_sprite_set_visible(int game_id, int sprite_id, int visible);
int gf_sprite_play_animation(int game_id, int sprite_id, int frames, float duration, int loop);

/* 声音 */
int gf_create_sound(int game_id, const char *name, int sample_rate, int channels, void *data, uint32_t size);
int gf_play_sound(int game_id, int sound_id);
int gf_stop_sound(int game_id, int sound_id);
int gf_set_sound_volume(int game_id, int sound_id, float volume);

/* 粒子 */
int gf_create_emitter(int game_id, float x, float y);
int gf_emitter_start(int game_id, int emitter_id);
int gf_emitter_stop(int game_id, int emitter_id);
int gf_update_particles(int game_id, float dt);

/* 相机 */
int gf_set_camera(int game_id, float x, float y, float zoom);

/* 图形API */
int gf_graphics_init(void);
int gf_graphics_begin_frame(void);
int gf_graphics_end_frame(void);
int gf_graphics_clear(int color);
int gf_graphics_draw_texture(int texture_id, float x, float y, float w, float h);
int gf_graphics_draw_rect(float x, float y, float w, float h, int color);
int gf_graphics_draw_line(float x1, float y1, float x2, float y2, int color);
int gf_graphics_set_vsync(int enable);

/* 游戏模式 */
int gf_game_mode_enable(int enable);
int gf_game_mode_is_enabled(void);
int gf_set_do_not_disturb(int enable);
int gf_start_screen_record(void);
int gf_stop_screen_record(void);

/* 性能监控 */
int gf_get_perf_stats(int *fps, float *frame_time, float *cpu, float *gpu, int *mem_mb, int *temp);

/* 存档 */
int gf_save_game(int game_id, int slot, void *data, int size);
int gf_load_game(int game_id, int slot, void *data, int max_size);

/* 主循环 */
void gf_tick(void);
int gf_render(void);

#endif /* GAMEFRAMEWORK_H */
