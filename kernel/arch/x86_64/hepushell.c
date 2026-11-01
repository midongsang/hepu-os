/* ============================================================================
 * 赫普OS - HepuShell 桌面环境实现
 * 复现 Quickshell 核心功能
 * V0.10.160
 * ========================================================================== */

#include "hepushell.h"
#include "hsqml.h"

/* 外部函数 */
extern void gfx64_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void gfx64_draw_rect(int x, int y, int w, int h, uint32_t color);
extern void gfx64_fill_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
extern void gfx64_draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
extern void gfx64_blur_region(int x, int y, int w, int h, int blur_radius, uint32_t overlay_color, int overlay_alpha);
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

/* 前向声明（定义在后面） */
static int g_sidebar_tab;
static int g_island_expanded;
int g_pending_app;

/* 右键菜单状态 */
static int g_context_menu_visible = 0;
static int g_context_menu_x = 0;
static int g_context_menu_y = 0;

/* 窗口拖拽状态（定义在后面） */
static hs_window_t *g_drag_window;
static int g_drag_offset_x;
static int g_drag_offset_y;
static int g_dragging;

/* 设置窗口当前页面 */
static int g_settings_page = 2;  /* 0=Appearance, 1=Panels, 2=System, 3=About */

/* 内置默认配置（hsqml格式） */
static const char *default_config =
"// HepuShell 默认配置 - Quickshell风格\n"
"Theme {\n"
"    mode: \"light\"\n"
"    name: \"quickshell\"\n"
"    background: #F0EBE0\n"
"    panel_bg: #E8DFD0\n"
"    panel_fg: #5C4A32\n"
"    border: #D4C8B0\n"
"    accent: #C9A96E\n"
"    window_bg: #FAF6EE\n"
"    window_fg: #3D3226\n"
"    titlebar_bg: #E8DFD0\n"
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
"    cc_margin_right: 300\n"
"    cc_y: 50\n"
"    cc_width: 280\n"
"    cc_height: 380\n"
"    bar_height: 40\n"
"    island_width: 120\n"
"    island_height: 28\n"
"    island_y: 6\n"
"    island_font_size: 20\n"
"    dock_width: 400\n"
"    dock_height: 64\n"
"    dock_margin_bottom: 12\n"
"    dock_icon_size: 44\n"
"    corner_radius: 16\n"
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
    g_shell.ui.cc_margin_right = hsqml_get_number(ui, "cc_margin_right", 300);
    g_shell.ui.cc_y = hsqml_get_number(ui, "cc_y", 50);
    g_shell.ui.cc_width = hsqml_get_number(ui, "cc_width", 280);
    g_shell.ui.cc_height = hsqml_get_number(ui, "cc_height", 380);
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
        /* 深色主题（Quickshell深色风格） */
        colors->bg = 0x1A1815;           /* 深棕黑背景 */
        colors->panel_bg = 0x2A2620;      /* 深棕面板 */
        colors->panel_fg = 0xE8DFD0;      /* 米色文字 */
        colors->border = 0x3D3830;        /* 深棕边框 */
        colors->accent = 0xD4B87A;        /* 亮金强调 */
        colors->window_bg = 0x242019;     /* 窗口背景 */
        colors->window_fg = 0xF0EBE0;     /* 窗口文字 */
        colors->titlebar_bg = 0x2A2620;   /* 标题栏 */
        colors->workspace_active = 0xD4B87A;   /* 活动工作区-亮金 */
        colors->workspace_inactive = 0x5C5448; /* 非活动-深棕 */
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

    /* 应用列表 */
    int list_y = y + 68;
    int visible = 0;
    for (int i = 0; i < g_shell.app_count && visible < 8; i++) {
        hs_app_t *app = &g_shell.apps[i];

        /* 过滤 */
        if (g_shell.launcher_input[0] != 0) {
            int match = 1;
            for (int j = 0; g_shell.launcher_input[j]; j++) {
                if (app->name[j] != g_shell.launcher_input[j]) {
                    match = 0;
                    break;
                }
            }
            if (!match) continue;
        }

        uint32_t bg = (i == g_shell.selected_app) ?
                      g_shell.colors.accent : g_shell.colors.window_bg;
        uint32_t fg = (i == g_shell.selected_app) ?
                      0x5C4A32 : g_shell.colors.window_fg;

        if (i == g_shell.selected_app) {
            gfx64_fill_rounded_rect(x + 16, list_y - 2, w - 32, 30, 6, bg);
        }

        /* 应用图标 */
        gfx64_fill_rounded_rect(x + 24, list_y + 2, 24, 24, 6, g_shell.colors.accent);

        /* 应用名称 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           x + 58, list_y + 6, app->name, fg, 16);
        list_y += 34;
        visible++;
    }
}

void hs_launcher_handle_key(int key)
{
    if (key == 27) {  /* ESC */
        g_shell.launcher_visible = 0;
        return;
    }
    if (key == '\n' || key == '\r') {  /* Enter */
        /* 启动选中的应用 */
        if (g_shell.selected_app < g_shell.app_count) {
            vga_puts("[Launcher] Starting: ");
            vga_puts(g_shell.apps[g_shell.selected_app].name);
            vga_puts("\n");
            /* 映射到g_pending_app，由desktop64主循环启动 */
            int app_map[] = {HS_APP_TERMINAL, HS_APP_FILES, HS_APP_BROWSER,
                             HS_APP_SETTINGS, HS_APP_EDITOR, HS_APP_CALCULATOR,
                             HS_APP_NOTES, HS_APP_ABOUT, HS_APP_ABOUT, HS_APP_ABOUT};
            if (g_shell.selected_app < 10) {
                g_pending_app = app_map[g_shell.selected_app];
            }
        }
        g_shell.launcher_visible = 0;
        return;
    }
    if (key == '\b') {  /* Backspace */
        if (g_shell.launcher_cursor > 0) {
            g_shell.launcher_cursor--;
            g_shell.launcher_input[g_shell.launcher_cursor] = 0;
        }
        return;
    }
    if (key >= 0x20 && key < 0x7F && g_shell.launcher_cursor < 63) {
        g_shell.launcher_input[g_shell.launcher_cursor++] = key;
        g_shell.launcher_input[g_shell.launcher_cursor] = 0;
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
    for (int i = 0; i < HS_MAX_WINDOWS; i++) {
        g_wm.windows[i].visible = 0;
        g_wm.z_order[i] = -1;
    }
}

