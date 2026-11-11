/* ============================================================================
 * 赫普OS - HepuShell 桌面环境实现
 * 复现 Quickshell 核心功能
 * V0.10.160
 * ========================================================================== */

#include "hepushell.h"
#include "l10n64.h"
#include "hsqml.h"
#include "hs_apps.h"
#include "hs_systools.h"
#include "hsinput.h"

/* V0.13.10: 缺失接口恢复 — 切换器渲染前向声明 */
static void hs_switcher_render(void);

/* V0.13.39: 设置页使用 */
static void hs_itoa(int v, char *buf);

/* V0.13.14: 统一矢量图标引擎前向声明(窗口标题栏先于定义使用) */
static void hs_icon_wifi(int x, int y, int size, uint32_t color);
static void hs_icon_battery(int x, int y, int size, uint32_t color, int percent);
static void hs_icon_volume(int x, int y, int size, uint32_t color);
static void hs_icon_settings(int x, int y, int size, uint32_t color);
static void hs_icon_power(int x, int y, int size, uint32_t color);
void hs_icon_folder(int x, int y, int size, uint32_t color);
void hs_icon_doc(int x, int y, int size, uint32_t color);
static void hs_icon_terminal(int x, int y, int size, uint32_t color);
static void hs_icon_task(int x, int y, int size, uint32_t color);
static void hs_icon_browser(int x, int y, int size, uint32_t color);
static void hs_icon_editor(int x, int y, int size, uint32_t color);
static void hs_icon_calc(int x, int y, int size, uint32_t color);
static void hs_icon_store(int x, int y, int size, uint32_t color);
static void hs_icon_info(int x, int y, int size, uint32_t color);
void hs_icon_media(int x, int y, int size, uint32_t color);
void hs_icon_file(int x, int y, int size, uint32_t color);

/* V0.13.10: 天气数据 (真实网络拉取, 渲染层消费) */
static char g_weather_temp[8];
static char g_weather_desc[24];
static int g_weather_ok = 0;

/* V0.13.10: 窗口切换器状态 (Alt+Tab LRU 卡片) */
static int g_sw_active = 0;
static int g_sw_sel = 0;
static int g_sw_count = 0;
static int g_sw_order[HS_MAX_WINDOWS];

/* 外部函数 */
extern void gfx64_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void gfx64_draw_rect(int x, int y, int w, int h, uint32_t color);
extern void gfx64_fill_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
extern void gfx64_draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
extern void gfx64_blur_region(int x, int y, int w, int h, int blur_radius, uint32_t overlay_color, int overlay_alpha);
extern void gfx64_fill_circle_gradient(int cx, int cy, int r, uint32_t c1, uint32_t c2);
extern uint32_t gfx64_get_pixel(int x, int y);
extern void font64_draw_string(void *fb, int fw, int fh,
                               int x, int y, const char *str,
                               uint32_t color, uint16_t size);
extern void *gfx64_get_framebuffer(void);
extern int gfx64_get_width(void);
extern int gfx64_get_height(void);
extern void vga_puts(const char *s);

/* SMP渲染锁：防止多CPU同时渲染导致竞争 */
typedef struct { volatile int lock; } render_lock_t;
static render_lock_t g_render_lock = {0};

/* 字符串函数兼容 */
extern int strcmp(const char *s1, const char *s2);
extern char *strcpy(char *dst, const char *src);
#define str_cmp strcmp
#define str_cpy strcpy

/* RTC */
extern uint8_t rtc64_get_seconds(void);
extern uint8_t rtc64_get_minutes(void);
extern uint8_t rtc64_get_hours(void);

/* 内存 */
extern void pmm64_get_stats(uint64_t *total, uint64_t *used, uint64_t *free);

/* SMP */
extern uint32_t smp_get_cpu_count(void);

/* PIT 毫秒计数 (窗口打开动画, 对齐 Clavis 200ms) */
extern uint64_t pit64_get_ticks(void);

/* 自适应缩放辅助函数（基准1024x768，纯整数运算避免FPU状态问题） */
static int hs_sx(int v) { return (v * g_shell.screen_width) / 1024; }
static int hs_sy(int v) { return (v * g_shell.screen_height) / 768; }
static int hs_s(int v)  {
    int sx = (v * g_shell.screen_width) / 1024;
    int sy = (v * g_shell.screen_height) / 768;
    return (sx < sy) ? sx : sy;
}

/* 全局实例 */
hepushell_t g_shell;

/* 渲染帧计数 (动画时基, 桌面循环约 60fps) */
static uint32_t g_frame_no;

/* 前向声明（定义在后面） */
static int g_sidebar_tab;
static int g_island_expanded;
int g_pending_app;

/* V0.55: 侧栏滚动 (对齐 Clavis SidebarFlickable: 垂直滚动+细滚动条)
 * 基准单位(1024x768), 渲染时经 hs_sy 缩放; 仅内容溢出的标签页生效 */
static int g_sidebar_scroll = 0;

/* V0.56: 程序化壁纸 (对齐 Clavis Wallpaper 切换 + matugen 壁纸取色)
 * 0=Classic 纯色(默认, 保回归稳定) 1=Dusk 紫橙落日 2=Ocean 青蓝渐变;
 * 切换到非 Classic 后下一帧壁纸绘制完成即采样网格提取源色并应用主题 */
static int g_wallpaper_style = 0;
static int g_wallpaper_dirty = 0;
static const char *g_wallpaper_names[] = {"Classic", "Dusk", "Ocean"};

/* V0.54.13: 桌面背景色预设色板 + 持久化 (Settings/Appearance 页与右键面板共用) */
static int g_color_panel_visible = 0;

static const uint32_t g_color_presets[9] = {
    0xF7F3EA, 0xFAF6EE, 0xE8DFD0,   /* 暖白系 */
    0x1C1B1F, 0x243447, 0x2E3440,   /* 深灰系 */
    0x0B3B4F, 0x1B3A2F, 0x3A1F2B,   /* 冷蓝 / 松绿 / 酒红 */
};

/* 把 "#RRGGBB" 写进配置文件的 Theme.background 行 */
static void hs_save_bg_color(uint32_t c)
{
    extern int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size);
    extern int ext2_64_write_file(const char *path, const uint8_t *data, uint32_t size);
    static char buf[4096];
    static const char hx[] = "0123456789ABCDEF";

    int n = ext2_64_read_file("/etc/hepushell/config.hsqml", (uint8_t *)buf, 4095);
    if (n <= 0) return;
    buf[n] = 0;

    char hex[8];
    hex[0] = '#';
    hex[1] = hx[(c >> 20) & 0xF];
    hex[2] = hx[(c >> 16) & 0xF];
    hex[3] = hx[(c >> 12) & 0xF];
    hex[4] = hx[(c >> 8) & 0xF];
    hex[5] = hx[(c >> 4) & 0xF];
    hex[6] = hx[c & 0xF];
    hex[7] = 0;

    for (int i = 0; i + 12 < n; i++) {
        if (buf[i] == 'b' && buf[i+1] == 'a' && buf[i+2] == 'c' &&
            buf[i+3] == 'k' && buf[i+4] == 'g' && buf[i+5] == 'r' &&
            buf[i+6] == 'o' && buf[i+7] == 'u' && buf[i+8] == 'n' &&
            buf[i+9] == 'd' && buf[i+10] == ':') {
            int j = i + 10;
            while (j < n && buf[j] != '#' && buf[j] != '\n') j++;
            if (j < n && buf[j] == '#' && j + 7 <= n) {
                for (int k = 0; k < 7; k++) buf[j + k] = hex[k];
                ext2_64_write_file("/etc/hepushell/config.hsqml", (uint8_t *)buf, (uint32_t)n);
            }
            break;
        }
    }
}

/* 右键菜单状态 */
static int g_context_menu_visible = 0;
static int g_context_menu_x = 0;
static int g_context_menu_y = 0;
/* V0.13.39: 通知位置配置 (0=右上 1=左上 2=右下 3=左下) */
static int g_notif_pos = 0;

/* 窗口拖拽状态（定义在后面） */
static hs_window_t *g_drag_window;
static int g_drag_offset_x;
static int g_drag_offset_y;
static int g_dragging;

/* 设置窗口当前页面 */
static int g_settings_page = 0;  /* 0=Appearance, 1=Panels, 2=System, 3=About */
static int g_settings_storage_ry = 0; /* V0.13.39: 存储管理行点击区域 */
static int g_settings_reg_ry = 0;    /* V0.13.39: 注册表查看行 */
static int g_settings_assoc_ry = 0;  /* V0.13.39: 文件关联查看行 */

/* 内置默认配置（hsqml格式） — 对齐 quickshell/Clavis: M3 tonal-spot dark */
static const char *default_config =
"// HepuShell 默认配置 - 对齐 quickshell/Clavis (Material You tonal-spot dark)\n"
"Theme {\n"
"    mode: \"dark\"\n"
"    name: \"clavis-m3\"\n"
"    background: #1C1B1F\n"
"    panel_bg: #211F26\n"
"    panel_fg: #E6E1E5\n"
"    border: #49454F\n"
"    accent: #D0BCFF\n"
"    window_bg: #1C1B1F\n"
"    window_fg: #E6E1E5\n"
"    titlebar_bg: #2B2930\n"
"}\n"
"UI {\n"
"    clock_x: 380\n"
"    clock_y: 200\n"
"    clock_font_size: 48\n"
"    date_font_size: 20\n"
"    weather_margin_right: 140\n"
"    weather_margin_bottom: 160\n"
"    weather_width: 120\n"
"    weather_height: 120\n"
"    weather_temp_font: 36\n"
"    weather_desc_font: 16\n"
"    sidebar_x: 12\n"
"    sidebar_y: 50\n"
"    sidebar_width: 280\n"
"    sidebar_height: 500\n"
"    cc_margin_right: 384\n"
"    cc_y: 108\n"
"    cc_width: 360\n"
"    cc_height: 440\n"
"    bar_height: 36\n"
"    island_width: 120\n"
"    island_height: 28\n"
"    island_y: 12\n"
"    island_font_size: 20\n"
"    dock_width: 400\n"
"    dock_height: 64\n"
"    dock_margin_bottom: 12\n"
"    dock_icon_size: 44\n"
"    corner_radius: 18\n"
"    font_small: 12\n"
"    font_normal: 14\n"
"    font_large: 18\n"
"}\n"
"Panel {\n"
"    position: top\n"
"    height: 32\n"
"    Widget { type: workspaces align: left }\n"
"    Widget { type: clock align: right }\n"
"}\n"
"Panel {\n"
"    position: bottom\n"
"    height: 28\n"
"    Widget { type: tasks align: left }\n"
"    Widget { type: systray align: right }\n"
"}\n"
"Panels {\n"
"    top_bar: 1\n"
"    dock: 1\n"
"    sidebar: 1\n"
"    control_center: 1\n"
"    desktop_clock: 1\n"
"    weather: 1\n"
"    dynamic_island: 1\n"
"}\n";

/* 从配置加载主题 */
static void load_theme_from_config(hsqml_node_t *theme)
{
    if (!theme) return;
    /* 读取主题模式 */
    const char *mode = hsqml_get_string(theme, "mode", "light");
    if (str_cmp(mode, "dark") == 0) {
        g_shell.theme = HS_THEME_DARK;
        hs_theme_init(&g_shell.colors, HS_THEME_DARK);
    } else if (str_cmp(mode, "mono") == 0) {
        g_shell.theme = HS_THEME_MONO;
        hs_theme_init(&g_shell.colors, HS_THEME_MONO);
    } else {
        g_shell.theme = HS_THEME_LIGHT;
        hs_theme_init(&g_shell.colors, HS_THEME_LIGHT);
    }
    /* 读取自定义颜色（覆盖主题默认值） */
    g_shell.colors.bg = hsqml_get_color(theme, "background", g_shell.colors.bg);
    g_shell.colors.panel_bg = hsqml_get_color(theme, "panel_bg", g_shell.colors.panel_bg);
    g_shell.colors.panel_fg = hsqml_get_color(theme, "panel_fg", g_shell.colors.panel_fg);
    g_shell.colors.border = hsqml_get_color(theme, "border", g_shell.colors.border);
    g_shell.colors.accent = hsqml_get_color(theme, "accent", g_shell.colors.accent);
    g_shell.colors.window_bg = hsqml_get_color(theme, "window_bg", g_shell.colors.window_bg);
    g_shell.colors.window_fg = hsqml_get_color(theme, "window_fg", g_shell.colors.window_fg);
    g_shell.colors.titlebar_bg = hsqml_get_color(theme, "titlebar_bg", g_shell.colors.titlebar_bg);
}

/* 从配置加载UI参数 */
static void load_ui_from_config(hsqml_node_t *ui)
{
    if (!ui) return;
    g_shell.ui.clock_x = hsqml_get_number(ui, "clock_x", 380);
    g_shell.ui.clock_y = hsqml_get_number(ui, "clock_y", 200);
    g_shell.ui.clock_font_size = hsqml_get_number(ui, "clock_font_size", 48);
    g_shell.ui.date_font_size = hsqml_get_number(ui, "date_font_size", 20);
    g_shell.ui.weather_margin_right = hsqml_get_number(ui, "weather_margin_right", 140);
    g_shell.ui.weather_margin_bottom = hsqml_get_number(ui, "weather_margin_bottom", 160);
    g_shell.ui.weather_width = hsqml_get_number(ui, "weather_width", 120);
    g_shell.ui.weather_height = hsqml_get_number(ui, "weather_height", 120);
    g_shell.ui.weather_temp_font = hsqml_get_number(ui, "weather_temp_font", 36);
    g_shell.ui.weather_desc_font = hsqml_get_number(ui, "weather_desc_font", 16);
    g_shell.ui.sidebar_x = hsqml_get_number(ui, "sidebar_x", 12);
    g_shell.ui.sidebar_y = hsqml_get_number(ui, "sidebar_y", 50);
    g_shell.ui.sidebar_width = hsqml_get_number(ui, "sidebar_width", 280);
    g_shell.ui.sidebar_height = hsqml_get_number(ui, "sidebar_height", 500);
    g_shell.ui.cc_margin_right = hsqml_get_number(ui, "cc_margin_right", 384);
    g_shell.ui.cc_y = hsqml_get_number(ui, "cc_y", 108);
    g_shell.ui.cc_width = hsqml_get_number(ui, "cc_width", 360);
    g_shell.ui.cc_height = hsqml_get_number(ui, "cc_height", 440);
    g_shell.ui.bar_height = hsqml_get_number(ui, "bar_height", 40);
    g_shell.ui.island_width = hsqml_get_number(ui, "island_width", 120);
    g_shell.ui.island_height = hsqml_get_number(ui, "island_height", 28);
    g_shell.ui.island_y = hsqml_get_number(ui, "island_y", 6);
    g_shell.ui.island_font_size = hsqml_get_number(ui, "island_font_size", 20);
    g_shell.ui.dock_width = hsqml_get_number(ui, "dock_width", 400);
    g_shell.ui.dock_height = hsqml_get_number(ui, "dock_height", 64);
    g_shell.ui.dock_margin_bottom = hsqml_get_number(ui, "dock_margin_bottom", 12);
    g_shell.ui.dock_icon_size = hsqml_get_number(ui, "dock_icon_size", 44);
    g_shell.ui.corner_radius = hsqml_get_number(ui, "corner_radius", 16);
    g_shell.ui.font_small = hsqml_get_number(ui, "font_small", 12);
    g_shell.ui.font_normal = hsqml_get_number(ui, "font_normal", 14);
    g_shell.ui.font_large = hsqml_get_number(ui, "font_large", 18);
}

/* 从配置加载面板开关 */
static void load_panels_from_config(hsqml_node_t *panels)
{
    (void)panels;
    /* 临时空操作，待hsqml解析器稳定后恢复 */
}

/* 从配置加载面板 */
static void load_panel_from_config(hsqml_node_t *panel)
{
    if (!panel) return;

    const char *pos = hsqml_get_string(panel, "position", "top");
    hs_panel_pos_t position = (str_cmp(pos, "bottom") == 0) ? HS_PANEL_BOTTOM : HS_PANEL_TOP;
    int height = hsqml_get_number(panel, "height", HS_PANEL_HEIGHT);

    hs_panel_t *p = (position == HS_PANEL_TOP) ? &g_shell.top_panel : &g_shell.bottom_panel;
    hs_panel_init(p, position, height);

    /* 加载小部件 */
    int widget_count = hsqml_count_children(panel, "Widget");
    for (int i = 0; i < widget_count && p->widget_count < HS_MAX_WIDGETS; i++) {
        hsqml_node_t *widget = hsqml_get_child(panel, "Widget", i);
        if (!widget) continue;

        const char *type = hsqml_get_string(widget, "type", "");
        hs_widget_type_t wtype = HS_WIDGET_CUSTOM;

        if (str_cmp(type, "clock") == 0) wtype = HS_WIDGET_CLOCK;
        else if (str_cmp(type, "cpu") == 0) wtype = HS_WIDGET_CPU;
        else if (str_cmp(type, "memory") == 0) wtype = HS_WIDGET_MEMORY;
        else if (str_cmp(type, "workspaces") == 0) wtype = HS_WIDGET_WORKSPACES;
        else if (str_cmp(type, "battery") == 0) wtype = HS_WIDGET_BATTERY;
        else if (str_cmp(type, "volume") == 0) wtype = HS_WIDGET_VOLUME;
        else if (str_cmp(type, "network") == 0) wtype = HS_WIDGET_NETWORK;

        hepushell_add_widget(position, wtype);
    }
}

/* 从ext2文件系统读取配置文件 */
static int hs_read_config_file(char *buf, int max_len)
{
    extern int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size);
    int n = ext2_64_read_file("/etc/hepushell/config.hsqml", (uint8_t *)buf, (uint32_t)(max_len - 1));
    if (n > 0) buf[n] = 0;
    return n;
}

/* 加载配置 */
static int hepushell_load_config(const char *config)
{
    (void)config;
    /* 完全跳过配置加载，使用硬编码默认值 */
    return 0;
}

/* 数字转字符串 */
static void itoa_hs(int value, char *buf)
{
    int i = 0;
    int n = value;
    if (n == 0) { buf[i++] = '0'; buf[i] = 0; return; }
    if (n < 0) { buf[i++] = '-'; n = -n; }
    char tmp[16];
    int j = 0;
    while (n > 0) { tmp[j++] = '0' + n % 10; n /= 10; }
    while (j > 0) buf[i++] = tmp[--j];
    buf[i] = 0;
}

/* 简单字符串拼接 */
static void strcat_hs(char *dst, const char *src)
{
    while (*dst) dst++;
    while (*src) *dst++ = *src++;
    *dst = 0;
}

/* 简单字符串复制 */
static void strcpy_hs(char *dst, const char *src)
{
    while (*src) *dst++ = *src++;
    *dst = 0;
}

/* 简单字符串长度 */
static int strlen_hs(const char *s)
{
    int n = 0;
    while (*s++) n++;
    return n;
}

/* ============================================================================
 * 主题系统
 * ========================================================================== */

void hs_theme_init(hs_theme_colors_t *colors, hs_theme_t theme)
{
    if (theme == HS_THEME_MONO) {
        /* 极简黑白风格 */
        colors->bg = 0x000000;
        colors->panel_bg = 0x000000;
        colors->panel_fg = 0xFFFFFF;
        colors->border = 0x333333;
        colors->accent = 0xFFFFFF;
        colors->window_bg = 0x0A0A0A;
        colors->window_fg = 0xFFFFFF;
        colors->titlebar_bg = 0x1A1A1A;
        colors->workspace_active = 0xFFFFFF;
        colors->workspace_inactive = 0x555555;
    } else if (theme == HS_THEME_DARK) {
        /* 深色主题 — 对齐 Clavis (StatIndet/quickshell) 默认 tonal-spot 青蓝色板 */
        colors->bg = 0x0F1416;              /* m3background */
        colors->panel_bg = 0x1B2023;         /* m3surfaceContainer */
        colors->panel_fg = 0xDEE3E6;         /* m3onSurface */
        colors->border = 0x40484C;           /* m3outlineVariant */
        colors->border_strong = 0x8A9296;    /* m3outline */
        colors->accent = 0x88D0EC;           /* m3primary */
        colors->accent_pressed = 0x004D61;   /* m3primaryContainer */
        colors->window_bg = 0x171C1F;        /* m3surfaceContainerLow */
        colors->window_fg = 0xDEE3E6;        /* m3onSurface */
        colors->text_secondary = 0xBFC8CC;   /* m3onSurfaceVariant */
        colors->titlebar_bg = 0x252B2D;      /* m3surfaceContainerHigh */
        colors->workspace_active = 0x88D0EC; /* m3primary */
        colors->workspace_inactive = 0xBFC8CC; /* m3onSurfaceVariant */
    } else {
        /* 浅色主题（Quickshell风格：浅米色+金色） */
        colors->bg = 0xF0EBE0;          /* 浅米色背景 */
        colors->panel_bg = 0xE8DFD0;    /* 奶油色面板 */
        colors->panel_fg = 0x5C4A32;    /* 深棕文字 */
        colors->border = 0xD4C8B0;      /* 浅棕边框 */
        colors->accent = 0xC9A96E;      /* 金色强调 */
        colors->window_bg = 0xFAF6EE;   /* 窗口背景 */
        colors->window_fg = 0x3D3226;   /* 窗口文字 */
        colors->titlebar_bg = 0xE8DFD0; /* 标题栏 */
        colors->workspace_active = 0xC9A96E;   /* 活动工作区-金色 */
        colors->workspace_inactive = 0xB8A88A; /* 非活动-浅棕 */
    }
}

void hepushell_set_theme(hs_theme_t theme)
{
    g_shell.theme = theme;
    hs_theme_init(&g_shell.colors, theme);
}

/* 强调色上的前景色：亮底用深青字，暗底用亮字（对齐 Clavis onPrimary） */
static uint32_t hs_on_accent(void)
{
    if (g_shell.theme == HS_THEME_MONO) return 0x000000;
    uint32_t c = g_shell.colors.accent;
    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    int lum = (r * 77 + g * 151 + b * 28) >> 8;
    return (lum > 140) ? 0x003544 : 0xF2F5F7;
}

/* ============================================================================
 * 主题取色器（对齐 quickshell matugen scheme-tonal-spot）
 *
 * 简化版 M3 色板生成：从源色提取 Hue，旋转所有"有色角色"的 Hue，
 * 中性色（bg/panel_bg 等）保持不变。不实现完整 CAM16/HCT 色彩空间，
 * 用 HSL 近似，足以让强调色跟随用户选色。
 * 内核无 SSE，全部使用定点整数。
 * ========================================================================== */

/* RGB → HSL（H:0-360, S:0-256, L:0-255，全部定点整数） */
static void rgb_to_hsl(uint32_t rgb, int *h, int *s, int *l)
{
    int r = (rgb >> 16) & 0xFF;
    int g = (rgb >> 8) & 0xFF;
    int b = rgb & 0xFF;
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    *l = (mx + mn) / 2;  /* 0-255 */
    if (mx == mn) { *h = 0; *s = 0; return; }
    int d = mx - mn;
    if (*l <= 127) {
        *s = (d * 256) / (mx + mn + 1);
    } else {
        *s = (d * 256) / (510 - mx - mn + 1);
    }
    if (*s > 256) *s = 256;
    if (mx == r) *h = ((g - b) * 60) / (d + 1) + (g < b ? 360 : 0);
    else if (mx == g) *h = ((b - r) * 60) / (d + 1) + 120;
    else *h = ((r - g) * 60) / (d + 1) + 240;
}

/* HSL → RGB（H:0-360, S:0-256, L:0-255） */
static uint32_t hsl_to_rgb(int h, int s, int l)
{
    if (s == 0) {
        int v = l;
        return ((uint32_t)v << 16) | ((uint32_t)v << 8) | v;
    }
    /* q = L < 0.5 ? L*(1+S) : L+S-L*S */
    int q;
    if (l < 128) {
        q = (l * (256 + s)) >> 8;
    } else {
        q = l + s - ((l * s) >> 8);
    }
    int p = 2 * l - q;
    if (p < 0) p = 0;
    if (p > 255) p = 255;
    if (q > 255) q = 255;
    int hk = ((h % 360) + 360) % 360;
    int tr = hk + 120;
    int tg = hk;
    int tb = hk + 240;

    int r, g, b;
    #define HSL_COMP(t) ({ \
        int _t = ((t) % 360 + 360) % 360; \
        int _v; \
        if (_t < 60) _v = p + ((q - p) * _t) / 60; \
        else if (_t < 180) _v = q; \
        else if (_t < 240) _v = p + ((q - p) * (240 - _t)) / 60; \
        else _v = p; \
        _v; })
    r = HSL_COMP(tr);
    g = HSL_COMP(tg);
    b = HSL_COMP(tb);
    #undef HSL_COMP
    if (r > 255) r = 255; if (r < 0) r = 0;
    if (g > 255) g = 255; if (g < 0) g = 0;
    if (b > 255) b = 255; if (b < 0) b = 0;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/* 用源色的 Hue 替换目标色的 Hue，保留 S/L */
static uint32_t rotate_hue(uint32_t target, int src_h)
{
    int h, s, l;
    rgb_to_hsl(target, &h, &s, &l);
    return hsl_to_rgb(src_h, s, l);
}

/* 取色器核心：从源色生成 M3 色板（旋转有色角色的 Hue） */
void hs_theme_apply_source_color(uint32_t source)
{
    int src_h, src_s, src_l;
    rgb_to_hsl(source, &src_h, &src_s, &src_l);

    /* 有色角色跟随源色 Hue；中性色不变 */
    g_shell.colors.accent = rotate_hue(g_shell.colors.accent, src_h);
    g_shell.colors.accent_pressed = rotate_hue(g_shell.colors.accent_pressed, src_h);
    g_shell.colors.workspace_active = rotate_hue(g_shell.colors.workspace_active, src_h);
    g_shell.colors.border_strong = rotate_hue(g_shell.colors.border_strong, src_h);
}

