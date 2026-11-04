/* ============================================================================
 * 赫普OS - 应用商店图形界面 (V6.80)
 * 基于app_store.c数据模型，使用vga_gfx绘图API
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "app_store.h"
#include "vga_gfx.h"
#include "string.h"

/* 颜色定义 */
#define STORE_BG_DARK       0x10
#define STORE_BG_LIGHT      0x14
#define STORE_PANEL         0x08
#define STORE_PANEL_LIGHT   0x07
#define STORE_TEXT          0x0F
#define STORE_TEXT_DIM      0x07
#define STORE_ACCENT        0x09
#define STORE_GREEN         0x0A
#define STORE_RED           0x0C
#define STORE_YELLOW        0x0E
#define STORE_ORANGE        0x06

/* 布局常量 */
#define HEADER_HEIGHT       50
#define TAB_HEIGHT          36
#define APP_ICON_SIZE       48
#define APP_CARD_WIDTH      140
#define APP_CARD_HEIGHT     90
#define APP_CARD_SPACING    12
#define APPS_PER_ROW        5

/* 应用商店UI状态 */
typedef enum {
    STORE_VIEW_FEATURED = 0,   /* 精选 */
    STORE_VIEW_CATEGORIES,     /* 分类 */
    STORE_VIEW_UPDATES,        /* 更新 */
    STORE_VIEW_SEARCH,         /* 搜索 */
    STORE_VIEW_DETAIL,         /* 应用详情 */
} store_view_t;

static store_view_t current_view = STORE_VIEW_FEATURED;
static int selected_app_id = -1;
static int scroll_offset = 0;
static int ui_initialized = 0;

/* 应用图标颜色（根据ID分配） */
static const uint8_t app_icon_colors[] = {
    0x09, 0x0A, 0x03, 0x05, 0x0C, 0x0E, 0x06, 0x0B,
    0x09, 0x0A, 0x03, 0x05, 0x0C, 0x0E, 0x06, 0x0B,
};

/* 绘制圆角矩形 */
static void draw_rounded_rect_store(int x, int y, int w, int h, int r, uint8_t color)
{
    gfx_fillrect(x + r, y, w - 2*r, h, color);
    gfx_fillrect(x, y + r, r, h - 2*r, color);
    gfx_fillrect(x + w - r, y + r, r, h - 2*r, color);
    gfx_fillcircle(x + r, y + r, r, color);
    gfx_fillcircle(x + w - r, y + r, r, color);
    gfx_fillcircle(x + r, y + h - r, r, color);
    gfx_fillcircle(x + w - r, y + h - r, r, color);
}

/* 绘制顶部标题栏 */
static void draw_header(void)
{
    gfx_fillrect(0, 0, gfx_width, HEADER_HEIGHT, STORE_PANEL);
    gfx_hline(0, HEADER_HEIGHT - 1, gfx_width, 0x00);

    /* 标题（V7.43 中文化） */
    gfx_puts(16, 16, "赫普应用商店", STORE_TEXT);
    gfx_puts(16, 32, "发现精彩应用", STORE_TEXT_DIM);

    /* 右上角统计 */
    appstore_app_t apps[STORE_MAX_APPS];
    int total = app_store_list_apps(apps, STORE_MAX_APPS);
    int installed = app_store_list_installed(apps, STORE_MAX_APPS);
    int updates = app_store_list_updates(apps, STORE_MAX_APPS);

    char stat_buf[64];
    snprintf(stat_buf, sizeof(stat_buf), "%d个应用 | %d个已安装 | %d个更新",
             total, installed, updates);
    int text_w = strlen(stat_buf) * 8;
    gfx_puts(gfx_width - text_w - 16, 20, stat_buf, STORE_TEXT_DIM);
}

/* 绘制标签栏 */
static void draw_tabs(void)
{
    int tab_y = HEADER_HEIGHT;
    gfx_fillrect(0, tab_y, gfx_width, TAB_HEIGHT, STORE_BG_DARK);

    const char *tabs[] = {"精选", "分类", "更新", "搜索"};
    int tab_width = gfx_width / 4;

    for (int i = 0; i < 4; i++) {
        int tx = i * tab_width;
        if (current_view == i) {
            /* 选中标签 */
            gfx_fillrect(tx, tab_y, tab_width, TAB_HEIGHT - 2, STORE_PANEL);
            gfx_fillrect(tx, tab_y + TAB_HEIGHT - 3, tab_width, 3, STORE_ACCENT);
            gfx_puts(tx + (tab_width - strlen(tabs[i]) * 8) / 2,
                     tab_y + 12, tabs[i], STORE_TEXT);
        } else {
            gfx_puts(tx + (tab_width - strlen(tabs[i]) * 8) / 2,
                     tab_y + 12, tabs[i], STORE_TEXT_DIM);
        }
    }
}