/* 创建窗口 */
hs_window_t *hs_window_create(hs_window_type_t type, const char *title,
                              int x, int y, int w, int h)
{
    if (g_wm.window_count >= HS_MAX_WINDOWS) return NULL;

    hs_window_t *win = &g_wm.windows[g_wm.window_count];
    win->id = g_wm.window_count;
    win->type = type;
    win->state = HS_WINDOW_STATE_NORMAL;

    /* 复制标题 */
    int i = 0;
    while (title && title[i] && i < 63) {
        win->title[i] = title[i];
        i++;
    }
    win->title[i] = 0;

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

    /* 添加到z_order */
    g_wm.z_order[g_wm.z_count++] = win->id;
    g_wm.window_count++;

    return win;
}

/* 销毁窗口 */
void hs_window_destroy(hs_window_t *win)
{
    if (!win) return;
    win->visible = 0;
    /* 从z_order移除 */
    int new_z = 0;
    for (int i = 0; i < g_wm.z_count; i++) {
        if (g_wm.z_order[i] != win->id) {
            g_wm.z_order[new_z++] = g_wm.z_order[i];
        }
    }
    g_wm.z_count = new_z;
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

/* 显示/隐藏窗口 */
void hs_window_show(hs_window_t *win) { if (win) win->visible = 1; }
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

        /* 窗口背景 */
        gfx64_fill_rect(w->x, w->y, w->width, w->height, w->bg_color);

        /* 标题栏（浮动窗口） */
        if (w->type == HS_WINDOW_FLOATING) {
            gfx64_fill_rect(w->x, w->y, w->width, 24,
                            g_shell.colors.titlebar_bg);
            /* 关闭按钮 */
            int close_x = w->x + w->width - 20;
            int close_y = w->y + 4;
            gfx64_fill_rounded_rect(close_x, close_y, 16, 16, 4, 0xCC3333);
            void *fb2 = gfx64_get_framebuffer();
            font64_draw_string(fb2, g_shell.screen_width, g_shell.screen_height,
                               close_x + 4, close_y + 2, "x", 0xFFFFFF, 12);
        }

        /* 边框 */
        uint32_t border = w->focus ?
                          g_shell.colors.accent : w->border_color;
        gfx64_draw_rect(w->x, w->y, w->width, w->height, border);

        /* 标题（使用深色文字确保可见） */
        if (w->type != HS_WINDOW_POPUP || w->title[0]) {
            void *fb = gfx64_get_framebuffer();
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               w->x + 8, w->y + 6, w->title,
                               0x2A2520, 12);
        }

        /* 自定义渲染回调 */
        if (w->render) {
            w->render(w);
        }
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
        uint32_t fg = (i == g_settings_page) ? 0x5C4A32 : g_shell.colors.panel_fg;
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
                           rx, ry, "Accent: Gold (#C9A96E)", g_shell.colors.panel_fg, 12);
        ry += 24;
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Font: DejaVu Sans", g_shell.colors.panel_fg, 12);
        ry += 24;
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Clock Size: 40px", g_shell.colors.panel_fg, 12);
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
        const char *values[] = {"Hepu OS v0.12.0", "64-bit SMP", "4 cores", "2GB",
                                "VBE 1024x768", "ext2"};
        for (int i = 0; i < 6; i++) {
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               rx, ry + i * 24, labels[i], g_shell.colors.panel_fg, 12);
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               rx + 80, ry + i * 24, values[i], g_shell.colors.accent, 12);
        }
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
    /* 按层级顺序渲染所有启用的面板（顶部面板优先，避免累积corruption） */
    int order[] = {
        HS_PANEL_TOP_BAR,
        HS_PANEL_DYNAMIC_ISLAND,
        HS_PANEL_DOCK,
        HS_PANEL_DESKTOP_CLOCK,
        HS_PANEL_WEATHER,
        HS_PANEL_SIDEBAR,
        HS_PANEL_CONTROL_CENTER
    };
    for (int i = 0; i < HS_PANEL_COUNT; i++) {
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

    gfx64_fill_rounded_rect(island_x, island_y, island_w, island_h, hs_s(15), 0x2A2520);

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
                           time, 0xE8DCC8, font_size);
    } else {
        /* 展开状态：时间+日期+天气+系统状态 */
        int cy = island_y + hs_sy(12);
        /* 时间（大字号） */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           island_x + hs_sx(20), cy, time, 0xE8DCC8, hs_s(28));
        cy += hs_sy(36);
        /* 日期 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           island_x + hs_sx(20), cy, "October 15, Thursday", 0xC9B89A, hs_s(14));
        /* 右侧天气 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           island_x + island_w - hs_sx(100), cy, "26C Cloudy", 0xC9B89A, hs_s(14));
        cy += hs_sy(28);
        /* 系统状态条 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           island_x + hs_sx(20), cy, "CPU 35%  MEM 60%  WiFi On", 0xA89880, hs_s(12));
    }
}

/* ============================================================================
 * 图标绘制函数（简化版，避免浮点运算）
 * ========================================================================== */