/* 预设源色板（对齐 matugen 常用 seed） */
static const uint32_t g_theme_presets[] = {
    0xD0BCFF, /* Lavender (默认, 当前 M3 primary) */
    0xFFB4AB, /* Red */
    0xFFB86C, /* Orange */
    0xFFD680, /* Yellow */
    0xA8DAD8, /* Teal */
    0x90CAF9, /* Blue */
    0xC3B8E8, /* Purple */
    0xF1B6E2, /* Pink */
};
#define HS_THEME_PRESET_COUNT (int)(sizeof(g_theme_presets) / sizeof(g_theme_presets[0]))

/* ============================================================================
 * V0.56: 程序化壁纸 + matugen 式壁纸取色
 * ========================================================================== */
static void hs_wallpaper_vgradient(uint32_t c1, uint32_t c2)
{
    /* 垂直渐变: 逐扫描线 fill_rect (行填充优化, 避免全屏逐像素 put_pixel 开销) */
    int w = g_shell.screen_width, h = g_shell.screen_height;
    int r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    int r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    int den = h - 1;
    if (den <= 0) den = 1;
    for (int y = 0; y < h; y++) {
        int a = (y * 255) / den, ia = 255 - a;
        uint32_t c = ((uint32_t)((r1 * ia + r2 * a) >> 8) << 16) |
                     ((uint32_t)((g1 * ia + g2 * a) >> 8) << 8) |
                     (uint32_t)((b1 * ia + b2 * a) >> 8);
        gfx64_fill_rect(0, y, w, 1, c);
    }
}

static void hs_wallpaper_render(void)
{
    int w = g_shell.screen_width, h = g_shell.screen_height;
    if (g_wallpaper_style == 1) {
        /* Dusk: 紫→橙渐变天空 + 径向渐变落日 */
        hs_wallpaper_vgradient(0x2B1B3D, 0xE8834A);
        gfx64_fill_circle_gradient(w / 2, h * 3 / 4, hs_s(120), 0xFFD680, 0xE85D4A);
    } else if (g_wallpaper_style == 2) {
        /* Ocean: 青→蓝渐变 */
        hs_wallpaper_vgradient(0x0B3B4F, 0x123E7C);
    } else {
        gfx64_fill_rect(0, 0, w, h, g_shell.colors.bg);
    }
}

/* matugen 式取色: 采样网格 6x4 (边距 12.5%), 取饱和度最高的像素为源色 */
static void hs_wallpaper_sample_theme(void)
{
    int w = g_shell.screen_width, h = g_shell.screen_height;
    uint32_t best = 0;
    int best_s = 40;                 /* 饱和度门槛: 近中性色不取 */
    for (int gy = 0; gy < 4; gy++) {
        for (int gx = 0; gx < 6; gx++) {
            int x = w / 8 + (w * gx) / 8 + w / 16;
            int y = h / 8 + (3 * h * gy) / 16 + (3 * h) / 32;
            uint32_t px = gfx64_get_pixel(x, y);
            int hh, ss, ll;
            rgb_to_hsl(px, &hh, &ss, &ll);
            if (ss > best_s) { best_s = ss; best = px; }
        }
    }
    hepushell_set_theme(g_shell.theme);  /* 重置默认色板再取色, 避免叠加旋转 */
    if (best) {
        hs_theme_apply_source_color(best);
        g_shell.theme_source = best;
        vga_printf("[WALLPAPER] source color #%06x applied\n", best);
    } else {
        g_shell.theme_source = 0xD0BCFF;
    }
}

static void hs_wallpaper_cycle(void)
{
    g_wallpaper_style = (g_wallpaper_style + 1) % 3;
    if (g_wallpaper_style == 0) {
        /* 回 Classic: 恢复默认色板, 无需采样 */
        hepushell_set_theme(g_shell.theme);
        g_shell.theme_source = 0xD0BCFF;
    } else {
        g_wallpaper_dirty = 1;      /* 下一帧壁纸绘制完成后采样 */
    }
    vga_printf("[WALLPAPER] style=%s\n", g_wallpaper_names[g_wallpaper_style]);
}

/* ============================================================================
 * 小部件
 * ========================================================================== */

void hs_widget_clock_render(hs_widget_t *w, int x, int y)
{
    uint8_t h = rtc64_get_hours();
    uint8_t m = rtc64_get_minutes();
    uint8_t s = rtc64_get_seconds();

    char time[32];
    time[0] = '0' + h / 10; time[1] = '0' + h % 10; time[2] = ':';
    time[3] = '0' + m / 10; time[4] = '0' + m % 10; time[5] = ':';
    time[6] = '0' + s / 10; time[7] = '0' + s % 10; time[8] = 0;

    void *fb = gfx64_get_framebuffer();
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       x, y + 6, time, g_shell.colors.panel_fg, 14);
}

void hs_widget_cpu_render(hs_widget_t *w, int x, int y)
{
    void *fb = gfx64_get_framebuffer();
    char buf[32];
    strcpy_hs(buf, "CPU: ");
    char num[8];
    itoa_hs(smp_get_cpu_count(), num);
    strcat_hs(buf, num);
    strcat_hs(buf, " cores");
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       x, y + 6, buf, g_shell.colors.panel_fg, 14);
}

void hs_widget_memory_render(hs_widget_t *w, int x, int y)
{
    uint64_t total, used, free;
    pmm64_get_stats(&total, &used, &free);

    void *fb = gfx64_get_framebuffer();
    char buf[32];
    strcpy_hs(buf, "MEM: ");
    char num[8];
    itoa_hs((int)(used / (1024 * 1024)), num);
    strcat_hs(buf, num);
    strcat_hs(buf, "MB");
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       x, y + 6, buf, g_shell.colors.panel_fg, 14);
}

void hs_widget_workspaces_render(hs_widget_t *w, int x, int y)
{
    void *fb = gfx64_get_framebuffer();
    int cx = x;

    for (int i = 0; i < HS_MAX_WORKSPACES; i++) {
        uint32_t color = (i == g_shell.current_workspace) ?
                         g_shell.colors.workspace_active :
                         g_shell.colors.workspace_inactive;

        char name[4];
        name[0] = '1' + i;
        name[1] = 0;

        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           cx, y + 6, name, color, 14);
        cx += 16;
    }
}

void hs_widget_update_all(void)
{
    /* 小部件在渲染时实时更新，无需预计算 */
}

/* ============================================================================
 * 面板系统
 * ========================================================================== */

void hs_panel_init(hs_panel_t *panel, hs_panel_pos_t pos, int height)
{
    panel->position = pos;
    panel->height = height;
    panel->widget_count = 0;
}

void hs_panel_render(hs_panel_t *panel)
{
    int y = (panel->position == HS_PANEL_TOP) ? 0 :
            g_shell.screen_height - panel->height;

    /* 面板背景 */
    gfx64_fill_rect(0, y, g_shell.screen_width, panel->height,
                    g_shell.colors.panel_bg);

    /* 面板底部/顶部边框 */
    int border_y = (panel->position == HS_PANEL_TOP) ?
                   y + panel->height - 1 : y;
    gfx64_fill_rect(0, border_y, g_shell.screen_width, 1,
                    g_shell.colors.border);

    /* 渲染小部件（左对齐） */
    int x = 8;
    for (int i = 0; i < panel->widget_count; i++) {
        hs_widget_t *w = &panel->widgets[i];
        w->x = x;
        w->y = y;

        switch (w->type) {
            case HS_WIDGET_CLOCK:
                hs_widget_clock_render(w, x, y);
                x += 80;
                break;
            case HS_WIDGET_CPU:
                hs_widget_cpu_render(w, x, y);
                x += 80;
                break;
            case HS_WIDGET_MEMORY:
                hs_widget_memory_render(w, x, y);
                x += 80;
                break;
            case HS_WIDGET_WORKSPACES:
                hs_widget_workspaces_render(w, x, y);
                x += HS_MAX_WORKSPACES * 16 + 8;
                break;
            default:
                x += 40;
                break;
        }
        x += 16;  /* 小部件间距 */
    }
}

void hepushell_add_widget(hs_panel_pos_t panel, hs_widget_type_t type)
{
    hs_panel_t *p = (panel == HS_PANEL_TOP) ?
                    &g_shell.top_panel : &g_shell.bottom_panel;

    if (p->widget_count >= HS_MAX_WIDGETS) return;

    hs_widget_t *w = &p->widgets[p->widget_count];
    w->type = type;
    w->x = 0;
    w->y = 0;
    w->width = 60;
    w->height = HS_PANEL_HEIGHT - 4;
    w->text[0] = 0;
    p->widget_count++;
}

/* ============================================================================
 * 启动器
 * ========================================================================== */

/* 启动器: 大小写不敏感前缀匹配 (V0.54.7) */
static int hs_app_match(const char *name, const char *input)
{
    int j = 0;
    while (input[j]) {
        char a = name[j], b = input[j];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b || !name[j]) return 0;
        j++;
    }
    return 1;
}

static int hs_launcher_first_match(void)
{
    if (g_shell.launcher_input[0] == 0) return 0;
    for (int i = 0; i < g_shell.app_count; i++)
        if (hs_app_match(g_shell.apps[i].name, g_shell.launcher_input)) return i;
    return -1;
}

/* V0.55: 启动器滚动窗口 (应用数超过 8 行时跟随选中项) */
static int g_launcher_scroll = 0;

void hs_launcher_render(void)
{
    if (!g_shell.launcher_visible) return;

    int w = 500;
    int h = 350;
    int x = (g_shell.screen_width - w) / 2;
    int y = (g_shell.screen_height - h) / 2 - 50;

    /* 启动器背景（圆角卡片） */
    gfx64_fill_rounded_rect(x, y, w, h, 16, g_shell.colors.window_bg);
    gfx64_draw_rounded_rect(x, y, w, h, 16, g_shell.colors.border);

    /* 搜索框 */
    gfx64_fill_rounded_rect(x + 16, y + 16, w - 32, 40, 8, g_shell.colors.panel_bg);
    gfx64_draw_rounded_rect(x + 16, y + 16, w - 32, 40, 8, g_shell.colors.accent);

    void *fb = gfx64_get_framebuffer();

    /* 搜索图标 */
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       x + 28, y + 26, "~", g_shell.colors.panel_fg, 20);

    /* 输入文字 */
    char prompt[80];
    strcpy_hs(prompt, g_shell.launcher_input);
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       x + 52, y + 26, prompt, g_shell.colors.window_fg, 18);

    /* 光标 */
    int cursor_x = x + 52 + strlen_hs(g_shell.launcher_input) * 10;
    gfx64_fill_rect(cursor_x, y + 26, 2, 18, g_shell.colors.accent);

    /* 应用网格 (对齐 Clavis SpotlightAppGrid: 6列, cell≈98x96, 图标44+标签, 滚动) */
    int list_y = y + 68;
    int match_idx[HS_MAX_APPS];
    int nm = 0, sel_pos = 0;
    for (int i = 0; i < g_shell.app_count; i++) {
        if (g_shell.launcher_input[0] != 0 &&
            !hs_app_match(g_shell.apps[i].name, g_shell.launcher_input))
            continue;
        if (i == g_shell.selected_app) sel_pos = nm;
        match_idx[nm++] = i;
    }
    const int cols = 6;
    const int rows_vis = 3;
    int pad = hs_sx(18);
    int cell_w = (w - 2 * pad) / cols;
    int cell_h = hs_sy(92);
    int rows = (nm + cols - 1) / cols;
    int sel_row = sel_pos / cols;
    if (sel_row < g_launcher_scroll) g_launcher_scroll = sel_row;
    if (sel_row >= g_launcher_scroll + rows_vis)
        g_launcher_scroll = sel_row - rows_vis + 1;
    {
        int maxs = rows - rows_vis;
        if (maxs < 0) maxs = 0;
        if (g_launcher_scroll > maxs) g_launcher_scroll = maxs;
        if (g_launcher_scroll < 0) g_launcher_scroll = 0;
    }

    for (int r = 0; r < rows_vis; r++) {
        int row = g_launcher_scroll + r;
        if (row >= rows) break;
        for (int c = 0; c < cols; c++) {
            int pos = row * cols + c;
            if (pos >= nm) break;
            int i = match_idx[pos];
            hs_app_t *app = &g_shell.apps[i];
            int cx = x + pad + c * cell_w;
            int cy = list_y + r * cell_h;
            int sel = (i == g_shell.selected_app);

            /* 选中高亮: primaryContainer 底 + primary 描边 */
            if (sel) {
                gfx64_fill_rounded_rect(cx + 2, cy + 2, cell_w - 4, cell_h - 4, 10,
                                        g_shell.colors.accent_pressed);
                gfx64_draw_rounded_rect(cx + 2, cy + 2, cell_w - 4, cell_h - 4, 10,
                                        g_shell.colors.accent);
            }

            /* 图标块 */
            int isz = hs_s(44);
            int ix = cx + (cell_w - isz) / 2;
            int iy = cy + hs_sy(10);
            gfx64_fill_rounded_rect(ix, iy, isz, isz, hs_s(10), g_shell.colors.accent);
            int tl = 0; while (app->icon[tl] && tl < 3) tl++;
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               ix + (isz - tl * hs_sx(9)) / 2, iy + hs_sy(12),
                               app->icon, hs_on_accent(), hs_s(18));

            /* 名称标签 (居中, 截断) */
            char label[16];
            int maxc = (cell_w - 8) / hs_sx(7);
            if (maxc > 15) maxc = 15;
            int ln = 0;
            while (app->name[ln] && ln < maxc) { label[ln] = app->name[ln]; ln++; }
            if (app->name[ln] && ln > 2) { label[ln - 2] = '.'; label[ln - 1] = '.'; }
            label[ln] = 0;
            int lw = ln * hs_sx(7);
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               cx + (cell_w - lw) / 2, cy + cell_h - hs_sy(24),
                               label, g_shell.colors.window_fg, hs_s(12));
        }
    }
}

void hs_launcher_handle_key(int key)
{
    if (key == 27) {  /* ESC */
        g_shell.launcher_visible = 0;
        return;
    }
    if (key == KEY_EXT_UP || key == KEY_EXT_DOWN ||
        key == KEY_EXT_LEFT || key == KEY_EXT_RIGHT) {
        /* 网格导航: 左右 ±1, 上下 ±6 (对齐 SpotlightAppGrid 列数) */
        int dir = (key == KEY_EXT_LEFT) ? -1 :
                  (key == KEY_EXT_RIGHT) ? 1 :
                  (key == KEY_EXT_UP) ? -6 : 6;
        int i = g_shell.selected_app + dir;
        while (i >= 0 && i < g_shell.app_count) {
            if (g_shell.launcher_input[0] == 0 ||
                hs_app_match(g_shell.apps[i].name, g_shell.launcher_input)) {
                g_shell.selected_app = i;
                return;
            }
            i += dir;
        }
        return;
    }
    if (key == '\n' || key == '\r') {  /* Enter */
        /* 启动选中的应用 */
        if (g_shell.selected_app < g_shell.app_count) {
            vga_puts("[Launcher] Starting: ");
            vga_puts(g_shell.apps[g_shell.selected_app].name);
            vga_puts("\n");
            /* 映射到g_pending_app，由desktop64主循环启动 */
            static const int app_map[HS_MAX_APPS] = {
                HS_APP_TERMINAL, HS_APP_FILES, HS_APP_BROWSER,
                HS_APP_SETTINGS, HS_APP_EDITOR, HS_APP_CALCULATOR,
                HS_APP_NOTES, HS_APP_TASK, HS_APP_ABOUT, HS_APP_ABOUT,
                HS_APP_DEVMGR, HS_APP_DISKMGR, HS_APP_LOGVIEW
            };
            g_pending_app = (g_shell.selected_app < 13) ?
                            app_map[g_shell.selected_app] : HS_APP_ABOUT;
        }
        g_shell.launcher_visible = 0;
        return;
    }
    if (key == '\b') {  /* Backspace */
        if (g_shell.launcher_cursor > 0) {
            g_shell.launcher_cursor--;
            g_shell.launcher_input[g_shell.launcher_cursor] = 0;
            int m = hs_launcher_first_match();
            g_shell.selected_app = (m >= 0) ? m : 0;
        }
        return;
    }
    if (key >= 0x20 && key < 0x7F && g_shell.launcher_cursor < 63) {
        g_shell.launcher_input[g_shell.launcher_cursor++] = key;
        g_shell.launcher_input[g_shell.launcher_cursor] = 0;
        /* 输入后自动选中所匹配的应用 (V0.54.7) */
        int m = hs_launcher_first_match();
        g_shell.selected_app = (m >= 0) ? m : 0;
    }
}

void hepushell_toggle_launcher(void)
{
    g_shell.launcher_visible = !g_shell.launcher_visible;
    g_shell.launcher_input[0] = 0;
    g_shell.launcher_cursor = 0;
    g_shell.selected_app = 0;
}

/* ============================================================================
 * 窗口管理
 * ========================================================================== */

/* 全局窗口管理器 */
static hs_window_manager_t g_wm;

void hs_window_manager_init(void)
{
    g_wm.window_count = 0;
    g_wm.focus_window = -1;
    g_wm.z_count = 0;
    g_wm.cascade_index = 0;
    for (int i = 0; i < HS_MAX_WINDOWS; i++) {
        g_wm.windows[i].visible = 0;
        g_wm.windows[i].id = -1;   /* 槽位空闲标记(visible=0 不等于空闲: 最小化/布局隐藏仍占用) */
        g_wm.z_order[i] = -1;
    }
}

/* V0.13.13: 自动级联定位 — x/y 传 -1 时在右侧区域级联排布, 避开左侧系统面板 */
static void hs_window_auto_pos(int *x, int *y, int w, int h)
{
    if (*x != -1 && *y != -1) {
        /* 已显式指定坐标: 仅强制不压侧边栏(除非窗口确实超宽) */
        if (*x < 300 && *x + w <= 1024) *x = 300;
        if (*y < 40) *y = 40;
        return;
    }
    int idx = g_wm.cascade_index;
    int nx = 300 + (idx % 6) * 30;
    int ny = 100 + (idx % 6) * 26;
    if (nx + w > 1024) nx = 1024 - w;
    if (ny + h > 768) ny = 768 - h;
    if (nx < 300) nx = 300;
    *x = nx;
    *y = ny;
    g_wm.cascade_index = (idx + 1) % 6;
}

/* 创建窗口 */
hs_window_t *hs_window_create(hs_window_type_t type, const char *title,
                              int x, int y, int w, int h)
{
    /* 找空闲槽(id==-1)并统计活跃窗口数；仅当活跃窗口真正占满才失败。
     * 修复槽位泄漏: 旧实现用 window_count 线性追加, 关闭不回收, 反复开关 32 次后无法再开窗 */
    int slot = -1, active = 0;
    for (int i = 0; i < HS_MAX_WINDOWS; i++) {
        if (g_wm.windows[i].id >= 0) {
            if (g_wm.windows[i].visible) active++;
        } else if (slot < 0) {
            slot = i;
        }
    }
    if (active >= HS_MAX_WINDOWS || slot < 0) return NULL;

    hs_window_t *win = &g_wm.windows[slot];
    win->id = slot;
    win->type = type;
    win->state = HS_WINDOW_STATE_NORMAL;

    /* 复制标题 */
    int i = 0;
    while (title && title[i] && i < 63) {
        win->title[i] = title[i];
        i++;
    }
    win->title[i] = 0;

    /* V0.13.13: 自动级联定位(传 -1 时) */
    hs_window_auto_pos(&x, &y, w, h);
    win->x = x;
    win->y = y;
    win->width = w;
    win->height = h;
    win->prev_x = x;
    win->prev_y = y;
    win->prev_w = w;
    win->prev_h = h;
    win->visible = 1;
    win->focus = 0;
    win->workspace = g_shell.current_workspace;
    win->always_on_top = (type == HS_WINDOW_PANEL || type == HS_WINDOW_POPUP);
    win->bg_color = g_shell.colors.window_bg;
    win->border_color = g_shell.colors.border;
    win->dragging = 0;
    win->render = NULL;
    win->anim_start = g_frame_no;
    win->anim_dur = 12;  /* ≈200ms @60fps, Clavis durations.small */

    /* 维护高水位 window_count(=最大已用槽位下标+1), 兼容 for i<window_count 遍历与 id<window_count 校验 */
    if (slot + 1 > g_wm.window_count) g_wm.window_count = slot + 1;
    /* 添加到 z_order(空闲槽被回收时其 id 已从 z_order 移除, 此处不会产生重复 id) */
    g_wm.z_order[g_wm.z_count++] = win->id;

    /* V0.13.16: 新窗口自动获得焦点(修复平铺/快捷键作用目标缺失) */
    for (int i = 0; i < g_wm.window_count; i++) g_wm.windows[i].focus = 0;
    win->focus = 1;
    g_wm.focus_window = win->id;

    return win;
}

/* 统一回收窗口槽位: 标记 id=-1 并从 z_order 紧凑移除。
 * visible=0 有两种语义: 真正关闭(走本函数回收) vs 最小化/布局隐藏(保留槽位, 不回收) */
static void hs_window_recycle(int id)
{
    if (id < 0 || id >= HS_MAX_WINDOWS) return;
    hs_window_t *w = &g_wm.windows[id];
    if (w->id < 0) return;   /* 已是空闲槽 */
    w->visible = 0;
    w->id = -1;
    int new_z = 0;
    for (int i = 0; i < g_wm.z_count; i++) {
        if (g_wm.z_order[i] != id) g_wm.z_order[new_z++] = g_wm.z_order[i];
    }
    g_wm.z_count = new_z;
    if (g_wm.focus_window == id) g_wm.focus_window = -1;
}

/* 销毁窗口 */
void hs_window_destroy(hs_window_t *win)
{
    if (!win || win->id < 0) return;
    hs_window_recycle(win->id);
}

/* 聚焦窗口 */
void hs_window_focus(hs_window_t *win)
{
    if (!win) return;
    for (int i = 0; i < g_wm.window_count; i++) {
        g_wm.windows[i].focus = 0;
    }
    win->focus = 1;
    g_wm.focus_window = win->id;
    hs_window_raise(win);
}

/* 显示/隐藏窗口 (重新显示的窗口需置顶并接管键盘焦点, 否则会藏在其它窗口后面) */
void hs_window_show(hs_window_t *win)
{
    if (!win) return;
    win->visible = 1;
    hs_window_focus(win);
}
void hs_window_hide(hs_window_t *win) { if (win) win->visible = 0; }

/* 最大化/还原 */
void hs_window_maximize(hs_window_t *win)
{
    if (!win || win->type != HS_WINDOW_FLOATING) return;
    win->prev_x = win->x;
    win->prev_y = win->y;
    win->prev_w = win->width;
    win->prev_h = win->height;
    win->x = 0;
    win->y = HS_PANEL_HEIGHT;
    win->width = g_shell.screen_width;
    win->height = g_shell.screen_height - HS_PANEL_HEIGHT * 2;
    win->state = HS_WINDOW_STATE_MAXIMIZED;
}

void hs_window_restore(hs_window_t *win)
{
    if (!win || win->state != HS_WINDOW_STATE_MAXIMIZED) return;
    win->x = win->prev_x;
    win->y = win->prev_y;
    win->width = win->prev_w;
    win->height = win->prev_h;
    win->state = HS_WINDOW_STATE_NORMAL;
}

/* V0.13.16: 最小化窗口(保留在窗口管理器, 可由Dock/切换器恢复) */
void hs_window_minimize(hs_window_t *win)
{
    if (!win || win->type != HS_WINDOW_FLOATING) return;
    win->state = HS_WINDOW_STATE_MINIMIZED;
    win->visible = 0;
    win->focus = 0;
    if (g_wm.focus_window == win->id) g_wm.focus_window = -1;
}

/* V0.13.16: 最大化/还原切换(标题栏按钮/双击标题栏) */
void hs_window_toggle_maximize(hs_window_t *win)
{
    if (!win || win->type != HS_WINDOW_FLOATING) return;
    if (win->state == HS_WINDOW_STATE_MAXIMIZED)
        hs_window_restore(win);
    else
        hs_window_maximize(win);
}

/* 窗口层级 */
void hs_window_raise(hs_window_t *win)
{
    if (!win) return;
    /* 从z_order移除并添加到顶部 */
    int new_z = 0;
    for (int i = 0; i < g_wm.z_count; i++) {
        if (g_wm.z_order[i] != win->id) {
            g_wm.z_order[new_z++] = g_wm.z_order[i];
        }
    }
    g_wm.z_order[new_z++] = win->id;
    g_wm.z_count = new_z;
}

void hs_window_lower(hs_window_t *win)
{
    if (!win) return;
    int new_z = 1;
    int saved = win->id;
    for (int i = 0; i < g_wm.z_count; i++) {
        if (g_wm.z_order[i] != win->id) {
            g_wm.z_order[new_z++] = g_wm.z_order[i];
        }
    }
    g_wm.z_order[0] = saved;
    g_wm.z_count = new_z;
}

/* 移动/调整大小 */
void hs_window_move(hs_window_t *win, int x, int y)
{
    if (!win) return;
    win->x = x;
    win->y = y;
}

void hs_window_resize(hs_window_t *win, int w, int h)
{
    if (!win) return;
    if (w > 50) win->width = w;
    if (h > 50) win->height = h;
}

/* 获取指定位置的窗口 */
hs_window_t *hs_window_at(int x, int y)
{
    /* 从顶层到底层查找 */
    for (int i = g_wm.z_count - 1; i >= 0; i--) {
        int id = g_wm.z_order[i];
        if (id < 0 || id >= g_wm.window_count) continue;
        hs_window_t *w = &g_wm.windows[id];
        if (!w->visible) continue;
        if (x >= w->x && x < w->x + w->width &&
            y >= w->y && y < w->y + w->height) {
            return w;
        }
    }
    return NULL;
}