/* 绘制应用卡片 */
static void draw_app_card(int x, int y, appstore_app_t *app, int selected)
{
    /* 卡片背景 */
    draw_rounded_rect_store(x, y, APP_CARD_WIDTH, APP_CARD_HEIGHT, 8,
                            selected ? STORE_PANEL_LIGHT : STORE_PANEL);

    /* 选中边框 */
    if (selected) {
        gfx_rect(x - 1, y - 1, APP_CARD_WIDTH + 2, APP_CARD_HEIGHT + 2, STORE_ACCENT);
    }

    /* 应用图标 */
    uint8_t icon_color = app_icon_colors[app->id % 16];
    draw_rounded_rect_store(x + 8, y + 8, APP_ICON_SIZE, APP_ICON_SIZE, 8, icon_color);

    /* 图标上的首字母 */
    char initial[2] = {app->name[0], 0};
    gfx_puts(x + 8 + (APP_ICON_SIZE - 8) / 2, y + 8 + (APP_ICON_SIZE - 16) / 2,
             initial, STORE_TEXT);

    /* 应用名称（截断） */
    char name_buf[20];
    strncpy(name_buf, app->name, 18);
    name_buf[18] = 0;
    /* 去除首尾空格 */
    char *trimmed = name_buf;
    while (*trimmed == ' ') trimmed++;
    gfx_puts(x + APP_ICON_SIZE + 16, y + 10, trimmed, STORE_TEXT);

    /* 开发者 */
    gfx_puts(x + APP_ICON_SIZE + 16, y + 26, app->author, STORE_TEXT_DIM);

    /* 评分（整数显示，内核snprintf不支持浮点） */
    char rating_buf[16];
    snprintf(rating_buf, sizeof(rating_buf), "%d/5 *", (int)app->rating);
    gfx_puts(x + APP_ICON_SIZE + 16, y + 42, rating_buf, STORE_YELLOW);

    /* 安装状态按钮 */
    int btn_x = x + APP_ICON_SIZE + 16;
    int btn_y = y + 58;
    if (app->installed) {
        if (app->has_update) {
            draw_rounded_rect_store(btn_x, btn_y, 60, 20, 4, STORE_ACCENT);
            gfx_puts(btn_x + 12, btn_y + 5, "更新", STORE_TEXT);
        } else {
            draw_rounded_rect_store(btn_x, btn_y, 60, 20, 4, STORE_PANEL_LIGHT);
            gfx_puts(btn_x + 14, btn_y + 5, "打开", STORE_TEXT_DIM);
        }
    } else {
        draw_rounded_rect_store(btn_x, btn_y, 60, 20, 4, STORE_GREEN);
        gfx_puts(btn_x + 12, btn_y + 5, "安装", STORE_TEXT);
    }
}

/* 绘制精选视图 */
static void draw_featured_view(void)
{
    appstore_app_t apps[STORE_MAX_APPS];
    int count = app_store_list_featured(apps, 10);

    int content_y = HEADER_HEIGHT + TAB_HEIGHT + 16;
    int total_width = APPS_PER_ROW * APP_CARD_WIDTH + (APPS_PER_ROW - 1) * APP_CARD_SPACING;
    int start_x = (gfx_width - total_width) / 2;

    /* "Featured" 标题 */
    gfx_puts(start_x, content_y, "精选应用", STORE_TEXT);
    content_y += 24;

    /* 应用网格 */
    for (int i = 0; i < count && i < 10; i++) {
        int row = i / APPS_PER_ROW;
        int col = i % APPS_PER_ROW;
        int x = start_x + col * (APP_CARD_WIDTH + APP_CARD_SPACING);
        int y = content_y + row * (APP_CARD_HEIGHT + APP_CARD_SPACING);
        draw_app_card(x, y, &apps[i], selected_app_id == apps[i].id);
    }
}