/* 绘制WiFi图标 */
static void hs_draw_wifi(int x, int y, int size, uint32_t color)
{
    /* 用三条弧线近似：用填充矩形模拟 */
    int cx = x + size/2;
    int cy = y + size/2;
    /* 最外层弧 */
    gfx64_fill_rect(cx - 6, cy - 4, 12, 2, color);
    gfx64_fill_rect(cx - 7, cy - 2, 2, 2, color);
    gfx64_fill_rect(cx + 5, cy - 2, 2, 2, color);
    /* 中层弧 */
    gfx64_fill_rect(cx - 4, cy, 8, 2, color);
    gfx64_fill_rect(cx - 5, cy + 2, 2, 2, color);
    gfx64_fill_rect(cx + 3, cy + 2, 2, 2, color);
    /* 内层点 */
    gfx64_fill_rect(cx - 1, cy + 4, 3, 3, color);
}

/* 绘制音量图标 */
static void hs_draw_volume(int x, int y, int size, uint32_t color)
{
    /* 扬声器 */
    gfx64_fill_rect(x, y + size/3, size/3, size/3, color);
    /* 锥形 */
    gfx64_fill_rect(x + size/3, y + size/4, size/4, size/2, color);
    /* 声波 */
    gfx64_fill_rect(x + size*2/3, y + size/3, 2, size/3, color);
    gfx64_fill_rect(x + size*2/3 + 4, y + size/4, 2, size/2, color);
}

/* 绘制电池图标 */
static void hs_draw_battery(int x, int y, int w, int h, uint32_t color, int level)
{
    /* 电池主体 */
    gfx64_draw_rounded_rect(x, y, w, h, 2, color);
    /* 电池头 */
    gfx64_fill_rect(x + w, y + h/3, 2, h/3, color);
    /* 电量 */
    int fill_w = (w - 4) * level / 100;
    if (fill_w < 0) fill_w = 0;
    uint32_t fill_color = (level > 20) ? 0x7CB342 : 0xE57373;
    gfx64_fill_rect(x + 2, y + 2, fill_w, h - 4, fill_color);
}

/* 绘制设置图标 */
static void hs_draw_settings(int x, int y, int size, uint32_t color)
{
    /* 外圈 */
    gfx64_draw_rounded_rect(x, y, size, size, size/2, color);
    /* 内圈 */
    gfx64_fill_rounded_rect(x + size/4, y + size/4, size/2, size/2, size/4, g_shell.colors.panel_bg);
    /* 中心 */
    gfx64_fill_rounded_rect(x + size/3, y + size/3, size/3, size/3, size/6, color);
}

/* 绘制电源图标 */
static void hs_draw_power(int x, int y, int size, uint32_t color)
{
    /* 顶部竖线 */
    gfx64_fill_rect(x + size/2 - 1, y + 2, 3, size/2, color);
    /* 圆圈（用四个角的矩形近似） */
    int r = size/2 - 2;
    int cx = x + size/2;
    int cy = y + size/2 + 2;
    gfx64_fill_rect(cx - r, cy - 1, r*2, 2, color);  /* 底部 */
    gfx64_fill_rect(cx - r, cy - r/2, 2, r/2, color);  /* 左下 */
    gfx64_fill_rect(cx + r - 2, cy - r/2, 2, r/2, color);  /* 右下 */
}

/* ============================================================================
 * 状态栏矢量图标（用矩形和点绘制，不依赖图标字体）
 * ==========================================================================*/