/* 获取聚焦窗口 */
hs_window_t *hs_window_get_focused(void)
{
    if (g_wm.focus_window < 0) return NULL;
    return &g_wm.windows[g_wm.focus_window];
}

void hs_window_tile(void)
{
    /* 简单平铺：当前工作区的浮动窗口左右分屏 */
    int visible = 0;
    int indices[HS_MAX_WINDOWS];

    for (int i = 0; i < g_wm.window_count; i++) {
        hs_window_t *w = &g_wm.windows[i];
        if (w->type == HS_WINDOW_FLOATING &&
            w->workspace == g_shell.current_workspace &&
            w->visible && w->state != HS_WINDOW_STATE_MAXIMIZED) {
            indices[visible++] = i;
        }
    }

    int panel_h = HS_PANEL_HEIGHT;
    int avail_h = g_shell.screen_height - panel_h * 2;
    int avail_y = panel_h;

    if (visible == 1) {
        hs_window_t *w = &g_wm.windows[indices[0]];
        w->x = 0;
        w->y = avail_y;
        w->width = g_shell.screen_width;
        w->height = avail_h;
    } else if (visible >= 2) {
        int half = g_shell.screen_width / 2;
        for (int i = 0; i < visible; i++) {
            hs_window_t *w = &g_wm.windows[indices[i]];
            w->x = (i % 2) * half;
            w->y = avail_y;
            w->width = half;
            w->height = avail_h / ((visible + 1) / 2);
        }
    }
}

/* V0.13.16: 单窗口平铺 (Win+←/→, 参考Windows Snap) */
void hepushell_tile_focused(int dir)
{
    hs_window_t *w = hs_window_get_focused();
    if (!w || w->type != HS_WINDOW_FLOATING) return;
    int panel_h = HS_PANEL_HEIGHT;
    int avail_h = g_shell.screen_height - panel_h * 2;
    int half = g_shell.screen_width / 2;

    /* 已在对应半边 → 还原 */
    int is_left = (w->x == 0 && w->width == half && w->y == panel_h && w->height == avail_h);
    int is_right = (w->x == half && w->width == half && w->y == panel_h && w->height == avail_h);
    if ((dir < 0 && is_left) || (dir > 0 && is_right)) {
        w->x = w->prev_x;
        w->y = w->prev_y;
        w->width = w->prev_w;
        w->height = w->prev_h;
        return;
    }
    /* 记录原位置并平铺 */
    if (w->state != HS_WINDOW_STATE_MAXIMIZED) {
        w->prev_x = w->x;
        w->prev_y = w->y;
        w->prev_w = w->width;
        w->prev_h = w->height;
    }
    if (dir < 0) { w->x = 0; } else { w->x = half; }
    w->y = panel_h;
    w->width = half;
    w->height = avail_h;
    w->state = HS_WINDOW_STATE_NORMAL;
}

void hs_window_render_all(void)
{
    /* 按z_order从底到顶渲染 */
    for (int zi = 0; zi < g_wm.z_count; zi++) {
        int id = g_wm.z_order[zi];
        if (id < 0 || id >= g_wm.window_count) continue;
        hs_window_t *w = &g_wm.windows[id];
        if (!w->visible || w->workspace != g_shell.current_workspace) continue;

        /* 面板窗口不渲染（由面板系统渲染） */
        if (w->type == HS_WINDOW_PANEL) continue;

        /* Clavis 动效: 新窗口 ≈200ms (12帧) cubic-out 滑入 */
        int save_x = w->x, save_y = w->y;
        if (w->anim_dur) {
            uint32_t elapsed = g_frame_no - (uint32_t)w->anim_start;
            if (elapsed >= w->anim_dur) {
                w->anim_dur = 0;
            } else {
                int remain = (int)((w->anim_dur - elapsed) * 100 / w->anim_dur);
                int ease = 100 - remain * remain * remain / 10000;
                w->x -= hs_sx(24) * (100 - ease) / 100;
                w->y += hs_sy(16) * (100 - ease) / 100;
            }
        }

        /* 窗口背景 */
        gfx64_fill_rect(w->x, w->y, w->width, w->height, w->bg_color);

        /* 标题栏（浮动窗口） */
        if (w->type == HS_WINDOW_FLOATING) {
            gfx64_fill_rect(w->x, w->y, w->width, 24,
                            g_shell.colors.titlebar_bg);
            /* V0.55: 标题栏前景跟随主题, 深色主题下不再不可见 */
            uint32_t tb_fg = g_shell.colors.window_fg;
            int by = w->y + 5;
            int cmin = w->x + w->width - 68;
            int cmax = w->x + w->width - 44;
            int cclo = w->x + w->width - 20;
            int mx = g_shell.mouse_x, my = g_shell.mouse_y;
            int hov_min = (my >= w->y && my < w->y + 24 &&
                           mx >= cmin && mx < cmin + 24);
            int hov_max = (my >= w->y && my < w->y + 24 &&
                           mx >= cmax && mx < cmax + 24);
            int hov_clo = (my >= w->y && my < w->y + 24 &&
                           mx >= cclo && mx < cclo + 20);
            /* 悬停底色(关闭键红色) */
            if (hov_min)
                gfx64_fill_rounded_rect(cmin, w->y + 2, 24, 20, 4, g_shell.colors.panel_bg);
            if (hov_max)
                gfx64_fill_rounded_rect(cmax, w->y + 2, 24, 20, 4, g_shell.colors.panel_bg);
            if (hov_clo)
                gfx64_fill_rounded_rect(cclo, w->y + 2, 20, 20, 4, 0xE5484D);
            uint32_t btnc = hov_clo ? 0xFFFFFF : tb_fg;
            /* 最小化: 水平粗线 */
            gfx64_fill_rect(cmin + 6, by + 6, 11, 2, btnc);
            /* 最大化: 方框; 已最大化时画双层表示还原 */
            gfx64_draw_rect(cmax + 6, by + 2, 10, 10, btnc);
            if (w->state == HS_WINDOW_STATE_MAXIMIZED) {
                gfx64_draw_rect(cmax + 8, by + 4, 10, 10, btnc);
                gfx64_draw_rect(cmax + 10, by + 6, 6, 6, btnc);
            }
            /* 关闭: ×(硬边对角线, 逐像素) */
            for (int i = 0; i <= 10; i++) {
                gfx64_put_pixel(cclo + 4 + i, by + 2 + i, btnc);
                gfx64_put_pixel(cclo + 14 - i, by + 2 + i, btnc);
            }
        }

        /* 边框 */
        uint32_t border = w->focus ?
                          g_shell.colors.accent : w->border_color;
        gfx64_draw_rect(w->x, w->y, w->width, w->height, border);

        /* 标题（跟随主题前景色） */
        if (w->type != HS_WINDOW_POPUP || w->title[0]) {
            void *fb = gfx64_get_framebuffer();
            uint32_t tc = g_shell.colors.window_fg;
            int tx = w->x + 8;
            /* 浮动窗口标题栏左侧应用图标(单色矢量) */
            if (w->type == HS_WINDOW_FLOATING) {
                if (strcmp(w->title, "Terminal") == 0)
                    hs_icon_terminal(w->x + 5, w->y + 4, 16, tc);
                else if (strcmp(w->title, "Files") == 0)
                    hs_icon_folder(w->x + 5, w->y + 4, 16, tc);
                else if (strcmp(w->title, "Task Manager") == 0)
                    hs_icon_task(w->x + 5, w->y + 4, 16, tc);
                else if (strcmp(w->title, "Settings") == 0)
                    hs_icon_settings(w->x + 5, w->y + 4, 16, tc);
                else if (strcmp(w->title, "Device Manager") == 0)
                    hs_icon_info(w->x + 5, w->y + 4, 16, tc);
                else if (strcmp(w->title, "Disk Manager") == 0)
                    hs_icon_store(w->x + 5, w->y + 4, 16, tc);
                else if (strcmp(w->title, "Log Viewer") == 0)
                    hs_icon_doc(w->x + 5, w->y + 4, 16, tc);
                else if (strcmp(w->title, "Calculator") == 0)
                    hs_icon_calc(w->x + 5, w->y + 4, 16, tc);
                else if (strcmp(w->title, "Editor") == 0)
                    hs_icon_editor(w->x + 5, w->y + 4, 16, tc);
                else if (strcmp(w->title, "Browser") == 0)
                    hs_icon_browser(w->x + 5, w->y + 4, 16, tc);
                else if (strcmp(w->title, "App Store") == 0)
                    hs_icon_store(w->x + 5, w->y + 4, 16, tc);
                else if (strcmp(w->title, "About") == 0)
                    hs_icon_info(w->x + 5, w->y + 4, 16, tc);
                tx = w->x + 26;
            }
            /* 标题过长时截断, 避免压住右侧窗口按钮 */
            char tbuf[40];
            int maxc = (w->width - 150) / 7;
            if (maxc < 4) maxc = 4;
            int n = 0;
            while (w->title[n] && n < maxc && n < 39) { tbuf[n] = w->title[n]; n++; }
            if (w->title[n] && n > 1) { tbuf[n - 1] = '.'; tbuf[n] = '.'; n++; }
            tbuf[n] = 0;
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               tx, w->y + 6, tbuf,
                               tc, 12);
        }

        /* 自定义渲染回调 */
        if (w->render) {
            w->render(w);
        }

        /* 还原动画临时偏移, 保证命中测试用持久坐标 */
        w->x = save_x;
        w->y = save_y;
    }
}

/* 设置窗口渲染回调 */
static void hs_settings_window_render(hs_window_t *w)
{
    void *fb = gfx64_get_framebuffer();
    int cx = w->x + 16;
    int cy = w->y + 36;  /* 跳过标题栏 */

    /* 左侧菜单 */
    const char *menu[] = {"Appearance", "Panels", "System", "About"};
    for (int i = 0; i < 4; i++) {
        uint32_t bg = (i == g_settings_page) ? g_shell.colors.accent : g_shell.colors.panel_bg;
        uint32_t fg = (i == g_settings_page) ? hs_on_accent() : g_shell.colors.panel_fg;
        gfx64_fill_rounded_rect(cx, cy + i * 32, 120, 28, 6, bg);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           cx + 10, cy + i * 32 + 7, menu[i], fg, 13);
    }

    /* 右侧内容 */
    int rx = cx + 140;
    int ry = cy;

    if (g_settings_page == 0) {
        /* Appearance */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Appearance", g_shell.colors.panel_fg, 16);
        ry += 30;
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Theme: Light / Dark", g_shell.colors.panel_fg, 12);
        ry += 24;
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Accent Color (click to apply):", g_shell.colors.panel_fg, 12);
        ry += 22;
        /* 8 个预设色板圆点，点击即应用 */
        for (int i = 0; i < HS_THEME_PRESET_COUNT; i++) {
            int sx = rx + i * 28;
            int sy = ry;
            gfx64_fill_circle(sx + 10, sy + 10, 10, g_theme_presets[i]);
            /* 当前生效色描边 */
            if (g_theme_presets[i] == g_shell.theme_source) {
                gfx64_draw_rounded_rect(sx - 2, sy - 2, 24, 24, 12, g_shell.colors.panel_fg);
            }
        }
        ry += 28;
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Font: DejaVu Sans", g_shell.colors.panel_fg, 12);
        ry += 24;
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Clock Size: 40px", g_shell.colors.panel_fg, 12);
        ry += 24;
        /* V0.56: 壁纸切换行 (点击循环 Classic/Dusk/Ocean) */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Wallpaper:", g_shell.colors.panel_fg, 12);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx + 84, ry, g_wallpaper_names[g_wallpaper_style],
                           g_shell.colors.accent, 12);
        /* V0.54.13: 桌面背景色 3x3 色板 (点击应用并写回 config.hsqml) */
        ry += 24;
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Desktop:", g_shell.colors.panel_fg, 12);
        for (int i = 0; i < 9; i++) {
            int bx = rx + 76 + (i % 3) * 22;
            int by = ry - 4 + (i / 3) * 22;
            gfx64_fill_rect(bx, by, 16, 16, g_color_presets[i]);
            uint32_t bc = (g_shell.colors.bg == g_color_presets[i])
                        ? g_shell.colors.panel_fg : g_shell.colors.border;
            gfx64_draw_rounded_rect(bx, by, 16, 16, 3, bc);
        }
    } else if (g_settings_page == 1) {
        /* Panels */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Panels", g_shell.colors.panel_fg, 16);
        ry += 30;
        const char *panels[] = {"Top Bar", "Dynamic Island", "Dock", "Clock",
                                "Weather", "Sidebar", "Control Center"};
        for (int i = 0; i < 7; i++) {
            int enabled = hs_panel_is_enabled(
                (const char*[]){"top_bar","dynamic_island","dock","desktop_clock",
                                "weather","sidebar","control_center"}[i]);
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               rx, ry + i * 22, enabled ? "[x] " : "[ ] ",
                               enabled ? g_shell.colors.accent : g_shell.colors.panel_fg, 12);
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               rx + 30, ry + i * 22, panels[i], g_shell.colors.panel_fg, 12);
        }
    } else if (g_settings_page == 2) {
        /* System */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "System Information", g_shell.colors.panel_fg, 16);
        ry += 30;
        const char *labels[] = {"OS:", "Kernel:", "CPU:", "Memory:", "Graphics:", "Filesystem:"};
        const char *values[] = {"Hepu OS v0.54.12", "64-bit SMP", "4 cores", "2GB",
                                "VBE 1024x768", "ext2"};
        for (int i = 0; i < 6; i++) {
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               rx, ry + i * 24, labels[i], g_shell.colors.panel_fg, 12);
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               rx + 80, ry + i * 24, values[i], g_shell.colors.accent, 12);
        }
        /* V0.13.39: 语言切换 + 驱动管理入口 */
        ry += 6 * 24 + 10;
        gfx64_fill_rect(rx, ry, w->width - 180, 1, g_shell.colors.border);
        ry += 8;
        extern int l10n64_get_lang(void);
        extern int drv_get_count(void);
        char lb[64];
        strcpy(lb, "Language: ");
        strcpy(lb + strlen(lb), l10n64_get_lang() ? "English  [switch]" : "中文  [切换]");
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, lb, g_shell.colors.panel_fg, 12);
        ry += 24;
        char db[64];
        strcpy(db, "Drivers: ");
        hs_itoa(drv_get_count(), db + strlen(db));
        strcpy(db + strlen(db), "  [Refresh]");
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, db, g_shell.colors.panel_fg, 12);
        ry += 24;
        /* V0.13.39: 通知位置行 */
        extern int hs_notification_get_pos(void);
        char npb[64];
        strcpy(npb, "Notify Pos: ");
        {
            int p = hs_notification_get_pos();
            strcpy(npb + strlen(npb), p == 0 ? "Top-Right" : p == 1 ? "Top-Left" :
                   p == 2 ? "Bottom-Right" : "Bottom-Left");
            strcpy(npb + strlen(npb), "  [Click to switch]");
        }
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, npb, g_shell.colors.panel_fg, 12);
        ry += 24;
        /* V0.13.39: 存储管理区 */
        gfx64_fill_rect(rx, ry, w->width - 180, 1, g_shell.colors.border);
        ry += 8;
        extern int storage_hal_get_device(void);
        extern int usb64_dev_count(void);
        extern int usb64_mount_usb(void);
        extern int usb64_umount_usb(void);
        char sb[80];
        strcpy(sb, "Storage: ");
        strcpy(sb + strlen(sb), storage_hal_get_device() ? "USB" : "System Disk");
        strcpy(sb + strlen(sb), "  [");
        strcpy(sb + strlen(sb), usb64_dev_count() > 0 ? "USB online" : "No USB");
        strcpy(sb + strlen(sb), "]");
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, sb, g_shell.colors.panel_fg, 12);
        ry += 24;
        char sb2[80];
        strcpy(sb2, storage_hal_get_device() ? "Mounted on USB ext2  [Unmount]" : "System disk ext2  [Mount USB]");
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, sb2, g_shell.colors.accent, 12);
        g_settings_storage_ry = ry;
        ry += 24;
        gfx64_fill_rect(rx, ry, w->width - 180, 1, g_shell.colors.border);
        ry += 8;
        /* V0.13.39: 注册表/文件关联管理 */
        extern int reg64_count(void);
        extern int file_assoc_count(void);
        char rb[80];
        strcpy(rb, "Registry: ");
        hs_itoa(reg64_count(), rb + strlen(rb));
        strcpy(rb + strlen(rb), "  [View]");
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, rb, g_shell.colors.panel_fg, 12);
        g_settings_reg_ry = ry;
        ry += 24;
        char ab[80];
        strcpy(ab, "Associations: ");
        hs_itoa(file_assoc_count(), ab + strlen(ab));
        strcpy(ab + strlen(ab), "  [View]");
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, ab, g_shell.colors.panel_fg, 12);
        g_settings_assoc_ry = ry;
    } else {
        /* About */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "About Hepu OS", g_shell.colors.panel_fg, 16);
        ry += 30;
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "A self-developed 64-bit OS", g_shell.colors.panel_fg, 12);
        ry += 24;
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Inspired by Quickshell", g_shell.colors.panel_fg, 12);
        ry += 24;
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "HepuShell Desktop Environment", g_shell.colors.panel_fg, 12);
        ry += 24;
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "(c) 2026 Hepu OS Project", g_shell.colors.accent, 12);
    }
}

/* ============================================================================
 * 工作区
 * ========================================================================== */

void hepushell_switch_workspace(int ws)
{
    if (ws < 0 || ws >= HS_MAX_WORKSPACES) return;
    g_shell.current_workspace = ws;
    hs_window_tile();
}

/* ============================================================================
 * 应用注册
 * ========================================================================== */

void hepushell_add_app(const char *name, const char *command, const char *icon)
{
    if (g_shell.app_count >= HS_MAX_APPS) return;
    hs_app_t *app = &g_shell.apps[g_shell.app_count];
    strcpy_hs(app->name, name);
    strcpy_hs(app->command, command);
    strcpy_hs(app->icon, icon);
    g_shell.app_count++;
}

/* ============================================================================
 * 面板注册表：配置驱动的模块化面板系统
 * ========================================================================== */

/* 前向声明渲染函数 */
static void hs_top_bar_render(void);
static void hs_dock_render(void);
static void hs_sidebar_render(void);
static void hs_control_center_render(void);
static void hs_desktop_clock_render(void);
static void hs_weather_widget_render(void);
static void hs_dynamic_island_render(void);

void hs_panel_registry_init(void)
{
    g_shell.panel_registry[HS_PANEL_TOP_BAR].name = "top_bar";
    g_shell.panel_registry[HS_PANEL_TOP_BAR].enabled = 1;
    g_shell.panel_registry[HS_PANEL_TOP_BAR].render = hs_top_bar_render;

    g_shell.panel_registry[HS_PANEL_DOCK].name = "dock";
    g_shell.panel_registry[HS_PANEL_DOCK].enabled = 1;
    g_shell.panel_registry[HS_PANEL_DOCK].render = hs_dock_render;

    g_shell.panel_registry[HS_PANEL_SIDEBAR].name = "sidebar";
    g_shell.panel_registry[HS_PANEL_SIDEBAR].enabled = 1;
    g_shell.panel_registry[HS_PANEL_SIDEBAR].render = hs_sidebar_render;

    g_shell.panel_registry[HS_PANEL_CONTROL_CENTER].name = "control_center";
    g_shell.panel_registry[HS_PANEL_CONTROL_CENTER].enabled = 1;
    g_shell.panel_registry[HS_PANEL_CONTROL_CENTER].render = hs_control_center_render;

    g_shell.panel_registry[HS_PANEL_DESKTOP_CLOCK].name = "desktop_clock";
    g_shell.panel_registry[HS_PANEL_DESKTOP_CLOCK].enabled = 1;
    g_shell.panel_registry[HS_PANEL_DESKTOP_CLOCK].render = hs_desktop_clock_render;

    g_shell.panel_registry[HS_PANEL_WEATHER].name = "weather";
    g_shell.panel_registry[HS_PANEL_WEATHER].enabled = 1;
    g_shell.panel_registry[HS_PANEL_WEATHER].render = hs_weather_widget_render;

    g_shell.panel_registry[HS_PANEL_DYNAMIC_ISLAND].name = "dynamic_island";
    g_shell.panel_registry[HS_PANEL_DYNAMIC_ISLAND].enabled = 1;
    g_shell.panel_registry[HS_PANEL_DYNAMIC_ISLAND].render = hs_dynamic_island_render;
}

int hs_panel_set_enabled(const char *name, int enabled)
{
    for (int i = 0; i < HS_PANEL_COUNT; i++) {
        if (str_cmp(g_shell.panel_registry[i].name, name) == 0) {
            g_shell.panel_registry[i].enabled = enabled ? 1 : 0;
            return 0;
        }
    }
    return -1;
}

int hs_panel_is_enabled(const char *name)
{
    for (int i = 0; i < HS_PANEL_COUNT; i++) {
        if (str_cmp(g_shell.panel_registry[i].name, name) == 0) {
            return g_shell.panel_registry[i].enabled;
        }
    }
    return 0;
}

void hs_panel_render_all(void)
{
    /* 按层级顺序渲染所有启用的面板（顶部面板优先，避免累积corruption）
     * 控制中心是常驻侧栏，绘制在窗口之上，见 hepushell_render */
    int order[] = {
        HS_PANEL_TOP_BAR,
        HS_PANEL_DYNAMIC_ISLAND,
        HS_PANEL_DOCK,
        HS_PANEL_DESKTOP_CLOCK,
        HS_PANEL_WEATHER,
        HS_PANEL_SIDEBAR
    };
    for (int i = 0; i < 6; i++) {
        int idx = order[i];
        if (g_shell.panel_registry[idx].enabled && g_shell.panel_registry[idx].render) {
            g_shell.panel_registry[idx].render();
        }
    }
}

/* ============================================================================
 * Quickshell风格UI组件
 * ========================================================================== */