/* 绘分类视图 */
static void draw_categories_view(void)
{
    store_category_info_t cats[STORE_MAX_CATEGORIES];
    int count = app_store_list_categories(cats, STORE_MAX_CATEGORIES);

    int content_y = HEADER_HEIGHT + TAB_HEIGHT + 16;
    gfx_puts(32, content_y, "Categories", STORE_TEXT);
    content_y += 30;

    /* 分类网格 */
    int cat_per_row = 5;
    int cat_width = 120;
    int cat_height = 60;
    int cat_spacing = 16;
    int total_width = cat_per_row * cat_width + (cat_per_row - 1) * cat_spacing;
    int start_x = (gfx_width - total_width) / 2;

    for (int i = 0; i < count && i < 15; i++) {
        int row = i / cat_per_row;
        int col = i % cat_per_row;
        int x = start_x + col * (cat_width + cat_spacing);
        int y = content_y + row * (cat_height + cat_spacing);

        draw_rounded_rect_store(x, y, cat_width, cat_height, 8, STORE_PANEL);

        /* 分类图标（彩色方块） */
        uint8_t cat_color = app_icon_colors[i % 16];
        draw_rounded_rect_store(x + 8, y + 14, 32, 32, 6, cat_color);

        /* 分类名称 */
        char *name = cats[i].name;
        while (*name == ' ') name++;
        gfx_puts(x + 48, y + 22, name, STORE_TEXT);
    }
}

/* 绘制更新视图 */
static void draw_updates_view(void)
{
    appstore_app_t apps[STORE_MAX_APPS];
    int count = app_store_list_updates(apps, STORE_MAX_APPS);

    int content_y = HEADER_HEIGHT + TAB_HEIGHT + 16;

    char title_buf[64];
    snprintf(title_buf, sizeof(title_buf), "%d个可用更新", count);
    gfx_puts(32, content_y, title_buf, STORE_TEXT);
    content_y += 30;

    if (count == 0) {
        gfx_puts(gfx_width / 2 - 70, content_y + 40, "所有应用已是最新！", STORE_GREEN);
        return;
    }

    /* "全部更新" 按钮 */
    draw_rounded_rect_store(gfx_width - 140, content_y - 4, 100, 28, 6, STORE_ACCENT);
    gfx_puts(gfx_width - 120, content_y + 3, "全部更新", STORE_TEXT);

    /* 更新列表 */
    for (int i = 0; i < count && i < 6; i++) {
        int y = content_y + i * 70;
        draw_rounded_rect_store(32, y, gfx_width - 64, 60, 8, STORE_PANEL);

        /* 图标 */
        uint8_t icon_color = app_icon_colors[apps[i].id % 16];
        draw_rounded_rect_store(44, y + 8, 44, 44, 8, icon_color);

        /* 名称和版本 */
        char *name = apps[i].name;
        while (*name == ' ') name++;
        gfx_puts(100, y + 10, name, STORE_TEXT);

        char ver_buf[64];
        snprintf(ver_buf, sizeof(ver_buf), "%s -> %s", apps[i].version, apps[i].new_version);
        gfx_puts(100, y + 28, ver_buf, STORE_TEXT_DIM);

        /* Update按钮 */
        draw_rounded_rect_store(gfx_width - 120, y + 16, 80, 28, 6, STORE_ACCENT);
        gfx_puts(gfx_width - 100, y + 22, "Update", STORE_TEXT);
    }
}

/* 绘制搜索视图 */
static void draw_search_view(void)
{
    int content_y = HEADER_HEIGHT + TAB_HEIGHT + 16;

    /* 搜索框 */
    draw_rounded_rect_store(32, content_y, gfx_width - 64, 36, 8, STORE_PANEL);
    gfx_puts(48, content_y + 11, "搜索应用...", STORE_TEXT_DIM);
    content_y += 50;

    /* 热门搜索 */
    gfx_puts(32, content_y, "热门搜索", STORE_TEXT);
    content_y += 24;

    const char *trending[] = {"浏览器", "音乐", "游戏", "相机", "笔记", "天气"};
    for (int i = 0; i < 6; i++) {
        int tx = 32 + (i % 3) * 180;
        int ty = content_y + (i / 3) * 32;
        draw_rounded_rect_store(tx, ty, 160, 24, 6, STORE_PANEL_LIGHT);
        gfx_puts(tx + 12, ty + 7, trending[i], STORE_TEXT);
    }
}