/* WiFi图标：3条递增弧线（用矩形近似） */
static void hs_icon_wifi(int x, int y, int size, uint32_t color)
{
    int cx = x + size / 2;
    int by = y + size - 2;
    /* 底部圆点 */
    gfx64_fill_rect(cx - 1, by - 2, 3, 3, color);
    /* 3条弧线：用短矩形段近似圆弧 */
    for (int ring = 1; ring <= 3; ring++) {
        int r = ring * (size / 4);
        int arc_w = r * 2 - 2;
        int arc_y = by - r - 1;
        /* 顶部横段 */
        gfx64_fill_rect(cx - arc_w / 2, arc_y, arc_w, 1, color);
        /* 左右斜段（用竖矩形近似） */
        gfx64_fill_rect(cx - arc_w / 2, arc_y + 1, 1, r / 2, color);
        gfx64_fill_rect(cx + arc_w / 2 - 1, arc_y + 1, 1, r / 2, color);
    }
}

/* 电池图标：外框+正极+电量填充 */
static void hs_icon_battery(int x, int y, int size, uint32_t color, int percent)
{
    int w = size - 3;
    int h = size / 2 + 2;
    int by = y + (size - h) / 2;
    /* 外框 */
    gfx64_draw_rect(x, by, w, h, color);
    /* 正极凸起 */
    gfx64_fill_rect(x + w, by + h / 4, 2, h / 2, color);
    /* 电量填充（75%绿，50%黄，25%红） */
    int fill_w = (w - 2) * percent / 100;
    if (fill_w > 0) {
        uint32_t fill_color = color;
        if (percent > 50) fill_color = 0x52C41A;
        else if (percent > 25) fill_color = 0xFAAD14;
        else fill_color = 0xEA6668;
        gfx64_fill_rect(x + 1, by + 1, fill_w, h - 2, fill_color);
    }
}

/* 音量图标：扬声器+声波 */
static void hs_icon_volume(int x, int y, int size, uint32_t color)
{
    int cy = y + size / 2;
    /* 扬声器本体（三角形用矩形阶梯近似） */
    gfx64_fill_rect(x, cy - 1, size / 3, 3, color);
    gfx64_fill_rect(x + size / 3, cy - 3, 2, 7, color);
    gfx64_fill_rect(x + size / 3 + 2, cy - 4, 2, 9, color);
    /* 声波：2条弧线 */
    int sx = x + size / 3 + 5;
    gfx64_fill_rect(sx, cy - 3, 1, 7, color);
    gfx64_fill_rect(sx + 3, cy - 5, 1, 11, color);
}

/* 设置图标：齿轮（用矩形段近似圆+十字） */
static void hs_icon_settings(int x, int y, int size, uint32_t color)
{
    int cx = x + size / 2;
    int cy = y + size / 2;
    int r = size / 2 - 2;
    /* 用矩形段近似圆（上下左右+四角） */
    gfx64_fill_rect(cx - r + 2, cy - r, r * 2 - 4, 2, color);  /* 顶 */
    gfx64_fill_rect(cx - r + 2, cy + r - 2, r * 2 - 4, 2, color);  /* 底 */
    gfx64_fill_rect(cx - r, cy - r + 2, 2, r * 2 - 4, color);  /* 左 */
    gfx64_fill_rect(cx + r - 2, cy - r + 2, 2, r * 2 - 4, color);  /* 右 */
    /* 四角斜段 */
    gfx64_fill_rect(cx - r + 1, cy - r + 1, 2, 2, color);
    gfx64_fill_rect(cx + r - 3, cy - r + 1, 2, 2, color);
    gfx64_fill_rect(cx - r + 1, cy + r - 3, 2, 2, color);
    gfx64_fill_rect(cx + r - 3, cy + r - 3, 2, 2, color);
    /* 中心圆孔（用背景色覆盖） */
    gfx64_fill_rect(cx - 2, cy - 2, 5, 5, g_shell.colors.panel_bg);
    /* 齿轮齿（上下左右突出） */
    gfx64_fill_rect(cx - 1, cy - r - 2, 3, 3, color);
    gfx64_fill_rect(cx - 1, cy + r - 1, 3, 3, color);
    gfx64_fill_rect(cx - r - 2, cy - 1, 3, 3, color);
    gfx64_fill_rect(cx + r - 1, cy - 1, 3, 3, color);
}

/* 电源图标：开口圆+竖线（用矩形段近似） */
static void hs_icon_power(int x, int y, int size, uint32_t color)
{
    int cx = x + size / 2;
    int cy = y + size / 2 + 1;
    int r = size / 2 - 2;
    /* 开口圆（顶部留缺口），用矩形段近似 */
    gfx64_fill_rect(cx - r + 2, cy + r - 2, r * 2 - 4, 2, color);  /* 底 */
    gfx64_fill_rect(cx - r, cy - r + 3, 2, r * 2 - 5, color);  /* 左 */
    gfx64_fill_rect(cx + r - 2, cy - r + 3, 2, r * 2 - 5, color);  /* 右 */
    /* 左右上斜段 */
    gfx64_fill_rect(cx - r + 1, cy - r + 2, 2, 2, color);
    gfx64_fill_rect(cx + r - 3, cy - r + 2, 2, 2, color);
    /* 顶部竖线（电源按钮的竖线） */
    gfx64_fill_rect(cx - 1, y + 1, 3, r + 2, color);
}