/* 灵动岛：顶部中央胶囊，显示时间 */
static void hs_dynamic_island_render(void)
{
    int island_w = hs_sx(g_shell.ui.island_width);
    int island_h = hs_sy(g_shell.ui.island_height);
    int island_x = (g_shell.screen_width - island_w) / 2;
    int island_y = hs_sy(g_shell.ui.island_y);

    if (g_island_expanded) {
        /* 展开状态：更大的卡片，显示时间+日期+天气+系统状态 */
        island_w = hs_sx(360);
        island_h = hs_sy(120);
        island_x = (g_shell.screen_width - island_w) / 2;
    }

    gfx64_fill_rounded_rect(island_x, island_y, island_w, island_h, hs_s(15),
                            g_shell.colors.titlebar_bg);
    gfx64_draw_rounded_rect(island_x, island_y, island_w, island_h, hs_s(15),
                            g_shell.colors.border);

    uint8_t h = rtc64_get_hours();
    uint8_t m = rtc64_get_minutes();
    char time[16];
    time[0] = '0' + h / 10; time[1] = '0' + h % 10; time[2] = ':';
    time[3] = '0' + m / 10; time[4] = '0' + m % 10; time[5] = 0;

    void *fb = gfx64_get_framebuffer();
    int font_size = hs_s(g_shell.ui.island_font_size);
    int text_w = 5 * font_size / 2;

    if (!g_island_expanded) {
        /* 收起状态：只显示时间 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           island_x + (island_w - text_w) / 2, island_y + (island_h - font_size) / 2,
                           time, g_shell.colors.panel_fg, font_size);
    } else {
        /* 展开状态：时间+日期+天气+系统状态 */
        int cy = island_y + hs_sy(12);
        /* 时间（大字号） */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           island_x + hs_sx(20), cy, time, g_shell.colors.panel_fg, hs_s(28));
        cy += hs_sy(36);
        /* 日期 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           island_x + hs_sx(20), cy, "October 15, Thursday", g_shell.colors.text_secondary, hs_s(14));
        /* 右侧天气 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           island_x + island_w - hs_sx(100), cy, "26C Cloudy", g_shell.colors.text_secondary, hs_s(14));
        cy += hs_sy(28);
        /* 系统状态条 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           island_x + hs_sx(20), cy, "CPU 35%  MEM 60%  WiFi On", g_shell.colors.text_secondary, hs_s(12));
    }
}

/* ============================================================================
 * 图标绘制函数（简化版，避免浮点运算）
 * ========================================================================== */

/* ============================================================================
 * V0.13.14: 统一矢量图标引擎
 * 全部图标用矢量原语(draw_arc/draw_line_aa/fill_circle/fill_polygon)绘制,
 * 摒弃"矩形段近似"画法; 单色透明底; 支持任意尺寸缩放
 * ========================================================================== */

/* WiFi: 底部圆点 + 3 条标准拱形弧(凸面朝上, 开口向下) */
static void hs_icon_wifi(int x, int y, int size, uint32_t color)
{
    int cx = x + size / 2;
    int cy = y + size - size / 8;
    gfx64_fill_circle(cx, cy, size / 10, color);               /* 底部圆点 */
    gfx64_draw_arc(cx, cy, size * 2 / 10, 45, 135, color);     /* 弧1 */
    gfx64_draw_arc(cx, cy, size * 34 / 100, 40, 140, color);   /* 弧2 */
    gfx64_draw_arc(cx, cy, size * 48 / 100, 35, 145, color);   /* 弧3 */
}

/* 电池: 圆角外框 + 正极 + 单色电量填充 */
static void hs_icon_battery(int x, int y, int size, uint32_t color, int percent)
{
    int w = size - 3;
    int h = size / 2 + 2;
    int by = y + (size - h) / 2;
    gfx64_draw_rounded_rect(x, by, w, h, 2, color);            /* 外框 */
    gfx64_fill_rect(x + w, by + h / 4, 2, h / 2, color);       /* 正极 */
    int fill_w = (w - 4) * percent / 100;
    if (fill_w > 0) gfx64_fill_rect(x + 2, by + 2, fill_w, h - 4, color);
}

/* 音量: 扬声器(多边形) + 锥形 + 声波弧 */
static void hs_icon_volume(int x, int y, int size, uint32_t color)
{
    int cy = y + size / 2;
    /* 扬声器主体 */
    int xs1[] = { x, x + size / 4, x + size / 4, x };
    int ys1[] = { cy - size / 5, cy - size / 5, cy + size / 5, cy + size / 5 };
    gfx64_fill_polygon(xs1, ys1, 4, color);
    /* 锥形 */
    int xs2[] = { x + size / 4, x + size * 2 / 3, x + size / 4 };
    int ys2[] = { cy - size / 5, cy, cy + size / 5 };
    gfx64_fill_polygon(xs2, ys2, 3, color);
    /* 声波弧(凸面朝右) */
    int sx = x + size * 2 / 3;
    gfx64_draw_arc(sx, cy, size / 5, 60, 120, color);
    gfx64_draw_arc(sx, cy, size * 4 / 15, 55, 125, color);
}

/* 设置: 齿轮(8 齿 + 齿体圆 + 中心孔) */
static void hs_icon_settings(int x, int y, int size, uint32_t color)
{
    int cx = x + size / 2, cy = y + size / 2;
    int r = size * 3 / 10;
    /* 8 个齿(8 方向小方块) */
    for (int i = 0; i < 8; i++) {
        int dx = 0, dy = 0;
        switch (i) {
            case 0: dy = -1; break;
            case 1: dx = 1; dy = -1; break;
            case 2: dx = 1; break;
            case 3: dx = 1; dy = 1; break;
            case 4: dy = 1; break;
            case 5: dx = -1; dy = 1; break;
            case 6: dx = -1; break;
            case 7: dx = -1; dy = -1; break;
        }
        int tw = size / 5, th = size / 5;
        int tx = cx + dx * (r + size / 12) - tw / 2;
        int ty = cy + dy * (r + size / 12) - th / 2;
        gfx64_fill_rect(tx, ty, tw, th, color);
    }
    gfx64_fill_circle(cx, cy, r, color);                       /* 齿体圆 */
    gfx64_fill_circle(cx, cy, size / 5, g_shell.colors.panel_bg); /* 中心孔 */
}

/* 电源: 大半圆弧(顶部开口) + 顶部竖线 */
static void hs_icon_power(int x, int y, int size, uint32_t color)
{
    int cx = x + size / 2, cy = y + size * 11 / 20;
    int r = size * 7 / 20;
    gfx64_draw_arc(cx, cy, r, 200, 340, color);                /* 圆环(开口朝上) */
    gfx64_fill_rect(cx - 1, y + size / 10, 3, r * 4 / 5, color); /* 竖线 */
}

/* 文件夹 */
void hs_icon_folder(int x, int y, int size, uint32_t color)
{
    int w = size - 2;
    int xs[] = { x, x + w, x + w, x };
    int ys[] = { y + size / 3, y + size / 3, y + size - 2, y + size - 2 };
    gfx64_fill_polygon(xs, ys, 4, color);                      /* 主体 */
    gfx64_fill_rect(x, y, size / 3, size / 3 - 2, color);      /* 标签 */
}

/* 文档 */
void hs_icon_doc(int x, int y, int size, uint32_t color)
{
    gfx64_fill_rect(x + 2, y, size - 4, size, color);          /* 页体 */
    /* 折角(用面板底色切出) */
    int xs[] = { x + size - 4, x + size - 4, x + size - size / 4 };
    int ys[] = { y + size / 4, y, y };
    gfx64_fill_polygon(xs, ys, 3, g_shell.colors.panel_bg);
    gfx64_draw_line_aa(x + size - 4, y + size / 4, x + size - size / 4, y, color);
}

/* 终端: 外框 + ">" 提示符 + 光标 */
static void hs_icon_terminal(int x, int y, int size, uint32_t color)
{
    gfx64_draw_rounded_rect(x, y, size, size, 3, color);
    gfx64_draw_line_aa(x + 3, y + size / 3, x + size / 2, y + size / 2, color);
    gfx64_draw_line_aa(x + 3, y + size * 2 / 3, x + size / 2, y + size / 2, color);
    gfx64_draw_line_aa(x + size * 2 / 3, y + size / 2, x + size - 4, y + size / 2, color);
}

/* 任务管理器: 3 根柱 + 基线 */
static void hs_icon_task(int x, int y, int size, uint32_t color)
{
    gfx64_fill_rect(x + 2, y + size / 2, size / 5, size / 2 - 2, color);
    gfx64_fill_rect(x + size * 2 / 5, y + size / 4, size / 5, size * 3 / 4 - 2, color);
    gfx64_fill_rect(x + size * 4 / 5 - size / 5, y + size / 6, size / 5, size * 5 / 6 - 2, color);
    gfx64_draw_line_aa(x, y + size - 2, x + size, y + size - 2, color);
}

/* V0.13.15: 应用/文件类型矢量图标 */

/* 浏览器: 地球(圆环+经纬) */
static void hs_icon_browser(int x, int y, int size, uint32_t color)
{
    int cx = x + size / 2, cy = y + size / 2, r = size / 2 - 2;
    gfx64_draw_circle(cx, cy, r, color);
    gfx64_draw_line_aa(cx - r, cy, cx + r, cy, color);          /* 水平线 */
    gfx64_draw_arc(cx, cy, r, 90, 270, color);                  /* 竖经线(右半) */
    gfx64_draw_arc(cx, cy, r * 2 / 3, 70, 290, color);          /* 内纬线 */
}

/* 编辑器: 文档 + 斜笔 */
static void hs_icon_editor(int x, int y, int size, uint32_t color)
{
    gfx64_fill_rect(x + 4, y, size - 8, size, color);
    gfx64_draw_line_aa(x + size / 3, y + size / 2, x + size - 4, y + size / 8, color);
    gfx64_draw_line_aa(x + size - 4, y + size / 8, x + size - size / 5, y + size / 5, color);
}

/* 计算器: 圆角面板 + 按键格 */
static void hs_icon_calc(int x, int y, int size, uint32_t color)
{
    gfx64_draw_rounded_rect(x, y, size, size, 3, color);
    gfx64_draw_line_aa(x + 3, y + size / 4, x + size - 3, y + size / 4, color);  /* 屏 */
    int gx[3] = { x + 4, x + size / 2 - 2, x + size - size / 3 + 1 };
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 2; j++)
            gfx64_fill_rect(gx[i], y + size / 4 + 3 + j * (size / 3), size / 3 - 3, size / 3 - 3, color);
}

/* 应用商店: 购物袋(多边形+提手) */
static void hs_icon_store(int x, int y, int size, uint32_t color)
{
    int xs[] = { x + size / 6, x + size * 5 / 6, x + size - 2, x + 2 };
    int ys[] = { y + size / 4, y + size / 4, y + size - 2, y + size - 2 };
    gfx64_fill_polygon(xs, ys, 4, color);
    gfx64_draw_arc(x + size / 2, y + size / 4, size / 5, 180, 360, color);  /* 提手 */
}

/* 关于: 圆 + i */
static void hs_icon_info(int x, int y, int size, uint32_t color)
{
    gfx64_fill_circle(x + size / 2, y + size / 2, size / 2 - 2, color);
    gfx64_fill_circle(x + size / 2, y + size / 3, size / 12, 0xFAF6EE);
    gfx64_fill_rect(x + size / 2 - size / 12, y + size * 2 / 5, size / 6, size / 4, 0xFAF6EE);
}

/* 媒体: 播放三角 + 音符 */
void hs_icon_media(int x, int y, int size, uint32_t color)
{
    int xs[] = { x + 2, x + size / 3, x + 2 };
    int ys[] = { y + 2, y + size / 2, y + size - 2 };
    gfx64_fill_polygon(xs, ys, 3, color);
    gfx64_fill_circle(x + size * 3 / 4, y + size / 3, size / 8, color);
    gfx64_fill_rect(x + size * 3 / 4 - 1, y + size / 3, 3, size / 3, color);
    gfx64_fill_circle(x + size * 3 / 4, y + size * 3 / 4, size / 8, color);
}

/* 未知文件: 圆角页 + 问号 */
void hs_icon_file(int x, int y, int size, uint32_t color)
{
    gfx64_draw_rounded_rect(x + 2, y, size - 4, size, 3, color);
    gfx64_fill_rect(x + size / 3, y + size / 3, size / 3, size / 6, color);
    gfx64_fill_rect(x + size / 3 + size / 6, y + size / 2, size / 6, size / 8, color);
    gfx64_fill_rect(x + size / 3, y + size * 3 / 4, size / 3, size / 8, color);
}


/* 顶部Bar：左侧工作区+右侧状态图标 */
static void hs_top_bar_render(void)
{
    /* 对齐 Clavis: 悬浮圆角胶囊顶栏(外边距 10, 高 36, 圆角 18) */
    int margin = hs_sx(10);
    int bar_top = hs_sy(8);
    int bar_h = hs_sy(g_shell.ui.bar_height);
    int bar_w = g_shell.screen_width - 2 * margin;

    gfx64_fill_rounded_rect(margin, bar_top, bar_w, bar_h, hs_s(18),
                            g_shell.colors.panel_bg);
    gfx64_draw_rounded_rect(margin, bar_top, bar_w, bar_h, hs_s(18),
                            g_shell.colors.border);

    /* 左侧工作区指示 */
    int ws_x = margin + hs_sx(16);
    for (int i = 0; i < 4; i++) {
        int dot_w = (i == g_shell.current_workspace) ? hs_sx(20) : hs_sx(8);
        int dot_h = hs_sy(8);
        int dot_y = bar_top + (bar_h - dot_h) / 2;
        uint32_t color = (i == g_shell.current_workspace) ?
                         g_shell.colors.accent : g_shell.colors.border;
        gfx64_fill_rounded_rect(ws_x, dot_y, dot_w, dot_h, hs_s(4), color);
        ws_x += dot_w + hs_sx(12);
    }

    /* 右侧状态栏矢量图标（替换文字"WiFi Bat Vol Set Pwr"） */
    int icon_size = hs_s(16);
    int icon_y = bar_top + (bar_h - icon_size) / 2;
    int icon_x = g_shell.screen_width - margin - hs_sx(14);
    uint32_t fg = g_shell.colors.panel_fg;

    /* 从右到左排列：电源、设置、音量、电池、WiFi */
    icon_x -= icon_size; hs_icon_power(icon_x, icon_y, icon_size, fg);
    icon_x -= hs_sx(8) + icon_size; hs_icon_settings(icon_x, icon_y, icon_size, fg);
    icon_x -= hs_sx(8) + icon_size; hs_icon_volume(icon_x, icon_y, icon_size, fg);
    icon_x -= hs_sx(8) + icon_size; hs_icon_battery(icon_x, icon_y, icon_size, fg, 75);
    icon_x -= hs_sx(8) + icon_size; hs_icon_wifi(icon_x, icon_y, icon_size, fg);
}

/* 桌面大时钟 */
static void hs_desktop_clock_render(void)
{
    void *fb = gfx64_get_framebuffer();
    int clock_x = hs_sx(g_shell.ui.clock_x);
    int clock_y = hs_sy(g_shell.ui.clock_y);

    uint8_t h = rtc64_get_hours();
    uint8_t m = rtc64_get_minutes();
    static char time[16];
    time[0] = '0' + h / 10; time[1] = '0' + h % 10; time[2] = ':';
    time[3] = '0' + m / 10; time[4] = '0' + m % 10; time[5] = 0;

    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       clock_x, clock_y, time, g_shell.colors.accent, hs_s(g_shell.ui.clock_font_size));

    static const char date[] = "October 15, Thursday";
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       clock_x + hs_sx(10), clock_y + hs_sy(85), (char*)date,
                       g_shell.colors.panel_fg, hs_s(g_shell.ui.date_font_size));
}

/* 天气小部件 */
static void hs_weather_widget_render(void)
{
    int wx = g_shell.screen_width - hs_sx(g_shell.ui.weather_margin_right);
    int wy = g_shell.screen_height - hs_sy(g_shell.ui.weather_margin_bottom);
    int ww = hs_sx(g_shell.ui.weather_width);
    int wh = hs_sy(g_shell.ui.weather_height);

    gfx64_fill_rounded_rect(wx, wy, ww, wh, hs_s(g_shell.ui.corner_radius), g_shell.colors.accent);

    void *fb = gfx64_get_framebuffer();

    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       wx + hs_sx(20), wy + hs_sy(15), g_weather_temp, hs_on_accent(), hs_s(g_shell.ui.weather_temp_font));

    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       wx + hs_sx(15), wy + hs_sy(70), g_weather_desc, hs_on_accent(), hs_s(g_shell.ui.weather_desc_font));
}

/* 左侧三标签侧边栏 */
/* 侧边栏当前标签（定义在文件开头前向声明处初始化） */

/* 锁屏状态 */
static int g_locked = 0;       /* 0=桌面, 1=锁屏, 2=登录 */
static char g_password[32] = {0};
static int g_password_len = 0;
static int g_vkb_visible = 0;  /* V0.13.39: 锁屏虚拟键盘 */
static int g_vkb_shift = 0;    /* 保留: shift状态 */

/* 锁屏界面：模糊背景+大时钟 */
static void hs_lock_screen_render(void)
{
    /* 深色半透明背景（模拟模糊效果） */
    gfx64_fill_rect(0, 0, g_shell.screen_width, g_shell.screen_height, g_shell.colors.bg);

    void *fb = gfx64_get_framebuffer();

    /* 大时钟 */
    int clock_x = g_shell.screen_width / 2 - hs_sx(180);
    int clock_y = hs_sy(200);

    uint8_t h = rtc64_get_hours();
    uint8_t m = rtc64_get_minutes();
    char time[16];
    time[0] = '0' + h / 10; time[1] = '0' + h % 10; time[2] = ':';
    time[3] = '0' + m / 10; time[4] = '0' + m % 10; time[5] = 0;

    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       clock_x, clock_y, time, g_shell.colors.panel_fg, hs_s(64));

    /* 日期 */
    char date[32] = "October 15, Thursday";
    int date_w = hs_sx(17 * 10);
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       clock_x + (hs_sx(360) - date_w) / 2, clock_y + hs_sy(110), date, g_shell.colors.accent, hs_s(20));

    /* 底部提示 */
    extern const char *l10n64_tr(int id);
    const char *hint = l10n64_tr(L10N_STR_LOCK_HINT);
    int hint_w = hs_sx(22 * 8);
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       (g_shell.screen_width - hint_w) / 2, g_shell.screen_height - hs_sy(80),
                       hint, 0x888888, hs_s(14));
}

/* 登录界面：头像+用户名+密码 */
static void hs_login_screen_render(void)
{
    /* 深色半透明背景 */
    gfx64_fill_rect(0, 0, g_shell.screen_width, g_shell.screen_height, g_shell.colors.bg);

    void *fb = gfx64_get_framebuffer();
    int center_x = g_shell.screen_width / 2;

    /* 用户头像（圆形） */
    int avatar_size = hs_s(96);
    int avatar_x = center_x - avatar_size / 2;
    int avatar_y = hs_sy(180);
    gfx64_fill_rounded_rect(avatar_x, avatar_y, avatar_size, avatar_size, hs_s(48), g_shell.colors.accent);
    /* 头像中的用户图标 */
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       avatar_x + hs_sx(30), avatar_y + hs_sy(25), "U", hs_on_accent(), hs_s(48));

    /* 用户名 */
    char username[32] = "hepu";
    int name_w = hs_sx(4 * 12);
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       center_x - name_w / 2, avatar_y + avatar_size + hs_sy(20),
                       username, g_shell.colors.panel_fg, hs_s(20));

    /* 密码输入框 */
    int input_w = hs_sx(240);
    int input_h = hs_sy(36);
    int input_x = center_x - input_w / 2;
    int input_y = avatar_y + avatar_size + hs_sy(60);
    gfx64_fill_rounded_rect(input_x, input_y, input_w, input_h, hs_s(8), g_shell.colors.titlebar_bg);
    gfx64_draw_rounded_rect(input_x, input_y, input_w, input_h, hs_s(8), g_shell.colors.accent);

    /* 密码圆点 */
    int dot_y = input_y + (input_h - hs_sy(10)) / 2;
    for (int i = 0; i < g_password_len; i++) {
        gfx64_fill_rounded_rect(input_x + hs_sx(20) + i * hs_sx(18), dot_y, hs_s(10), hs_s(10), hs_s(5), g_shell.colors.panel_fg);
    }

    /* 登录按钮 */
    int btn_w = hs_sx(100);
    int btn_h = hs_sy(32);
    int btn_x = center_x - btn_w / 2;
    int btn_y = input_y + input_h + hs_sy(20);
    gfx64_fill_rounded_rect(btn_x, btn_y, btn_w, btn_h, hs_s(8), g_shell.colors.accent);
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       btn_x + hs_sx(28), btn_y + hs_sy(8), l10n64_tr(L10N_STR_LOGIN_BTN), hs_on_accent(), hs_s(16));

    /* 提示 */
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       center_x - hs_sx(80), btn_y + btn_h + hs_sy(30),
                       l10n64_tr(L10N_STR_ESC_LOCK), 0x888888, hs_s(12));

    /* V0.13.39: 右下角 关机键 + 虚拟键盘键 */
    {
        int kb_x = g_shell.screen_width - hs_sx(200);
        int kb_y = g_shell.screen_height - hs_sy(120);
        gfx64_fill_rounded_rect(kb_x, kb_y, hs_sx(88), hs_sy(34), hs_s(8), g_shell.colors.titlebar_bg);
        gfx64_draw_rounded_rect(kb_x, kb_y, hs_sx(88), hs_sy(34), hs_s(8), g_shell.colors.accent);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           kb_x + hs_sx(14), kb_y + hs_sy(8), "VKB", g_shell.colors.panel_fg, hs_s(16));
        int po_x = kb_x + hs_sx(104);
        gfx64_fill_rounded_rect(po_x, kb_y, hs_sx(88), hs_sy(34), hs_s(8), 0x6E3A3A);
        gfx64_draw_rounded_rect(po_x, kb_y, hs_sx(88), hs_sy(34), hs_s(8), g_shell.colors.accent);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           po_x + hs_sx(20), kb_y + hs_sy(8), "PWR", g_shell.colors.panel_fg, hs_s(16));
    }

    /* V0.13.39: 虚拟键盘渲染 */
    if (g_vkb_visible) {
        static const char *vkb_rows[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
        static const int vkb_row_n[] = {10, 10, 9, 7};
        int kb_w = hs_sx(640);
        int kb_h = hs_sy(4 * 38 + 10);
        int kb_x = center_x - kb_w / 2;
        int kb_y = g_shell.screen_height - kb_h - hs_sy(16);
        gfx64_fill_rounded_rect(kb_x - hs_sx(6), kb_y - hs_sy(6), kb_w + hs_sx(12), kb_h + hs_sy(12), hs_s(12), 0x22201C);
        for (int r = 0; r < 4; r++) {
            int key_w = (kb_w - (vkb_row_n[r] - 1) * hs_sx(4)) / vkb_row_n[r];
            int row_x = kb_x + (kb_w - vkb_row_n[r] * key_w - (vkb_row_n[r] - 1) * hs_sx(4)) / 2;
            for (int c = 0; c < vkb_row_n[r]; c++) {
                int kx = row_x + c * (key_w + hs_sx(4));
                int ky = kb_y + r * hs_sy(38);
                gfx64_fill_rounded_rect(kx, ky, key_w, hs_sy(32), hs_s(6), g_shell.colors.titlebar_bg);
                font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                                   kx + (key_w - hs_sx(14)) / 2, ky + hs_sy(7),
                                   vkb_rows[r] + c, g_shell.colors.panel_fg, hs_s(16));
            }
        }
        /* Backspace 键(第4行右侧) */
        int row4_x = kb_x + (kb_w - 8 * 0) / 2;
        int bsw = hs_sx(76);
        int bsx = kb_x + kb_w - bsw;
        int bsy = kb_y + 3 * hs_sy(38);
        gfx64_fill_rounded_rect(bsx, bsy, bsw, hs_sy(32), hs_s(6), g_shell.colors.accent_pressed);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           bsx + hs_sx(8), bsy + hs_sy(7), "DEL", g_shell.colors.panel_fg, hs_s(16));
    }
}

/* V0.55: 侧栏内容高度(基准单位) — 内容超出可视区的高度即可滚动量 */
static int hs_sidebar_max_scroll(int tab)
{
    int content;
    if (tab == 1) {
        /* Apps: 标题 35 + 行数*88 (行高对齐 Clavis cell 间距) */
        int rows = (g_shell.app_count + 2) / 3;
        content = 35 + rows * 88;
    } else if (tab == 0) {
        content = 35 + 28 + 28 + 40 + 24 + 20 + 20 + 20;  /* Info 固定行 */
    } else {
        content = 40 + 70 + 24 + 18 + 30 + 40;            /* Weather 固定行 */
    }
    int visible = g_shell.ui.sidebar_height - 52 - 12;    /* 内容顶 52, 底留 12 */
    int maxs = content - visible;
    return maxs > 0 ? maxs : 0;
}

/* V0.55: 侧栏滚轮 — 指针在侧栏内且当前页可滚动时消费, 返回 1 */
static int hs_sidebar_handle_wheel(int wheel)
{
    if (!hs_panel_is_enabled("sidebar")) return 0;
    int sx = hs_sx(g_shell.ui.sidebar_x);
    int sy = hs_sy(g_shell.ui.sidebar_y);
    int sw = hs_sx(g_shell.ui.sidebar_width);
    int sh = hs_sy(g_shell.ui.sidebar_height);
    if (g_shell.mouse_x < sx || g_shell.mouse_x > sx + sw ||
        g_shell.mouse_y < sy || g_shell.mouse_y > sy + sh)
        return 0;
    int maxs = hs_sidebar_max_scroll(g_sidebar_tab);
    if (maxs <= 0) return 0;
    g_sidebar_scroll += (wheel < 0) ? 24 : -24;
    if (g_sidebar_scroll > maxs) g_sidebar_scroll = maxs;
    if (g_sidebar_scroll < 0) g_sidebar_scroll = 0;
    return 1;
}