/* 绘制应用详情视图 */
static void draw_detail_view(void)
{
    if (selected_app_id < 0) {
        current_view = STORE_VIEW_FEATURED;
        return;
    }

    appstore_app_t app;
    if (app_store_get_app(selected_app_id, &app) != 0) {
        current_view = STORE_VIEW_FEATURED;
        return;
    }

    int content_y = HEADER_HEIGHT + 8;

    /* 返回按钮 */
    draw_rounded_rect_store(16, content_y, 60, 28, 6, STORE_PANEL);
    gfx_puts(28, content_y + 7, "< Back", STORE_TEXT);
    content_y += 40;

    /* 应用大图标 */
    uint8_t icon_color = app_icon_colors[app.id % 16];
    draw_rounded_rect_store(32, content_y, 80, 80, 12, icon_color);
    char initial[2] = {app.name[0], 0};
    gfx_puts(32 + (80 - 8) / 2, content_y + (80 - 16) / 2, initial, STORE_TEXT);

    /* 应用信息 */
    char *name = app.name;
    while (*name == ' ') name++;
    gfx_puts(128, content_y + 8, name, STORE_TEXT);
    gfx_puts(128, content_y + 28, app.author, STORE_TEXT_DIM);

    char info_buf[128];
    snprintf(info_buf, sizeof(info_buf), "版本 %s | %.1f分 | %d次下载",
             app.version, app.rating, app.download_count);
    gfx_puts(128, content_y + 48, info_buf, STORE_TEXT_DIM);
    content_y += 100;

    /* 安装/打开按钮 */
    if (app.installed) {
        draw_rounded_rect_store(32, content_y, 120, 36, 8, STORE_GREEN);
        gfx_puts(64, content_y + 11, "打开", STORE_TEXT);
    } else {
        draw_rounded_rect_store(32, content_y, 120, 36, 8, STORE_ACCENT);
        gfx_puts(56, content_y + 11, "安装", STORE_TEXT);
    }
    content_y += 50;

    /* 应用描述 */
    gfx_puts(32, content_y, "应用介绍", STORE_TEXT);
    content_y += 24;

    /* 描述文本（简单换行） */
    char *desc = app.description;
    while (*desc == ' ') desc++;
    int line_len = 60;
    int line = 0;
    while (*desc && line < 4) {
        char line_buf[64];
        int i = 0;
        while (*desc && *desc != '\n' && i < line_len - 1) {
            line_buf[i++] = *desc++;
        }
        line_buf[i] = 0;
        if (*desc == '\n') desc++;
        gfx_puts(32, content_y + line * 18, line_buf, STORE_TEXT_DIM);
        line++;
    }
}

/* 主渲染函数 */
void app_store_render(void)
{
    /* 背景 */
    gfx_gradient_v(0, 0, gfx_width, gfx_height, STORE_BG_DARK, STORE_BG_LIGHT);

    /* 各视图 */
    draw_header();
    draw_tabs();

    switch (current_view) {
        case STORE_VIEW_FEATURED:
            draw_featured_view();
            break;
        case STORE_VIEW_CATEGORIES:
            draw_categories_view();
            break;
        case STORE_VIEW_UPDATES:
            draw_updates_view();
            break;
        case STORE_VIEW_SEARCH:
            draw_search_view();
            break;
        case STORE_VIEW_DETAIL:
            draw_detail_view();
            break;
    }
}