/* 顶部Bar：左侧工作区+右侧状态图标 */
static void hs_top_bar_render(void)
{
    int bar_h = hs_sy(g_shell.ui.bar_height);

    /* 半透明背景条 */
    gfx64_fill_rect(0, 0, g_shell.screen_width, bar_h, g_shell.colors.panel_bg);
    gfx64_fill_rect(0, bar_h - 1, g_shell.screen_width, 1, g_shell.colors.border);

    /* 左侧工作区指示 */
    int ws_x = hs_sx(20);
    for (int i = 0; i < 4; i++) {
        int dot_w = (i == g_shell.current_workspace) ? hs_sx(20) : hs_sx(8);
        int dot_h = hs_sy(8);
        int dot_y = (bar_h - dot_h) / 2;
        uint32_t color = (i == g_shell.current_workspace) ?
                         g_shell.colors.accent : g_shell.colors.border;
        gfx64_fill_rounded_rect(ws_x, dot_y, dot_w, dot_h, hs_s(4), color);
        ws_x += dot_w + hs_sx(12);
    }

    /* 右侧状态栏矢量图标（替换文字"WiFi Bat Vol Set Pwr"） */
    int icon_size = hs_s(16);
    int icon_y = (bar_h - icon_size) / 2;
    int icon_x = g_shell.screen_width - hs_sx(16);
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
                       wx + hs_sx(20), wy + hs_sy(15), "26C", 0x5C4A32, hs_s(g_shell.ui.weather_temp_font));

    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       wx + hs_sx(15), wy + hs_sy(70), "Cloudy", 0x5C4A32, hs_s(g_shell.ui.weather_desc_font));
}

/* 左侧三标签侧边栏 */
/* 侧边栏当前标签（定义在文件开头前向声明处初始化） */

/* 锁屏状态 */
static int g_locked = 0;       /* 0=桌面, 1=锁屏, 2=登录 */
static char g_password[32] = {0};
static int g_password_len = 0;

/* 锁屏界面：模糊背景+大时钟 */
static void hs_lock_screen_render(void)
{
    /* 深色半透明背景（模拟模糊效果） */
    gfx64_fill_rect(0, 0, g_shell.screen_width, g_shell.screen_height, 0x2A2520);

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
                       clock_x, clock_y, time, 0xF0EBE0, hs_s(64));

    /* 日期 */
    char date[32] = "October 15, Thursday";
    int date_w = hs_sx(17 * 10);
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       clock_x + (hs_sx(360) - date_w) / 2, clock_y + hs_sy(110), date, 0xC9A96E, hs_s(20));

    /* 底部提示 */
    char hint[32] = "Press any key to unlock";
    int hint_w = hs_sx(22 * 8);
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       (g_shell.screen_width - hint_w) / 2, g_shell.screen_height - hs_sy(80),
                       hint, 0x888888, hs_s(14));
}