static void hs_sidebar_render(void)
{
    int sx = hs_sx(g_shell.ui.sidebar_x);
    int sy = hs_sy(g_shell.ui.sidebar_y);
    int sw = hs_sx(g_shell.ui.sidebar_width);
    int sh = hs_sy(g_shell.ui.sidebar_height);

    /* 侧边栏背景（毛玻璃效果） */
    gfx64_fill_rounded_rect(sx, sy, sw, sh, hs_s(g_shell.ui.corner_radius), g_shell.colors.window_bg);
    gfx64_draw_rounded_rect(sx, sy, sw, sh, hs_s(g_shell.ui.corner_radius), g_shell.colors.border);

    void *fb = gfx64_get_framebuffer();

    /* 标签栏 */
    int tab_y = sy + hs_sy(12);
    int tab_w = (sw - hs_sx(24)) / 3;
    const char *tabs[] = {"Info", "Apps", "Weather"};
    for (int i = 0; i < 3; i++) {
        int tx = sx + hs_sx(12) + i * tab_w;
        uint32_t bg = (i == g_sidebar_tab) ? g_shell.colors.accent : g_shell.colors.panel_bg;
        uint32_t fg = (i == g_sidebar_tab) ? hs_on_accent() : g_shell.colors.panel_fg;
        gfx64_fill_rounded_rect(tx, tab_y, tab_w - hs_sx(4), hs_sy(28), hs_s(8), bg);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           tx + (tab_w - hs_sx(4) - hs_sx(32)) / 2, tab_y + hs_sy(7),
                           tabs[i], fg, hs_s(14));
    }

    /* 内容区域 */
    int content_y = tab_y + hs_sy(40);
    if (g_sidebar_tab == 0) {
        /* 信息标签：系统状态 (V0.13.39: 真实数据) */
        extern int process64_cpu_usage(void);
        extern void pmm64_get_stats(uint64_t *total, uint64_t *used, uint64_t *free);
        extern uint32_t ext2_64_used_percent(void);
        uint64_t tp = 0, up = 0, fp = 0;
        pmm64_get_stats(&tp, &up, &fp);
        int cpu_p = process64_cpu_usage();
        int mem_p = (tp > 0) ? (int)(up * 100 / tp) : 0;
        int dsk_p = ext2_64_used_percent();
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "System Info", g_shell.colors.panel_fg, hs_s(18));
        content_y += hs_sy(35);

        /* CPU */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "CPU", g_shell.colors.panel_fg, hs_s(14));
        gfx64_fill_rounded_rect(sx + hs_sx(70), content_y + hs_sy(2), hs_sx(180), hs_sy(12), hs_s(4), g_shell.colors.border);
        gfx64_fill_rounded_rect(sx + hs_sx(70), content_y + hs_sy(2), hs_sx(180 * cpu_p / 100), hs_sy(12), hs_s(4), g_shell.colors.accent);
        {
            char pc[8];
            pc[0] = '0' + cpu_p / 100 % 10; pc[1] = '0' + cpu_p / 10 % 10; pc[2] = '0' + cpu_p % 10; pc[3] = '%'; pc[4] = 0;
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               sx + hs_sx(255), content_y, pc, g_shell.colors.panel_fg, hs_s(12));
        }
        content_y += hs_sy(28);

        /* Memory */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "MEM", g_shell.colors.panel_fg, hs_s(14));
        gfx64_fill_rounded_rect(sx + hs_sx(70), content_y + hs_sy(2), hs_sx(180), hs_sy(12), hs_s(4), g_shell.colors.border);
        gfx64_fill_rounded_rect(sx + hs_sx(70), content_y + hs_sy(2), hs_sx(180 * mem_p / 100), hs_sy(12), hs_s(4), g_shell.colors.accent);
        {
            char pc[8];
            pc[0] = '0' + mem_p / 100 % 10; pc[1] = '0' + mem_p / 10 % 10; pc[2] = '0' + mem_p % 10; pc[3] = '%'; pc[4] = 0;
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               sx + hs_sx(255), content_y, pc, g_shell.colors.panel_fg, hs_s(12));
        }
        content_y += hs_sy(28);

        /* Disk */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "DSK", g_shell.colors.panel_fg, hs_s(14));
        gfx64_fill_rounded_rect(sx + hs_sx(70), content_y + hs_sy(2), hs_sx(180), hs_sy(12), hs_s(4), g_shell.colors.border);
        gfx64_fill_rounded_rect(sx + hs_sx(70), content_y + hs_sy(2), hs_sx(180 * dsk_p / 100), hs_sy(12), hs_s(4), g_shell.colors.accent);
        {
            char pc[8];
            pc[0] = '0' + dsk_p / 100 % 10; pc[1] = '0' + dsk_p / 10 % 10; pc[2] = '0' + dsk_p % 10; pc[3] = '%'; pc[4] = 0;
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               sx + hs_sx(255), content_y, pc, g_shell.colors.panel_fg, hs_s(12));
        }
        content_y += hs_sy(40);

        /* 系统信息 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "Hepu OS v0.54.12", g_shell.colors.accent, hs_s(14));
        content_y += hs_sy(24);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "Kernel: 64-bit SMP", g_shell.colors.panel_fg, hs_s(12));
        content_y += hs_sy(20);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "CPU: 4 cores", g_shell.colors.panel_fg, hs_s(12));
        content_y += hs_sy(20);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "Memory: 2GB", g_shell.colors.panel_fg, hs_s(12));
    } else if (g_sidebar_tab == 1) {
        /* 应用抽屉 (V0.55: 真实注册应用列表 + Clavis 式垂直滚动) */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "Applications", g_shell.colors.panel_fg, hs_s(18));
        content_y += hs_sy(35);

        int scroll = hs_sy(g_sidebar_scroll);
        int clip_top = content_y;                 /* 网格视口顶 (标题固定) */
        int clip_bot = sy + sh - hs_sy(8);        /* 侧栏底内边距 */
        int row_h = hs_sy(88);
        for (int i = 0; i < g_shell.app_count; i++) {
            int ax = sx + hs_sx(20) + (i % 3) * hs_sx(80);
            int ay = content_y + (i / 3) * row_h - scroll;
            /* 无裁剪原语: 整行剔除 — 行不完全落在视口内时跳过 */
            if (ay < clip_top || ay + hs_sy(70) > clip_bot) continue;
            /* V0.13.15: 应用矢量图标(白, 透明底) */
            gfx64_fill_rounded_rect(ax, ay, hs_s(48), hs_s(48), hs_s(10), g_shell.colors.accent);
            uint32_t ic = 0xFDF8EF;
            switch (i) {
                case 0: hs_icon_terminal(ax + hs_s(8), ay + hs_s(8), hs_s(32), ic); break;
                case 1: hs_icon_folder(ax + hs_s(8), ay + hs_s(8), hs_s(32), ic); break;
                case 2: hs_icon_browser(ax + hs_s(8), ay + hs_s(8), hs_s(32), ic); break;
                case 3: hs_icon_settings(ax + hs_s(8), ay + hs_s(8), hs_s(32), ic); break;
                case 4: hs_icon_editor(ax + hs_s(8), ay + hs_s(8), hs_s(32), ic); break;
                case 5: hs_icon_calc(ax + hs_s(8), ay + hs_s(8), hs_s(32), ic); break;
                case 6: hs_icon_doc(ax + hs_s(8), ay + hs_s(8), hs_s(32), ic); break;
                case 7: hs_icon_task(ax + hs_s(8), ay + hs_s(8), hs_s(32), ic); break;
                case 8: hs_icon_store(ax + hs_s(8), ay + hs_s(8), hs_s(32), ic); break;
                case 9: hs_icon_info(ax + hs_s(8), ay + hs_s(8), hs_s(32), ic); break;
                case 10: hs_icon_settings(ax + hs_s(8), ay + hs_s(8), hs_s(32), ic); break;
                case 11: hs_icon_task(ax + hs_s(8), ay + hs_s(8), hs_s(32), ic); break;
                default: hs_icon_doc(ax + hs_s(8), ay + hs_s(8), hs_s(32), ic); break;
            }
            /* 名称 (截断至 10 字符, 对齐单元格宽) */
            char nm[11];
            int nl = 0;
            while (g_shell.apps[i].name[nl] && nl < 10) { nm[nl] = g_shell.apps[i].name[nl]; nl++; }
            nm[nl] = 0;
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               ax + hs_sx(2), ay + hs_sy(54), nm, g_shell.colors.panel_fg, hs_s(10));
        }

        /* Clavis StyledScrollBar: 内容溢出时右缘细 accent 滑块 */
        {
            int maxs = hs_sidebar_max_scroll(1);
            if (maxs > 0) {
                int track_y = clip_top;
                int track_h = clip_bot - clip_top;
                int tx = sx + sw - hs_sx(6);
                gfx64_fill_rounded_rect(tx, track_y, hs_sx(3), track_h, hs_s(2), g_shell.colors.border);
                int thumb_h = track_h * track_h / (track_h + hs_sy(maxs));
                int thumb_y = track_y + (track_h - thumb_h) * g_sidebar_scroll / maxs;
                gfx64_fill_rounded_rect(tx, thumb_y, hs_sx(3), thumb_h, hs_s(2), g_shell.colors.accent);
            }
        }
    } else {
        /* 天气标签 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "Weather", g_shell.colors.panel_fg, hs_s(18));
        content_y += hs_sy(40);

        /* 大温度 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "26", g_shell.colors.accent, hs_s(64));
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(90), content_y + hs_sy(10), "C", g_shell.colors.panel_fg, hs_s(24));
        content_y += hs_sy(70);

        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "Cloudy", g_shell.colors.panel_fg, hs_s(16));
        content_y += hs_sy(24);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "Humidity: 65%", g_shell.colors.panel_fg, hs_s(12));
        content_y += hs_sy(18);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "Wind: 12 km/h", g_shell.colors.panel_fg, hs_s(12));
        content_y += hs_sy(30);

        /* 5天预报 */
        const char *days[] = {"Mon", "Tue", "Wed", "Thu", "Fri"};
        const char *temps[] = {"26", "28", "25", "27", "24"};
        for (int i = 0; i < 5; i++) {
            int dx = sx + hs_sx(20) + i * hs_sx(48);
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               dx, content_y, days[i], g_shell.colors.panel_fg, hs_s(10));
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               dx + hs_sx(8), content_y + hs_sy(16), temps[i], g_shell.colors.accent, hs_s(14));
        }
    }
}

/* 右侧控制中心（对照 Clavis ControlCenterWindow 的导航栏 + 分页结构） */
static int g_cc_page = 0;

#define CC_RAIL_W 104        /* 导航栏宽度 */
#define CC_PAD    16         /* 内容区内边距 */

static void cc_geometry(int *cx, int *cy, int *cw, int *ch)
{
    int w = g_shell.ui.cc_width;
    int h = g_shell.ui.cc_height;
    if (w < 300) w = 360;
    if (h < 320) h = 440;
    *cw = w;
    *ch = h;
    *cx = g_shell.screen_width - g_shell.ui.cc_margin_right;
    if (*cx < 0) *cx = 0;
    if (*cx + w > g_shell.screen_width) *cx = g_shell.screen_width - w - 8;
    *cy = g_shell.ui.cc_y;
    if (*cy + h > g_shell.screen_height) *cy = g_shell.screen_height - h - 8;
}

/* 开关卡片：开启态用强调色填充，关闭态用次要表面 */
static void cc_toggle_card(void *fb, int x, int y, int w, int h,
                           int on, const char *label, const char *sub)
{
    uint32_t bg = on ? g_shell.colors.accent : g_shell.colors.titlebar_bg;
    uint32_t fg = on ? hs_on_accent() : g_shell.colors.panel_fg;
    uint32_t fg2 = on ? hs_on_accent() : g_shell.colors.text_secondary;
    gfx64_fill_rounded_rect(x, y, w, h, 10, bg);
    if (!on) gfx64_draw_rounded_rect(x, y, w, h, 10, g_shell.colors.border);
    int lx = x + (w - (int)strlen(label) * 7) / 2;
    if (lx < x + 6) lx = x + 6;
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       lx, y + 14, label, fg, 13);
    int sx = x + (w - (int)strlen(sub) * 5) / 2;
    if (sx < x + 6) sx = x + 6;
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       sx, y + 36, sub, fg2, 11);
}

/* 滑块卡片：标题 + 百分比 + 轨道 */
static void cc_slider_card(void *fb, int x, int y, int w, int h,
                           const char *label, int val)
{
    gfx64_fill_rounded_rect(x, y, w, h, 10, g_shell.colors.titlebar_bg);
    gfx64_draw_rounded_rect(x, y, w, h, 10, g_shell.colors.border);

    char vb[8];
    int p = 0;
    if (val >= 100) {
        vb[p++] = '1'; vb[p++] = '0'; vb[p++] = '0';
    } else if (val >= 10) {
        vb[p++] = '0' + val / 10; vb[p++] = '0' + val % 10;
    } else {
        vb[p++] = '0' + val;
    }
    vb[p++] = '%';
    vb[p] = 0;

    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       x + 14, y + 12, label, g_shell.colors.panel_fg, 13);
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       x + w - 52, y + 12, vb, g_shell.colors.text_secondary, 12);

    int tx = x + 14;
    int tw = w - 28;
    int ty = y + h - 22;
    int fill = tw * val / 100;
    gfx64_fill_rounded_rect(tx, ty, tw, 8, 4, g_shell.colors.border);
    if (fill > 8)
        gfx64_fill_rounded_rect(tx, ty, fill, 8, 4, g_shell.colors.accent);
    gfx64_fill_circle(tx + fill - 8, ty + 4, 7, g_shell.colors.accent);
}

static void hs_control_center_render(void)
{
    int cx, cy, cw, ch;
    cc_geometry(&cx, &cy, &cw, &ch);

    gfx64_fill_rounded_rect(cx, cy, cw, ch, 12, g_shell.colors.window_bg);
    gfx64_draw_rounded_rect(cx, cy, cw, ch, 12, g_shell.colors.border);

    void *fb = gfx64_get_framebuffer();

    /* 导航栏（Clavis NavigationRail） */
    const char *nav[] = {"Quick", "Devices", "Audio"};
    for (int i = 0; i < 3; i++) {
        int nx = cx + 8;
        int ny = cy + 14 + i * 46;
        int nw = CC_RAIL_W - 16;
        int nh = 38;
        uint32_t bg = (i == g_cc_page) ? g_shell.colors.accent : g_shell.colors.window_bg;
        uint32_t fg = (i == g_cc_page) ? hs_on_accent() : g_shell.colors.panel_fg;
        if (i != g_cc_page)
            gfx64_fill_rounded_rect(nx, ny, nw, nh, 8, g_shell.colors.titlebar_bg);
        else
            gfx64_fill_rounded_rect(nx, ny, nw, nh, 8, bg);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           nx + 12, ny + 12, nav[i], fg, 13);
    }

    int rx = cx + CC_RAIL_W + CC_PAD;
    int rw = cw - CC_RAIL_W - CC_PAD * 2;
    const char *toggles[] = {"WiFi", "BT", "Air", "DND", "Loc", "Torch"};

    if (g_cc_page == 0) {
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, cy + 18, "Quick Controls", g_shell.colors.panel_fg, 16);
        int card_w = (rw - 8) / 2;
        int card_h = 60;
        for (int i = 0; i < 6; i++) {
            int tx = rx + (i % 2) * (card_w + 8);
            int ty = cy + 48 + (i / 2) * (card_h + 8);
            cc_toggle_card(fb, tx, ty, card_w, card_h, g_shell.cc.toggles[i],
                           toggles[i], g_shell.cc.toggles[i] ? "On" : "Off");
        }
        cc_slider_card(fb, rx, cy + 258, rw, 58, "Volume", g_shell.cc.volume);
        cc_slider_card(fb, rx, cy + 322, rw, 58, "Brightness", g_shell.cc.brightness);
    } else if (g_cc_page == 1) {
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, cy + 18, "Devices", g_shell.colors.panel_fg, 16);
        for (int i = 0; i < 6; i++) {
            int ry = cy + 48 + i * 54;
            gfx64_fill_rounded_rect(rx, ry, rw, 48, 10, g_shell.colors.titlebar_bg);
            gfx64_draw_rounded_rect(rx, ry, rw, 48, 10, g_shell.colors.border);
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               rx + 14, ry + 16, toggles[i], g_shell.colors.panel_fg, 13);
            const char *st = g_shell.cc.toggles[i] ? "On" : "Off";
            uint32_t stc = g_shell.cc.toggles[i] ? g_shell.colors.accent
                                                : g_shell.colors.text_secondary;
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               rx + rw - 44, ry + 16, st, stc, 12);
            gfx64_fill_circle(rx + rw - 16, ry + 24, 5, stc);
        }
    } else {
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, cy + 18, "Audio & Display", g_shell.colors.panel_fg, 16);
        cc_slider_card(fb, rx, cy + 52, rw, 96, "Volume", g_shell.cc.volume);
        cc_slider_card(fb, rx, cy + 160, rw, 96, "Brightness", g_shell.cc.brightness);
        const char *note = "Adjust output level and panel backlight.";
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, cy + 276, note, g_shell.colors.text_secondary, 11);
        cc_toggle_card(fb, rx, cy + 300, rw, 60, g_shell.cc.toggles[3], "DND",
                       g_shell.cc.toggles[3] ? "Do Not Disturb on" : "Do Not Disturb off");
        cc_toggle_card(fb, rx, cy + 368, rw, 60, g_shell.cc.toggles[5], "Torch",
                       g_shell.cc.toggles[5] ? "Light on" : "Light off");
    }
}

/* 把点击横坐标换算成滑块百分比 */
static void cc_slider_set(int x, int tx, int tw, int *dst)
{
    int v = (x - tx) * 100 / tw;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    *dst = v;
}

/* 控制中心点击处理；命中面板区域即消费点击（面板绘制在窗口之上） */
static int hs_control_center_handle_click(int x, int y)
{
    int cx, cy, cw, ch;
    cc_geometry(&cx, &cy, &cw, &ch);
    if (x < cx || x > cx + cw || y < cy || y > cy + ch) return 0;

    /* 导航栏切页 */
    for (int i = 0; i < 3; i++) {
        int nx = cx + 8;
        int ny = cy + 14 + i * 46;
        if (x >= nx && x <= nx + CC_RAIL_W - 16 && y >= ny && y <= ny + 38) {
            g_cc_page = i;
            return 1;
        }
    }

    int rx = cx + CC_RAIL_W + CC_PAD;
    int rw = cw - CC_RAIL_W - CC_PAD * 2;
    int card_w = (rw - 8) / 2;

    if (g_cc_page == 0) {
        for (int i = 0; i < 6; i++) {
            int tx = rx + (i % 2) * (card_w + 8);
            int ty = cy + 48 + (i / 2) * 68;
            if (x >= tx && x <= tx + card_w && y >= ty && y <= ty + 60) {
                g_shell.cc.toggles[i] = !g_shell.cc.toggles[i];
                return 1;
            }
        }
        if (y >= cy + 294 && y <= cy + 312) {
            cc_slider_set(x, rx + 14, rw - 28, &g_shell.cc.volume);
            return 1;
        }
        if (y >= cy + 358 && y <= cy + 376) {
            cc_slider_set(x, rx + 14, rw - 28, &g_shell.cc.brightness);
            return 1;
        }
    } else if (g_cc_page == 1) {
        for (int i = 0; i < 6; i++) {
            int ry = cy + 48 + i * 54;
            if (x >= rx && x <= rx + rw && y >= ry && y <= ry + 48) {
                g_shell.cc.toggles[i] = !g_shell.cc.toggles[i];
                return 1;
            }
        }
    } else {
        if (y >= cy + 124 && y <= cy + 138) {
            cc_slider_set(x, rx + 14, rw - 28, &g_shell.cc.volume);
            return 1;
        }
        if (y >= cy + 232 && y <= cy + 246) {
            cc_slider_set(x, rx + 14, rw - 28, &g_shell.cc.brightness);
            return 1;
        }
        if (y >= cy + 300 && y <= cy + 360) {
            g_shell.cc.toggles[3] = !g_shell.cc.toggles[3];
            return 1;
        }
        if (y >= cy + 368 && y <= cy + 428) {
            g_shell.cc.toggles[5] = !g_shell.cc.toggles[5];
            return 1;
        }
    }
    return 1;
}

/* 右键菜单渲染 */
static int g_menu_file_mode = 0;   /* V0.13.39: 1=文件管理器右键菜单 */

static void hs_context_menu_render(void)
{
    const char *f_items[] = {"Open", "Delete", "New Folder", "Rename", "Refresh", "Up Dir", "---", "Close Menu"};
    const char *items[] = {"New Window", "Change Theme", "Settings", "About", "---", "Lock Screen", "Desktop Color"};
    const char **items_p = g_menu_file_mode ? f_items : items;
    int item_count = g_menu_file_mode ? 8 : 7;
    int menu_w = 180;
    int item_h = 32;
    int menu_h = item_count * item_h + 8;

    int mx = g_context_menu_x;
    int my = g_context_menu_y;
    if (mx + menu_w > g_shell.screen_width) mx = g_shell.screen_width - menu_w;
    if (my + menu_h > g_shell.screen_height) my = g_shell.screen_height - menu_h;

    gfx64_fill_rounded_rect(mx, my, menu_w, menu_h, 10, g_shell.colors.window_bg);
    gfx64_draw_rounded_rect(mx, my, menu_w, menu_h, 10, g_shell.colors.border);

    void *fb = gfx64_get_framebuffer();
    for (int i = 0; i < item_count; i++) {
        int iy = my + 4 + i * item_h;
        if (items_p[i][0] == '-' && items_p[i][1] == '-') {
            gfx64_fill_rect(mx + 12, iy + item_h / 2, menu_w - 24, 1, g_shell.colors.border);
        } else {
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               mx + 16, iy + 8, items_p[i], g_shell.colors.window_fg, 14);
        }
    }
}

/* ============================================================================
 * V0.54.13: 桌面配色面板 — 图形化更换桌面背景色, 并持久化到 config.hsqml
 * 此前背景色只能靠改代码/手改配置文件, 这里点选预设色块即时生效并写回配置
 * ========================================================================== */
#define HS_COLOR_PANEL_W 320
#define HS_COLOR_PANEL_H 340

static void hs_color_panel_geometry(int *px, int *py)
{
    *px = (g_shell.screen_width - HS_COLOR_PANEL_W) / 2;
    *py = (g_shell.screen_height - HS_COLOR_PANEL_H) / 2;
    if (*px < 0) *px = 0;
    if (*py < 0) *py = 0;
}


static void hs_color_panel_render(void)
{
    if (!g_color_panel_visible) return;
    int x, y;
    hs_color_panel_geometry(&x, &y);
    void *fb = gfx64_get_framebuffer();

    gfx64_fill_rounded_rect(x, y, HS_COLOR_PANEL_W, HS_COLOR_PANEL_H, 12,
                            g_shell.colors.window_bg);
    gfx64_draw_rounded_rect(x, y, HS_COLOR_PANEL_W, HS_COLOR_PANEL_H, 12,
                            g_shell.colors.border);
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       x + 20, y + 18, "Desktop Color", g_shell.colors.window_fg, 18);

    /* 当前色小预览 */
    gfx64_fill_rect(x + HS_COLOR_PANEL_W - 56, y + 16, 36, 20, g_shell.colors.bg);
    gfx64_draw_rounded_rect(x + HS_COLOR_PANEL_W - 56, y + 16, 36, 20, 3,
                            g_shell.colors.border);

    for (int i = 0; i < 9; i++) {
        int cx = x + 20 + (i % 3) * 96;
        int cy = y + 56 + (i / 3) * 92;
        gfx64_fill_rect(cx, cy, 80, 80, g_color_presets[i]);
        uint32_t bc = (g_shell.colors.bg == g_color_presets[i])
                    ? g_shell.colors.accent : g_shell.colors.border;
        gfx64_draw_rounded_rect(cx, cy, 80, 80, 6, bc);
    }
}

static int hs_color_panel_handle_click(int x, int y)
{
    if (!g_color_panel_visible) return 0;
    int px, py;
    hs_color_panel_geometry(&px, &py);
    if (x < px || x > px + HS_COLOR_PANEL_W ||
        y < py || y > py + HS_COLOR_PANEL_H) {
        g_color_panel_visible = 0;      /* 点面板外关闭 */
        return 1;
    }
    for (int i = 0; i < 9; i++) {
        int cx = px + 20 + (i % 3) * 96;
        int cy = py + 56 + (i / 3) * 92;
        if (x >= cx && x <= cx + 80 && y >= cy && y <= cy + 80) {
            g_shell.colors.bg = g_color_presets[i];
            g_wallpaper_style = 0;      /* 纯色桌面 */
            hs_save_bg_color(g_color_presets[i]);
            g_color_panel_visible = 0;
            return 1;
        }
    }
    return 1;
}

static void hs_context_menu_handle_click(int x, int y)
{
    const char *f_items[] = {"Open", "Delete", "New Folder", "Rename", "Refresh", "Up Dir", "---", "Close Menu"};
    const char *items[] = {"New Window", "Change Theme", "Settings", "About", "---", "Lock Screen", "Desktop Color"};
    const char **items_p = g_menu_file_mode ? f_items : items;
    int item_count = g_menu_file_mode ? 8 : 7;
    int menu_w = 180;
    int item_h = 32;
    int menu_h = item_count * item_h + 8;

    int mx = g_context_menu_x;
    int my = g_context_menu_y;
    if (mx + menu_w > g_shell.screen_width) mx = g_shell.screen_width - menu_w;
    if (my + menu_h > g_shell.screen_height) my = g_shell.screen_height - menu_h;

    if (x < mx || x > mx + menu_w || y < my || y > my + menu_h) {
        g_context_menu_visible = 0;
        return;
    }

    int idx = (y - my - 4) / item_h;
    if (idx >= 0 && idx < item_count && items_p[idx][0] != '-') {
        if (g_menu_file_mode) {
            extern void hs_files_do_action(int action);
            if (idx == 0) hs_files_do_action(0);        /* Open */
            else if (idx == 1) hs_files_do_action(1);   /* Delete */
            else if (idx == 2) hs_files_do_action(4);   /* New Folder */
            else if (idx == 3) hs_files_do_action(5);   /* Rename */
            else if (idx == 4) hs_files_do_action(2);   /* Refresh */
            else if (idx == 5) hs_files_do_action(3);   /* Up Dir */
            g_menu_file_mode = 0;
            g_context_menu_visible = 0;
            return;
        }
        switch (idx) {
            case 1:
                if (g_shell.theme == HS_THEME_LIGHT) g_shell.theme = HS_THEME_DARK;
                else g_shell.theme = HS_THEME_LIGHT;
                hs_theme_init(&g_shell.colors, g_shell.theme);
                break;
            case 2: g_pending_app = HS_APP_SETTINGS; break;
            case 3: g_pending_app = HS_APP_ABOUT; break;
            case 5: g_locked = 1; break;
            case 6: g_color_panel_visible = 1; break;   /* V0.54.13: 桌面配色面板 */
        }
        g_context_menu_visible = 0;
    }
}

