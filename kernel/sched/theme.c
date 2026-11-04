/* ============================================================================
 * 赫普OS - 主题体系 (V2.35)
 * 默认主题 "Hepu Blue": 所有 UI 颜色/尺寸统一入口。
 * ========================================================================== */

#include "theme.h"
#include "vga_gfx.h"

hepu_theme_t hepu_theme;
static hepu_theme_id_t current_theme = THEME_HEPU_BLUE;

/* 三套预设主题 */
static void theme_set_hepu_blue(void)
{
    hepu_theme.desktop_bg      = GFX_COLOR_BLACK;
    hepu_theme.wallpaper_top   = 0x11;
    hepu_theme.wallpaper_bot   = 0x01;
    hepu_theme.title_active    = GFX_COLOR_LIGHTBLUE;
    hepu_theme.title_inactive  = GFX_COLOR_BLUE;
    hepu_theme.title_text      = GFX_COLOR_WHITE;
    hepu_theme.window_bg       = GFX_COLOR_LIGHTGREY;
    hepu_theme.border          = GFX_COLOR_DARKGREY;
    hepu_theme.button          = GFX_COLOR_LIGHTGREY;
    hepu_theme.button_press    = GFX_COLOR_DARKGREY;
    hepu_theme.button_disabled = 0x07;
    hepu_theme.button_text     = GFX_COLOR_BLACK;
    hepu_theme.label_text      = GFX_COLOR_BLACK;
    hepu_theme.input_bg        = GFX_COLOR_WHITE;
    hepu_theme.input_text      = GFX_COLOR_BLACK;
    hepu_theme.focus           = GFX_COLOR_YELLOW;
    hepu_theme.select          = GFX_COLOR_LIGHTBLUE;
    hepu_theme.select_text     = GFX_COLOR_WHITE;
    hepu_theme.taskbar         = GFX_COLOR_DARKGREY;
    hepu_theme.taskbar_active  = GFX_COLOR_LIGHTBLUE;
    hepu_theme.warn            = GFX_COLOR_LIGHTRED;
    hepu_theme.ok              = GFX_COLOR_LIGHTGREEN;
    hepu_theme.close_btn       = GFX_COLOR_RED;
    hepu_theme.min_btn         = GFX_COLOR_DARKGREY;
}

static void theme_set_dark(void)
{
    hepu_theme.desktop_bg      = 0x00;
    hepu_theme.wallpaper_top   = 0x08;
    hepu_theme.wallpaper_bot   = 0x00;
    hepu_theme.title_active    = 0x0B;
    hepu_theme.title_inactive  = 0x08;
    hepu_theme.title_text      = 0x0F;
    hepu_theme.window_bg       = 0x08;
    hepu_theme.border          = 0x07;
    hepu_theme.button          = 0x07;
    hepu_theme.button_press    = 0x08;
    hepu_theme.button_disabled = 0x08;
    hepu_theme.button_text     = 0x0F;
    hepu_theme.label_text      = 0x0F;
    hepu_theme.input_bg        = 0x00;
    hepu_theme.input_text      = 0x0F;
    hepu_theme.focus           = 0x0E;
    hepu_theme.select          = 0x01;
    hepu_theme.select_text     = 0x0F;
    hepu_theme.taskbar         = 0x08;
    hepu_theme.taskbar_active  = 0x0B;
    hepu_theme.warn            = 0x0C;
    hepu_theme.ok              = 0x0A;
    hepu_theme.close_btn       = 0x04;
    hepu_theme.min_btn         = 0x08;
}

static void theme_set_oled_black(void)
{
    hepu_theme.desktop_bg      = 0x00;
    hepu_theme.wallpaper_top   = 0x00;
    hepu_theme.wallpaper_bot   = 0x00;
    hepu_theme.title_active    = 0x0A;
    hepu_theme.title_inactive  = 0x00;
    hepu_theme.title_text      = 0x0F;
    hepu_theme.window_bg       = 0x00;
    hepu_theme.border          = 0x0A;
    hepu_theme.button          = 0x00;
    hepu_theme.button_press    = 0x02;
    hepu_theme.button_disabled = 0x00;
    hepu_theme.button_text     = 0x0A;
    hepu_theme.label_text      = 0x0A;
    hepu_theme.input_bg        = 0x00;
    hepu_theme.input_text      = 0x0A;
    hepu_theme.focus           = 0x0E;
    hepu_theme.select          = 0x02;
    hepu_theme.select_text     = 0x0F;
    hepu_theme.taskbar         = 0x00;
    hepu_theme.taskbar_active  = 0x0A;
    hepu_theme.warn            = 0x0C;
    hepu_theme.ok              = 0x0A;
    hepu_theme.close_btn       = 0x04;
    hepu_theme.min_btn         = 0x00;
}

/* V3.82: TRON 主题——深黑底 + 电光青霓虹 (TRON 标志性未来感) */
static void theme_set_tron(void)
{
    hepu_theme.desktop_bg      = 0x00;
    hepu_theme.wallpaper_top   = 0x0B;   /* 电光青 */
    hepu_theme.wallpaper_bot   = 0x01;   /* 深蓝 */
    hepu_theme.title_active    = 0x0B;
    hepu_theme.title_inactive  = 0x01;
    hepu_theme.title_text      = 0x00;
    hepu_theme.window_bg       = 0x00;
    hepu_theme.border          = 0x0B;   /* 霓虹青边框 */
    hepu_theme.button          = 0x01;
    hepu_theme.button_press    = 0x0B;
    hepu_theme.button_disabled = 0x08;
    hepu_theme.button_text     = 0x0B;
    hepu_theme.label_text      = 0x0B;
    hepu_theme.input_bg        = 0x00;
    hepu_theme.input_text      = 0x0B;
    hepu_theme.focus           = 0x0B;
    hepu_theme.select          = 0x01;
    hepu_theme.select_text     = 0x0F;
    hepu_theme.taskbar         = 0x01;
    hepu_theme.taskbar_active  = 0x0B;
    hepu_theme.warn            = 0x0C;
    hepu_theme.ok              = 0x0A;
    hepu_theme.close_btn       = 0x04;
    hepu_theme.min_btn         = 0x01;
}

const char *theme_apply(hepu_theme_id_t id)
{
    current_theme = id;
    switch (id) {
        case THEME_DARK:       theme_set_dark();       return "Dark";
        case THEME_OLED_BLACK: theme_set_oled_black(); return "OLED Black";
        case THEME_TRON:       theme_set_tron();       return "TRON";
        default:               theme_set_hepu_blue(); return "Hepu Blue";
    }
}

hepu_theme_id_t theme_current(void) { return current_theme; }

void theme_init(void)
{
    theme_apply(THEME_HEPU_BLUE);

    /* 尺寸 */
    hepu_theme.title_h    = 12;
    hepu_theme.border_w   = 1;
    hepu_theme.btn_h      = 14;
    hepu_theme.input_h    = 14;
    hepu_theme.taskbar_h  = 14;
    hepu_theme.icon_w     = 32;
    hepu_theme.icon_h     = 32;
    hepu_theme.icon_gap   = 18;
}