/* 登录界面：头像+用户名+密码 */
static void hs_login_screen_render(void)
{
    /* 深色半透明背景 */
    gfx64_fill_rect(0, 0, g_shell.screen_width, g_shell.screen_height, 0x2A2520);

    void *fb = gfx64_get_framebuffer();
    int center_x = g_shell.screen_width / 2;

    /* 用户头像（圆形） */
    int avatar_size = hs_s(96);
    int avatar_x = center_x - avatar_size / 2;
    int avatar_y = hs_sy(180);
    gfx64_fill_rounded_rect(avatar_x, avatar_y, avatar_size, avatar_size, hs_s(48), 0xC9A96E);
    /* 头像中的用户图标 */
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       avatar_x + hs_sx(30), avatar_y + hs_sy(25), "U", 0x5C4A32, hs_s(48));

    /* 用户名 */
    char username[32] = "hepu";
    int name_w = hs_sx(4 * 12);
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       center_x - name_w / 2, avatar_y + avatar_size + hs_sy(20),
                       username, 0xF0EBE0, hs_s(20));

    /* 密码输入框 */
    int input_w = hs_sx(240);
    int input_h = hs_sy(36);
    int input_x = center_x - input_w / 2;
    int input_y = avatar_y + avatar_size + hs_sy(60);
    gfx64_fill_rounded_rect(input_x, input_y, input_w, input_h, hs_s(8), 0x3A3530);
    gfx64_draw_rounded_rect(input_x, input_y, input_w, input_h, hs_s(8), 0xC9A96E);

    /* 密码圆点 */
    int dot_y = input_y + (input_h - hs_sy(10)) / 2;
    for (int i = 0; i < g_password_len; i++) {
        gfx64_fill_rounded_rect(input_x + hs_sx(20) + i * hs_sx(18), dot_y, hs_s(10), hs_s(10), hs_s(5), 0xF0EBE0);
    }

    /* 登录按钮 */
    int btn_w = hs_sx(100);
    int btn_h = hs_sy(32);
    int btn_x = center_x - btn_w / 2;
    int btn_y = input_y + input_h + hs_sy(20);
    gfx64_fill_rounded_rect(btn_x, btn_y, btn_w, btn_h, hs_s(8), 0xC9A96E);
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       btn_x + hs_sx(28), btn_y + hs_sy(8), "Login", 0x5C4A32, hs_s(16));

    /* 提示 */
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       center_x - hs_sx(80), btn_y + btn_h + hs_sy(30),
                       "ESC to lock", 0x888888, hs_s(12));
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
        uint32_t fg = (i == g_sidebar_tab) ? 0x5C4A32 : g_shell.colors.panel_fg;
        gfx64_fill_rounded_rect(tx, tab_y, tab_w - hs_sx(4), hs_sy(28), hs_s(8), bg);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           tx + (tab_w - hs_sx(4) - hs_sx(32)) / 2, tab_y + hs_sy(7),
                           tabs[i], fg, hs_s(14));
    }

    /* 内容区域 */
    int content_y = tab_y + hs_sy(40);
    if (g_sidebar_tab == 0) {
        /* 信息标签：系统状态 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "System Info", g_shell.colors.panel_fg, hs_s(18));
        content_y += hs_sy(35);

        /* CPU */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "CPU", g_shell.colors.panel_fg, hs_s(14));
        gfx64_fill_rounded_rect(sx + hs_sx(70), content_y + hs_sy(2), hs_sx(180), hs_sy(12), hs_s(4), g_shell.colors.border);
        gfx64_fill_rounded_rect(sx + hs_sx(70), content_y + hs_sy(2), hs_sx(63), hs_sy(12), hs_s(4), g_shell.colors.accent);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(255), content_y, "35%", g_shell.colors.panel_fg, hs_s(12));
        content_y += hs_sy(28);

        /* Memory */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "MEM", g_shell.colors.panel_fg, hs_s(14));
        gfx64_fill_rounded_rect(sx + hs_sx(70), content_y + hs_sy(2), hs_sx(180), hs_sy(12), hs_s(4), g_shell.colors.border);
        gfx64_fill_rounded_rect(sx + hs_sx(70), content_y + hs_sy(2), hs_sx(108), hs_sy(12), hs_s(4), g_shell.colors.accent);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(255), content_y, "60%", g_shell.colors.panel_fg, hs_s(12));
        content_y += hs_sy(28);

        /* Disk */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "DSK", g_shell.colors.panel_fg, hs_s(14));
        gfx64_fill_rounded_rect(sx + hs_sx(70), content_y + hs_sy(2), hs_sx(180), hs_sy(12), hs_s(4), g_shell.colors.border);
        gfx64_fill_rounded_rect(sx + hs_sx(70), content_y + hs_sy(2), hs_sx(126), hs_sy(12), hs_s(4), g_shell.colors.accent);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(255), content_y, "70%", g_shell.colors.panel_fg, hs_s(12));
        content_y += hs_sy(40);

        /* 系统信息 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "Hepu OS v0.10.171", g_shell.colors.accent, hs_s(14));
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
        /* 应用抽屉 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "Applications", g_shell.colors.panel_fg, hs_s(18));
        content_y += hs_sy(35);

        const char *apps[] = {"Terminal", "Files", "Browser", "Settings",
                              "Editor", "Calculator", "Notes", "Task Mgr",
                              "App Store", "About"};
        for (int i = 0; i < 10; i++) {
            int ax = sx + hs_sx(20) + (i % 3) * hs_sx(80);
            int ay = content_y + (i / 3) * hs_sy(70);
            /* 图标 */
            gfx64_fill_rounded_rect(ax, ay, hs_s(48), hs_s(48), hs_s(10), g_shell.colors.accent);
            /* 名称 */
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               ax + hs_sx(2), ay + hs_sy(54), apps[i], g_shell.colors.panel_fg, hs_s(10));
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

