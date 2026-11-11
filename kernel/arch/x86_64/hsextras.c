/* ============================================================================
 * 赫普OS - Hepu Desktop 高级功能实现
 * V0.10.170
 * ========================================================================== */

#include "hsextras.h"
#include "hpdesk.h"

extern void vga_puts(const char *s);
extern void gfx64_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void gfx64_draw_rect(int x, int y, int w, int h, uint32_t color);
extern void *gfx64_get_framebuffer(void);
extern int gfx64_get_width(void);
extern int gfx64_get_height(void);
extern void font64_draw_string(void *fb, int fw, int fh,
                               int x, int y, const char *str,
                               uint32_t color, uint16_t size);

/* 简单字符串操作 */
static int str_cmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

static void str_cpy(char *dst, const char *src)
{
    while (*src) *dst++ = *src++;
    *dst = 0;
}

static void str_ncpy(char *dst, const char *src, int n)
{
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* 全局状态 */
static hs_lock_screen_t g_lock;
static hs_animation_t g_animations[HS_MAX_ANIMATIONS];
static int g_anim_count = 0;
static hs_theme_def_t g_themes[HS_MAX_THEMES];
static int g_theme_count = 0;
static hs_theme_def_t *g_current_theme = NULL;
static hs_plugin_t g_plugins[HS_MAX_PLUGINS];
static int g_plugin_count = 0;

/* ============================================================================
 * 锁屏
 * ========================================================================== */

int hs_lock_init(const char *password)
{
    g_lock.locked = 0;
    g_lock.attempts = 0;
    g_lock.lock_time = 0;
    g_lock.input_len = 0;
    g_lock.input[0] = 0;

    if (password) {
        str_ncpy(g_lock.password, password, HS_MAX_PASSWORD_LEN);
    } else {
        str_cpy(g_lock.password, "hepu");  /* 默认密码 */
    }

    vga_puts("[LOCK] Screen lock initialized\n");
    return 0;
}

void hs_lock_lock(void)
{
    g_lock.locked = 1;
    g_lock.input_len = 0;
    g_lock.input[0] = 0;
    g_lock.attempts = 0;
    vga_puts("[LOCK] Screen locked\n");
}

void hs_lock_unlock(void)
{
    g_lock.locked = 0;
    g_lock.input_len = 0;
    g_lock.input[0] = 0;
    vga_puts("[LOCK] Screen unlocked\n");
}

int hs_lock_is_locked(void)
{
    return g_lock.locked;
}

void hs_lock_handle_key(int key)
{
    if (!g_lock.locked) return;

    if (key == '\n' || key == '\r') {
        /* 验证密码 */
        if (str_cmp(g_lock.input, g_lock.password) == 0) {
            hs_lock_unlock();
        } else {
            g_lock.attempts++;
            g_lock.input_len = 0;
            g_lock.input[0] = 0;
        }
    } else if (key == '\b' || key == 127) {
        /* 退格 */
        if (g_lock.input_len > 0) {
            g_lock.input_len--;
            g_lock.input[g_lock.input_len] = 0;
        }
    } else if (key >= 32 && key < 127 && g_lock.input_len < HS_MAX_PASSWORD_LEN - 1) {
        /* 可打印字符 */
        g_lock.input[g_lock.input_len++] = key;
        g_lock.input[g_lock.input_len] = 0;
    }
}

void hs_lock_render(void)
{
    if (!g_lock.locked) return;

    int w = gfx64_get_width();
    int h = gfx64_get_height();
    void *fb = gfx64_get_framebuffer();

    /* 半透明黑色背景 */
    gfx64_fill_rect(0, 0, w, h, 0x000000);

    /* 锁屏框 */
    int box_w = 320;
    int box_h = 200;
    int box_x = (w - box_w) / 2;
    int box_y = (h - box_h) / 2;

    gfx64_fill_rect(box_x, box_y, box_w, box_h, 0x1A1B26);
    gfx64_draw_rect(box_x, box_y, box_w, box_h, 0x7AA2F7);

    /* 标题 */
    font64_draw_string(fb, w, h, box_x + box_w/2 - 40, box_y + 20,
                       "HepuOS Locked", 0xC0CAF5, 14);

    /* 密码输入框 */
    int input_y = box_y + 80;
    gfx64_fill_rect(box_x + 20, input_y, box_w - 40, 30, 0x24283B);
    gfx64_draw_rect(box_x + 20, input_y, box_w - 40, 30, 0x3B4261);

    /* 显示星号 */
    char stars[HS_MAX_PASSWORD_LEN + 1];
    for (int i = 0; i < g_lock.input_len; i++) stars[i] = '*';
    stars[g_lock.input_len] = 0;
    font64_draw_string(fb, w, h, box_x + 30, input_y + 8,
                       stars, 0xA9B1D3, 12);

    /* 提示 */
    font64_draw_string(fb, w, h, box_x + 20, box_y + 130,
                       "Enter password to unlock", 0x6B7280, 10);

    /* 错误提示 */
    if (g_lock.attempts > 0) {
        char msg[64];
        str_cpy(msg, "Invalid password (attempt ");
        int n = g_lock.attempts, i = 24;
        if (n == 0) msg[i++] = '0';
        while (n > 0) { msg[i++] = '0' + n % 10; n /= 10; }
        msg[i++] = ')';
        msg[i] = 0;
        font64_draw_string(fb, w, h, box_x + 20, box_y + 155,
                           msg, 0xF44336, 10);
    }
}

/* ============================================================================
 * 动画系统
 * ========================================================================== */

int hs_animation_init(void)
{
    g_anim_count = 0;
    for (int i = 0; i < HS_MAX_ANIMATIONS; i++) {
        g_animations[i].state = HS_ANIM_IDLE;
    }
    vga_puts("[ANIM] Animation system initialized\n");
    return 0;
}

int hs_animation_create(hs_anim_type_t type, int x, int y, int w, int h,
                        int duration, void *target,
                        void (*on_complete)(void *))
{
    if (g_anim_count >= HS_MAX_ANIMATIONS) return -1;

    hs_animation_t *anim = &g_animations[g_anim_count];
    anim->id = g_anim_count;
    anim->type = type;
    anim->state = HS_ANIM_IDLE;
    anim->x = x; anim->y = y;
    anim->width = w; anim->height = h;
    anim->start_x = x; anim->start_y = y;
    anim->start_w = w; anim->start_h = h;
    anim->end_x = x; anim->end_y = y;
    anim->end_w = w; anim->end_h = h;
    anim->duration = duration;
    anim->elapsed = 0;
    anim->alpha = 255;
    anim->target = target;
    anim->on_complete = on_complete;

    return g_anim_count++;
}

void hs_animation_start(int id)
{
    if (id < 0 || id >= g_anim_count) return;
    g_animations[id].state = HS_ANIM_RUNNING;
    g_animations[id].elapsed = 0;
}

void hs_animation_stop(int id)
{
    if (id < 0 || id >= g_anim_count) return;
    g_animations[id].state = HS_ANIM_IDLE;
}

void hs_animation_update(int delta_ms)
{
    for (int i = 0; i < g_anim_count; i++) {
        hs_animation_t *anim = &g_animations[i];
        if (anim->state != HS_ANIM_RUNNING) continue;

        anim->elapsed += delta_ms;
        if (anim->elapsed >= anim->duration) {
            anim->state = HS_ANIM_DONE;
            anim->x = anim->end_x;
            anim->y = anim->end_y;
            anim->width = anim->end_w;
            anim->height = anim->end_h;
            anim->alpha = 255;
            if (anim->on_complete) anim->on_complete(anim->target);
        } else {
            float t = (float)anim->elapsed / anim->duration;
            /* 缓动函数：ease-in-out */
            float ease = t < 0.5 ? 2*t*t : -1+(4-2*t)*t;

            anim->x = anim->start_x + (int)((anim->end_x - anim->start_x) * ease);
            anim->y = anim->start_y + (int)((anim->end_y - anim->start_y) * ease);
            anim->width = anim->start_w + (int)((anim->end_w - anim->start_w) * ease);
            anim->height = anim->start_h + (int)((anim->end_h - anim->start_h) * ease);

            if (anim->type == HS_ANIM_FADE) {
                anim->alpha = (int)(255 * ease);
            }
        }
    }
}

void hs_animation_render(int id)
{
    if (id < 0 || id >= g_anim_count) return;
    hs_animation_t *anim = &g_animations[id];
    if (anim->state == HS_ANIM_IDLE) return;

    /* 渲染动画帧（简化：半透明矩形） */
    if (anim->alpha < 255) {
        /* 简化：直接渲染，不做真正的alpha混合 */
        gfx64_fill_rect(anim->x, anim->y, anim->width, anim->height,
                        0x7AA2F7);
    }
}

int hs_animation_is_done(int id)
{
    if (id < 0 || id >= g_anim_count) return 1;
    return g_animations[id].state == HS_ANIM_DONE;
}

void hs_animation_cancel_all(void)
{
    for (int i = 0; i < g_anim_count; i++) {
        g_animations[i].state = HS_ANIM_IDLE;
    }
}

/* ============================================================================
 * 主题引擎
 * ========================================================================== */

int hs_theme_engine_init(void)
{
    g_theme_count = 0;

    /* 注册默认主题 */
    hs_theme_def_t default_theme = {
        .name = "default",
        .bg = 0x1A1B26, .panel_bg = 0x16161E, .panel_fg = 0xA9B1D3,
        .border = 0x3B4261, .accent = 0x7AA2F7,
        .window_bg = 0x24283B, .window_fg = 0xC0CAF5,
        .titlebar_bg = 0x1F2335,
        .workspace_active = 0x7AA2F7, .workspace_inactive = 0x3B4261
    };
    hs_theme_register(&default_theme);

    /* 注册极简黑白主题 */
    hs_theme_def_t mono_theme = {
        .name = "mono",
        .bg = 0x000000, .panel_bg = 0x111111, .panel_fg = 0xFFFFFF,
        .border = 0x333333, .accent = 0xFFFFFF,
        .window_bg = 0x0A0A0A, .window_fg = 0xEEEEEE,
        .titlebar_bg = 0x1A1A1A,
        .workspace_active = 0xFFFFFF, .workspace_inactive = 0x444444
    };
    hs_theme_register(&mono_theme);

    /* 注册浅色主题 */
    hs_theme_def_t light_theme = {
        .name = "light",
        .bg = 0xF4F3EE, .panel_bg = 0xFFFFFF, .panel_fg = 0x1A1B1C,
        .border = 0xE4E3DD, .accent = 0x9EACEA,
        .window_bg = 0xFFFFFF, .window_fg = 0x1A1B1C,
        .titlebar_bg = 0xF4F3EE,
        .workspace_active = 0x9EACEA, .workspace_inactive = 0xE4E3DD
    };
    hs_theme_register(&light_theme);

    g_current_theme = &g_themes[0];
    vga_puts("[THEME] Theme engine initialized, themes: ");
    char buf[8];
    int n = g_theme_count, i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    vga_puts(buf);
    vga_puts("\n");

    return 0;
}

int hs_theme_register(hs_theme_def_t *theme)
{
    if (g_theme_count >= HS_MAX_THEMES || !theme) return -1;
    g_themes[g_theme_count] = *theme;
    return g_theme_count++;
}

int hs_theme_load(const char *name)
{
    for (int i = 0; i < g_theme_count; i++) {
        if (str_cmp(g_themes[i].name, name) == 0) {
            hs_theme_apply(&g_themes[i]);
            return 0;
        }
    }
    return -1;
}

hs_theme_def_t *hs_theme_get_current(void)
{
    return g_current_theme;
}

hs_theme_def_t *hs_theme_get_list(int *count)
{
    if (count) *count = g_theme_count;
    return g_themes;
}

void hs_theme_apply(hs_theme_def_t *theme)
{
    if (!theme) return;
    g_current_theme = theme;

    /* 应用到Hepu Desktop */
    g_shell.colors.bg = theme->bg;
    g_shell.colors.panel_bg = theme->panel_bg;
    g_shell.colors.panel_fg = theme->panel_fg;
    g_shell.colors.border = theme->border;
    g_shell.colors.accent = theme->accent;
    g_shell.colors.window_bg = theme->window_bg;
    g_shell.colors.window_fg = theme->window_fg;
    g_shell.colors.titlebar_bg = theme->titlebar_bg;
    g_shell.colors.workspace_active = theme->workspace_active;
    g_shell.colors.workspace_inactive = theme->workspace_inactive;

    vga_puts("[THEME] Applied: ");
    vga_puts(theme->name);
    vga_puts("\n");
}

/* ============================================================================
 * 插件系统
 * ========================================================================== */

int hs_plugin_init(void)
{
    g_plugin_count = 0;
    for (int i = 0; i < HS_MAX_PLUGINS; i++) {
        g_plugins[i].loaded = 0;
        g_plugins[i].enabled = 0;
    }
    vga_puts("[PLUGIN] Plugin system initialized\n");
    return 0;
}

int hs_plugin_register(hs_plugin_type_t type, const char *name,
                       const char *version,
                       void (*init)(void),
                       void (*render)(int x, int y),
                       void (*update)(void),
                       void (*shutdown)(void))
{
    if (g_plugin_count >= HS_MAX_PLUGINS) return -1;

    hs_plugin_t *p = &g_plugins[g_plugin_count];
    p->id = g_plugin_count;
    p->type = type;
    str_ncpy(p->name, name, 32);
    str_ncpy(p->version, version, 16);
    p->enabled = 1;
    p->loaded = 0;
    p->init = init;
    p->render = render;
    p->update = update;
    p->shutdown = shutdown;

    vga_puts("[PLUGIN] Registered: ");
    vga_puts(name);
    vga_puts(" v");
    vga_puts(version);
    vga_puts("\n");

    return g_plugin_count++;
}

void hs_plugin_load_all(void)
{
    for (int i = 0; i < g_plugin_count; i++) {
        hs_plugin_t *p = &g_plugins[i];
        if (p->enabled && !p->loaded && p->init) {
            p->init();
            p->loaded = 1;
        }
    }
}

void hs_plugin_update_all(void)
{
    for (int i = 0; i < g_plugin_count; i++) {
        hs_plugin_t *p = &g_plugins[i];
        if (p->enabled && p->loaded && p->update) {
            p->update();
        }
    }
}

void hs_plugin_render_all(int x, int y)
{
    int cur_x = x;
    for (int i = 0; i < g_plugin_count; i++) {
        hs_plugin_t *p = &g_plugins[i];
        if (p->enabled && p->loaded && p->render && p->type == HS_PLUGIN_WIDGET) {
            p->render(cur_x, y);
            cur_x += 60;  /* 每个小部件占60像素 */
        }
    }
}

hs_plugin_t *hs_plugin_get(int id)
{
    if (id < 0 || id >= g_plugin_count) return NULL;
    return &g_plugins[id];
}

int hs_plugin_count(void)
{
    return g_plugin_count;
}