/* HepuShell鼠标点击处理（由desktop64主循环调用） */
void hepushell_handle_click(int x, int y)
{
    /* V0.55 P3-GUI: 首启向导模态拦截点击 */
    {
        extern int hs_oobe_active(void);
        extern int hs_oobe_handle_click(int x, int y);
        if (hs_oobe_active()) { hs_oobe_handle_click(x, y); return; }
    }

    /* V0.54.13: 桌面配色面板优先接管点击 */
    if (hs_color_panel_handle_click(x, y)) return;

    /* 控制中心绘制在窗口之上，优先接管该区域点击 */
    if (!g_locked && g_shell.panel_registry[HS_PANEL_CONTROL_CENTER].enabled && !g_context_menu_visible) {
        if (hs_control_center_handle_click(x, y)) return;
    }

    /* V0.13.39: 登录界面点击(虚拟键盘/关机/登录) */
    if (g_locked == 2) {
        /* 虚拟键盘键 */
        if (g_vkb_visible) {
            static const char *vkb_rows[] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
            static const int vkb_row_n[] = {10, 10, 9, 7};
            int center_x = g_shell.screen_width / 2;
            int kb_w = hs_sx(640);
            int kb_h = hs_sy(4 * 38 + 10);
            int kb_x = center_x - kb_w / 2;
            int kb_y = g_shell.screen_height - kb_h - hs_sy(16);
            if (y >= kb_y && y <= kb_y + kb_h) {
                /* Backspace 键 */
                int bsw = hs_sx(76);
                int bsx = kb_x + kb_w - bsw;
                int bsy = kb_y + 3 * hs_sy(38);
                if (x >= bsx && x <= bsx + bsw && y >= bsy && y <= bsy + hs_sy(32)) {
                    if (g_password_len > 0) { g_password_len--; g_password[g_password_len] = 0; }
                    return;
                }
                for (int r = 0; r < 4; r++) {
                    int key_w = (kb_w - (vkb_row_n[r] - 1) * hs_sx(4)) / vkb_row_n[r];
                    int row_x = kb_x + (kb_w - vkb_row_n[r] * key_w - (vkb_row_n[r] - 1) * hs_sx(4)) / 2;
                    for (int c = 0; c < vkb_row_n[r]; c++) {
                        int kx = row_x + c * (key_w + hs_sx(4));
                        int ky = kb_y + r * hs_sy(38);
                        if (x >= kx && x <= kx + key_w && y >= ky && y <= ky + hs_sy(32)) {
                            if (g_password_len < 31) {
                                g_password[g_password_len++] = vkb_rows[r][c];
                                g_password[g_password_len] = 0;
                            }
                            return;
                        }
                    }
                }
                return;
            }
        }
        /* 右下角 VKB / PWR 按钮 */
        {
            int kb_x = g_shell.screen_width - hs_sx(200);
            int kb_y = g_shell.screen_height - hs_sy(120);
            if (x >= kb_x && x <= kb_x + hs_sx(88) && y >= kb_y && y <= kb_y + hs_sy(34)) {
                g_vkb_visible = !g_vkb_visible;
                return;
            }
            int po_x = kb_x + hs_sx(104);
            if (x >= po_x && x <= po_x + hs_sx(88) && y >= kb_y && y <= kb_y + hs_sy(34)) {
                vga_puts("Shutting down (ACPI)...\n");
                __asm__ volatile(
                    "movw $0x604, %%dx\n"
                    "movl $0x2000, %%eax\n"
                    "outl %%eax, %%dx\n" : : : "eax", "edx");
                while (1) { asm volatile("hlt"); }
            }
        }
        /* 登录按钮 */
        {
            int center_x = g_shell.screen_width / 2;
            int input_w = hs_sx(240);
            int input_h = hs_sy(36);
            int avatar_size = hs_s(96);
            int avatar_y = hs_sy(180);
            int input_x = center_x - input_w / 2;
            int input_y = avatar_y + avatar_size + hs_sy(60);
            int btn_w = hs_sx(100);
            int btn_h = hs_sy(32);
            int btn_x = center_x - btn_w / 2;
            int btn_y = input_y + input_h + hs_sy(20);
            if (x >= btn_x && x <= btn_x + btn_w && y >= btn_y && y <= btn_y + btn_h) {
                g_locked = 0;
                g_password_len = 0;
                g_password[0] = 0;
                g_vkb_visible = 0;
                return;
            }
        }
        return;
    }

    /* 如果右键菜单可见，优先处理菜单点击 */
    if (g_context_menu_visible) {
        hs_context_menu_handle_click(x, y);
        return;
    }

    /* 点击窗口内容 → 提升到最顶层并获得焦点（按 z 序取最顶层命中窗口） */
    hs_window_t *top_win = 0;
    for (int zi = g_wm.z_count - 1; zi >= 0; zi--) {
        int id = g_wm.z_order[zi];
        if (id < 0 || id >= g_wm.window_count) continue;
        hs_window_t *w = &g_wm.windows[id];
        if (!w->visible || w->type != HS_WINDOW_FLOATING) continue;
        if (x >= w->x && x <= w->x + w->width &&
            y >= w->y && y <= w->y + w->height) {
            top_win = w;
            break;
        }
    }
    if (top_win && !top_win->focus) hs_window_focus(top_win);

    /* 窗口关闭按钮检测（从顶层窗口开始） */
    for (int zi = g_wm.z_count - 1; zi >= 0; zi--) {
        int id = g_wm.z_order[zi];
        if (id < 0 || id >= g_wm.window_count) continue;
        hs_window_t *w = &g_wm.windows[id];
        if (!w->visible || w->type != HS_WINDOW_FLOATING) continue;
        int close_x = w->x + w->width - 20;
        int close_y = w->y + 4;
        if (x >= close_x && x <= close_x + 16 && y >= close_y && y <= close_y + 16) {
            hs_window_recycle(w->id);
            return;
        }
        /* 系统工具窗口点击 (标签切换/挂载/fsck) — 仅最顶层窗口 */
        if (w == top_win) {
            if (hs_systools_click(x, y)) return;
        }
        /* V0.13.39: 文件管理器窗口点击 (选中/双击) — 仅最顶层窗口 */
        if (w == top_win) {
            extern int hs_files_hit(int x, int y);
            extern void hs_files_click(int x, int y);
            if (hs_files_hit(x, y)) { hs_files_click(x, y); return; }
        }
        /* 设置窗口左侧菜单点击 */
        if (w == top_win && w->render == hs_settings_window_render && w->visible) {
            int menu_x = w->x + 16;
            int menu_y = w->y + 36;
            if (x >= menu_x && x <= menu_x + 120 && y >= menu_y) {
                int item = (y - menu_y) / 32;
                if (item >= 0 && item < 4) {
                    g_settings_page = item;
                    /* V0.13.39: System tab 内可点击操作 (语言切换 / 驱动刷新) */
                    if (item == 2 && g_settings_page == 2) {
                        int lang_y = w->y + 36 + 6 * 24 + 18;
                        int drv_y = lang_y + 24;
                        if (y >= lang_y && y <= lang_y + 20) {
                            extern int l10n64_get_lang(void);
                            extern int l10n64_set_lang(int lang);
                            extern int reg64_set(const char *k, const char *v);
                            extern int reg64_save(void);
                            int nl = l10n64_get_lang() ? 0 : 1;
                            l10n64_set_lang(nl);
                            reg64_set("sys.lang", nl ? "en" : "zh");
                            reg64_save();
                            vga_puts("[SETTINGS] language switched\n");
                            return;
                        }
                        if (y >= drv_y && y <= drv_y + 20) {
                            extern int drv_mgr_update(void);
                            vga_puts("[SETTINGS] refresh driver repo...\n");
                            int dn = drv_mgr_update();
                            if (dn > 0) {
                                char nb[64];
                                strcpy(nb, "Repo: ");
                                hs_itoa(dn, nb + strlen(nb));
                                strcpy(nb + strlen(nb), " drivers");
                                hs_notification_send("Driver Store", nb, 0, 5000);
                            } else {
                                hs_notification_send("Driver Store", "Refresh failed (network required)", 1, 5000);
                            }
                            return;
                        }
                        {
                            int npy = drv_y + 24;
                            if (y >= npy && y <= npy + 20) {
                                extern void hs_notification_set_pos(int pos);
                                extern int hs_notification_get_pos(void);
                                int np = (hs_notification_get_pos() + 1) & 3;
                                hs_notification_set_pos(np);
                                vga_puts("[SETTINGS] notify pos changed\n");
                                return;
                            }
                        }
                        /* V0.13.39: 存储管理行点击 (挂载/卸载U盘) */
                        {
                            int sry = g_settings_storage_ry;
                            if (sry && y >= sry && y <= sry + 20) {
                                extern int storage_hal_get_device(void);
                                extern int usb64_mount_usb(void);
                                extern int usb64_umount_usb(void);
                                int on_usb = storage_hal_get_device() == 1;
                                int rc = on_usb ? usb64_umount_usb() : usb64_mount_usb();
                                vga_puts(rc == 0 ? "[SETTINGS] storage switched\n" : "[SETTINGS] storage switch failed\n");
                                return;
                            }
                        }
                        /* V0.13.39: 注册表/文件关联查看 */
                        {
                            int rry = g_settings_reg_ry;
                            if (rry && y >= rry && y <= rry + 20) {
                                extern int reg64_count(void);
                                extern const char *reg64_key_at(int i);
                                extern const char *reg64_value_at(int i);
                                vga_puts("== Registry ==\n");
                                int n = reg64_count();
                                for (int i = 0; i < n; i++) {
                                    const char *k = reg64_key_at(i);
                                    const char *v = reg64_value_at(i);
                                    if (k && v) {
                                        vga_puts("  ");
                                        vga_puts(k);
                                        vga_puts("=");
                                        vga_puts(v);
                                        vga_puts("\n");
                                    }
                                }
                                vga_puts("== end ==\n");
                                return;
                            }
                            int ary = g_settings_assoc_ry;
                            if (ary && y >= ary && y <= ary + 20) {
                                extern int file_assoc_count(void);
                                vga_puts("== File Associations ==\n");
                                int n = file_assoc_count();
                                for (int i = 0; i < n; i++) vga_puts("  (app.assoc.*)\n");
                                vga_puts("  default: txt/md/log->notes c/h/sh/py/json->editor\n");
                                vga_puts("  png/ico->imageviewer\n");
                                vga_puts("== end ==\n");
                                return;
                            }
                        }
                    }
                    return;
                }
            }
            /* V0.54.7: Appearance 页色板点击 — 取色器应用源色 */
            if (g_settings_page == 0) {
                int sw_x = w->x + 156;       /* rx = cx+140 = w->x+16+140 */
                int sw_y = w->y + 112;       /* cy+30+24+22 = w->y+36+76 */
                for (int i = 0; i < HS_THEME_PRESET_COUNT; i++) {
                    int bx = sw_x + i * 28;
                    if (x >= bx && x <= bx + 20 && y >= sw_y && y <= sw_y + 20) {
                        hepushell_set_theme(g_shell.theme);  /* 重置为当前主题默认色板 */
                        hs_theme_apply_source_color(g_theme_presets[i]);
                        g_shell.theme_source = g_theme_presets[i];
                        vga_puts("[SETTINGS] theme source color applied\n");
                        return;
                    }
                }
                /* V0.56: Wallpaper 行点击循环壁纸 (Clock Size 后 ry = w->y+188) */
                {
                    int wp_y = w->y + 188;
                    if (x >= sw_x && x <= sw_x + 150 && y >= wp_y && y <= wp_y + 20) {
                        hs_wallpaper_cycle();
                        return;
                    }
                }
                /* V0.54.13: 桌面背景色 3x3 色板点击 */
                for (int i = 0; i < 9; i++) {
                    int bx = sw_x + 76 + (i % 3) * 22;
                    int by = w->y + 208 + (i / 3) * 22;
                    if (x >= bx && x <= bx + 16 && y >= by && y <= by + 16) {
                        g_shell.colors.bg = g_color_presets[i];
                        g_wallpaper_style = 0;
                        hs_save_bg_color(g_color_presets[i]);
                        vga_puts("[SETTINGS] desktop color applied\n");
                        return;
                    }
                }
            }
        }
    }
    /* 灵动岛点击切换展开/收起 */
    int island_w = g_island_expanded ? hs_sx(360) : hs_sx(g_shell.ui.island_width);
    int island_h = g_island_expanded ? hs_sy(120) : hs_sy(g_shell.ui.island_height);
    int island_x = (g_shell.screen_width - island_w) / 2;
    int island_y = hs_sy(g_shell.ui.island_y);
    if (x >= island_x && x <= island_x + island_w && y >= island_y && y <= island_y + island_h) {
        g_island_expanded = !g_island_expanded;
        return;
    }

    /* 侧边栏标签点击切换 */
    int sx = hs_sx(g_shell.ui.sidebar_x);
    int sy = hs_sy(g_shell.ui.sidebar_y);
    int sw = hs_sx(g_shell.ui.sidebar_width);
    int tab_y = sy + hs_sy(12);
    int tab_h = hs_sy(28);
    int tab_w = (sw - hs_sx(24)) / 3;
    if (y >= tab_y && y <= tab_y + tab_h) {
        for (int i = 0; i < 3; i++) {
            int tx = sx + hs_sx(12) + i * tab_w;
            if (x >= tx && x <= tx + tab_w - hs_sx(4)) {
                g_sidebar_tab = i;
                g_sidebar_scroll = 0;   /* V0.55: 切页重置滚动 */
                return;
            }
        }
    }

    /* Dock图标点击启动应用（Dock完全显示时才响应） */
    int dock_h = hs_sy(g_shell.ui.dock_height);
    int dock_w = hs_sx(g_shell.ui.dock_width);
    int dock_x = (g_shell.screen_width - dock_w) / 2;
    int dock_y = g_shell.screen_height - dock_h - hs_sy(g_shell.ui.dock_margin_bottom);
    int icon_size = hs_s(g_shell.ui.dock_icon_size);
    int icon_count = 8;
    int spacing = (dock_w - icon_count * icon_size) / (icon_count + 1);
    if (y >= dock_y && y <= dock_y + dock_h) {
        for (int i = 0; i < icon_count; i++) {
            int ix = dock_x + spacing + i * (icon_size + spacing);
            if (x >= ix && x <= ix + icon_size) {
                /* 图标映射：T=终端 F=文件 B=浏览器 S=设置 E=编辑器 C=计算器 N=记事本 A=关于 */
                int app_map[] = {HS_APP_TERMINAL, HS_APP_FILES, HS_APP_BROWSER,
                                 HS_APP_SETTINGS, HS_APP_EDITOR, HS_APP_CALCULATOR,
                                 HS_APP_NOTES, HS_APP_ABOUT};
                g_pending_app = app_map[i];
                return;
            }
        }
    }

}
/* HepuShell右键点击处理（显示上下文菜单） */
void hepushell_handle_right_click(int x, int y)
{
    /* V0.55 P3-GUI: 首启向导期间不弹桌面右键菜单 */
    {
        extern int hs_oobe_active(void);
        if (hs_oobe_active()) return;
    }
    extern int hs_files_hit(int x, int y);
    g_context_menu_x = x;
    g_context_menu_y = y;
    g_menu_file_mode = hs_files_hit(x, y) ? 1 : 0;
    g_context_menu_visible = 1;
}

/* 底部Dock栏 */
static void hs_dock_render(void)
{
    int dock_h = hs_sy(g_shell.ui.dock_height);
    int dock_w = hs_sx(g_shell.ui.dock_width);
    int dock_x = (g_shell.screen_width - dock_w) / 2;

    /* 悬停检测：鼠标接近底部时完全显示Dock，否则只显示小把手 */
    int hover_zone = hs_sy(100);  /* 底部100像素为触发区 */
    int dock_visible = (g_shell.mouse_y > g_shell.screen_height - hover_zone);

    int handle_h = hs_sy(6);  /* 隐藏状态下的小把手高度 */
    int dock_y;

    if (dock_visible) {
        /* 完全显示 */
        dock_y = g_shell.screen_height - dock_h - hs_sy(g_shell.ui.dock_margin_bottom);
    } else {
        /* 只显示底部小把手 */
        dock_y = g_shell.screen_height - handle_h - hs_sy(4);
        dock_h = handle_h;
        /* 小把手更窄 */
        dock_w = hs_sx(120);
        dock_x = (g_shell.screen_width - dock_w) / 2;
    }

    /* Dock背景（毛玻璃效果; 悬停放大时同步变高, 对齐 Clavis bandThickness） */
    int icon_count = 8;
    int base_size = hs_s(g_shell.ui.dock_icon_size);
    int spacing = (dock_w - icon_count * base_size) / (icon_count + 1);
    int range = hs_sx(110);
    int max_extra = 0;
    if (dock_visible) {
        for (int i = 0; i < icon_count; i++) {
            int slot_cx = dock_x + spacing + i * (base_size + spacing) + base_size / 2;
            int d = g_shell.mouse_x - slot_cx;
            if (d < 0) d = -d;
            int extra = (d < range) ? (range - d) * (base_size / 2) / range : 0;
            if (extra > max_extra) max_extra = extra;
        }
        dock_y -= max_extra;
        dock_h += max_extra;
    }
    gfx64_fill_rounded_rect(dock_x, dock_y, dock_w, dock_h, hs_s(g_shell.ui.corner_radius), g_shell.colors.window_bg);
    gfx64_draw_rounded_rect(dock_x, dock_y, dock_w, dock_h, hs_s(g_shell.ui.corner_radius), g_shell.colors.border);

    if (!dock_visible) {
        /* 隐藏状态：只画一个小指示条 */
        int indicator_w = hs_sx(40);
        int indicator_x = dock_x + (dock_w - indicator_w) / 2;
        gfx64_fill_rounded_rect(indicator_x, dock_y + 1, indicator_w, handle_h - 2, hs_s(2), g_shell.colors.accent);
        return;
    }

    void *fb = gfx64_get_framebuffer();

    /* 应用图标 (对齐 Clavis DockSurface: 悬停放大, 线性衰减, 底边对齐) */
    const char *icons[] = {"T", "F", "B", "S", "E", "C", "N", "A"};
    int baseline = dock_y + dock_h - hs_sy(8);

    for (int i = 0; i < icon_count; i++) {
        int slot_cx = dock_x + spacing + i * (base_size + spacing) + base_size / 2;
        int d = g_shell.mouse_x - slot_cx;
        if (d < 0) d = -d;
        int extra = (d < range) ? (range - d) * (base_size / 2) / range : 0;
        int sz = base_size + extra;
        int ix = slot_cx - sz / 2;
        int iy = baseline - sz;

        /* 图标背景 */
        gfx64_fill_rounded_rect(ix, iy, sz, sz, hs_s(10), g_shell.colors.accent);

        /* 图标符号 (字号随放大) */
        int fs = hs_s(20) * sz / base_size;
        int gw = hs_sx(12) * sz / base_size;
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           ix + (sz - gw) / 2, iy + (sz - fs) / 2 + hs_sy(2),
                           icons[i], hs_on_accent(), fs);
    }
}

/* 鼠标更新：跟踪位置 */
void hepushell_update_mouse(int x, int y)
{
    g_shell.mouse_x = x;
    g_shell.mouse_y = y;
    /* 窗口拖拽移动 */
    hs_window_drag_move(x, y);
}

/* 鼠标按下：检测窗口标题栏启动拖拽 */
void hepushell_mouse_down(int x, int y)
{
    /* V0.55 P3-GUI: 首启向导期间禁止拖拽桌面窗口 */
    {
        extern int hs_oobe_active(void);
        if (hs_oobe_active()) return;
    }
    for (int zi = g_wm.z_count - 1; zi >= 0; zi--) {
        int id = g_wm.z_order[zi];
        if (id < 0 || id >= g_wm.window_count) continue;
        hs_window_t *w = &g_wm.windows[id];
        if (!w->visible || w->type != HS_WINDOW_FLOATING) continue;
        if (x >= w->x && x <= w->x + w->width &&
            y >= w->y && y <= w->y + 24) {
            /* V0.13.16: 标题栏三按钮: 最小化/最大化/关闭 (无背景) */
            int min_x = w->x + w->width - 68;
            int max_x = w->x + w->width - 44;
            int close_x = w->x + w->width - 20;
            if (x >= min_x && x <= min_x + 24) {
                hs_window_minimize(w);
                return;
            }
            if (x >= max_x && x <= max_x + 24) {
                hs_window_toggle_maximize(w);
                return;
            }
            if (x >= close_x && x <= close_x + 24) {
                hs_window_recycle(w->id);
                return;
            }
            /* V0.13.16: 双击标题栏(非按钮区) → 最大化/还原 */
            extern uint64_t pit64_get_ms(void);
            static hs_window_t *dbl_win = 0;
            static uint64_t dbl_ms = 0;
            uint64_t now = pit64_get_ms();
            if (dbl_win == w && now - dbl_ms < 400) {
                hs_window_toggle_maximize(w);
                dbl_win = 0;
                return;
            }
            dbl_win = w;
            dbl_ms = now;
            g_drag_window = w;
            g_drag_offset_x = x - w->x;
            g_drag_offset_y = y - w->y;
            g_dragging = 1;
            return;
        }
    }
}

/* 鼠标释放：结束拖拽 */
void hepushell_mouse_up(int x, int y)
{
    (void)x; (void)y;
    hs_window_drag_end();
}

/* ============================================================================
 * 主渲染
 * ========================================================================== */

extern void gfx64_set_target_back(void);
extern void gfx64_present(void);
void hepushell_render(void)
{
    /* V0.13.11: 双缓冲原子帧 — 全部绘制到后台缓冲, 帧末一次性提交(消除撕裂/半帧) */
    g_frame_no++;
    gfx64_set_target_back();

    /* 检测g_shell是否被野指针写入破坏 */
    if (g_shell.canary_start != 0xDEADBEEF || g_shell.canary_end != 0xCAFEBABE) {
        extern void vga_puts(const char *s);
        vga_puts("[HEPUSHELL] g_shell corruption detected! reinit\n");
        hepushell_init(g_shell.screen_width, g_shell.screen_height);
    }

    /* 每帧重新初始化面板注册表，防止野指针破坏render函数指针 */
    hs_panel_registry_init();

    /* 锁屏/登录界面优先 */
    if (g_locked == 1) {
        hs_lock_screen_render();
        gfx64_present();
        return;
    }
    if (g_locked == 2) {
        hs_login_screen_render();
        gfx64_present();
        return;
    }

    hs_wallpaper_render();
    /* V0.56: 切壁纸后首帧 — 壁纸绘制完成即采样提取源色 (窗口/面板尚未绘制) */
    if (g_wallpaper_dirty) {
        g_wallpaper_dirty = 0;
        hs_wallpaper_sample_theme();
    }


    /* 通过面板注册表渲染所有启用的面板 */
    hs_panel_render_all();

    hs_window_render_all();
    /* 控制中心绘制在窗口之上，避免被最大化窗口遮挡 */
    if (g_shell.panel_registry[HS_PANEL_CONTROL_CENTER].enabled)
        hs_control_center_render();
    /* V0.13.40: USB存储自动挂载(启动检测 + 热插拔事件) */
    {
        extern int usb64_hotplug_pending(void);
        extern int usb64_storage_present(void);
        extern int usb64_mount_usb(void);
        extern int usb64_umount_usb(void);
        static int auto_done = 0;
        if (!auto_done && usb64_storage_present()) {
            auto_done = 1;
            if (usb64_mount_usb() == 0)
                hs_notification_send("USB Storage", "USB auto-mounted", 0, 5000);
        }
        if (usb64_hotplug_pending()) {
            if (usb64_storage_present())
                hs_notification_send("USB", "USB connected (use usb mount)", 0, 4000);
            else
                hs_notification_send("USB", "USB removed", 0, 4000);
        }
    }
    hs_status_update();    hs_notification_render();
    /* V0.13.39: 网络就绪后自动检查驱动仓库(系统更新通知) */
    {
        extern int dhcp64_is_bound(void);
        static int up_checked = 0;
        if (!up_checked && dhcp64_is_bound()) {
            up_checked = 1;
            extern int drv_mgr_update(void);
            int dn = drv_mgr_update();
            if (dn > 0) {
                char nb[64];
                strcpy(nb, "Found ");
                hs_itoa(dn, nb + strlen(nb));
                strcpy(nb + strlen(nb), " driver updates");
                hs_notification_send("System Update", nb, 1, 7000);
            } else {
                hs_notification_send("System Update", "Driver store unreachable (network required)", 0, 5000);
            }
        }
    }    hs_launcher_render();

    /* 右键菜单（最顶层） */
    if (g_context_menu_visible) {
        hs_context_menu_render();
    }

    /* V0.54.13: 桌面配色面板 */
    hs_color_panel_render();

    /* V0.13.10: 窗口切换器（最顶层浮层） */
    if (g_sw_active) {
        hs_switcher_render();
    }

    /* V0.55 P3-GUI: 首启图形向导覆盖在桌面之上 (模态, 独占输入) */
    {
        extern int hs_oobe_active(void);
        extern void hs_oobe_render(void *fb, int w, int h);
        if (hs_oobe_active()) {
            hs_oobe_render(gfx64_get_framebuffer(),
                           g_shell.screen_width, g_shell.screen_height);
        }
    }

    /* V0.54.13: 渲染鼠标指针 — 优先使用 Xcursor 主题(/usr/share/cursors/default.xcur),
     * 尺寸按屏幕分辨率自适应(24/32/48), 热点取主题文件 xhot/yhot;
     * 主题不可用时回退 gfx64 内置位图。 */
    {
        extern void cursor64_draw(int x, int y);
        extern int cursor64_load_file(const char *path, int want);
        extern int cursor64_pick_size(int w, int h);
        static int cur_inited = 0;
        if (!cur_inited) {
            cur_inited = 1;
            cursor64_load_file("/usr/share/cursors/default.xcur",
                               cursor64_pick_size(g_shell.screen_width,
                                                  g_shell.screen_height));
        }
        cursor64_draw(g_shell.mouse_x, g_shell.mouse_y);
    }

    /* V0.13.11: 原子提交后台帧到前台 */
    gfx64_present();
}

/* ============================================================================
 * 键盘处理
 * ========================================================================== */