/* 右侧控制中心 */
static void hs_control_center_render(void)
{
    int cx = 724;
    int cy = 50;
    int cw = 280;
    int ch = 380;

    gfx64_fill_rounded_rect(cx, cy, cw, ch, 16, g_shell.colors.window_bg);
    gfx64_draw_rounded_rect(cx, cy, cw, ch, 16, g_shell.colors.border);

    void *fb = gfx64_get_framebuffer();

    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       cx + 20, cy + 16, "Control Center", g_shell.colors.panel_fg, 18);

    /* 快捷开关网格（状态来自g_shell.cc，可交互） */
    int toggle_y = cy + 50;
    const char *toggles[] = {"WiFi", "BT", "Air", "DND", "Loc", "Torch"};
    for (int i = 0; i < 6; i++) {
        int tx = cx + 20 + (i % 3) * 84;
        int ty = toggle_y + (i / 3) * 60;
        uint32_t bg = g_shell.cc.toggles[i] ? g_shell.colors.accent : g_shell.colors.border;
        uint32_t fg = g_shell.cc.toggles[i] ? 0x5C4A32 : g_shell.colors.panel_fg;
        gfx64_fill_rounded_rect(tx, ty, 72, 48, 10, bg);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           tx + 20, ty + 16, toggles[i], fg, 14);
    }

    /* 音量滑块（状态来自g_shell.cc.volume） */
    int slider_y = toggle_y + 140;
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       cx + 20, slider_y, "Volume", g_shell.colors.panel_fg, 14);
    int vol_fill = 240 * g_shell.cc.volume / 100;
    int vol_thumb = cx + 20 + vol_fill - 8;
    gfx64_fill_rounded_rect(cx + 20, slider_y + 22, 240, 8, 4, g_shell.colors.border);
    gfx64_fill_rounded_rect(cx + 20, slider_y + 22, vol_fill, 8, 4, g_shell.colors.accent);
    gfx64_fill_rounded_rect(vol_thumb, slider_y + 18, 16, 16, 8, g_shell.colors.accent);

    /* 亮度滑块（状态来自g_shell.cc.brightness） */
    slider_y += 50;
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       cx + 20, slider_y, "Brightness", g_shell.colors.panel_fg, 14);
    int bri_fill = 240 * g_shell.cc.brightness / 100;
    int bri_thumb = cx + 20 + bri_fill - 8;
    gfx64_fill_rounded_rect(cx + 20, slider_y + 22, 240, 8, 4, g_shell.colors.border);
    gfx64_fill_rounded_rect(cx + 20, slider_y + 22, bri_fill, 8, 4, g_shell.colors.accent);
    gfx64_fill_rounded_rect(bri_thumb, slider_y + 18, 16, 16, 8, g_shell.colors.accent);
}

/* 控制中心点击处理 */
static void hs_control_center_handle_click(int x, int y)
{
    int cx = 724, cy = 50, cw = 280, ch = 380;
    if (x < cx || x > cx + cw || y < cy || y > cy + ch) return;

    /* 检查toggle点击 */
    int toggle_y = cy + 50;
    for (int i = 0; i < 6; i++) {
        int tx = cx + 20 + (i % 3) * 84;
        int ty = toggle_y + (i / 3) * 60;
        if (x >= tx && x <= tx + 72 && y >= ty && y <= ty + 48) {
            g_shell.cc.toggles[i] = !g_shell.cc.toggles[i];
            return;
        }
    }

    /* 检查音量滑块点击 */
    int slider_y = toggle_y + 140;
    if (x >= cx + 20 && x <= cx + 260 && y >= slider_y + 18 && y <= slider_y + 34) {
        g_shell.cc.volume = (x - cx - 20) * 100 / 240;
        if (g_shell.cc.volume < 0) g_shell.cc.volume = 0;
        if (g_shell.cc.volume > 100) g_shell.cc.volume = 100;
        return;
    }

    /* 检查亮度滑块点击 */
    slider_y += 50;
    if (x >= cx + 20 && x <= cx + 260 && y >= slider_y + 18 && y <= slider_y + 34) {
        g_shell.cc.brightness = (x - cx - 20) * 100 / 240;
        if (g_shell.cc.brightness < 0) g_shell.cc.brightness = 0;
        if (g_shell.cc.brightness > 100) g_shell.cc.brightness = 100;
        return;
    }
}

/* 右键菜单渲染 */
static void hs_context_menu_render(void)
{
    const char *items[] = {"New Window", "Change Theme", "Settings", "About", "---", "Lock Screen"};
    int item_count = 6;
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
        if (items[i][0] == '-' && items[i][1] == '-') {
            gfx64_fill_rect(mx + 12, iy + item_h / 2, menu_w - 24, 1, g_shell.colors.border);
        } else {
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               mx + 16, iy + 8, items[i], g_shell.colors.window_fg, 14);
        }
    }
}

static void hs_context_menu_handle_click(int x, int y)
{
    const char *items[] = {"New Window", "Change Theme", "Settings", "About", "---", "Lock Screen"};
    int item_count = 6;
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
    if (idx >= 0 && idx < item_count && items[idx][0] != '-') {
        switch (idx) {
            case 1:
                if (g_shell.theme == HS_THEME_LIGHT) g_shell.theme = HS_THEME_DARK;
                else g_shell.theme = HS_THEME_LIGHT;
                hs_theme_init(&g_shell.colors, g_shell.theme);
                break;
            case 2: g_pending_app = HS_APP_SETTINGS; break;
            case 3: g_pending_app = HS_APP_ABOUT; break;
            case 5: g_locked = 1; break;
        }
        g_context_menu_visible = 0;
    }
}