/* 应用商店主循环（V7.25: 添加键盘交互） */
void app_store_main_loop(void)
{
    extern void serial_puts(uint16_t, const char *);
    extern char keyboard_getchar(void);
    extern void vga_gfx_text_mode(void);

    serial_puts(0x3F8, "[APPSTORE] entering app store with keyboard interaction...\n");

    /* 确保应用商店已初始化 */
    if (!ui_initialized) {
        app_store_init();
        ui_initialized = 1;
    }

    /* 当前选中的应用索引（在当前视图列表中） */
    int selected_index = 0;
    int running = 1;

    while (running) {
        /* 根据当前选中索引更新selected_app_id */
        {
            appstore_app_t apps[STORE_MAX_APPS];
            int count = 0;
            switch (current_view) {
                case STORE_VIEW_FEATURED:
                    count = app_store_list_featured(apps, STORE_MAX_APPS);
                    break;
                case STORE_VIEW_CATEGORIES:
                    count = app_store_list_apps(apps, STORE_MAX_APPS);
                    break;
                case STORE_VIEW_UPDATES:
                    count = app_store_list_updates(apps, STORE_MAX_APPS);
                    break;
                default:
                    count = app_store_list_apps(apps, STORE_MAX_APPS);
                    break;
            }
            if (selected_index >= 0 && selected_index < count) {
                selected_app_id = apps[selected_index].id;
            } else {
                selected_app_id = -1;
                if (selected_index >= count && count > 0) {
                    selected_index = count - 1;
                }
            }
        }

        /* 渲染 */
        app_store_render();
        gfx_flip();

        /* 处理键盘输入 */
        char key = keyboard_getchar();
        if (key != 0) {
            switch (key) {
                case 27:  /* ESC - 退出 */
                    serial_puts(0x3F8, "[APPSTORE] ESC pressed, exiting\n");
                    running = 0;
                    break;

                case '\t':  /* Tab - 切换标签页 */
                    current_view = (store_view_t)((current_view + 1) % 4);
                    selected_index = 0;
                    scroll_offset = 0;
                    serial_puts(0x3F8, "[APPSTORE] tab switched\n");
                    break;

                case 'h':  /* H - 左移 */
                case 75:   /* 左方向键（扫描码） */
                    if (selected_index > 0) {
                        selected_index--;
                    }
                    break;

                case 'l':  /* L - 右移 */
                case 77:   /* 右方向键 */
                    selected_index++;
                    break;

                case 'j':  /* J - 下移 */
                case 80:   /* 下方向键 */
                    selected_index += APPS_PER_ROW;
                    break;

                case 'k':  /* K - 上移 */
                case 72:   /* 上方向键 */
                    if (selected_index >= APPS_PER_ROW) {
                        selected_index -= APPS_PER_ROW;
                    }
                    break;

                case '\r':  /* 回车 - 安装/打开应用 */
                case '\n': {
                    /* 获取当前视图的应用列表 */
                    appstore_app_t apps[STORE_MAX_APPS];
                    int count = 0;
                    switch (current_view) {
                        case STORE_VIEW_FEATURED:
                            count = app_store_list_featured(apps, STORE_MAX_APPS);
                            break;
                        case STORE_VIEW_CATEGORIES:
                            count = app_store_list_apps(apps, STORE_MAX_APPS);
                            break;
                        case STORE_VIEW_UPDATES:
                            count = app_store_list_updates(apps, STORE_MAX_APPS);
                            break;
                        default:
                            count = app_store_list_apps(apps, STORE_MAX_APPS);
                            break;
                    }

                    if (selected_index >= 0 && selected_index < count) {
                        appstore_app_t *app = &apps[selected_index];
                        selected_app_id = app->id;

                        if (app->installed) {
                            serial_puts(0x3F8, "[APPSTORE] opening app: ");
                            serial_puts(0x3F8, app->name);
                            serial_puts(0x3F8, "\n");
                            /* 打开应用（演示） */
                        } else {
                            serial_puts(0x3F8, "[APPSTORE] installing app: ");
                            serial_puts(0x3F8, app->name);
                            serial_puts(0x3F8, "\n");
                            /* 安装应用（演示） */
                            app_store_install(app->id);
                        }
                    }
                    break;
                }

                case 'd':  /* D - 查看详情 */
                    if (selected_app_id >= 0) {
                        current_view = STORE_VIEW_DETAIL;
                    }
                    break;

                case '?':  /* ? - 帮助 */
                    serial_puts(0x3F8, "[APPSTORE] keys: Tab=switch view, arrows/HJKL=select, Enter=install/open, D=details, ESC=exit\n");
                    break;
            }
        }

        /* 小延迟，避免CPU占用过高 */
        for (volatile int i = 0; i < 100000; i++);
    }

    /* 退出时返回文本模式 */
    vga_gfx_text_mode();
    serial_puts(0x3F8, "[APPSTORE] done\n");
}

/* 切换视图 */
void app_store_set_view(int view)
{
    if (view >= 0 && view <= 4) {
        current_view = (store_view_t)view;
    }
}