void hepushell_handle_key(int key)
{
    /* V0.55 P3-GUI: 首启向导优先消费全部按键 */
    {
        extern int hs_oobe_active(void);
        extern int hs_oobe_handle_key(int key);
        if (hs_oobe_active()) { hs_oobe_handle_key(key); return; }
    }

    /* 锁屏状态：任意键进入登录界面 */
    if (g_locked == 1) {
        g_locked = 2;
        g_password_len = 0;
        g_password[0] = 0;
        return;
    }

    /* 登录界面：输入密码 */
    if (g_locked == 2) {
        if (key == 27) {  /* ESC返回锁屏 */
            g_locked = 1;
            return;
        }
        if (key == '\n' || key == '\r' || key == 0x1C) {  /* 回车登录 */
            /* 简单验证：任意密码都可以解锁 */
            g_locked = 0;
            g_password_len = 0;
            g_password[0] = 0;
            return;
        }
        if (key == '\b' || key == 0x0E) {  /* 退格 */
            if (g_password_len > 0) {
                g_password_len--;
                g_password[g_password_len] = 0;
            }
            return;
        }
        if (g_password_len < 31 && key >= 32 && key < 127) {
            g_password[g_password_len++] = (char)key;
            g_password[g_password_len] = 0;
        }
        return;
    }

    if (g_shell.launcher_visible) {
        hs_launcher_handle_key(key);
        return;
    }

    /* 系统工具窗口 (设备/磁盘/日志) 优先消费按键 */
    if (hs_systools_handle_key(key)) return;

    /* 聚焦的应用窗口优先消费按键(终端/文件管理器等) */
    if (hs_apps_handle_key(key)) return;

    /* L键锁屏 */
    if (key == 'l' || key == 'L') {
        g_locked = 1;
        return;
    }

    /* T键切换主题（浅色/深色） */
    if (key == 't' || key == 'T') {
        if (g_shell.theme == HS_THEME_LIGHT) {
            g_shell.theme = HS_THEME_DARK;
        } else {
            g_shell.theme = HS_THEME_LIGHT;
        }
        hs_theme_init(&g_shell.colors, g_shell.theme);
        g_shell.theme_source = 0xD0BCFF;  /* 重置源色 */
        return;
    }

    /* C键循环切换取色器预设源色（对齐 quickshell matugen seed） */
    if (key == 'c' || key == 'C') {
        static int preset_idx = 0;
        hepushell_set_theme(g_shell.theme);  /* 重置为当前主题默认色板 */
        preset_idx = (preset_idx + 1) % HS_THEME_PRESET_COUNT;
        hs_theme_apply_source_color(g_theme_presets[preset_idx]);
        g_shell.theme_source = g_theme_presets[preset_idx];
        vga_printf("[THEME] source color preset %d applied\n", preset_idx);
        return;
    }

    /* W键循环切换程序化壁纸 (Dusk/Ocean 触发壁纸取色, Classic 恢复默认) */
    if (key == 'w' || key == 'W') {
        hs_wallpaper_cycle();
        return;
    }

    /* Tab键切换侧边栏标签 */
    if (key == 0x0F || key == '\t') {
        g_sidebar_tab = (g_sidebar_tab + 1) % 3;
        g_sidebar_scroll = 0;   /* V0.55: 切页重置滚动 */
        return;
    }

    /* 工作区切换 Alt+1..9 简化为数字键 */
    if (key >= '1' && key <= '9') {
        hepushell_switch_workspace(key - '1');
        return;
    }

    /* Super/Win 键切换启动器（简化为空格） */
    if (key == ' ') {
        hepushell_toggle_launcher();
        return;
    }

    /* F2-F8 切换面板显示/隐藏 */
    if (key >= 0x3C && key <= 0x42) {
        const char *panel_names[] = {
            "top_bar", "dynamic_island", "dock",
            "desktop_clock", "weather", "sidebar", "control_center"
        };
        int idx = key - 0x3C;
        if (idx < 7) {
            int enabled = hs_panel_is_enabled(panel_names[idx]);
            hs_panel_set_enabled(panel_names[idx], !enabled);
        }
        return;
    }

    /* F11(0x57): 快速打开文件管理器 (无鼠标环境下也可达) */
    if (key == 0x57) {
        extern hs_window_t *hs_files_create(void);
        hs_files_create();
        return;
    }

    /* V0.13.41: F12(0x58) 快捷键快速打开终端 (无鼠标环境下也可达) */
    if (key == 0x58) {
        hs_terminal_create();
        return;
    }
}

/* ============================================================================
 * 初始化
 * ========================================================================== */

int hepushell_init(int width, int height)
{
    g_shell.canary_start = 0xDEADBEEF;
    g_shell.canary_end = 0xCAFEBABE;
    g_shell.initialized = 0;
    g_shell.screen_width = width;
    g_shell.screen_height = height;
    /* 自适应缩放因子（基准分辨率1024x768） */
    g_shell.scale_x = (float)width / 1024.0f;
    g_shell.scale_y = (float)height / 768.0f;
    g_shell.scale = (g_shell.scale_x < g_shell.scale_y) ? g_shell.scale_x : g_shell.scale_y;
    g_shell.current_workspace = 0;
    g_shell.launcher_visible = 0;
    g_shell.launcher_cursor = 0;
    g_shell.selected_app = 0;
    g_shell.app_count = 0;
    g_shell.mouse_x = 0;
    g_shell.mouse_y = 0;

    /* 初始化控制中心状态 */
    g_shell.cc.toggles[0] = 1;  /* WiFi on */
    g_shell.cc.toggles[1] = 0;  /* BT off */
    g_shell.cc.toggles[2] = 0;  /* Air off */
    g_shell.cc.toggles[3] = 1;  /* DND on */
    g_shell.cc.toggles[4] = 1;  /* Loc on */
    g_shell.cc.toggles[5] = 0;  /* Torch off */
    g_shell.cc.volume = 70;
    g_shell.cc.brightness = 85;

    /* 初始化面板注册表 */
    hs_panel_registry_init();

    /* 初始化主题（对齐 quickshell/Clavis: M3 tonal-spot dark） */
    hepushell_set_theme(HS_THEME_DARK);
    g_shell.theme_source = 0xD0BCFF;  /* 默认取色器源色 = Lavender */

    /* 设置UI配置默认值（配置加载会覆盖） */
    g_shell.ui.clock_x = 380;
    g_shell.ui.clock_y = 200;
    g_shell.ui.clock_font_size = 40;  /* 40px测试大字号稳定性 */
    g_shell.ui.date_font_size = 20;
    g_shell.ui.weather_margin_right = 140;
    g_shell.ui.weather_margin_bottom = 160;
    g_shell.ui.weather_width = 120;
    g_shell.ui.weather_height = 120;
    g_shell.ui.weather_temp_font = 24;  /* 从36降到24，测试大字号稳定性 */
    g_shell.ui.weather_desc_font = 16;
    g_shell.ui.sidebar_x = 12;
    g_shell.ui.sidebar_y = 50;
    g_shell.ui.sidebar_width = 280;
    g_shell.ui.sidebar_height = 500;
    g_shell.ui.cc_margin_right = 384;
    g_shell.ui.cc_y = 108;
    g_shell.ui.cc_width = 360;
    g_shell.ui.cc_height = 440;
    g_shell.ui.bar_height = 40;
    g_shell.ui.island_width = 120;
    g_shell.ui.island_height = 28;
    g_shell.ui.island_y = 6;
    g_shell.ui.island_font_size = 20;
    g_shell.ui.dock_width = 400;
    g_shell.ui.dock_height = 64;
    g_shell.ui.dock_margin_bottom = 12;
    g_shell.ui.dock_icon_size = 44;
    g_shell.ui.corner_radius = 16;
    g_shell.ui.font_small = 12;
    g_shell.ui.font_normal = 14;
    g_shell.ui.font_large = 18;

    /* 初始化面板（配置加载会覆盖） */
    hs_panel_init(&g_shell.top_panel, HS_PANEL_TOP, HS_PANEL_HEIGHT);
    hs_panel_init(&g_shell.bottom_panel, HS_PANEL_BOTTOM, HS_PANEL_HEIGHT);

    /* 从配置加载主题和面板 */
    hepushell_load_config(NULL);

    /* 初始化工作区 */
    for (int i = 0; i < HS_MAX_WORKSPACES; i++) {
        g_shell.workspaces[i].id = i;
        g_shell.workspaces[i].name[0] = '1' + i;
        g_shell.workspaces[i].name[1] = 0;
        g_shell.workspaces[i].window_count = 0;
        g_shell.workspaces[i].layout = HS_LAYOUT_TILE;
    }

    /* 初始化窗口管理 */
    hs_window_manager_init();

    /* 初始化窗口规则 */
    hs_window_rule_init();

    /* 初始化系统托盘和通知中心 */
    hs_tray_init();
    hs_notification_init();
    {
        extern int reg64_get_int(const char *key, int def);
        g_notif_pos = reg64_get_int("sys.notif.pos", 0) & 3;
    }

    /* 初始化媒体和系统状态 */
    hs_media_init();
    hs_status_init();

    /* 注册默认应用 */
    hepushell_add_app("Terminal", "terminal", "[>]");
    hepushell_add_app("Files", "files", "[/]");
    hepushell_add_app("Browser", "browser", "[@]");
    hepushell_add_app("Settings", "settings", "[*]");
    hepushell_add_app("Editor", "editor", "[~]");
    hepushell_add_app("Calculator", "calc", "[=]");
    hepushell_add_app("Notes", "notes", "[#]");
    hepushell_add_app("Task Manager", "taskmgr", "[!]");
    hepushell_add_app("App Store", "store", "[$]");
    hepushell_add_app("About", "about", "[?]");
    /* V0.55: 系统工具 */
    hepushell_add_app("Device Manager", "devmgr", "[D]");
    hepushell_add_app("Disk Manager", "diskmgr", "[S]");
    hepushell_add_app("Log Viewer", "logview", "[L]");

    g_shell.initialized = 1;

    vga_puts("[HepuShell] Initialized (");
    char buf[8];
    itoa_hs(width, buf);
    vga_puts(buf);
    vga_puts("x");
    itoa_hs(height, buf);
    vga_puts(buf);
    vga_puts(")\n");


    return 0;
}

static int g_frame_tick = 0;   /* V0.13.39: 帧计数(双击检测) */

int hepushell_get_tick(void) { return g_frame_tick; }

void hepushell_run(void)
{
    if (!g_shell.initialized) return;

    /* 主循环由外部调用渲染和键盘处理 */
    g_frame_tick++;
    hepushell_render();
}

/* 重新加载配置 */
int hepushell_reload(void)
{
    if (!g_shell.initialized) return -1;

    vga_puts("[HepuShell] Reloading configuration...\n");

    /* 重置面板 */
    hs_panel_init(&g_shell.top_panel, HS_PANEL_TOP, HS_PANEL_HEIGHT);
    hs_panel_init(&g_shell.bottom_panel, HS_PANEL_BOTTOM, HS_PANEL_HEIGHT);

    /* 重新加载配置 */
    int result = hepushell_load_config(NULL);

    if (result == 0) {
        vga_puts("[HepuShell] Configuration reloaded successfully\n");
    } else {
        vga_puts("[HepuShell] Configuration reload failed, using defaults\n");
        /* 回退到默认小部件 */
        hepushell_add_widget(HS_PANEL_TOP, HS_WIDGET_WORKSPACES);
        hepushell_add_widget(HS_PANEL_TOP, HS_WIDGET_CPU);
        hepushell_add_widget(HS_PANEL_TOP, HS_WIDGET_MEMORY);
        hepushell_add_widget(HS_PANEL_BOTTOM, HS_WIDGET_CLOCK);
    }

    /* 重新平铺窗口 */
    hs_window_tile();

    return result;
}

/* ============================================================================
 * 系统托盘
 * ========================================================================== */

void hs_tray_init(void)
{
    g_shell.tray_count = 0;
    for (int i = 0; i < HS_MAX_TRAY_ICONS; i++) {
        g_shell.tray_icons[i].visible = 0;
    }

    /* 添加默认托盘图标 */
    hs_tray_add_icon("Network", "[N]");
    hs_tray_add_icon("Audio", "[A]");
    hs_tray_add_icon("Battery", "[B]");

    /* 欢迎通知 */
    hs_notification_send("Welcome to Hepu OS", "HepuShell desktop loaded", 0, 5000);

    /* 创建设置浮动窗口 */
    hs_window_manager_init();
    hs_window_t *settings_win = hs_window_create(HS_WINDOW_FLOATING, "Settings",
                                                  -1, -1, 460, 430);
    if (settings_win) {
        settings_win->render = hs_settings_window_render;
    }
}

int hs_tray_add_icon(const char *name, const char *icon)
{
    if (g_shell.tray_count >= HS_MAX_TRAY_ICONS) return -1;

    hs_tray_icon_t *ti = &g_shell.tray_icons[g_shell.tray_count];
    int i = 0;
    while (name && name[i] && i < 31) { ti->name[i] = name[i]; i++; }
    ti->name[i] = 0;
    i = 0;
    while (icon && icon[i] && i < 7) { ti->icon[i] = icon[i]; i++; }
    ti->icon[i] = 0;
    ti->visible = 1;
    ti->active = 0;
    ti->on_click = NULL;

    return g_shell.tray_count++;
}

void hs_tray_remove_icon(const char *name)
{
    for (int i = 0; i < g_shell.tray_count; i++) {
        if (str_cmp(g_shell.tray_icons[i].name, name) == 0) {
            g_shell.tray_icons[i].visible = 0;
            return;
        }
    }
}

void hs_tray_render(int x, int y, int max_width)
{
    void *fb = gfx64_get_framebuffer();
    int cur_x = x;
    int icon_width = 24;

    for (int i = 0; i < g_shell.tray_count; i++) {
        hs_tray_icon_t *ti = &g_shell.tray_icons[i];
        if (!ti->visible) continue;
        if (cur_x + icon_width > x + max_width) break;

        /* 图标背景 */
        uint32_t bg = ti->active ? g_shell.colors.accent : g_shell.colors.panel_bg;
        gfx64_fill_rect(cur_x, y + 2, icon_width - 2, HS_PANEL_HEIGHT - 4, bg);

        /* 图标文字 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           cur_x + 4, y + 6, ti->icon,
                           g_shell.colors.panel_fg, 10);

        cur_x += icon_width;
    }
}

void hs_tray_handle_click(int x, int y)
{
    int icon_width = 24;
    int start_x = g_shell.screen_width - (g_shell.tray_count * icon_width) - 8;

    for (int i = 0; i < g_shell.tray_count; i++) {
        hs_tray_icon_t *ti = &g_shell.tray_icons[i];
        if (!ti->visible) continue;

        int ix = start_x + i * icon_width;
        if (x >= ix && x < ix + icon_width) {
            ti->active = !ti->active;
            if (ti->on_click) ti->on_click();
            return;
        }
    }
}

/* ============================================================================
 * 通知中心
 * ========================================================================== */

void hs_notification_set_pos(int pos)
{
    g_notif_pos = pos & 3;
    extern int reg64_set(const char *k, const char *v);
    extern int reg64_save(void);
    const char *ps = (g_notif_pos == 0) ? "tr" :
                     (g_notif_pos == 1) ? "tl" :
                     (g_notif_pos == 2) ? "br" : "bl";
    reg64_set("sys.notif.pos", ps);
    reg64_save();
}

int hs_notification_get_pos(void) { return g_notif_pos; }

void hs_notification_init(void)
{
    g_shell.notifications.count = 0;
    g_shell.notifications.visible = 0;
    g_shell.notifications.next_id = 1;
    for (int i = 0; i < HS_MAX_NOTIFICATIONS; i++) {
        g_shell.notifications.notifications[i].visible = 0;
    }
}

int hs_notification_send(const char *title, const char *body, int urgency, int timeout)
{
    if (g_shell.notifications.count >= HS_MAX_NOTIFICATIONS) {
        /* 移除最旧的通知 */
        g_shell.notifications.notifications[0].visible = 0;
        for (int i = 1; i < HS_MAX_NOTIFICATIONS; i++) {
            g_shell.notifications.notifications[i - 1] =
                g_shell.notifications.notifications[i];
        }
        g_shell.notifications.count--;
    }

    hs_notification_t *n = &g_shell.notifications.notifications[g_shell.notifications.count];
    n->id = g_shell.notifications.next_id++;
    n->urgency = urgency;
    n->timeout = timeout;
    n->visible = 1;
    n->timestamp = 0;  /* TODO: 使用RTC */
    n->born_frame = g_frame_no;

    int i = 0;
    while (title && title[i] && i < 63) { n->title[i] = title[i]; i++; }
    n->title[i] = 0;
    i = 0;
    while (body && body[i] && i < 127) { n->body[i] = body[i]; i++; }
    n->body[i] = 0;

    g_shell.notifications.count++;

    vga_puts("[NOTIFY] ");
    vga_puts(title);
    vga_puts(": ");
    vga_puts(body);
    vga_puts("\n");

    return n->id;
}

void hs_notification_render(void)
{
    void *fb = gfx64_get_framebuffer();

    /* V0.55: Clavis 风格通知卡片栈 (Keystone NotificationContent):
       圆角卡片 + 应用图标圆 + 到期进度条 + 滑入动效, 位置可配置 */
    int w = 288;
    int h = 64;
    int gap = 10;
    int popup_count = 0;

    for (int i = g_shell.notifications.count - 1; i >= 0 && popup_count < 3; i--) {
        hs_notification_t *n = &g_shell.notifications.notifications[i];
        if (!n->visible) continue;

        /* 到期隐藏 (timeout 毫秒, 桌面循环约 60fps) */
        uint32_t age = g_frame_no - n->born_frame;
        uint32_t life = (uint32_t)n->timeout * 60 / 1000;
        if (n->timeout > 0 && age > life) { n->visible = 0; continue; }

        int x, y;
        int right_side = (g_notif_pos == 0 || g_notif_pos == 2);
        if (g_notif_pos == 0) {      /* 右上: 向下堆叠 */
            x = g_shell.screen_width - w - 8;
            y = HS_PANEL_HEIGHT + 8 + popup_count * (h + gap);
        } else if (g_notif_pos == 1) { /* 左上 */
            x = 8;
            y = HS_PANEL_HEIGHT + 8 + popup_count * (h + gap);
        } else if (g_notif_pos == 2) { /* 右下: 向上堆叠 */
            x = g_shell.screen_width - w - 8;
            y = g_shell.screen_height - 40 - (popup_count + 1) * (h + gap);
        } else {                       /* 左下 */
            x = 8;
            y = g_shell.screen_height - 40 - (popup_count + 1) * (h + gap);
        }

        /* 滑入动效: 前 10 帧从锚定边缘滑入 (二次缓出) */
        if (age < 10) {
            int t = 10 - (int)age;
            int slide = t * t * (w + 16) / 100;
            x += right_side ? slide : -slide;
        }

        /* 卡片底: surfaceContainerHigh + 细描边, 圆角 10 */
        gfx64_fill_rounded_rect(x, y, w, h, HS_RD_WIN, g_shell.colors.titlebar_bg);
        gfx64_draw_rounded_rect(x, y, w, h, HS_RD_WIN, g_shell.colors.border);

        /* 图标: 普通=accent 圆形, 紧急=红色圆角方块, 内绘标题首字母 */
        uint32_t icon_bg = (n->urgency == 2) ? 0xE5484D : g_shell.colors.accent;
        uint32_t icon_fg = (n->urgency == 2) ? 0xFFFFFF : hs_on_accent();
        int icx = x + 28, icy = y + 30, ir = 15;
        if (n->urgency == 2)
            gfx64_fill_rounded_rect(icx - ir, icy - ir, ir * 2, ir * 2, 8, icon_bg);
        else
            gfx64_fill_circle(icx, icy, ir, icon_bg);
        if (n->title[0]) {
            char ch[2] = { n->title[0], 0 };
            if (ch[0] >= 'a' && ch[0] <= 'z') ch[0] -= 32;
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               icx - 4, icy - 7, ch, icon_fg, 14);
        }

        /* 标题 (12px) 与正文 (10px), 按宽度截断 */
        int tx = x + 52;
        int max_chars = (w - 52 - 12) / 7;
        char tb[64];
        int ti = 0;
        while (n->title[ti] && ti < max_chars && ti < 62) { tb[ti] = n->title[ti]; ti++; }
        tb[ti] = 0;
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           tx, y + 10, tb, g_shell.colors.window_fg, 12);
        max_chars = (w - 52 - 12) / 6;
        ti = 0;
        while (n->body[ti] && ti < max_chars && ti < 62) { tb[ti] = n->body[ti]; ti++; }
        tb[ti] = 0;
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           tx, y + 30, tb, g_shell.colors.panel_fg, 10);

        /* 时间戳 (RTC 真实时间), 卡片右上角 */
        {
            uint8_t th = rtc64_get_hours();
            uint8_t tm = rtc64_get_minutes();
            char ts[8];
            ts[0] = '0' + th / 10; ts[1] = '0' + th % 10; ts[2] = ':';
            ts[3] = '0' + tm / 10; ts[4] = '0' + tm % 10; ts[5] = 0;
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               x + w - 40, y + 8, ts, g_shell.colors.panel_fg, 9);
        }

        /* 到期进度条: 底部 2px, accent 剩余比例 (对齐 Clavis progressTrack) */
        if (n->timeout > 0 && life > 0) {
            int track_w = w - 16;
            int remain = (int)((life - age) * track_w / life);
            gfx64_fill_rounded_rect(x + 8, y + h - 5, track_w, 2, 1,
                                    g_shell.colors.border);
            if (remain > 2)
                gfx64_fill_rounded_rect(x + 8, y + h - 5, remain, 2, 1,
                                        g_shell.colors.accent);
        }

        popup_count++;
    }

    /* 渲染通知中心（如果可见） */
    if (g_shell.notifications.visible) {
        int cw = 320;
        int ch = 400;
        int cx = g_shell.screen_width - cw - 8;
        int cy = HS_PANEL_HEIGHT + 8;

        gfx64_fill_rect(cx, cy, cw, ch, g_shell.colors.window_bg);
        gfx64_draw_rect(cx, cy, cw, ch, g_shell.colors.border);

        /* 标题栏 */
        gfx64_fill_rect(cx, cy, cw, 28, g_shell.colors.titlebar_bg);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           cx + 8, cy + 6, "Notifications",
                           g_shell.colors.window_fg, 12);

        /* 通知列表 */
        int ny = cy + 36;
        for (int i = g_shell.notifications.count - 1; i >= 0; i--) {
            hs_notification_t *n = &g_shell.notifications.notifications[i];
            if (!n->visible) continue;
            if (ny + 50 > cy + ch) break;

            gfx64_fill_rect(cx + 4, ny, cw - 8, 46, g_shell.colors.panel_bg);
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               cx + 10, ny + 4, n->title,
                               g_shell.colors.window_fg, 11);
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               cx + 10, ny + 20, n->body,
                               g_shell.colors.panel_fg, 9);
            ny += 50;
        }
    }
}

void hs_notification_toggle_center(void)
{
    g_shell.notifications.visible = !g_shell.notifications.visible;
}

void hs_notification_dismiss(int id)
{
    for (int i = 0; i < g_shell.notifications.count; i++) {
        if (g_shell.notifications.notifications[i].id == id) {
            g_shell.notifications.notifications[i].visible = 0;
            return;
        }
    }
}

void hs_notification_clear_all(void)
{
    for (int i = 0; i < HS_MAX_NOTIFICATIONS; i++) {
        g_shell.notifications.notifications[i].visible = 0;
    }
    g_shell.notifications.count = 0;
}

/* ============================================================================
 * 媒体控制
 * ========================================================================== */

void hs_media_init(void)
{
    g_shell.media.playing = 0;
    g_shell.media.volume = 75;
    g_shell.media.position = 0;
    g_shell.media.duration = 0;
    g_shell.media.title[0] = 0;
    g_shell.media.artist[0] = 0;
}

void hs_media_render(int x, int y)
{
    void *fb = gfx64_get_framebuffer();
    int btn_w = 20;
    int cur_x = x;

    /* 上一首 */
    gfx64_fill_rect(cur_x, y + 4, btn_w, HS_PANEL_HEIGHT - 8, g_shell.colors.panel_bg);
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       cur_x + 4, y + 6, "|<", g_shell.colors.panel_fg, 10);
    cur_x += btn_w + 2;

    /* 播放/暂停 */
    gfx64_fill_rect(cur_x, y + 4, btn_w, HS_PANEL_HEIGHT - 8,
                    g_shell.media.playing ? g_shell.colors.accent : g_shell.colors.panel_bg);
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       cur_x + 4, y + 6,
                       g_shell.media.playing ? "||" : ">",
                       g_shell.colors.panel_fg, 10);
    cur_x += btn_w + 2;

    /* 下一首 */
    gfx64_fill_rect(cur_x, y + 4, btn_w, HS_PANEL_HEIGHT - 8, g_shell.colors.panel_bg);
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       cur_x + 4, y + 6, ">|", g_shell.colors.panel_fg, 10);
}

void hs_media_play_pause(void)
{
    g_shell.media.playing = !g_shell.media.playing;
}

void hs_media_next(void)
{
    g_shell.media.position = 0;
}

void hs_media_prev(void)
{
    g_shell.media.position = 0;
}

void hs_media_set_volume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    g_shell.media.volume = vol;
    g_shell.status.volume = vol;
}

/* ============================================================================
 * 系统状态小部件
 * ========================================================================== */

void hs_status_init(void)
{
    g_shell.status.volume = 75;
    g_shell.status.brightness = 80;
    g_shell.status.battery_percent = 100;
    g_shell.status.battery_charging = 0;
    g_shell.status.network_connected = 1;
    str_cpy(g_shell.status.network_type, "ethernet");
    g_shell.status.cpu_usage = 0;
    g_shell.status.mem_usage = 0;
}

void hs_status_update(void)
{
    /* V0.13.39: 从实际系统获取状态 (CPU调度统计 + 物理内存统计) */
    extern int process64_cpu_usage(void);
    extern void pmm64_get_stats(uint64_t *total, uint64_t *used, uint64_t *free);
    uint64_t tp = 0, up = 0, fp = 0;
    pmm64_get_stats(&tp, &up, &fp);
    if (tp > 0) g_shell.status.mem_usage = (int)(up * 100 / tp);
    else g_shell.status.mem_usage = 0;
    g_shell.status.cpu_usage = process64_cpu_usage();
    /* V0.13.39: 托盘网络图标真实链路状态 (e1000 STATUS bit1) */
    extern int e1000_link_status(void);
    extern int dhcp64_is_bound(void);
    int link = e1000_link_status();
    int bound = dhcp64_is_bound();
    g_shell.status.network_connected = (link && bound) ? 1 : 0;
}

void hs_volume_render(int x, int y, int width)
{
    void *fb = gfx64_get_framebuffer();

    /* 音量图标 */
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       x, y + 6, "V:", g_shell.colors.panel_fg, 10);

    /* 音量条 */
    int bar_x = x + 16;
    int bar_w = width - 20;
    int bar_h = 6;
    int bar_y = y + (HS_PANEL_HEIGHT - bar_h) / 2;

    gfx64_fill_rect(bar_x, bar_y, bar_w, bar_h, g_shell.colors.border);
    int fill_w = (bar_w * g_shell.status.volume) / 100;
    gfx64_fill_rect(bar_x, bar_y, fill_w, bar_h, g_shell.colors.accent);
}

void hs_brightness_render(int x, int y, int width)
{
    void *fb = gfx64_get_framebuffer();

    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       x, y + 6, "B:", g_shell.colors.panel_fg, 10);

    int bar_x = x + 16;
    int bar_w = width - 20;
    int bar_h = 6;
    int bar_y = y + (HS_PANEL_HEIGHT - bar_h) / 2;

    gfx64_fill_rect(bar_x, bar_y, bar_w, bar_h, g_shell.colors.border);
    int fill_w = (bar_w * g_shell.status.brightness) / 100;
    gfx64_fill_rect(bar_x, bar_y, fill_w, bar_h, 0xFFCC00);
}