/* HepuShell鼠标点击处理（由desktop64主循环调用） */
void hepushell_handle_click(int x, int y)
{
    /* 如果右键菜单可见，优先处理菜单点击 */
    if (g_context_menu_visible) {
        hs_context_menu_handle_click(x, y);
        return;
    }

    /* 窗口关闭按钮检测（从顶层窗口开始） */
    for (int zi = g_wm.z_count - 1; zi >= 0; zi--) {
        int id = g_wm.z_order[zi];
        if (id < 0 || id >= g_wm.window_count) continue;
        hs_window_t *w = &g_wm.windows[id];
        if (!w->visible || w->type != HS_WINDOW_FLOATING) continue;
        int close_x = w->x + w->width - 20;
        int close_y = w->y + 4;
        if (x >= close_x && x <= close_x + 16 && y >= close_y && y <= close_y + 16) {
            w->visible = 0;
            return;
        }
        /* 设置窗口左侧菜单点击 */
        if (w->render == hs_settings_window_render && w->visible) {
            int menu_x = w->x + 16;
            int menu_y = w->y + 36;
            if (x >= menu_x && x <= menu_x + 120 && y >= menu_y) {
                int item = (y - menu_y) / 32;
                if (item >= 0 && item < 4) {
                    g_settings_page = item;
                    return;
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

    hs_control_center_handle_click(x, y);
}

/* HepuShell右键点击处理（显示上下文菜单） */
void hepushell_handle_right_click(int x, int y)
{
    g_context_menu_x = x;
    g_context_menu_y = y;
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

    /* Dock背景（毛玻璃效果） */
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

    /* 应用图标 */
    const char *icons[] = {"T", "F", "B", "S", "E", "C", "N", "A"};
    int icon_count = 8;
    int icon_size = hs_s(g_shell.ui.dock_icon_size);
    int spacing = (dock_w - icon_count * icon_size) / (icon_count + 1);

    for (int i = 0; i < icon_count; i++) {
        int ix = dock_x + spacing + i * (icon_size + spacing);
        int iy = dock_y + (dock_h - icon_size) / 2;

        /* 图标背景 */
        gfx64_fill_rounded_rect(ix, iy, icon_size, icon_size, hs_s(10), g_shell.colors.accent);

        /* 图标符号 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           ix + (icon_size - hs_sx(12)) / 2, iy + hs_sy(10), icons[i], 0x5C4A32, hs_s(20));
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
    for (int zi = g_wm.z_count - 1; zi >= 0; zi--) {
        int id = g_wm.z_order[zi];
        if (id < 0 || id >= g_wm.window_count) continue;
        hs_window_t *w = &g_wm.windows[id];
        if (!w->visible || w->type != HS_WINDOW_FLOATING) continue;
        if (x >= w->x && x <= w->x + w->width &&
            y >= w->y && y <= w->y + 24) {
            int close_x = w->x + w->width - 20;
            if (x >= close_x && x <= close_x + 16) continue;
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

void hepushell_render(void)
{
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
        return;
    }
    if (g_locked == 2) {
        hs_login_screen_render();
        return;
    }

    gfx64_fill_rect(0, 0, g_shell.screen_width, g_shell.screen_height,
                    g_shell.colors.bg);

    /* 通过面板注册表渲染所有启用的面板 */
    hs_panel_render_all();

    hs_window_render_all();
    hs_status_update();
    hs_notification_render();
    hs_launcher_render();

    /* 右键菜单（最顶层） */
    if (g_context_menu_visible) {
        hs_context_menu_render();
    }
}

/* ============================================================================
 * 键盘处理
 * ========================================================================== */

void hepushell_handle_key(int key)
{
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
        return;
    }

    /* Tab键切换侧边栏标签 */
    if (key == 0x0F || key == '\t') {
        g_sidebar_tab = (g_sidebar_tab + 1) % 3;
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

    /* 初始化主题（默认现代风格，作为配置加载的基础） */
    hepushell_set_theme(HS_THEME_LIGHT);

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
    g_shell.ui.cc_margin_right = 300;
    g_shell.ui.cc_y = 50;
    g_shell.ui.cc_width = 280;
    g_shell.ui.cc_height = 380;
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

void hepushell_run(void)
{
    if (!g_shell.initialized) return;

    /* 主循环由外部调用渲染和键盘处理 */
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
                                                  280, 180, 460, 300);
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

    /* 渲染弹出通知（右上角） */
    int popup_y = HS_PANEL_HEIGHT + 8;
    int popup_count = 0;

    for (int i = g_shell.notifications.count - 1; i >= 0 && popup_count < 3; i--) {
        hs_notification_t *n = &g_shell.notifications.notifications[i];
        if (!n->visible) continue;

        int w = 280;
        int h = 60;
        int x = g_shell.screen_width - w - 8;
        int y = popup_y + popup_count * (h + 8);

        /* 背景 */
        uint32_t bg = n->urgency == 2 ? 0xCC3333 : g_shell.colors.window_bg;
        gfx64_fill_rect(x, y, w, h, bg);
        gfx64_draw_rect(x, y, w, h, g_shell.colors.accent);

        /* 标题 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           x + 8, y + 6, n->title,
                           g_shell.colors.window_fg, 12);

        /* 内容 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           x + 8, y + 24, n->body,
                           g_shell.colors.panel_fg, 10);

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
    /* TODO: 从实际系统获取状态 */
    static int tick = 0;
    tick++;
    g_shell.status.cpu_usage = (tick * 7) % 60 + 10;
    g_shell.status.mem_usage = 45 + (tick % 10);
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