void hs_battery_render(int x, int y)
{
    void *fb = gfx64_get_framebuffer();
    char buf[16];

    /* 电池图标 */
    int bat_w = 24;
    int bat_h = 12;
    int bat_y = y + (HS_PANEL_HEIGHT - bat_h) / 2;

    gfx64_draw_rect(x, bat_y, bat_w, bat_h, g_shell.colors.panel_fg);
    gfx64_fill_rect(x + bat_w, bat_y + 3, 2, 6, g_shell.colors.panel_fg);

    /* 电量填充 */
    int fill_w = ((bat_w - 2) * g_shell.status.battery_percent) / 100;
    uint32_t color = g_shell.status.battery_percent > 20 ?
                     0x4CAF50 : 0xF44336;
    gfx64_fill_rect(x + 1, bat_y + 1, fill_w, bat_h - 2, color);

    /* 百分比文字 */
    int n = g_shell.status.battery_percent, i = 0;
    if (n == 0) buf[i++] = '0';
    while (n > 0) { buf[i++] = '0' + n % 10; n /= 10; }
    buf[i++] = '%';
    buf[i] = 0;
    for (int j = 0; j < i / 2; j++) {
        char t = buf[j]; buf[j] = buf[i-1-j]; buf[i-1-j] = t;
    }
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       x + bat_w + 4, y + 6, buf, g_shell.colors.panel_fg, 10);
}

void hs_network_render(int x, int y)
{
    void *fb = gfx64_get_framebuffer();

    if (g_shell.status.network_connected) {
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           x, y + 6, "[NET]", g_shell.colors.accent, 10);
    } else {
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           x, y + 6, "[---]", 0xF44336, 10);
    }
}

void hs_tasklist_render(int x, int y, int width)
{
    void *fb = gfx64_get_framebuffer();
    int cur_x = x;
    int btn_h = HS_PANEL_HEIGHT - 4;

    /* 显示当前工作区的窗口 */
    for (int i = 0; i < g_wm.window_count; i++) {
        hs_window_t *w = &g_wm.windows[i];
        if (!w->visible || w->workspace != g_shell.current_workspace) continue;
        if (w->type != HS_WINDOW_FLOATING) continue;

        int btn_w = 100;
        if (cur_x + btn_w > x + width) break;

        uint32_t bg = w->focus ? g_shell.colors.accent : g_shell.colors.panel_bg;
        gfx64_fill_rect(cur_x, y + 2, btn_w, btn_h, bg);

        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           cur_x + 4, y + 6, w->title,
                           g_shell.colors.panel_fg, 10);

        cur_x += btn_w + 2;
    }
}

void hs_status_render_all(int panel_y)
{
    int right_x = g_shell.screen_width - 8;

    /* 从右到左渲染 */
    hs_network_render(right_x - 50, panel_y);
    right_x -= 56;

    hs_battery_render(right_x - 60, panel_y);
    right_x -= 66;

    hs_volume_render(right_x - 80, panel_y, 70);
    right_x -= 86;

    hs_media_render(right_x - 70, panel_y);
}

/* ============================================================================
 * P2: 布局管理
 * ========================================================================== */

void hs_layout_set(hs_layout_mode_t mode)
{
    g_shell.workspaces[g_shell.current_workspace].layout = mode;
    hs_layout_apply();
}

hs_layout_mode_t hs_layout_get(void)
{
    return g_shell.workspaces[g_shell.current_workspace].layout;
}

void hs_layout_cycle(void)
{
    hs_layout_mode_t current = hs_layout_get();
    hs_layout_mode_t next = (current + 1) % (HS_LAYOUT_BSP + 1);
    hs_layout_set(next);
}

void hs_layout_apply(void)
{
    hs_layout_mode_t mode = hs_layout_get();

    switch (mode) {
        case HS_LAYOUT_TILE:
            hs_layout_tile();
            break;
        case HS_LAYOUT_BSP:
            hs_layout_bsp();
            break;
        case HS_LAYOUT_SINGLE:
            hs_layout_single();
            break;
        case HS_LAYOUT_FLOATING:
        case HS_LAYOUT_TAB:
        default:
            /* 浮动和标签布局不自动排列 */
            break;
    }
}

void hs_layout_tile(void)
{
    int visible = 0;
    int indices[HS_MAX_WINDOWS];

    for (int i = 0; i < g_wm.window_count; i++) {
        hs_window_t *w = &g_wm.windows[i];
        if (w->type == HS_WINDOW_FLOATING &&
            w->workspace == g_shell.current_workspace &&
            w->visible && w->state != HS_WINDOW_STATE_MAXIMIZED) {
            indices[visible++] = i;
        }
    }

    int panel_h = HS_PANEL_HEIGHT;
    int avail_h = g_shell.screen_height - panel_h * 2;
    int avail_y = panel_h;

    if (visible == 0) return;

    if (visible == 1) {
        hs_window_t *w = &g_wm.windows[indices[0]];
        w->x = 0; w->y = avail_y;
        w->width = g_shell.screen_width;
        w->height = avail_h;
    } else {
        /* 主窗口 + 堆叠窗口 */
        int main_w = g_shell.screen_width * 2 / 3;
        int stack_w = g_shell.screen_width - main_w;
        int stack_h = avail_h / (visible - 1);

        /* 主窗口 */
        hs_window_t *w = &g_wm.windows[indices[0]];
        w->x = 0; w->y = avail_y;
        w->width = main_w; w->height = avail_h;

        /* 堆叠窗口 */
        for (int i = 1; i < visible; i++) {
            w = &g_wm.windows[indices[i]];
            w->x = main_w;
            w->y = avail_y + (i - 1) * stack_h;
            w->width = stack_w;
            w->height = stack_h;
        }
    }
}

void hs_layout_bsp(void)
{
    /* BSP二分布局 */
    int visible = 0;
    int indices[HS_MAX_WINDOWS];

    for (int i = 0; i < g_wm.window_count; i++) {
        hs_window_t *w = &g_wm.windows[i];
        if (w->type == HS_WINDOW_FLOATING &&
            w->workspace == g_shell.current_workspace &&
            w->visible) {
            indices[visible++] = i;
        }
    }

    if (visible == 0) return;

    int panel_h = HS_PANEL_HEIGHT;
    int x = 0, y = panel_h;
    int w = g_shell.screen_width;
    int h = g_shell.screen_height - panel_h * 2;

    /* 简单二分：偶数水平分，奇数垂直分 */
    for (int i = 0; i < visible; i++) {
        hs_window_t *win = &g_wm.windows[indices[i]];
        if (i == visible - 1) {
            win->x = x; win->y = y;
            win->width = w; win->height = h;
        } else {
            if (i % 2 == 0) {
                /* 水平分 */
                win->x = x; win->y = y;
                win->width = w / 2; win->height = h;
                x += w / 2; w = w - w / 2;
            } else {
                /* 垂直分 */
                win->x = x; win->y = y;
                win->width = w; win->height = h / 2;
                y += h / 2; h = h - h / 2;
            }
        }
    }
}

void hs_layout_single(void)
{
    /* 单窗布局：只显示聚焦窗口 */
    hs_window_t *focused = hs_window_get_focused();

    for (int i = 0; i < g_wm.window_count; i++) {
        hs_window_t *w = &g_wm.windows[i];
        if (w->type != HS_WINDOW_FLOATING) continue;
        if (w->workspace != g_shell.current_workspace) continue;

        if (w == focused || !focused) {
            w->x = 0;
            w->y = HS_PANEL_HEIGHT;
            w->width = g_shell.screen_width;
            w->height = g_shell.screen_height - HS_PANEL_HEIGHT * 2;
            w->visible = 1;
        } else {
            w->visible = 0;
        }
    }
}

/* ============================================================================
 * P2: 窗口拖动
 * ========================================================================== */

void hs_window_drag_start(int x, int y)
{
    hs_window_t *win = hs_window_at(x, y);
    if (!win || win->type != HS_WINDOW_FLOATING) return;
    if (win->state == HS_WINDOW_STATE_MAXIMIZED) return;

    g_drag_window = win;
    g_drag_offset_x = x - win->x;
    g_drag_offset_y = y - win->y;
    g_dragging = 1;
    hs_window_focus(win);
}

void hs_window_drag_move(int x, int y)
{
    if (!g_dragging || !g_drag_window) return;

    int new_x = x - g_drag_offset_x;
    int new_y = y - g_drag_offset_y;

    /* 边界限制 */
    if (new_x < 0) new_x = 0;
    if (new_y < HS_PANEL_HEIGHT) new_y = HS_PANEL_HEIGHT;
    if (new_x + g_drag_window->width > g_shell.screen_width)
        new_x = g_shell.screen_width - g_drag_window->width;
    if (new_y + g_drag_window->height > g_shell.screen_height - HS_PANEL_HEIGHT)
        new_y = g_shell.screen_height - HS_PANEL_HEIGHT - g_drag_window->height;

    g_drag_window->x = new_x;
    g_drag_window->y = new_y;
}

void hs_window_drag_end(void)
{
    g_dragging = 0;
    g_drag_window = NULL;
}

int hs_window_is_dragging(void)
{
    return g_dragging;
}

/* ============================================================================
 * P2: 窗口规则
 * ========================================================================== */

#define HS_MAX_RULES 16
static hs_window_rule_t g_rules[HS_MAX_RULES];
static int g_rule_count = 0;

void hs_window_rule_init(void)
{
    g_rule_count = 0;
    /* 默认规则 */
    hs_window_rule_add("Terminal", HS_LAYOUT_TILE, -1, 0);
    hs_window_rule_add("Browser", HS_LAYOUT_TILE, -1, 0);
    hs_window_rule_add("Settings", HS_LAYOUT_FLOATING, -1, 1);
}

int hs_window_rule_add(const char *app, hs_layout_mode_t layout,
                       int workspace, int floating)
{
    if (g_rule_count >= HS_MAX_RULES) return -1;

    hs_window_rule_t *rule = &g_rules[g_rule_count];
    int i = 0;
    while (app && app[i] && i < 31) { rule->app_name[i] = app[i]; i++; }
    rule->app_name[i] = 0;
    rule->layout = layout;
    rule->workspace = workspace;
    rule->floating = floating;

    return g_rule_count++;
}

void hs_window_rule_apply(hs_window_t *win)
{
    if (!win) return;

    for (int i = 0; i < g_rule_count; i++) {
        if (str_cmp(g_rules[i].app_name, win->title) == 0) {
            if (g_rules[i].workspace >= 0) {
                win->workspace = g_rules[i].workspace;
            }
            if (g_rules[i].floating) {
                /* 浮动窗口不参与平铺 */
            }
            return;
        }
    }
}

/* ============================================================================
 * P2: 工作区完善
 * ========================================================================== */

void hs_workspace_set_name(int ws, const char *name)
{
    if (ws < 0 || ws >= HS_MAX_WORKSPACES) return;
    int i = 0;
    while (name && name[i] && i < 15) {
        g_shell.workspaces[ws].name[i] = name[i];
        i++;
    }
    g_shell.workspaces[ws].name[i] = 0;
}

void hs_workspace_move_window(hs_window_t *win, int ws)
{
    if (!win || ws < 0 || ws >= HS_MAX_WORKSPACES) return;
    win->workspace = ws;
    hs_layout_apply();
}

void hs_workspace_send_to_next(void)
{
    hs_window_t *focused = hs_window_get_focused();
    if (!focused) return;
    int next = (g_shell.current_workspace + 1) % HS_MAX_WORKSPACES;
    hs_workspace_move_window(focused, next);
}

void hs_workspace_send_to_prev(void)
{
    hs_window_t *focused = hs_window_get_focused();
    if (!focused) return;
    int prev = (g_shell.current_workspace - 1 + HS_MAX_WORKSPACES) % HS_MAX_WORKSPACES;
    hs_workspace_move_window(focused, prev);
}

/* ============================================================================
 * V0.13.10: 缺失接口恢复 — 会话源码损坏后重建
 * 覆盖: 天气拉取/窗口切换器/焦点循环/锁屏/主题/工作区/显示桌面/滚轮/应用启动
 * ========================================================================== */

/* ---- 天气: 键查找与整数解析 (无 libc, 手写) ---- */
static int hs_find_key(const uint8_t *b, int len, const char *key)
{
    int klen = 0;
    while (key[klen]) klen++;
    for (int i = 0; i + klen < len; i++) {
        int ok = 1;
        for (int k = 0; k < klen; k++) {
            if (b[i + k] != (uint8_t)key[k]) { ok = 0; break; }
        }
        if (ok) return i + klen;
    }
    return -1;
}

static int hs_parse_int_at(const uint8_t *b, int pos, int len, int *out)
{
    while (pos < len && (b[pos] == ' ' || b[pos] == ':' || b[pos] == '"')) pos++;
    int v = 0, neg = 0, started = 0;
    if (pos < len && b[pos] == '-') { neg = 1; pos++; }
    while (pos < len && b[pos] >= '0' && b[pos] <= '9') {
        v = v * 10 + (b[pos] - '0');
        started = 1; pos++;
    }
    if (!started) return -1;
    *out = neg ? -v : v;
    return 0;
}

/* 整数转字符串 (无 sprintf) */
static void hs_itoa(int v, char *buf)
{
    int a = v < 0 ? -v : v;
    char tmp[12];
    int n = 0;
    do { tmp[n++] = '0' + (a % 10); a /= 10; } while (a);
    if (v < 0) tmp[n++] = '-';
    int s = 0, e = n - 1;
    while (s < e) { char c = tmp[s]; tmp[s] = tmp[e]; tmp[e] = c; s++; e--; }
    tmp[n] = 0;
    strcpy(buf, tmp);
}

/* 天气拉取: open-meteo 常德 (29.03N/111.69E) */
void hepushell_fetch_weather(void)
{
    extern int http64_get(const char *url, uint8_t *response, int max_response);
    extern int http64_get_status(const uint8_t *response);
    extern int http64_get_body(const uint8_t *response, int response_len,
                               uint8_t *body, int max_body);
    static const char *url =
        "http://api.open-meteo.com/v1/forecast?latitude=29.03&longitude=111.69&current_weather=true";
    uint8_t resp[2048];
    uint8_t body[1024];
    if (http64_get(url, resp, sizeof(resp)) < 0) return;
    int st = http64_get_status(resp);
    if (st < 200 || st >= 300) return;
    int blen = http64_get_body(resp, sizeof(resp), body, sizeof(body));
    if (blen <= 0) return;

    /* 温度: 仅当冒号后解析出数字才取用 (修复 temperature_units 误匹配) */
    int t = -999;
    int tp = hs_find_key(body, blen, "\"temperature\":");
    if (tp >= 0 && hs_parse_int_at(body, tp, blen, &t) == 0) {
        hs_itoa(t, g_weather_temp);
    }
    /* 天气码 → 描述 */
    int wc = -1;
    int wp = hs_find_key(body, blen, "\"weathercode\":");
    if (wp >= 0 && hs_parse_int_at(body, wp, blen, &wc) == 0) {
        const char *desc = "Cloudy";
        switch (wc) {
            case 0: desc = "Clear"; break;
            case 1: case 2: desc = "Few clouds"; break;
            case 3: desc = "Cloudy"; break;
            case 45: case 48: desc = "Fog"; break;
            case 51: case 53: case 55: case 56: case 57: desc = "Drizzle"; break;
            case 61: case 63: case 65: case 66: case 67: desc = "Rain"; break;
            case 71: case 73: case 75: case 77: desc = "Snow"; break;
            case 80: case 81: case 82: desc = "Showers"; break;
            case 85: case 86: desc = "Snow showers"; break;
            case 95: case 96: case 99: desc = "Thunderstorm"; break;
            default: break;
        }
        strcpy(g_weather_desc, desc);
    }
    g_weather_ok = 1;
    vga_puts("[WX] weather fetched\n");
}

/* ---- 窗口切换器 (Alt+Tab, LRU 卡片) ---- */
static void hs_switcher_build(void)
{
    g_sw_count = 0;
    /* V0.13.16: 收集可见+最小化窗口(最小化也可恢复), 焦点窗口最前 */
    for (int i = 0; i < g_wm.window_count && g_sw_count < HS_MAX_WINDOWS; i++) {
        hs_window_t *w = &g_wm.windows[i];
        if (w->type == HS_WINDOW_PANEL) continue;
        if (w->workspace != g_shell.current_workspace) continue;
        if (!w->visible && w->state != HS_WINDOW_STATE_MINIMIZED) continue;
        if (w->focus) g_sw_order[g_sw_count++] = i;
    }
    for (int i = 0; i < g_wm.window_count && g_sw_count < HS_MAX_WINDOWS; i++) {
        hs_window_t *w = &g_wm.windows[i];
        if (w->type == HS_WINDOW_PANEL) continue;
        if (w->workspace != g_shell.current_workspace) continue;
        if (!w->visible && w->state != HS_WINDOW_STATE_MINIMIZED) continue;
        if (!w->focus) g_sw_order[g_sw_count++] = i;
    }
    if (g_sw_count > 12) g_sw_count = 12;
}

int hepushell_switcher_active(void) { return g_sw_active; }

void hepushell_switcher_next(void)
{
    if (!g_sw_active) {
        hs_switcher_build();
        if (g_sw_count <= 0) return;
        g_sw_active = 1;
        g_sw_sel = 0;
    } else {
        if (g_sw_count <= 0) return;
        g_sw_sel = (g_sw_sel + 1) % g_sw_count;
    }
    int idx = g_sw_order[g_sw_sel];
    if (idx >= 0 && idx < g_wm.window_count) {
        /* V0.13.16: 切换器选中最小化窗口时恢复可见 */
        if (g_wm.windows[idx].state == HS_WINDOW_STATE_MINIMIZED) {
            g_wm.windows[idx].state = HS_WINDOW_STATE_NORMAL;
            g_wm.windows[idx].visible = 1;
        }
        for (int i = 0; i < g_wm.window_count; i++) g_wm.windows[i].focus = 0;
        g_wm.windows[idx].focus = 1;
        g_wm.focus_window = idx;
    }
}

void hepushell_switcher_close(void)
{
    if (!g_sw_active) return;
    /* 关闭选中窗口 (× 语义) */
    if (g_sw_count > 0 && g_sw_sel >= 0 && g_sw_sel < g_sw_count) {
        int idx = g_sw_order[g_sw_sel];
        if (idx >= 0 && idx < g_wm.window_count) {
            hs_window_recycle(idx);
        }
    }
    g_sw_active = 0;
}

static void hs_switcher_render(void)
{
    if (g_sw_count <= 0) return;
    void *fb = gfx64_get_framebuffer();
    int W = g_shell.screen_width, H = g_shell.screen_height;
    int card_w = 160, card_h = 56, gap = 12;
    int total = g_sw_count * (card_w + gap) - gap;
    int x0 = (W - total) / 2;
    int y0 = H / 2 - card_h / 2 - 20;
    /* 半透明遮罩 */
    gfx64_fill_rect(0, 0, W, H, 0x88000000);
    for (int i = 0; i < g_sw_count; i++) {
        int idx = g_sw_order[i];
        if (idx < 0 || idx >= g_wm.window_count) continue;
        hs_window_t *w = &g_wm.windows[idx];
        int cx = x0 + i * (card_w + gap);
        uint32_t bg = (i == g_sw_sel) ? g_shell.colors.accent : g_shell.colors.panel_bg;
        uint32_t fg = (i == g_sw_sel) ? 0xFFFFFF : g_shell.colors.panel_fg;
        gfx64_fill_rounded_rect(cx, y0, card_w, card_h, HS_RD_INPUT, bg);
        gfx64_draw_rounded_rect(cx, y0, card_w, card_h, HS_RD_INPUT, g_shell.colors.border_strong);
        font64_draw_string(fb, W, H, cx + 12, y0 + 18, w->title, fg, 12);
    }
    font64_draw_string(fb, W, H, W / 2 - 90, y0 + card_h + 14,
                       "Alt+Tab: next   Esc: close window", 0xCCCCCC, 10);
}

/* ---- 窗口焦点循环 (Alt+Tab 未激活时) ---- */
void hepushell_cycle_window_focus(void)
{
    if (g_wm.window_count <= 0) return;
    int start = g_wm.focus_window;
    for (int step = 1; step <= g_wm.window_count; step++) {
        int idx = (start + step) % g_wm.window_count;
        hs_window_t *w = &g_wm.windows[idx];
        if (!w->visible || w->workspace != g_shell.current_workspace) continue;
        for (int i = 0; i < g_wm.window_count; i++) g_wm.windows[i].focus = 0;
        w->focus = 1;
        g_wm.focus_window = idx;
        return;
    }
}

/* ---- 关闭聚焦窗口 ---- */
void hepushell_close_focused_window(void)
{
    if (g_wm.focus_window < 0) return;
    hs_window_recycle(g_wm.focus_window);
}

/* ---- 主题切换 (Light → Dark → Mono 循环) ---- */
void hepushell_toggle_theme(void)
{
    g_shell.theme = (hs_theme_t)(((int)g_shell.theme + 1) % 3);
    hs_theme_init(&g_shell.colors, g_shell.theme);
    vga_puts("[Theme] toggled\n");
}

/* ---- 锁屏 ---- */
void hepushell_lock(void)
{
    if (g_locked == 0) g_locked = 1;
}

/* ---- 工作区相对切换 ---- */
void hepushell_switch_workspace_delta(int delta)
{
    int ws = g_shell.current_workspace + delta;
    if (ws < 0) ws = 0;
    if (ws >= HS_MAX_WORKSPACES) ws = HS_MAX_WORKSPACES - 1;
    if (ws != g_shell.current_workspace) hepushell_switch_workspace(ws);
}

/* ---- 显示桌面 (最小化全部窗口) ---- */
void hepushell_show_desktop(void)
{
    for (int i = 0; i < g_wm.window_count; i++) {
        if (g_wm.windows[i].visible) g_wm.windows[i].visible = 0;
    }
    g_wm.focus_window = -1;
}

/* ---- 滚轮 (桌面缩放占位: 无终端滚动目标时调节 UI 缩放) ---- */
static int g_ui_scale_v = 100;
void hepushell_update_wheel(int wheel)
{
    /* 应用窗口滚动优先(系统工具/终端等), 否则保留 UI 缩放 */
    if (!hs_systools_handle_wheel(wheel)) hs_apps_handle_wheel(wheel);
    /* V0.55: 侧栏滚动 (指针在侧栏内且内容溢出时消费) */
    if (hs_sidebar_handle_wheel(wheel)) return;
    if (wheel > 0 && g_ui_scale_v < 150) g_ui_scale_v += 5;
    else if (wheel < 0 && g_ui_scale_v > 50) g_ui_scale_v -= 5;
}

/* ---- 待启动应用处理 (desktop64 主循环调用) ---- */
static void hs_app_placeholder_render(hs_window_t *w)
{
    if (!w) return;
    void *fb = gfx64_get_framebuffer();
    int W = g_shell.screen_width, H = g_shell.screen_height;
    font64_draw_string(fb, W, H, w->x + 24, w->y + 40, w->title,
                       g_shell.colors.window_fg, 16);
    font64_draw_string(fb, W, H, w->x + 24, w->y + 70,
                       "App content rebuilding (V0.54.12)", g_shell.colors.text_secondary, 12);
}

void hepushell_handle_pending_app(void)
{
    if (g_pending_app == HS_APP_NONE) return;
    int app = g_pending_app;
    g_pending_app = HS_APP_NONE;

    const char *title = "App";
    switch (app) {
        case HS_APP_TERMINAL: title = "Terminal"; break;
        case HS_APP_FILES: title = "Files"; break;
        case HS_APP_BROWSER: title = "Browser"; break;
        case HS_APP_SETTINGS: title = "Settings"; break;
        case HS_APP_EDITOR: title = "Editor"; break;
        case HS_APP_CALCULATOR: title = "Calculator"; break;
        case HS_APP_NOTES: title = "Notes"; break;
        case HS_APP_ABOUT: title = "About"; break;
        case HS_APP_TASK: title = "Task Manager"; break;
        case HS_APP_DEVMGR: title = "Device Manager"; break;
        case HS_APP_DISKMGR: title = "Disk Manager"; break;
        case HS_APP_LOGVIEW: title = "Log Viewer"; break;
        default: return;
    }
    /* V0.13.16: Dock/启动器点击 → 若该应用已有最小化/隐藏窗口则恢复, 否则新建 */
    for (int i = 0; i < g_wm.window_count; i++) {
        hs_window_t *e = &g_wm.windows[i];
        if (e->type != HS_WINDOW_FLOATING) continue;
        if (strcmp(e->title, title) != 0) continue;
        if (!e->visible || e->state == HS_WINDOW_STATE_MINIMIZED) {
            e->state = HS_WINDOW_STATE_NORMAL;
            e->visible = 1;
            hs_window_focus(e);
            return;
        }
    }
    if (app == HS_APP_SETTINGS) {
        hs_window_t *w = hs_window_create(HS_WINDOW_FLOATING, title,
                                          -1, -1, 460, 300);
        if (w) w->render = hs_settings_window_render;
        return;
    }
    /* 已实现的应用窗口 */
    if (app == HS_APP_TERMINAL) { hs_terminal_create(); return; }
    if (app == HS_APP_FILES) { hs_files_create(); return; }
    if (app == HS_APP_TASK) { hs_taskmgr_create(); return; }
    if (app == HS_APP_DEVMGR) { hs_devmgr_create(); return; }
    if (app == HS_APP_DISKMGR) { hs_diskmgr_create(); return; }
    if (app == HS_APP_LOGVIEW) { hs_logview_create(); return; }
    if (app == HS_APP_EDITOR) { hs_editor_create(); return; }
    hs_window_t *w = hs_window_create(HS_WINDOW_FLOATING, title,
                                      -1, -1, 480, 320);
    if (w) w->render = hs_app_placeholder_render;
}
