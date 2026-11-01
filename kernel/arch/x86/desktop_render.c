/* ============================================================================
 * 赫普OS - 现代桌面渲染器 (V6.80 重设计)
 * 现代渐变背景 + 圆角图标 + 半透明状态栏 + Dock栏
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "elastic_desktop.h"
#include "vga_gfx.h"
#include "graphics.h"
#include "string.h"
#include "kstdio.h"
#include "font_antialiased.h"
#include "process.h"

/* ============================================================================
 * 颜色定义 (V6.90 32位真彩色)
 * ========================================================================== */
/* 背景渐变 */
#define RGB_BG_TOP      0x0A0E27  /* 深蓝黑 */
#define RGB_BG_MID      0x1A1F3A  /* 深蓝 */
#define RGB_BG_BOTTOM   0x2D1B4E  /* 深紫 */
/* 面板色 */
#define RGB_PANEL       0x1E1E2E  /* 深灰面板 */
#define RGB_PANEL_LIGHT 0x2A2A3C  /* 浅灰面板 */
#define RGB_PANEL_HOVER 0x3A3A5C  /* 悬停面板 */
/* 文字色 */
#define RGB_TEXT        0xFFFFFF  /* 白色 */
#define RGB_TEXT_DIM    0xA0A0B0  /* 灰色 */
/* 强调色 */
#define RGB_ACCENT      0x4A9EFF  /* 蓝色 */
#define RGB_ACCENT_LIGHT 0x7AB8FF /* 亮蓝 */
/* 图标渐变色 */
#define RGB_ICON_BLUE1   0x4A9EFF
#define RGB_ICON_BLUE2   0x2D7ADB
#define RGB_ICON_GREEN1  0x52C41A
#define RGB_ICON_GREEN2  0x389E08
#define RGB_ICON_CYAN1   0x36CFC9
#define RGB_ICON_CYAN2   0x13A8A8
#define RGB_ICON_PURPLE1 0x9254DE
#define RGB_ICON_PURPLE2 0x722ED1
#define RGB_ICON_RED1    0xFF6B6B
#define RGB_ICON_RED2    0xDB4A4A
#define RGB_ICON_ORANGE1 0xFFA940
#define RGB_ICON_ORANGE2 0xDB8A20
#define RGB_ICON_YELLOW1 0xFFC53D
#define RGB_ICON_YELLOW2 0xDBA920
/* 边框和阴影 */
#define RGB_BORDER      0x3A3A5C
#define RGB_SHADOW      0x000000

/* VGA调色板颜色转RGB */
static inline uint32_t vga_color_to_rgb(uint8_t vga_color)
{
    static const uint32_t vga_palette[16] = {
        0x000000, /* 0: 黑 */
        0x0000AA, /* 1: 蓝 */
        0x00AA00, /* 2: 绿 */
        0x00AAAA, /* 3: 青 */
        0xAA0000, /* 4: 红 */
        0xAA00AA, /* 5: 紫 */
        0xAA5500, /* 6: 棕 */
        0xC0C0C0, /* 7: 浅灰 */
        0x555555, /* 8: 深灰 */
        0x5555FF, /* 9: 亮蓝 */
        0x55FF55, /* 10: 亮绿 */
        0x55FFFF, /* 11: 亮青 */
        0xFF5555, /* 12: 亮红 */
        0xFF55FF, /* 13: 亮紫 */
        0xFFFF55, /* 14: 黄 */
        0xFFFFFF  /* 15: 白 */
    };
    return vga_palette[vga_color & 0x0F];
}

/* V7.41: 计算UTF-8字符串的像素宽度 */
static int utf8_string_width(const char *s, int size)
{
    int width = 0;
    /* V7.94: 中文字符实际advance = tt_size = size * 3 / 2（TrueType全角字符advance_width=units_per_em）
     * 之前使用size导致宽度计算偏小，文字居中偏移错误导致被截断 */
    int cjk_width = size * 3 / 2;
    while (*s) {
        if (*s & 0x80) {
            /* UTF-8多字节字符（中文等），宽度约为size*3/2 */
            width += cjk_width;
            /* 跳过剩余字节 */
            int skip = 1;
            if ((*s & 0xE0) == 0xC0) skip = 1;
            else if ((*s & 0xF0) == 0xE0) skip = 2;
            else if ((*s & 0xF8) == 0xF0) skip = 3;
            s += skip + 1;
        } else {
            /* ASCII字符，宽度约为size/2 */
            width += size / 2;
            s++;
        }
    }
    return width;
}

/* 桌面文字绘制（兼容8位调色板颜色，使用字体管理器） */
static inline void desktop_puts(int x, int y, const char *s, uint8_t color)
{
    /* V7.56: 使用设置的字号，中文字符需要更大字号才能清晰显示 */
    extern uint16_t font_get_size(void);
    uint16_t fsize = font_get_size();
    if (fsize < 20) fsize = 20;  /* V7.91: 增大最小字号，确保中文清晰 */
    if (fsize > 32) fsize = 32;  /* 限制最大字号 */
    gfx_draw_string_font((uint32_t)x, (uint32_t)y, s, vga_color_to_rgb(color), fsize);
}

/* 图标位置缓存（用于点击检测） */
static int icon_positions[8][2]; /* x, y */
static int dock_icon_positions[8][2]; /* Dock栏图标位置 */
static int selected_icon = -1;

/* V6.94: UI缩放因子 (根据分辨率自动调整) */
static int ui_scale = 100;  /* 百分比, 100=正常 */
static void calc_ui_scale(void)
{
    int scale_w = (gfx_width * 100) / 1024;
    int scale_h = (gfx_height * 100) / 768;
    ui_scale = (scale_w < scale_h) ? scale_w : scale_h;
    if (ui_scale < 75) ui_scale = 75;
    if (ui_scale > 200) ui_scale = 200;
}
#define SCALE(x) ((x) * ui_scale / 100)

/* 多窗口支持 */
#define MAX_WINDOWS 4
typedef struct {
    int used;
    int app_id;
    int x, y;
    int z_order;  /* Z轴顺序，越大越上层 */
    int anim_state; /* 0=正常, 1=打开动画, 2=关闭动画 */
    int anim_frame;  /* 动画帧计数 */
    /* V6.98: 窗口状态 */
    int minimized;   /* 1=最小化 */
    int maximized;   /* 1=最大化 */
    int prev_x, prev_y;  /* 最大化前的位置 */
} window_t;
static window_t windows[MAX_WINDOWS];
static int window_count = 0;
static int next_z = 0;
static int dragging_window = -1;
static int drag_offset_x = 0, drag_offset_y = 0;

/* 状态栏高度 */
#define STATUSBAR_HEIGHT    28
/* Dock栏高度 */
#define DOCK_HEIGHT         70
/* 图标大小 */
#define ICON_SIZE           56
#define ICON_RADIUS         8
/* 图标间距 */
#define ICON_SPACING        20

/* 鼠标光标位置 */
static int mouse_x = 0;
static int mouse_y = 0;
static int mouse_visible = 1;

/* 右键菜单状态 */
static int context_menu_visible = 0;
static int context_menu_x = 0;
static int context_menu_y = 0;
static int context_menu_selected = -1;

/* 前向声明 */
static int create_window(int app_id);

/* 右键菜单项（V7.42 中文化） */
static const char *context_menu_items[] = {
    "打开终端",
    "打开文件管理器",
    "---",
    "刷新桌面",
    "关闭所有窗口",
    "---",
    "更换壁纸",
    "显示设置",
    "系统设置",
    "---",
    "重启",
    "关机",
    "---",
    "退出到Shell"
};
#define CONTEXT_MENU_COUNT (sizeof(context_menu_items)/sizeof(context_menu_items[0]))
#define CONTEXT_MENU_WIDTH 180
#define CONTEXT_MENU_ITEM_HEIGHT 24

/* V6.97: 关机/重启确认对话框状态 */
static int power_dialog_visible = 0;
static int power_dialog_type = 0; /* 0=关机, 1=重启 */
static int power_dialog_selected = 0; /* 0=确认, 1=取消 */

/* V8.50: 开始菜单/应用启动器状态 */
static int start_menu_visible = 0;
static char start_menu_search[32] = {0};
static int start_menu_search_focus = 0;
#define START_MENU_WIDTH 320
#define START_MENU_HEIGHT 400
#define START_MENU_ITEM_H 44

/* ============================================================================
 * 右键菜单
 * ========================================================================== */
static void draw_context_menu(void)
{
    if (!context_menu_visible) return;

    int x = context_menu_x;
    int y = context_menu_y;
    int total_h = CONTEXT_MENU_COUNT * CONTEXT_MENU_ITEM_HEIGHT + 8;

    /* 扁平化菜单背景 - 去掉阴影 */
    gfx_fill_rounded_rect(x, y, CONTEXT_MENU_WIDTH, total_h, 6, 0xFFFFFF);
    gfx_fill_rounded_rect(x - 1, y - 1, CONTEXT_MENU_WIDTH + 2, total_h + 2, 7, 0xCBD5E0);
    gfx_fill_rounded_rect(x, y, CONTEXT_MENU_WIDTH, total_h, 6, 0xFFFFFF);

    /* 菜单项 */
    for (int i = 0; i < (int)CONTEXT_MENU_COUNT; i++) {
        int item_y = y + 4 + i * CONTEXT_MENU_ITEM_HEIGHT;

        /* 分隔线 */
        if (context_menu_items[i][0] == '-') {
            gfx_draw_hline(x + 10, item_y + CONTEXT_MENU_ITEM_HEIGHT / 2,
                          CONTEXT_MENU_WIDTH - 20, 0xE2E8F0);
            continue;
        }

        /* 悬停高亮 - 扁平化浅蓝色 */
        if (i == context_menu_selected) {
            gfx_fill_rect(x + 4, item_y, CONTEXT_MENU_WIDTH - 8,
                             CONTEXT_MENU_ITEM_HEIGHT - 2, 0xEBF8FF);
        }

        /* 菜单项文字 */
        desktop_puts(x + 16, item_y + 6, context_menu_items[i],
                 (i == context_menu_selected) ? 0x09 : 0x00);
    }
}

/* 检测右键菜单点击 */
static int hit_test_context_menu(int mx, int my)
{
    if (!context_menu_visible) return -1;
    int x = context_menu_x;
    int y = context_menu_y;
    if (mx < x || mx > x + CONTEXT_MENU_WIDTH ||
        my < y || my > y + CONTEXT_MENU_COUNT * CONTEXT_MENU_ITEM_HEIGHT + 8) {
        return -1;
    }
    int idx = (my - y - 4) / CONTEXT_MENU_ITEM_HEIGHT;
    if (idx < 0 || idx >= (int)CONTEXT_MENU_COUNT) return -1;
    if (context_menu_items[idx][0] == '-') return -1;
    return idx;
}

/* 执行右键菜单项 */
static void execute_context_menu(int idx)
{
    extern void serial_puts(uint16_t, const char *);
    switch (idx) {
        case 0: /* Open Terminal */
            create_window(1);
            serial_puts(0x3F8, "[MENU] Open Terminal\n");
            break;
        case 1: /* Open File Manager */
            create_window(0);
            serial_puts(0x3F8, "[MENU] Open File Manager\n");
            break;
        case 3: /* Refresh Desktop */
            serial_puts(0x3F8, "[MENU] Refresh Desktop\n");
            break;
        case 4: /* Close All Windows */
            for (int i = 0; i < MAX_WINDOWS; i++) {
                windows[i].used = 0;
            }
            window_count = 0;
            serial_puts(0x3F8, "[MENU] Close All Windows\n");
            break;
        case 6: /* Change Wallpaper */
            serial_puts(0x3F8, "[MENU] Change Wallpaper\n");
            break;
        case 7: /* Display Settings */
            create_window(3);
            serial_puts(0x3F8, "[MENU] Display Settings\n");
            break;
        case 8: /* System Settings */
            create_window(3); /* 复用设置窗口 */
            serial_puts(0x3F8, "[MENU] System Settings\n");
            break;
        case 10: /* Restart */
            power_dialog_type = 1;
            power_dialog_visible = 1;
            power_dialog_selected = 0;
            serial_puts(0x3F8, "[MENU] Restart dialog\n");
            break;
        case 11: /* Shut Down */
            power_dialog_type = 0;
            power_dialog_visible = 1;
            power_dialog_selected = 0;
            serial_puts(0x3F8, "[MENU] Shutdown dialog\n");
            break;
        case 13: /* Exit to Shell */
            context_menu_visible = 0;
            extern void vga_gfx_text_mode(void);
            vga_gfx_text_mode();
            extern void sys_reboot_after_gfx(void);
            sys_reboot_after_gfx();
            break;
    }
    context_menu_visible = 0;
}

/* ============================================================================
 * V6.97: 关机/重启确认对话框
 * ========================================================================== */
static void draw_power_dialog(void)
{
    if (!power_dialog_visible) return;

    int dlg_w = 360;
    int dlg_h = 160;
    int dlg_x = (gfx_width - dlg_w) / 2;
    int dlg_y = (gfx_height - dlg_h) / 2;

    /* 半透明遮罩 */
    for (int y = 0; y < gfx_height; y += 2) {
        gfx_draw_hline(0, y, gfx_width, 0x000000);
    }

    /* 对话框背景 - 白色 */
    gfx_fill_rounded_rect(dlg_x, dlg_y, dlg_w, dlg_h, 12, 0xFFFFFF);

    /* 标题栏 */
    gfx_fill_rounded_rect(dlg_x, dlg_y, dlg_w, 40, 12, 0x2D3748);
    gfx_fill_rect(dlg_x, dlg_y + 20, dlg_w, 20, 0x2D3748);

    /* 标题图标和文字（V7.42 中文化） */
    const char *title = (power_dialog_type == 0) ? "关机" : "重启";
    desktop_puts(dlg_x + 20, dlg_y + 14, title, 0x0F);

    /* 提示文字 */
    const char *msg = (power_dialog_type == 0) ?
        "确定要关机吗？" :
        "确定要重启吗？";
    desktop_puts(dlg_x + 30, dlg_y + 65, msg, 0x00);

    /* 按钮 */
    int btn_w = 100;
    int btn_h = 36;
    int btn_y = dlg_y + dlg_h - 55;
    int btn1_x = dlg_x + 40;  /* 确认 */
    int btn2_x = dlg_x + dlg_w - btn_w - 40;  /* 取消 */

    /* 确认按钮 */
    uint32_t confirm_color = (power_dialog_type == 0) ? 0xE53E3E : 0x3182CE;
    if (power_dialog_selected == 0) {
        gfx_fill_rounded_rect(btn1_x - 2, btn_y - 2, btn_w + 4, btn_h + 4, 8, 0x1A202C);
    }
    gfx_fill_rounded_rect(btn1_x, btn_y, btn_w, btn_h, 6, confirm_color);
    desktop_puts(btn1_x + (btn_w - 32) / 2, btn_y + 12,
             (power_dialog_type == 0) ? "关机" : "重启", 0x0F);

    /* 取消按钮 */
    if (power_dialog_selected == 1) {
        gfx_fill_rounded_rect(btn2_x - 2, btn_y - 2, btn_w + 4, btn_h + 4, 8, 0x1A202C);
    }
    gfx_fill_rounded_rect(btn2_x, btn_y, btn_w, btn_h, 6, 0xE2E8F0);
    desktop_puts(btn2_x + (btn_w - 32) / 2, btn_y + 12, "取消", 0x00);
}

/* 处理关机对话框点击 */
static void handle_power_dialog_click(int mx, int my)
{
    if (!power_dialog_visible) return;

    int dlg_w = 360;
    int dlg_h = 160;
    int dlg_x = (gfx_width - dlg_w) / 2;
    int dlg_y = (gfx_height - dlg_h) / 2;
    int btn_w = 100;
    int btn_h = 36;
    int btn_y = dlg_y + dlg_h - 55;
    int btn1_x = dlg_x + 40;
    int btn2_x = dlg_x + dlg_w - btn_w - 40;

    if (mx >= btn1_x && mx <= btn1_x + btn_w &&
        my >= btn_y && my <= btn_y + btn_h) {
        /* 确认关机/重启 */
        power_dialog_visible = 0;
        extern void vga_gfx_text_mode(void);
        vga_gfx_text_mode();
        if (power_dialog_type == 0) {
            extern void machine_poweroff(void);
            machine_poweroff();
        } else {
            extern void machine_reboot(void);
            machine_reboot();
        }
    } else if (mx >= btn2_x && mx <= btn2_x + btn_w &&
               my >= btn_y && my <= btn_y + btn_h) {
        /* 取消 */
        power_dialog_visible = 0;
    }
}

/* ============================================================================
 * 鼠标控制
 * ========================================================================== */
void desktop_set_mouse(int x, int y)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= gfx_width) x = gfx_width - 1;
    if (y >= gfx_height) y = gfx_height - 1;
    mouse_x = x;
    mouse_y = y;
}

void desktop_get_mouse(int *x, int *y)
{
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
}

void desktop_show_mouse(int show)
{
    mouse_visible = show;
}

/* 绘制鼠标光标（箭头形状） */
static void draw_mouse_cursor(void)
{
    if (!mouse_visible) return;
    int x = mouse_x, y = mouse_y;
    static const uint8_t cursor_mask[] = {
        0x01,0x00, 0x03,0x00, 0x07,0x00, 0x0F,0x00,
        0x1F,0x00, 0x3F,0x00, 0x7F,0x00, 0xFF,0x00,
        0xFF,0x01, 0xE7,0x01, 0xC3,0x01, 0x81,0x03,
        0x01,0x03, 0x01,0x06, 0x01,0x0C, 0x00,0x08,
    };
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 11; col++) {
            int byte_idx = row * 2 + (col >= 8 ? 1 : 0);
            int bit_idx = col % 8;
            if (cursor_mask[byte_idx] & (1 << bit_idx)) {
                gfx_put_pixel(x + col, y + row, 0x000000);
                if (col > 0 && row > 0 && col < 10 && row < 15) {
                    int ic = col - 1, ir = row - 1;
                    int ib = ir * 2 + (ic >= 8 ? 1 : 0);
                    if (cursor_mask[ib] & (1 << (ic % 8)))
                        gfx_put_pixel(x + col, y + row, 0xFFFFFF);
                }
            }
        }
    }
}

/* ============================================================================
 * 扁平化背景绘制 (V6.93)
 * ========================================================================== */
static void draw_modern_background(void)
{
    /* 扁平化纯色背景 - 深灰蓝 */
    gfx_fill_rect(0, 0, gfx_width, gfx_height, 0x2D3748);

    /* 顶部细微分隔线 */
    gfx_draw_hline(0, STATUSBAR_HEIGHT, gfx_width, 0x1A202C);
}

/* ============================================================================
 * 顶部状态栏 (V6.93 扁平化)
 * ========================================================================== */
/* 绘制系统托盘图标 */
static void draw_tray_icons(int x, int y)
{
    /* 电池图标 (24x12) */
    int bx = x, by = y + 4;
    /* 电池边框 */
    gfx_draw_hline(bx, by, 22, 0xFFFFFF);
    gfx_draw_hline(bx, by + 11, 22, 0xFFFFFF);
    gfx_draw_vline(bx, by, 12, 0xFFFFFF);
    gfx_draw_vline(bx + 21, by, 12, 0xFFFFFF);
    /* 电池正极 */
    gfx_fill_rect(bx + 22, by + 3, 2, 6, 0xFFFFFF);
    /* 电池电量 (75%) */
    gfx_fill_rect(bx + 2, by + 2, 15, 8, 0x52C41A);

    /* 声音图标 (扬声器) */
    int sx = x + 34, sy = y + 2;
    gfx_fill_rect(sx, sy + 4, 6, 6, 0xFFFFFF);
    gfx_fill_rect(sx + 6, sy + 2, 3, 10, 0xFFFFFF);
    /* 声波 */
    gfx_draw_hline(sx + 11, sy + 4, 2, 0xFFFFFF);
    gfx_draw_hline(sx + 11, sy + 10, 2, 0xFFFFFF);
    gfx_draw_vline(sx + 13, sy + 3, 10, 0xFFFFFF);

    /* 网络图标 (WiFi信号) */
    int nx = x + 60, ny = y + 2;
    /* WiFi弧线 */
    gfx_draw_hline(nx + 2, ny + 10, 12, 0xFFFFFF);  /* 最底层 */
    gfx_draw_hline(nx + 4, ny + 7, 8, 0xFFFFFF);   /* 中间层 */
    gfx_draw_hline(nx + 6, ny + 4, 4, 0xFFFFFF);   /* 最上层 */
    gfx_fill_circle(nx + 8, ny + 12, 2, 0xFFFFFF); /* 中心点 */
}

static void draw_statusbar(void)
{
    /* 扁平化纯色状态栏 - 深灰 */
    gfx_fill_rect(0, 0, gfx_width, STATUSBAR_HEIGHT, 0x1A202C);
    /* 底部分隔线 */
    gfx_draw_hline(0, STATUSBAR_HEIGHT - 1, gfx_width, 0x2D3748);

    /* V8.50: 开始按钮 */
    uint32_t start_btn_color = start_menu_visible ? 0x2D3748 : 0x1A202C;
    gfx_fill_rect(0, 0, 44, STATUSBAR_HEIGHT, start_btn_color);
    /* Hepu logo - 四格窗口图标 */
    gfx_fill_rect(12, 7, 7, 7, 0x4A9EFF);
    gfx_fill_rect(21, 7, 7, 7, 0x4A9EFF);
    gfx_fill_rect(12, 16, 7, 7, 0x4A9EFF);
    gfx_fill_rect(21, 16, 7, 7, 0x4A9EFF);

    /* 左侧：系统名称 + 分辨率/刷新率 */
    /* V7.12: 使用字体管理器，支持抗锯齿和字号缩放 */
    gfx_draw_string_font(52, 6, "Hepu OS", 0xFFFFFF, 16);
    /* V6.96: 显示分辨率和刷新率 */
    extern int gfx_refresh_rate;
    char res_buf[32];
    snprintf(res_buf, sizeof(res_buf), "%dx%d@%dHz", gfx_width, gfx_height, gfx_refresh_rate);
    gfx_draw_string_font(120, 7, res_buf, 0xA0AEC0, 14);

    /* 中间：桌面指示（V7.42 中文化） */
    gfx_draw_string_font(gfx_width / 2 - 20, 7, "桌面", 0xA0AEC0, 14);

    /* 右侧：系统托盘图标 + 实时时间 */
    extern int rtc_get_time(int *h, int *m, int *s, int *d, int *mo, int *y);
    int h, m, s, d, mo, y;
    rtc_get_time(&h, &m, &s, &d, &mo, &y);
    char time_buf[16];
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d", h, m);

    /* 系统托盘图标 (在时间左侧) */
    draw_tray_icons(gfx_width - 160, 6);

    /* 时间 - 字体管理器抗锯齿 */
    gfx_draw_string_font(gfx_width - 60, 7, time_buf, 0xFFFFFF, 16);
}

/* ============================================================================
 * 应用图标绘制 (V6.90 真彩色渐变)
 * ========================================================================== */
typedef struct {
    const char *name;
    uint32_t color1;      /* 渐变起始色 */
    uint32_t color2;      /* 渐变结束色 */
    int symbol_type;
} app_icon_t;

static const app_icon_t app_icons[] = {
    {"文件",    RGB_ICON_BLUE1,   RGB_ICON_BLUE2,   0},
    {"终端",    RGB_ICON_GREEN1,  RGB_ICON_GREEN2,  1},
    {"浏览器",  RGB_ICON_CYAN1,   RGB_ICON_CYAN2,   2},
    {"设置",    RGB_ICON_PURPLE1, RGB_ICON_PURPLE2, 3},
    {"游戏",    RGB_ICON_RED1,    RGB_ICON_RED2,    4},
    {"应用商店",RGB_ICON_BLUE1,   RGB_ICON_PURPLE2, 5},
    {"助手",    RGB_ICON_ORANGE1, RGB_ICON_ORANGE2, 6},
    {"相机",    RGB_ICON_YELLOW1, RGB_ICON_YELLOW2, 7},
    {"系统监控",RGB_ICON_GREEN1,  RGB_ICON_CYAN2,   8},
    {"截图",    RGB_ICON_CYAN1,   RGB_ICON_GREEN2,  9},
    {"音乐",    RGB_ICON_RED1,    RGB_ICON_ORANGE2, 10},
    {"日历",    RGB_ICON_PURPLE1, RGB_ICON_BLUE2,   11},
    {"计算器",  RGB_ICON_ORANGE1, RGB_ICON_YELLOW2, 12},
    {"天气",    RGB_ICON_CYAN1,   RGB_ICON_BLUE2,   13},
    {"备忘录",  RGB_ICON_YELLOW1, RGB_ICON_ORANGE2, 14},
    {"通讯录",  RGB_ICON_GREEN1,  RGB_ICON_BLUE2,   15},
    {"视频",    RGB_ICON_RED1,    RGB_ICON_PURPLE2, 16},
    {"帮助",    RGB_ICON_BLUE1,   RGB_ICON_CYAN2,   17},
    {"向导",    RGB_ICON_PURPLE1, RGB_ICON_BLUE2,   18},
    {"反馈",    RGB_ICON_ORANGE1, RGB_ICON_RED2,     19},
    {"游戏中心",RGB_ICON_RED1,    RGB_ICON_ORANGE2,  20},
    {"画板",    RGB_ICON_PURPLE1, RGB_ICON_RED2,     21},
    {"时钟",    RGB_ICON_CYAN1,   RGB_ICON_BLUE2,    22},
    {"更新",    RGB_ICON_GREEN1,  RGB_ICON_CYAN2,    23},
    {"搜索",    RGB_ICON_ORANGE1, RGB_ICON_YELLOW2,  24},
    {"下载",    RGB_ICON_BLUE1,   RGB_ICON_PURPLE2,  25},
    {"打印",    RGB_ICON_CYAN1,   RGB_ICON_BLUE2,    26},
    {"清理",    RGB_ICON_GREEN1,  RGB_ICON_YELLOW2,  27},
    {"备份",    RGB_ICON_BLUE1,   RGB_ICON_GREEN2,   28},
    {"密码",    RGB_ICON_PURPLE1, RGB_ICON_RED2,     29},
    {"VPN",     RGB_ICON_GREEN1,  RGB_ICON_BLUE2,    30},
    {"远程",    RGB_ICON_BLUE1,   RGB_ICON_PURPLE2,  31},
    {"体检",    RGB_ICON_RED1,    RGB_ICON_ORANGE2,  32},
    {"任务",    RGB_ICON_CYAN1,   RGB_ICON_GREEN2,   33},
    {"记事本",  RGB_ICON_YELLOW1, RGB_ICON_ORANGE2,  34},
    {"画图",    RGB_ICON_PURPLE1, RGB_ICON_BLUE2,    35},
};
#define APP_COUNT (sizeof(app_icons)/sizeof(app_icons[0]))

/* 绘制图标内部符号 (V6.95 优化版 - 简洁白色符号) */
static void draw_icon_symbol(int cx, int cy, int type, uint32_t color_rgb)
{
    int s = 18; /* 符号大小 */
    switch (type) {
        case 0: { /* 文件 - 文件夹 */
            gfx_fill_rect(cx - s/2, cy - s/4, s, s*3/4, color_rgb);
            gfx_fill_rect(cx - s/2, cy - s/2, s*2/5, s/4, color_rgb);
            break;
        }
        case 1: { /* 终端 - 方框+提示符 */
            gfx_draw_rect(cx - s/2, cy - s/2, s, s, color_rgb);
            gfx_draw_line(cx - s/3, cy - s/4, cx - s/6, cy, color_rgb);
            gfx_draw_line(cx - s/6, cy, cx - s/3, cy + s/4, color_rgb);
            gfx_fill_rect(cx, cy + s/4 - 1, s/4, 2, color_rgb);
            break;
        }
        case 2: { /* 浏览器 - 圆形 */
            gfx_fill_circle(cx, cy, s/2, color_rgb);
            break;
        }
        case 3: { /* 设置 - 齿轮 */
            gfx_fill_circle(cx, cy, s/2 - 2, color_rgb);
            static const int gear_offsets[8][2] = {
                {0, -9}, {6, -6}, {9, 0}, {6, 6},
                {0, 9}, {-6, 6}, {-9, 0}, {-6, -6}
            };
            for (int i = 0; i < 8; i++) {
                gfx_fill_rect(cx + gear_offsets[i][0] - 2,
                                 cy + gear_offsets[i][1] - 2, 4, 4, color_rgb);
            }
            break;
        }
        case 4: { /* 游戏 - 手柄 */
            gfx_fill_rect(cx - s/2, cy - 3, s, 6, color_rgb);
            gfx_fill_circle(cx - s/2 + 3, cy + 2, 5, color_rgb);
            gfx_fill_circle(cx + s/2 - 3, cy + 2, 5, color_rgb);
            break;
        }
        case 5: { /* 应用商店 - 购物袋 */
            gfx_fill_rect(cx - s/3, cy - s/4, s*2/3, s*3/4, color_rgb);
            gfx_draw_ellipse(cx, cy - s/4, s/4, s/5, color_rgb);
            break;
        }
        case 6: { /* 助手 - 对话框 */
            gfx_fill_rect(cx - s/2, cy - s/3, s, s*2/3, color_rgb);
            gfx_draw_triangle(cx - 4, cy + s/3, cx + 4, cy + s/3, cx, cy + s/2, color_rgb);
            break;
        }
        case 7: { /* 相机 - 相机机身+镜头 */
            gfx_fill_rect(cx - s/2, cy - s/4, s, s*2/3, color_rgb);
            gfx_fill_rect(cx - s/4, cy - s/2, s/2, s/5, color_rgb);
            gfx_fill_circle(cx, cy + s/8, s/4, color_rgb);
            break;
        }
        case 8: { /* 系统监控 - 柱状图 */
            gfx_fill_rect(cx - s/2 + 1, cy + s/4, s/5, s/4, color_rgb);
            gfx_fill_rect(cx - s/6, cy, s/5, s/2, color_rgb);
            gfx_fill_rect(cx + s/6, cy - s/6, s/5, s*2/3, color_rgb);
            gfx_draw_line(cx - s/2, cy + s/2, cx + s/2, cy + s/2, color_rgb);
            break;
        }
        case 9: { /* 截图 - 屏幕+剪刀 */
            gfx_draw_rect(cx - s/2, cy - s/3, s, s*2/3, color_rgb);
            gfx_fill_rect(cx - s/2 + 2, cy - s/3 + 2, s - 4, s*2/3 - 4, color_rgb);
            /* 剪刀线 */
            gfx_draw_line(cx - s/4, cy + s/3, cx + s/4, cy - s/3, color_rgb);
            break;
        }
        case 10: { /* 音乐 - 音符 */
            /* 音符杆 */
            gfx_fill_rect(cx - 1, cy - s/2, 2, s*3/4, color_rgb);
            /* 音符头（圆形） */
            gfx_fill_circle(cx - s/6, cy + s/4, s/5, color_rgb);
            /* 音符旗 */
            gfx_fill_rect(cx, cy - s/2, s/3, 2, color_rgb);
            gfx_fill_rect(cx + s/3 - 2, cy - s/2, 2, s/4, color_rgb);
            break;
        }
        case 11: { /* 日历 - 日历页 */
            /* 日历主体 */
            gfx_fill_rect(cx - s/2, cy - s/3, s, s*2/3, color_rgb);
            /* 顶部装订条 */
            gfx_fill_rect(cx - s/2, cy - s/3, s, s/6, 0xFFFFFF);
            /* 装订环 */
            gfx_fill_rect(cx - s/3, cy - s/3 - 2, 3, 6, color_rgb);
            gfx_fill_rect(cx + s/3 - 3, cy - s/3 - 2, 3, 6, color_rgb);
            /* 日期数字 */
            gfx_fill_rect(cx - s/6, cy, s/3, s/4, 0xFFFFFF);
            break;
        }
        case 12: { /* 计算器 - 计算器按钮 */
            /* 计算器主体 */
            gfx_fill_rect(cx - s/3, cy - s/2, s*2/3, s, color_rgb);
            /* 显示屏 */
            gfx_fill_rect(cx - s/3 + 3, cy - s/2 + 4, s*2/3 - 6, s/5, 0xFFFFFF);
            /* 按钮网格 */
            for (int row = 0; row < 3; row++) {
                for (int col = 0; col < 3; col++) {
                    gfx_fill_rect(cx - s/3 + 4 + col*(s/5), 
                                  cy - s/2 + s/5 + 8 + row*(s/6), 
                                  s/6, s/8, 0xFFFFFF);
                }
            }
            break;
        }
        case 13: { /* 天气 - 太阳+云 */
            /* 太阳 */
            gfx_fill_circle(cx - s/6, cy - s/6, s/5, 0xFFD700);
            /* 太阳光芒 */
            gfx_fill_rect(cx - s/6 - 1, cy - s/6 - s/4, 2, s/8, 0xFFD700);
            gfx_fill_rect(cx - s/6 - s/4, cy - s/6 - 1, s/8, 2, 0xFFD700);
            /* 云（用多个圆形组合） */
            gfx_fill_circle(cx + s/8, cy + s/8, s/6, 0xFFFFFF);
            gfx_fill_circle(cx + s/4, cy + s/6, s/7, 0xFFFFFF);
            gfx_fill_circle(cx + s/3, cy + s/8, s/8, 0xFFFFFF);
            break;
        }
        case 14: { /* 备忘录 - 便签纸+铅笔 */
            /* 便签纸 */
            gfx_fill_rect(cx - s/3, cy - s/2, s*2/3, s*4/5, color_rgb);
            /* 折角 */
            gfx_fill_rect(cx + s/3 - s/6, cy - s/2, s/6, s/6, 0xFFFFFF);
            /* 横线 */
            for (int i = 0; i < 3; i++) {
                gfx_fill_rect(cx - s/3 + 4, cy - s/2 + s/5 + i*(s/7), s*2/3 - 8, 1, 0xFFFFFF);
            }
            /* 铅笔 */
            gfx_fill_rect(cx + s/4, cy + s/6, s/8, s/4, 0xFFD700);
            gfx_fill_rect(cx + s/4, cy + s/6 + s/4, s/8, s/8, 0x8B4513);
            break;
        }
        case 15: { /* 通讯录 - 人头+电话本 */
            /* 电话本主体 */
            gfx_fill_rect(cx - s/3, cy - s/2, s*2/3, s, color_rgb);
            /* 左侧装订条 */
            gfx_fill_rect(cx - s/3, cy - s/2, s/8, s, 0xFFFFFF);
            /* 装订孔 */
            for (int i = 0; i < 3; i++) {
                gfx_fill_circle(cx - s/3 + s/16, cy - s/2 + s/5 + i*(s/4), 2, color_rgb);
            }
            /* 人头图标（圆形头+矩形身体） */
            gfx_fill_circle(cx + s/12, cy - s/8, s/8, 0xFFFFFF);
            gfx_fill_rect(cx + s/12 - s/6, cy + s/12, s/3, s/4, 0xFFFFFF);
            break;
        }
        case 16: { /* 视频播放器 - 播放按钮 */
            /* 显示器外框 */
            gfx_fill_rect(cx - s/2, cy - s/3, s, s*2/3, color_rgb);
            /* 屏幕 */
            gfx_fill_rect(cx - s/2 + 3, cy - s/3 + 3, s - 6, s*2/3 - 6, 0x1A1A2E);
            /* 播放按钮（三角形） */
            gfx_fill_rect(cx - s/8, cy - s/8, s/4, s/4, 0xFFFFFF);
            /* 底座 */
            gfx_fill_rect(cx - s/6, cy + s/3, s/3, s/8, color_rgb);
            break;
        }
        case 17: { /* 帮助中心 - 问号+书本 */
            /* 圆形背景 */
            gfx_fill_circle(cx, cy, s/2 - 2, color_rgb);
            /* 问号 */
            gfx_fill_rect(cx - 4, cy - s/5, 8, 3, 0xFFFFFF);
            gfx_fill_rect(cx - 4, cy - s/5, 3, s/5, 0xFFFFFF);
            gfx_fill_rect(cx + 1, cy - s/10, 3, s/10, 0xFFFFFF);
            gfx_fill_circle(cx, cy + s/5, 3, 0xFFFFFF);
            break;
        }
        case 18: { /* 设置向导 - 魔法棒+星星 */
            /* 魔法棒主体 */
            gfx_fill_rect(cx - s/6, cy - s/3, s/8, s*2/3, color_rgb);
            /* 魔法棒头部（星星） */
            gfx_fill_circle(cx - s/12, cy - s/3, s/8, 0xFFD700);
            /* 星星光芒 */
            gfx_fill_rect(cx - s/12 - 1, cy - s/3 - s/6, 2, s/8, 0xFFD700);
            gfx_fill_rect(cx - s/12 - s/6, cy - s/3 - 1, s/8, 2, 0xFFD700);
            break;
        }
        case 19: { /* 反馈中心 - 对话气泡+感叹号 */
            /* 对话气泡 */
            gfx_fill_rect(cx - s/3, cy - s/3, s*2/3, s/2, color_rgb);
            /* 气泡尖角 */
            gfx_fill_rect(cx - s/6, cy + s/6, s/6, s/8, color_rgb);
            /* 感叹号 */
            gfx_fill_rect(cx - 2, cy - s/6, 4, s/4, 0xFFFFFF);
            gfx_fill_circle(cx, cy + s/8, 3, 0xFFFFFF);
            break;
        }
        case 20: { /* 游戏中心 - 游戏手柄 */
            /* 手柄主体 */
            gfx_fill_rect(cx - s/2, cy - s/6, s, s/3, color_rgb);
            /* 左右握把 */
            gfx_fill_circle(cx - s/2 + s/8, cy, s/6, color_rgb);
            gfx_fill_circle(cx + s/2 - s/8, cy, s/6, color_rgb);
            /* 方向键（十字） */
            gfx_fill_rect(cx - s/4 - 1, cy - s/12, 2, s/6, 0xFFFFFF);
            gfx_fill_rect(cx - s/4 - s/12, cy - 1, s/6, 2, 0xFFFFFF);
            /* 按钮（ABXY） */
            gfx_fill_circle(cx + s/6, cy - s/12, 3, 0xFFD700);
            gfx_fill_circle(cx + s/4, cy, 3, 0xE94560);
            break;
        }
        case 21: { /* 画板 - 调色板+画笔 */
            /* 调色板主体 */
            gfx_fill_rect(cx - s/3, cy - s/3, s*2/3, s*2/3, color_rgb);
            /* 拇指孔 */
            gfx_fill_circle(cx + s/6, cy - s/6, s/10, 0xFFFFFF);
            /* 颜料点 */
            gfx_fill_circle(cx - s/6, cy - s/6, 4, 0xE94560);
            gfx_fill_circle(cx - s/6, cy, 4, 0x4CAF50);
            gfx_fill_circle(cx, cy + s/6, 4, 0x4A90D9);
            /* 画笔 */
            gfx_fill_rect(cx + s/4, cy - s/4, s/8, s/2, 0x8B4513);
            gfx_fill_rect(cx + s/4, cy + s/4, s/8, s/8, 0xFFD700);
            break;
        }
        case 22: { /* 时钟 - 圆形表盘+指针 */
            /* 表盘外圈 */
            gfx_fill_circle(cx, cy, s/2 - 2, color_rgb);
            /* 表盘内圈 */
            gfx_fill_circle(cx, cy, s/2 - 6, 0xFFFFFF);
            /* 时针（指向10点方向） */
            gfx_fill_rect(cx - 2, cy - s/5, 4, s/4, 0x333333);
            /* 分针（指向2点方向） */
            gfx_fill_rect(cx + s/8, cy - 2, s/4, 4, 0x333333);
            /* 中心点 */
            gfx_fill_circle(cx, cy, 3, 0xE94560);
            break;
        }
        case 23: { /* 系统更新 - 下载箭头+进度 */
            /* 圆形背景 */
            gfx_fill_circle(cx, cy, s/2 - 2, color_rgb);
            /* 向下箭头 */
            gfx_fill_rect(cx - 2, cy - s/4, 4, s/3, 0xFFFFFF);
            /* 箭头头部 */
            gfx_fill_rect(cx - s/8, cy + s/12, s/4, 3, 0xFFFFFF);
            gfx_fill_rect(cx - s/12, cy + s/12 + 3, s/6, 3, 0xFFFFFF);
            gfx_fill_rect(cx - 2, cy + s/12 + 6, 4, 3, 0xFFFFFF);
            break;
        }
        case 24: { /* 文件搜索 - 放大镜+文件 */
            /* 放大镜圆圈 */
            gfx_fill_circle(cx - s/8, cy - s/8, s/4, color_rgb);
            gfx_fill_circle(cx - s/8, cy - s/8, s/5, 0xFFFFFF);
            /* 放大镜手柄 */
            gfx_fill_rect(cx + s/12, cy + s/12, s/6, 4, color_rgb);
            /* 文件图标 */
            gfx_fill_rect(cx + s/6, cy - s/4, s/4, s/3, 0xFFFFFF);
            gfx_fill_rect(cx + s/6, cy - s/4, s/8, s/8, color_rgb);
            break;
        }
        case 25: { /* 下载管理器 - 向下箭头+托盘 */
            /* 圆形背景 */
            gfx_fill_circle(cx, cy, s/2 - 2, color_rgb);
            /* 向下箭头 */
            gfx_fill_rect(cx - 2, cy - s/4, 4, s/3, 0xFFFFFF);
            /* 箭头头部 */
            gfx_fill_rect(cx - s/8, cy + s/12, s/4, 3, 0xFFFFFF);
            gfx_fill_rect(cx - s/12, cy + s/12 + 3, s/6, 3, 0xFFFFFF);
            /* 托盘 */
            gfx_fill_rect(cx - s/5, cy + s/4, s*2/5, 3, 0xFFFFFF);
            break;
        }
        case 26: { /* 打印管理 - 打印机 */
            /* 打印机主体 */
            gfx_fill_rect(cx - s/3, cy - s/6, s*2/3, s/2, color_rgb);
            /* 进纸口 */
            gfx_fill_rect(cx - s/4, cy - s/4, s/2, s/8, 0xFFFFFF);
            /* 出纸口 */
            gfx_fill_rect(cx - s/4, cy + s/4, s/2, s/6, 0xFFFFFF);
            /* 纸张 */
            gfx_fill_rect(cx - s/5, cy + s/3, s*2/5, s/6, 0xFFFFFF);
            /* 按钮 */
            gfx_fill_circle(cx + s/4, cy, 3, 0x4CAF50);
            break;
        }
        case 27: { /* 磁盘清理 - 扫帚+磁盘 */
            /* 磁盘 */
            gfx_fill_circle(cx, cy, s/2 - 2, color_rgb);
            gfx_fill_circle(cx, cy, s/4, 0xFFFFFF);
            gfx_fill_circle(cx, cy, s/8, color_rgb);
            /* 扫帚 */
            gfx_fill_rect(cx + s/6, cy - s/3, 3, s/2, 0x8B4513);
            gfx_fill_rect(cx + s/8, cy + s/6, s/4, s/6, 0xDEB887);
            break;
        }
        case 28: { /* 备份恢复 - 盾牌+箭头 */
            /* 盾牌 */
            gfx_fill_rect(cx - s/3, cy - s/3, s*2/3, s*2/3, color_rgb);
            gfx_fill_rect(cx - s/4, cy - s/4, s/2, s/2, 0xFFFFFF);
            /* 循环箭头 */
            gfx_fill_rect(cx - s/6, cy - s/8, s/3, 3, color_rgb);
            gfx_fill_rect(cx - s/6, cy - s/8, 3, s/4, color_rgb);
            gfx_fill_rect(cx + s/12, cy, s/4, 3, color_rgb);
            gfx_fill_rect(cx + s/4, cy - s/8, 3, s/4, color_rgb);
            break;
        }
        case 29: { /* 密码管理器 - 锁 */
            /* 锁体 */
            gfx_fill_rect(cx - s/4, cy - s/8, s/2, s/2, color_rgb);
            /* 锁梁 */
            gfx_fill_rect(cx - s/5, cy - s/3, 3, s/4, color_rgb);
            gfx_fill_rect(cx + s/5 - 3, cy - s/3, 3, s/4, color_rgb);
            gfx_fill_rect(cx - s/5, cy - s/3, s*2/5, 3, color_rgb);
            /* 钥匙孔 */
            gfx_fill_circle(cx, cy + s/8, s/10, 0xFFFFFF);
            gfx_fill_rect(cx - 2, cy + s/8, 4, s/8, 0xFFFFFF);
            break;
        }
        case 30: { /* VPN客户端 - 地球+盾牌 */
            /* 地球 */
            gfx_fill_circle(cx, cy, s/2 - 2, color_rgb);
            /* 经纬线 */
            gfx_fill_rect(cx - s/3, cy - 1, s*2/3, 2, 0xFFFFFF);
            gfx_fill_rect(cx - 1, cy - s/3, 2, s*2/3, 0xFFFFFF);
            gfx_fill_rect(cx - s/4, cy - s/4, s/2, 2, 0xFFFFFF);
            /* 盾牌 */
            gfx_fill_rect(cx - s/6, cy - s/6, s/3, s/3, 0xFFFFFF);
            gfx_fill_rect(cx - s/8, cy - s/8, s/4, s/4, color_rgb);
            break;
        }
        case 31: { /* 远程桌面 - 显示器+箭头 */
            /* 显示器 */
            gfx_fill_rect(cx - s/3, cy - s/3, s*2/3, s/2, color_rgb);
            gfx_fill_rect(cx - s/4, cy - s/4, s/2, s/3, 0xFFFFFF);
            /* 底座 */
            gfx_fill_rect(cx - s/8, cy + s/6, s/4, 3, color_rgb);
            gfx_fill_rect(cx - s/5, cy + s/6 + 3, s*2/5, 3, color_rgb);
            /* 箭头 */
            gfx_fill_rect(cx - s/6, cy - s/6, s/3, 3, color_rgb);
            gfx_fill_rect(cx + s/8, cy - s/4, 3, s/4, color_rgb);
            break;
        }
        case 32: { /* 系统体检 - 心形+心跳 */
            /* 心形 */
            gfx_fill_circle(cx - s/6, cy - s/8, s/5, color_rgb);
            gfx_fill_circle(cx + s/6, cy - s/8, s/5, color_rgb);
            gfx_fill_rect(cx - s/4, cy - s/8, s/2, s/4, color_rgb);
            /* 心形底部尖角 */
            gfx_fill_rect(cx - s/8, cy + s/8, s/4, s/8, color_rgb);
            /* 心跳线 */
            gfx_fill_rect(cx - s/4, cy, s/8, 2, 0xFFFFFF);
            gfx_fill_rect(cx - s/8, cy - s/12, s/8, 2, 0xFFFFFF);
            gfx_fill_rect(cx, cy + s/12, s/8, 2, 0xFFFFFF);
            gfx_fill_rect(cx + s/8, cy, s/8, 2, 0xFFFFFF);
            break;
        }
        case 33: { /* 任务管理器 - 柱状图+列表 */
            /* 背景 */
            gfx_fill_rect(cx - s/3, cy - s/3, s*2/3, s*2/3, color_rgb);
            /* 柱状图 */
            gfx_fill_rect(cx - s/5, cy - s/6, s/8, s/3, 0xFFFFFF);
            gfx_fill_rect(cx - s/12, cy - s/3, s/8, s*5/12, 0xFFFFFF);
            gfx_fill_rect(cx + s/12, cy - s/4, s/8, s*7/12, 0xFFFFFF);
            gfx_fill_rect(cx + s/5, cy - s/6, s/8, s/3, 0xFFFFFF);
            break;
        }
        case 34: { /* 记事本增强 - 笔记本+铅笔 */
            /* 笔记本 */
            gfx_fill_rect(cx - s/3, cy - s/3, s*2/3, s*2/3, color_rgb);
            gfx_fill_rect(cx - s/4, cy - s/4, s/2, s*5/12, 0xFFFFFF);
            /* 横线 */
            gfx_fill_rect(cx - s/5, cy - s/6, s*2/5, 2, 0xCCCCCC);
            gfx_fill_rect(cx - s/5, cy - s/12, s*2/5, 2, 0xCCCCCC);
            gfx_fill_rect(cx - s/5, cy, s*2/5, 2, 0xCCCCCC);
            /* 铅笔 */
            gfx_fill_rect(cx + s/6, cy - s/4, 3, s/3, 0x8B4513);
            gfx_fill_rect(cx + s/6, cy + s/12, 3, s/12, 0xDEB887);
            break;
        }
        case 35: { /* 图片编辑器 - 调色板+画笔 */
            /* 调色板 */
            gfx_fill_rect(cx - s/3, cy - s/4, s*2/3, s/2, color_rgb);
            /* 颜色圆点 */
            gfx_fill_circle(cx - s/5, cy - s/12, s/12, 0xE94560);
            gfx_fill_circle(cx, cy - s/12, s/12, 0xFAAD14);
            gfx_fill_circle(cx + s/5, cy - s/12, s/12, 0x52C41A);
            gfx_fill_circle(cx - s/5, cy + s/8, s/12, 0x1890FF);
            gfx_fill_circle(cx, cy + s/8, s/12, 0x722ED1);
            /* 画笔 */
            gfx_fill_rect(cx + s/4, cy - s/3, 3, s/3, 0x8B4513);
            gfx_fill_rect(cx + s/4 - 2, cy - s/3 - 4, 7, 6, 0xDEB887);
            break;
        }
    }
}

/* V8.50: 绘制开始菜单/应用启动器 */
static void draw_start_menu(void)
{
    if (!start_menu_visible) return;

    int x = 0;
    int y = STATUSBAR_HEIGHT;
    int w = START_MENU_WIDTH;
    int h = START_MENU_HEIGHT;

    /* 菜单背景 - 深色 */
    gfx_fill_rect(x, y, w, h, 0x1A202C);
    gfx_draw_rect(x, y, w, h, 0x2D3748);

    /* 搜索框 */
    int search_y = y + 12;
    gfx_fill_rounded_rect(x + 12, search_y, w - 24, 32, 6, 0x2D3748);
    gfx_draw_rounded_rect(x + 12, search_y, w - 24, 32, 6, 0x4A5568);
    /* 搜索图标 */
    gfx_draw_circle(x + 28, search_y + 16, 6, 0xA0AEC0);
    gfx_draw_line(x + 32, search_y + 20, x + 38, search_y + 26, 0xA0AEC0);
    /* 搜索文字 */
    if (start_menu_search[0]) {
        gfx_draw_string_font(x + 44, search_y + 8, start_menu_search, 0xFFFFFF, 16);
    } else {
        gfx_draw_string_font(x + 44, search_y + 8, "搜索应用...", 0x718096, 16);
    }

    /* 应用列表标题 */
    int list_y = search_y + 44;
    gfx_draw_string_font(x + 16, list_y, "所有应用", 0xA0AEC0, 14);
    gfx_draw_hline(x + 12, list_y + 20, w - 24, 0x2D3748);

    /* 应用列表 */
    int item_y = list_y + 28;
    int visible_count = (h - (item_y - y) - 60) / START_MENU_ITEM_H;
    if (visible_count > APP_COUNT) visible_count = APP_COUNT;

    for (int i = 0; i < visible_count; i++) {
        const app_icon_t *app = &app_icons[i];
        int iy = item_y + i * START_MENU_ITEM_H;

        /* 搜索过滤 */
        if (start_menu_search[0]) {
            int match = 0;
            const char *name = app->name;
            const char *search = start_menu_search;
            int name_len = 0;
            while (name[name_len]) name_len++;
            int search_len = 0;
            while (search[search_len]) search_len++;
            if (search_len <= name_len) {
                for (int j = 0; j <= name_len - search_len; j++) {
                    int found = 1;
                    for (int k = 0; k < search_len; k++) {
                        if (name[j + k] != search[k]) { found = 0; break; }
                    }
                    if (found) { match = 1; break; }
                }
            }
            if (!match) continue;
        }

        /* 应用项背景 */
        gfx_fill_rect(x + 8, iy, w - 16, START_MENU_ITEM_H - 4, 0x2D3748);

        /* 应用图标 */
        int icon_x = x + 20;
        int icon_cx = icon_x + 16;
        int icon_cy = iy + START_MENU_ITEM_H / 2 - 2;
        gfx_fill_rounded_rect(icon_x, iy + 6, 32, 32, 6, app->color1);
        draw_icon_symbol(icon_cx, icon_cy, app->symbol_type, 0xFFFFFF);

        /* 应用名称 */
        gfx_draw_string_font(x + 64, iy + 14, app->name, 0xFFFFFF, 16);
    }

    /* 底部分隔线 */
    int bottom_y = y + h - 52;
    gfx_draw_hline(x + 12, bottom_y, w - 24, 0x2D3748);

    /* 底部按钮：关机/重启 */
    gfx_fill_rounded_rect(x + 16, bottom_y + 10, (w - 40) / 2, 32, 6, 0xC53030);
    gfx_draw_string_font(x + 16 + 30, bottom_y + 18, "关机", 0xFFFFFF, 16);
    gfx_fill_rounded_rect(x + 16 + (w - 40) / 2 + 8, bottom_y + 10, (w - 40) / 2, 32, 6, 0x3182CE);
    gfx_draw_string_font(x + 16 + (w - 40) / 2 + 8 + 30, bottom_y + 18, "重启", 0xFFFFFF, 16);
}

/* 绘制单个应用图标 (V6.95 优化版 - 渐变+高光+精细符号) */
static void draw_app_icon_modern(int x, int y, const app_icon_t *app, int selected)
{
    int icon_size = ICON_SIZE;
    int icon_radius = 12; /* 更大圆角 */
    int offset_x = 0, offset_y = 0;

    /* 悬停放大效果 */
    if (selected) {
        icon_size = ICON_SIZE + 8;
        icon_radius = 14;
        offset_x = -4;
        offset_y = -4;
    }

    int ix = x + offset_x;
    int iy = y + offset_y;

    /* 图标阴影 */
    gfx_fill_rounded_rect(ix + 2, iy + 3, icon_size, icon_size, icon_radius, 0x000000);

    /* 图标背景 - 垂直渐变 (从上到下轻微变亮) */
    for (int row = 0; row < icon_size; row++) {
        int t = row * 100 / icon_size;
        uint32_t c1 = app->color1;
        uint32_t c2 = app->color2;
        uint8_t r = ((c1 >> 16) & 0xFF) + (((c2 >> 16) & 0xFF) - ((c1 >> 16) & 0xFF)) * t / 100;
        uint8_t g = ((c1 >> 8) & 0xFF) + (((c2 >> 8) & 0xFF) - ((c1 >> 8) & 0xFF)) * t / 100;
        uint8_t b = (c1 & 0xFF) + ((c2 & 0xFF) - (c1 & 0xFF)) * t / 100;
        uint32_t row_color = (r << 16) | (g << 8) | b;
        /* 用圆角矩形的每一行 */
        int clip_r = icon_radius;
        int row_y = iy + row;
        int left = ix, right = ix + icon_size;
        if (row < clip_r) {
            int d = clip_r - row;
            left += d;
            right -= d;
        } else if (row >= icon_size - clip_r) {
            int d = row - (icon_size - clip_r) + 1;
            left += d;
            right -= d;
        }
        if (left < right) {
            gfx_fill_rect(left, row_y, right - left, 1, row_color);
        }
    }

    /* 顶部高光 - 微妙的白色渐变 */
    for (int row = 0; row < icon_size / 3; row++) {
        int alpha = 40 - row * 40 / (icon_size / 3);
        if (alpha > 0) {
            int clip_r = icon_radius;
            int row_y = iy + row;
            int left = ix + 2, right = ix + icon_size - 2;
            if (row < clip_r) {
                int d = clip_r - row;
                left += d;
                right -= d;
            }
            if (left < right) {
                /* 用半透明白色模拟高光 */
                gfx_fill_rect(left, row_y, right - left, 1, 0xFFFFFF);
            }
        }
    }

    /* 选中状态 - 发光边框 */
    if (selected) {
        gfx_fill_rounded_rect(ix - 3, iy - 3, icon_size + 6, icon_size + 6, icon_radius + 3, 0xFFFFFF);
        gfx_fill_rounded_rect(ix - 2, iy - 2, icon_size + 4, icon_size + 4, icon_radius + 2, app->color1);
    }

    /* 图标符号 - 白色 */
    int sym_x = ix + icon_size / 2;
    int sym_y = iy + icon_size / 2 - 2;
    draw_icon_symbol(sym_x, sym_y, app->symbol_type, 0xFFFFFF);

    /* 图标名称 - 白色带阴影 */
    /* V7.41: 使用UTF-8字符串宽度计算文字居中
     * V7.91: 使用实际字号计算宽度，避免字号不匹配导致文字溢出 */
    extern uint16_t font_get_size(void);
    uint16_t label_size = font_get_size();
    if (label_size < 20) label_size = 20;
    if (label_size > 32) label_size = 32;
    int text_w = utf8_string_width(app->name, label_size);
    int text_x = x + (ICON_SIZE - text_w) / 2;
    desktop_puts(text_x + 1, y + ICON_SIZE + 7, app->name, 0x00); /* 阴影 */
    desktop_puts(text_x, y + ICON_SIZE + 6, app->name, 0x0F); /* 白色 */
}

/* ============================================================================
 * V6.98: UI组件库 (按钮/输入框/复选框)
 * ========================================================================== */

/* 绘制按钮 */
static void ui_draw_button(int x, int y, int w, int h, const char *label, int hovered, int pressed)
{
    uint32_t bg_color, border_color, text_color;
    if (pressed) {
        bg_color = 0x2B6CB0;
        border_color = 0x1A365D;
        text_color = 0xFFFFFF;
    } else if (hovered) {
        bg_color = 0x4299E1;
        border_color = 0x2B6CB0;
        text_color = 0xFFFFFF;
    } else {
        bg_color = 0x3182CE;
        border_color = 0x2B6CB0;
        text_color = 0xFFFFFF;
    }
    gfx_fill_rounded_rect(x - 1, y - 1, w + 2, h + 2, 6, border_color);
    gfx_fill_rounded_rect(x, y, w, h, 5, bg_color);
    /* 文字居中 */
    int text_w = 0;
    while (label[text_w]) text_w++;
    desktop_puts(x + (w - text_w * 8) / 2, y + (h - 16) / 2 + 2, label, 0x0F);
}

/* 绘制输入框 */
static void ui_draw_input(int x, int y, int w, int h, const char *text, int focused, int cursor_pos)
{
    /* 背景 */
    gfx_fill_rounded_rect(x - 1, y - 1, w + 2, h + 2, 4, focused ? 0x3182CE : 0xCBD5E0);
    gfx_fill_rect(x, y, w, h, 0xFFFFFF);
    /* 文字 */
    desktop_puts(x + 6, y + (h - 16) / 2 + 2, text, 0x00);
    /* 光标 */
    if (focused) {
        int cursor_x = x + 6 + cursor_pos * 8;
        gfx_fill_rect(cursor_x, y + 4, 1, h - 8, 0x1A202C);
    }
}

/* 绘制复选框 */
static void ui_draw_checkbox(int x, int y, const char *label, int checked)
{
    /* 方框 */
    gfx_fill_rounded_rect(x, y, 16, 16, 3, 0xCBD5E0);
    gfx_fill_rect(x + 1, y + 1, 14, 14, 0xFFFFFF);
    if (checked) {
        gfx_fill_rect(x + 3, y + 3, 10, 10, 0x3182CE);
    }
    /* 标签 */
    desktop_puts(x + 22, y + 2, label, 0x00);
}

/* 绘制滑块 */
static void ui_draw_slider(int x, int y, int w, int value, int min, int max)
{
    /* 轨道 */
    gfx_fill_rounded_rect(x, y + 6, w, 4, 2, 0xE2E8F0);
    /* 已填充部分 */
    int fill_w = (w - 16) * (value - min) / (max - min);
    gfx_fill_rounded_rect(x, y + 6, fill_w + 8, 4, 2, 0x3182CE);
    /* 滑块 */
    int knob_x = x + fill_w;
    gfx_fill_rounded_rect(knob_x, y, 16, 16, 8, 0x3182CE);
}

/* V8.49: 绘制进度条 */
static void ui_draw_progress(int x, int y, int w, int h, int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    /* 背景 */
    gfx_fill_rounded_rect(x, y, w, h, h/2, 0xE2E8F0);
    /* 已填充部分 - 根据百分比变色 */
    uint32_t bar_color = 0x52C41A; /* 绿色 */
    if (percent > 70) bar_color = 0xFAAD14; /* 黄色警告 */
    if (percent > 90) bar_color = 0xFF6B6B; /* 红色危险 */
    int fill_w = (w - 4) * percent / 100;
    if (fill_w > 0) {
        gfx_fill_rounded_rect(x + 2, y + 2, fill_w, h - 4, (h-4)/2, bar_color);
    }
}

/* ============================================================================
 * 底部Dock栏 (V6.90 真彩色毛玻璃)
 * ========================================================================== */
static void draw_dock_modern(void)
{
    int dock_y = gfx_height - DOCK_HEIGHT - 10;
    int dock_w = (int)APP_COUNT * (ICON_SIZE + 16) + 20;
    int dock_x = (gfx_width - dock_w) / 2;

    /* Dock背景 - 扁平化纯色 */
    gfx_fill_rounded_rect(dock_x, dock_y, dock_w, DOCK_HEIGHT, 14, 0x1A202C);

    /* Dock图标 - 优化版 */
    for (int i = 0; i < (int)APP_COUNT; i++) {
        int ix = dock_x + 10 + i * (ICON_SIZE + 16);
        int iy = dock_y + (DOCK_HEIGHT - ICON_SIZE) / 2 - 4;
        dock_icon_positions[i][0] = ix;
        dock_icon_positions[i][1] = iy;
        const app_icon_t *app = &app_icons[i];
        /* 渐变背景 */
        gfx_fill_rounded_rect(ix, iy + 1, ICON_SIZE - 2, ICON_SIZE - 2, 10, app->color1);
        /* 符号 */
        draw_icon_symbol(ix + ICON_SIZE / 2 - 1, iy + ICON_SIZE / 2 - 2, app->symbol_type, 0xFFFFFF);

        /* V6.98: 窗口打开状态指示点 */
        int has_window = 0;
        int has_minimized = 0;
        for (int w = 0; w < MAX_WINDOWS; w++) {
            if (windows[w].used && windows[w].app_id == i) {
                has_window = 1;
                if (windows[w].minimized) has_minimized = 1;
            }
        }
        if (has_window) {
            uint32_t dot_color = has_minimized ? 0xFAAD14 : 0x52C41A;
            gfx_fill_rect(ix + ICON_SIZE / 2 - 2, iy + ICON_SIZE + 2, 4, 4, dot_color);
        }
    }
}

/* ============================================================================
 * 桌面主渲染
 * ========================================================================== */

/* 计算图标位置 */
static void calc_icon_positions(void)
{
    int icon_size = SCALE(ICON_SIZE);
    int icon_spacing = SCALE(ICON_SPACING);
    int area_top = STATUSBAR_HEIGHT + 30;
    int area_bottom = gfx_height - DOCK_HEIGHT - 40;
    int area_height = area_bottom - area_top;
    int icons_per_row = 4;
    /* V7.94: 增大行间距，给图标下方文字留出足够空间（字号20+边距约需36像素） */
    int row_height = icon_size + 42;
    int total_width = icons_per_row * (icon_size + icon_spacing) - icon_spacing;
    int start_x = (gfx_width - total_width) / 2;
    int start_y = area_top + (area_height - 2 * row_height) / 2;

    for (int i = 0; i < (int)APP_COUNT && i < 8; i++) {
        int row = i / icons_per_row;
        int col = i % icons_per_row;
        icon_positions[i][0] = start_x + col * (icon_size + icon_spacing);
        icon_positions[i][1] = start_y + row * row_height;
    }
}

/* 检测点击的图标 */
static int hit_test_icon(int mx, int my)
{
    int icon_size = SCALE(ICON_SIZE);
    for (int i = 0; i < (int)APP_COUNT && i < 8; i++) {
        int ix = icon_positions[i][0];
        int iy = icon_positions[i][1];
        if (mx >= ix && mx <= ix + icon_size &&
            my >= iy && my <= iy + icon_size) {
            return i;
        }
    }
    return -1;
}

/* 检测点击的Dock栏图标 */
static int hit_test_dock_icon(int mx, int my)
{
    for (int i = 0; i < (int)APP_COUNT && i < 8; i++) {
        int ix = dock_icon_positions[i][0];
        int iy = dock_icon_positions[i][1];
        if (mx >= ix && mx <= ix + ICON_SIZE &&
            my >= iy && my <= iy + ICON_SIZE) {
            return i;
        }
    }
    return -1;
}

/* 绘制启动的应用窗口 */
/* 绘制单个窗口 (V6.93 扁平化 + 打开动画) */
static void draw_window(int win_idx)
{
    if (win_idx < 0 || win_idx >= MAX_WINDOWS || !windows[win_idx].used) return;

    int app_id = windows[win_idx].app_id;
    if (app_id < 0 || app_id >= (int)APP_COUNT) return;

    int wx = windows[win_idx].x;
    int wy = windows[win_idx].y;
    int win_w = SCALE(gfx_width - 200);
    int win_h = SCALE(gfx_height - 160);

    /* V6.98: 最小化窗口不显示 */
    if (windows[win_idx].minimized) return;

    /* V6.98: 最大化窗口占满屏幕（除状态栏和Dock） */
    if (windows[win_idx].maximized) {
        wx = 0;
        wy = STATUSBAR_HEIGHT;
        win_w = gfx_width;
        win_h = gfx_height - STATUSBAR_HEIGHT - DOCK_HEIGHT;
    }

    /* 打开动画：从小到大缩放 (10帧) */
    if (windows[win_idx].anim_state == 1) {
        int frame = windows[win_idx].anim_frame;
        if (frame < 10) {
            int scale = (frame + 1) * 100 / 10;
            int new_w = win_w * scale / 100;
            int new_h = win_h * scale / 100;
            int cx = wx + win_w / 2;
            int cy = wy + win_h / 2;
            wx = cx - new_w / 2;
            wy = cy - new_h / 2;
            win_w = new_w;
            win_h = new_h;
            windows[win_idx].anim_frame++;
        } else {
            windows[win_idx].anim_state = 0;
        }
    }

    /* 扁平化窗口背景 - 去掉阴影 */
    gfx_fill_rounded_rect(wx, wy, win_w, win_h, 8, 0xFFFFFF);
    gfx_fill_rounded_rect(wx - 1, wy - 1, win_w + 2, win_h + 2, 9, 0xCBD5E0);
    gfx_fill_rounded_rect(wx, wy, win_w, win_h, 8, 0xFFFFFF);

    /* 标题栏 - 扁平化纯色 */
    if (win_h > 50) {
        gfx_fill_rect(wx + 8, wy, win_w - 16, 36, 0xF7FAFC);
        desktop_puts(wx + 16, wy + 12, app_icons[app_id].name, 0x00);

        /* 关闭按钮 - 扁平化红色 */
        if (win_w > 80) {
            gfx_fill_rounded_rect(wx + win_w - 40, wy + 8, 24, 20, 4, 0xFC8181);
            desktop_puts(wx + win_w - 34, wy + 12, "X", 0x00);
        }

        /* V6.98: 最大化按钮 - 扁平化绿色 */
        if (win_w > 110) {
            uint32_t max_color = windows[win_idx].maximized ? 0x68D391 : 0x9AE6B4;
            gfx_fill_rounded_rect(wx + win_w - 70, wy + 8, 24, 20, 4, max_color);
            desktop_puts(wx + win_w - 63, wy + 12, windows[win_idx].maximized ? "□" : "▢", 0x00);
        }

        /* 最小化按钮 - 扁平化黄色 */
        if (win_w > 140) {
            gfx_fill_rounded_rect(wx + win_w - 100, wy + 8, 24, 20, 4, 0xF6E05E);
            desktop_puts(wx + win_w - 94, wy + 12, "-", 0x00);
        }
    }

    /* 应用内容区域 */
    if (win_h > 100) {
        int content_y = wy + 50;

        /* 文件管理器 */
        if (app_id == 0) {
            int fm_x = wx + 20;
            int fm_y = content_y + 10;

            /* 工具栏 */
            ui_draw_button(fm_x, fm_y - 3, 50, 24, "返回", 0, 0);
            ui_draw_button(fm_x + 55, fm_y - 3, 50, 24, "前进", 0, 0);
            ui_draw_button(fm_x + 110, fm_y - 3, 50, 24, "向上", 0, 0);
            gfx_fill_rounded_rect(fm_x + 170, fm_y - 3, win_w - 250, 24, 4, 0xFFFFFF);
            gfx_draw_rect(fm_x + 170, fm_y - 3, win_w - 250, 24, 0xCCCCCC);
            desktop_puts(fm_x + 178, fm_y + 3, "/home/user", 0x333333);
            ui_draw_button(fm_x + win_w - 75, fm_y - 3, 55, 24, "搜索", 0, 0);
            gfx_draw_hline(fm_x, fm_y + 25, win_w - 60, 0xE2E8F0);

            /* 左侧目录树 */
            gfx_fill_rect(fm_x, fm_y + 30, 120, win_h - 120, 0xF7FAFC);
            const char *dirs[] = {"桌面", "文档", "下载", "音乐", "图片", "视频", "系统"};
            for (int i = 0; i < 7; i++) {
                if (i == 1) {
                    gfx_fill_rect(fm_x, fm_y + 35 + i * 26, 120, 24, 0x3182CE);
                    desktop_puts(fm_x + 12, fm_y + 41 + i * 26, dirs[i], 0x0F);
                } else {
                    desktop_puts(fm_x + 12, fm_y + 41 + i * 26, dirs[i], 0x00);
                }
            }

            /* 右侧文件列表 */
            int file_x = fm_x + 130;
            const char *files[] = {"项目计划.docx", "会议记录.txt", "系统架构.png", "开发笔记.md", "README", "配置文件.ini"};
            const char *file_sizes[] = {"256 KB", "12 KB", "2.4 MB", "8 KB", "4 KB", "2 KB"};
            const char *file_dates[] = {"2026-10-07", "2026-10-06", "2026-10-05", "2026-10-04", "2026-10-03", "2026-10-02"};
            const uint32_t file_colors[] = {0x4A90D9, 0x52C41A, 0xE94560, 0x722ED1, 0x607D8B, 0xFAAD14};

            desktop_puts(file_x, fm_y + 35, "名称", 0x666666);
            desktop_puts(file_x + 200, fm_y + 35, "大小", 0x666666);
            desktop_puts(file_x + 280, fm_y + 35, "修改日期", 0x666666);
            gfx_draw_hline(file_x, fm_y + 50, win_w - 200, 0xE2E8F0);

            for (int i = 0; i < 6; i++) {
                int fy = fm_y + 58 + i * 32;
                gfx_fill_rounded_rect(file_x, fy + 2, 24, 24, 3, file_colors[i]);
                desktop_puts(file_x + 32, fy + 8, files[i], 0x00);
                desktop_puts(file_x + 200, fy + 8, file_sizes[i], 0x888888);
                desktop_puts(file_x + 280, fy + 8, file_dates[i], 0x888888);
            }

            /* 状态栏 */
            int sb_y = wy + win_h - 30;
            gfx_fill_rect(wx + 8, sb_y, win_w - 16, 22, 0xF7FAFC);
            desktop_puts(wx + 16, sb_y + 5, "6个项目 | 已选择0个", 0x666666);
        }
        /* 终端 */
        else if (app_id == 1) {
            int term_x = wx + 15;
            int term_y = content_y + 10;
            gfx_fill_rect(term_x, term_y, win_w - 30, win_h - 80, 0x1E1E1E);

            const char *term_lines[] = {
                "HepuOS Terminal v1.0",
                "Copyright (c) 2026 HepuOS Team",
                "",
                "user@hepu-os:~$ ls -la",
                "drwxr-xr-x  2 user user 4096 Oct  7 10:00 Desktop",
                "drwxr-xr-x  3 user user 4096 Oct  7 09:30 Documents",
                "drwxr-xr-x  2 user user 4096 Oct  7 08:15 Downloads",
                "-rw-r--r--  1 user user  256 Oct  7 10:30 README.md",
                "",
                "user@hepu-os:~$ uname -a",
                "HepuOS hepu-os 0.10.0 x86_64 GNU/Hepu",
                "",
                "user@hepu-os:~$ _"
            };
            for (int i = 0; i < 13; i++) {
                desktop_puts(term_x + 10, term_y + 10 + i * 18, term_lines[i], 0x00FF00);
            }
        }
        /* 浏览器 */
        else if (app_id == 2) {
            int br_x = wx + 15;
            int br_y = content_y + 10;

            /* 标签栏 */
            gfx_fill_rounded_rect(br_x, br_y, 120, 26, 4, 0xFFFFFF);
            gfx_draw_rect(br_x, br_y, 120, 26, 0xCCCCCC);
            desktop_puts(br_x + 10, br_y + 7, "HepuOS首页", 0x333333);
            ui_draw_button(br_x + 125, br_y, 24, 26, "+", 0, 0);

            /* 地址栏 */
            gfx_fill_rounded_rect(br_x, br_y + 32, win_w - 100, 26, 4, 0xFFFFFF);
            gfx_draw_rect(br_x, br_y + 32, win_w - 100, 26, 0xCCCCCC);
            desktop_puts(br_x + 10, br_y + 39, "https://www.hepuos.com", 0x333333);
            ui_draw_button(br_x + win_w - 80, br_y + 32, 60, 26, "刷新", 0, 0);

            /* 网页内容 */
            int page_y = br_y + 70;
            gfx_fill_rect(br_x, page_y, win_w - 30, win_h - 140, 0xFFFFFF);
            desktop_puts(br_x + 20, page_y + 15, "欢迎使用 HepuOS 浏览器", 0x00);
            desktop_puts(br_x + 20, page_y + 40, "这是一个现代化的网页浏览器，支持：", 0x333333);
            desktop_puts(br_x + 30, page_y + 65, "- HTML5 标准支持", 0x555555);
            desktop_puts(br_x + 30, page_y + 85, "- CSS3 样式渲染", 0x555555);
            desktop_puts(br_x + 30, page_y + 105, "- JavaScript 引擎", 0x555555);
            desktop_puts(br_x + 30, page_y + 125, "- 多标签页浏览", 0x555555);
            desktop_puts(br_x + 30, page_y + 145, "- 书签管理", 0x555555);
            desktop_puts(br_x + 20, page_y + 180, "快速访问：", 0x333333);

            const char *sites[] = {"百度", "谷歌", "B站", "知乎", "微博"};
            const uint32_t site_colors[] = {0x2932E1, 0x4285F4, 0xFB7299, 0x0084FF, 0xE6162D};
            for (int i = 0; i < 5; i++) {
                int sx = br_x + 30 + i * 70;
                gfx_fill_rounded_rect(sx, page_y + 205, 50, 50, 8, site_colors[i]);
                desktop_puts(sx + 15, page_y + 220, sites[i][0] ? &sites[i][0] : "?", 0xFFFFFF);
                desktop_puts(sx + 8, page_y + 260, sites[i], 0x333333);
            }
        }
        /* 游戏 */
        else if (app_id == 4) {
            int game_x = wx + 20;
            int game_y = content_y + 10;

            desktop_puts(game_x, game_y, "游戏中心", 0x00);
            gfx_draw_hline(game_x, game_y + 18, win_w - 60, 0xE2E8F0);

            /* 游戏列表 */
            const char *games[] = {"贪吃蛇", "俄罗斯方块", "扫雷", "2048", "五子棋", "象棋"};
            const char *game_desc[] = {"经典益智游戏", "经典方块游戏", "经典扫雷游戏", "数字合并游戏", "双人对战", "中国象棋"};
            const uint32_t game_colors[] = {0x4CAF50, 0x2196F3, 0xFF9800, 0x9C27B0, 0xF44336, 0x795548};

            for (int i = 0; i < 6; i++) {
                int gy = game_y + 30 + i * 60;
                gfx_fill_rounded_rect(game_x, gy, win_w - 60, 50, 6, 0xF5F7FA);
                gfx_fill_rounded_rect(game_x + 8, gy + 8, 34, 34, 6, game_colors[i]);
                desktop_puts(game_x + 52, gy + 8, games[i], 0x00);
                desktop_puts(game_x + 52, gy + 28, game_desc[i], 0x888888);
                ui_draw_button(game_x + win_w - 100, gy + 12, 70, 26, "开始", 1, 0);
            }
        }
        /* 应用商店 */
        else if (app_id == 5) {
            int store_x = wx + 20;
            int store_y = content_y + 10;

            desktop_puts(store_x, store_y, "应用商店", 0x00);
            ui_draw_button(store_x + 75, store_y - 3, 60, 22, "搜索", 0, 0);
            gfx_draw_hline(store_x, store_y + 18, win_w - 60, 0xE2E8F0);

            /* 分类标签 */
            const char *cats[] = {"推荐", "工具", "社交", "娱乐", "教育", "游戏"};
            for (int i = 0; i < 6; i++) {
                int cx = store_x + i * 60;
                if (i == 0) {
                    gfx_fill_rounded_rect(cx, store_y + 25, 50, 24, 12, 0x3182CE);
                    desktop_puts(cx + 12, store_y + 31, cats[i], 0x0F);
                } else {
                    desktop_puts(cx + 12, store_y + 31, cats[i], 0x666666);
                }
            }

            /* 应用列表 */
            const char *apps[] = {"Hepu浏览器", "Hepu音乐", "Hepu视频", "Hepu办公", "Hepu地图", "Hepu天气"};
            const char *app_ver[] = {"v2.1.0", "v1.5.2", "v3.0.1", "v1.2.0", "v2.0.0", "v1.8.5"};
            const uint32_t app_colors[] = {0x4A90D9, 0xE94560, 0x722ED1, 0x52C41A, 0xFAAD14, 0x13C2C2};

            for (int i = 0; i < 6; i++) {
                int ay = store_y + 60 + i * 55;
                gfx_fill_rounded_rect(store_x + 8, ay + 5, 40, 40, 8, app_colors[i]);
                desktop_puts(store_x + 58, ay + 8, apps[i], 0x00);
                desktop_puts(store_x + 58, ay + 28, app_ver[i], 0x888888);
                ui_draw_button(store_x + win_w - 90, ay + 12, 65, 26, "安装", 1, 0);
            }
        }
        /* 助手 */
        else if (app_id == 6) {
            int ai_x = wx + 20;
            int ai_y = content_y + 10;

            desktop_puts(ai_x, ai_y, "智能助手", 0x00);
            gfx_draw_hline(ai_x, ai_y + 18, win_w - 60, 0xE2E8F0);

            /* 对话区域 */
            gfx_fill_rounded_rect(ai_x, ai_y + 25, win_w - 60, win_h - 120, 6, 0xF7FAFC);

            /* AI消息 */
            gfx_fill_rounded_rect(ai_x + 10, ai_y + 35, win_w - 100, 40, 8, 0xFFFFFF);
            gfx_draw_rect(ai_x + 10, ai_y + 35, win_w - 100, 40, 0xE0E0E0);
            desktop_puts(ai_x + 20, ai_y + 45, "你好！我是Hepu智能助手，有什么可以帮你的？", 0x333333);

            /* 用户消息 */
            gfx_fill_rounded_rect(ai_x + 80, ai_y + 85, win_w - 100, 35, 8, 0x3182CE);
            desktop_puts(ai_x + 90, ai_y + 94, "今天天气怎么样？", 0xFFFFFF);

            /* AI回复 */
            gfx_fill_rounded_rect(ai_x + 10, ai_y + 130, win_w - 100, 50, 8, 0xFFFFFF);
            gfx_draw_rect(ai_x + 10, ai_y + 130, win_w - 100, 50, 0xE0E0E0);
            desktop_puts(ai_x + 20, ai_y + 140, "常德今天晴，气温18-26°C，", 0x333333);
            desktop_puts(ai_x + 20, ai_y + 158, "东北风3级，空气质量良。", 0x333333);

            /* 输入框 */
            gfx_fill_rounded_rect(ai_x, ai_y + win_h - 105, win_w - 80, 32, 6, 0xFFFFFF);
            gfx_draw_rect(ai_x, ai_y + win_h - 105, win_w - 80, 32, 0xCCCCCC);
            desktop_puts(ai_x + 12, ai_y + win_h - 97, "输入消息...", 0x999999);
            ui_draw_button(ai_x + win_w - 70, ai_y + win_h - 105, 55, 32, "发送", 1, 0);
        }
        /* 相机 */
        else if (app_id == 7) {
            int cam_x = wx + 20;
            int cam_y = content_y + 10;

            desktop_puts(cam_x, cam_y, "相机", 0x00);
            ui_draw_button(cam_x + 50, cam_y - 3, 50, 22, "拍照", 1, 0);
            ui_draw_button(cam_x + 105, cam_y - 3, 50, 22, "录像", 0, 0);
            ui_draw_button(cam_x + 160, cam_y - 3, 50, 22, "设置", 0, 0);
            gfx_draw_hline(cam_x, cam_y + 18, win_w - 60, 0xE2E8F0);

            /* 取景框 */
            int view_y = cam_y + 30;
            gfx_fill_rect(cam_x, view_y, win_w - 60, win_h - 140, 0x000000);
            /* 模拟画面 */
            gfx_fill_rect(cam_x + 20, view_y + 20, win_w - 100, win_h - 180, 0x1a1a2e);
            /* 网格线 */
            for (int i = 1; i < 3; i++) {
                gfx_fill_rect(cam_x + 20 + (win_w - 100) * i / 3, view_y + 20, 1, win_h - 180, 0x333333);
                gfx_fill_rect(cam_x + 20, view_y + 20 + (win_h - 180) * i / 3, win_w - 100, 1, 0x333333);
            }
            /* 对焦框 */
            int fx = cam_x + (win_w - 60) / 2 - 30;
            int fy = view_y + (win_h - 140) / 2 - 30;
            gfx_draw_rect(fx, fy, 60, 60, 0x4CAF50);
            desktop_puts(cam_x + 30, view_y + 30, "1080P | 自动对焦", 0xFFFFFF);

            /* 底部控制栏 */
            int ctrl_y = view_y + win_h - 140 + 10;
            gfx_fill_rounded_rect(cam_x, ctrl_y, win_w - 60, 40, 6, 0x222222);
            /* 快门按钮 */
            gfx_fill_circle(cam_x + (win_w - 60) / 2, ctrl_y + 20, 15, 0xFFFFFF);
            gfx_fill_circle(cam_x + (win_w - 60) / 2, ctrl_y + 20, 12, 0x333333);
            /* 切换摄像头 */
            ui_draw_button(cam_x + 20, ctrl_y + 8, 50, 26, "切换", 0, 0);
            /* 滤镜 */
            ui_draw_button(cam_x + win_w - 90, ctrl_y + 8, 50, 26, "滤镜", 0, 0);
        }
        /* V6.97: 设置应用显示系统信息 */
        else if (app_id == 3) {
            /* 左侧设置菜单（增强版） */
            gfx_fill_rect(wx + 8, content_y, 140, win_h - 80, 0xF7FAFC);
            const char *settings_menu[] = {
                "显示", "系统", "网络", "声音",
                "电源", "安全", "存储", "个性化",
                "用户", "更新", "关于"
            };
            for (int i = 0; i < 11; i++) {
                if (i == 0) {
                    gfx_fill_rect(wx + 8, content_y + i * 28, 140, 26, 0x3182CE);
                    desktop_puts(wx + 20, content_y + i * 28 + 7, settings_menu[i], 0x0F);
                } else {
                    desktop_puts(wx + 20, content_y + i * 28 + 7, settings_menu[i], 0x00);
                }
            }

            /* 右侧显示设置内容 */
            int info_x = wx + 160;
            desktop_puts(info_x, content_y + 10, "显示设置", 0x00);
            gfx_draw_hline(info_x, content_y + 28, win_w - 180, 0xE2E8F0);

            /* 分辨率 */
            desktop_puts(info_x, content_y + 40, "分辨率", 0x07);
            gfx_fill_rounded_rect(info_x, content_y + 55, 120, 26, 4, 0xFFFFFF);
            gfx_draw_rect(info_x, content_y + 55, 120, 26, 0xCCCCCC);
            desktop_puts(info_x + 10, content_y + 61, "1024 x 768", 0x333333);
            desktop_puts(info_x + 135, content_y + 61, "推荐", 0x4CAF50);

            /* 刷新率 */
            desktop_puts(info_x, content_y + 90, "刷新率", 0x07);
            gfx_fill_rounded_rect(info_x, content_y + 105, 80, 26, 4, 0xFFFFFF);
            gfx_draw_rect(info_x, content_y + 105, 80, 26, 0xCCCCCC);
            desktop_puts(info_x + 10, content_y + 111, "60 Hz", 0x333333);

            /* 亮度滑块 */
            desktop_puts(info_x, content_y + 140, "亮度", 0x07);
            ui_draw_slider(info_x, content_y + 155, 150, 75, 0, 100);

            /* 夜间模式 */
            ui_draw_checkbox(info_x, content_y + 185, "夜间模式", 0);
            ui_draw_checkbox(info_x, content_y + 210, "护眼模式", 1);
            ui_draw_checkbox(info_x, content_y + 235, "自动亮度", 0);

            /* 主题选择 */
            desktop_puts(info_x, content_y + 270, "主题", 0x00);
            gfx_draw_hline(info_x, content_y + 288, win_w - 180, 0xE2E8F0);

            const char *themes[] = {"浅色", "深色", "自动"};
            const uint32_t theme_colors[] = {0xFFFFFF, 0x333333, 0x4A90D9};
            for (int i = 0; i < 3; i++) {
                int tx = info_x + i * 70;
                gfx_fill_rounded_rect(tx, content_y + 300, 55, 40, 6, theme_colors[i]);
                gfx_draw_rect(tx, content_y + 300, 55, 40, i == 0 ? 0x4A90D9 : 0xCCCCCC);
                if (i == 0) {
                    gfx_draw_rect(tx - 1, content_y + 299, 57, 42, 0x4A90D9);
                }
                desktop_puts(tx + 15, content_y + 312, themes[i], i == 1 ? 0xFFFFFF : 0x333333);
            }

            /* 字体大小 */
            desktop_puts(info_x, content_y + 355, "字体大小", 0x07);
            ui_draw_slider(info_x, content_y + 370, 150, 50, 0, 100);

            /* 缩放 */
            desktop_puts(info_x, content_y + 405, "界面缩放", 0x07);
            gfx_fill_rounded_rect(info_x, content_y + 420, 80, 26, 4, 0xFFFFFF);
            gfx_draw_rect(info_x, content_y + 420, 80, 26, 0xCCCCCC);
            desktop_puts(info_x + 15, content_y + 426, "100%", 0x333333);

            /* 系统信息 */
            desktop_puts(info_x, content_y + 460, "系统信息", 0x00);
            gfx_draw_hline(info_x, content_y + 478, win_w - 180, 0xE2E8F0);
            desktop_puts(info_x, content_y + 490, "操作系统: Hepu OS 0.10.0", 0x07);
            desktop_puts(info_x, content_y + 510, "内核: v1.0.2", 0x07);
            desktop_puts(info_x, content_y + 530, "架构: x86 (64位支持)", 0x07);
            desktop_puts(info_x, content_y + 550, "主机名: hepu-os", 0x07);

            /* 电源按钮 */
            int btn_y = wy + win_h - 60;
            ui_draw_button(info_x, btn_y, 90, 32, "关机", 0, 0);
            ui_draw_button(info_x + 100, btn_y, 90, 32, "重启", 0, 0);
            ui_draw_button(info_x + 200, btn_y, 90, 32, "应用", 1, 0);
        } else if (app_id == 8) {
            /* V8.49: 系统监控应用 */
            int mon_x = wx + 20;
            int mon_y = content_y + 10;

            /* CPU使用率 */
            desktop_puts(mon_x, mon_y, "CPU 使用率", 0x00);
            extern int kopt_get_cpu_usage(int cpu_id, float *percent);
            float cpu_usage = 0.0f;
            kopt_get_cpu_usage(0, &cpu_usage);
            int cpu_pct = (int)(cpu_usage * 100);
            if (cpu_pct > 100) cpu_pct = 100;
            ui_draw_progress(mon_x, mon_y + 20, win_w - 60, 16, cpu_pct);
            char cpu_str[32];
            snprintf(cpu_str, sizeof(cpu_str), "%d%%", cpu_pct);
            desktop_puts(mon_x + win_w - 80, mon_y + 22, cpu_str, 0x09);

            /* 内存使用 */
            mon_y += 55;
            desktop_puts(mon_x, mon_y, "内存使用", 0x00);
            extern int kcore_get_memory_stats(uint64_t *total, uint64_t *used, uint64_t *free, uint64_t *kernel, uint64_t *user);
            uint64_t mem_total = 0, mem_used = 0, mem_free = 0, mem_kernel = 0, mem_user = 0;
            kcore_get_memory_stats(&mem_total, &mem_used, &mem_free, &mem_kernel, &mem_user);
            /* 转换为MB（32位），避免64位除法 */
            uint32_t mem_total_mb = (uint32_t)(mem_total >> 20);
            uint32_t mem_used_mb = (uint32_t)(mem_used >> 20);
            uint32_t mem_free_mb = (uint32_t)(mem_free >> 20);
            int mem_pct = mem_total_mb > 0 ? (int)(mem_used_mb * 100 / mem_total_mb) : 0;
            if (mem_pct > 100) mem_pct = 100;
            ui_draw_progress(mon_x, mon_y + 20, win_w - 60, 16, mem_pct);
            char mem_str[64];
            snprintf(mem_str, sizeof(mem_str), "%d%% (%dMB/%dMB)",
                     mem_pct, mem_used_mb, mem_total_mb);
            desktop_puts(mon_x + win_w - 160, mon_y + 22, mem_str, 0x09);

            /* 系统信息 */
            mon_y += 55;
            desktop_puts(mon_x, mon_y, "系统信息", 0x00);
            gfx_draw_hline(mon_x, mon_y + 18, win_w - 60, 0xE2E8F0);
            char sys_str[128];
            snprintf(sys_str, sizeof(sys_str), "内核: Hepu OS v1.0.2");
            desktop_puts(mon_x, mon_y + 28, sys_str, 0x07);
            extern int gfx_refresh_rate;
            snprintf(sys_str, sizeof(sys_str), "显示: %dx%d @ %dHz, %dbpp",
                     gfx_width, gfx_height, gfx_refresh_rate, gfx_bpp);
            desktop_puts(mon_x, mon_y + 48, sys_str, 0x07);
            snprintf(sys_str, sizeof(sys_str), "内存: 总计%dMB 已用%dMB 空闲%dMB",
                     mem_total_mb, mem_used_mb, mem_free_mb);
            desktop_puts(mon_x, mon_y + 68, sys_str, 0x07);

            /* 进程列表标题 */
            mon_y += 100;
            if (mon_y + 80 < wy + win_h - 40) {
                desktop_puts(mon_x, mon_y, "进程列表", 0x00);
                gfx_draw_hline(mon_x, mon_y + 18, win_w - 60, 0xE2E8F0);
                /* 表头 */
                desktop_puts(mon_x, mon_y + 28, "PID", 0x08);
                desktop_puts(mon_x + 60, mon_y + 28, "名称", 0x08);
                desktop_puts(mon_x + 200, mon_y + 28, "状态", 0x08);
                desktop_puts(mon_x + 280, mon_y + 28, "CPU", 0x08);
                /* 显示前5个进程 */
                extern process_t proc_table[];
                extern int process_count;
                int shown = 0;
                for (int p = 0; p < 64 && shown < 5; p++) {
                    if (proc_table[p].state != 0) {
                        int py = mon_y + 48 + shown * 20;
                        char pid_str[16];
                        snprintf(pid_str, sizeof(pid_str), "%d", proc_table[p].pid);
                        desktop_puts(mon_x, py, pid_str, 0x07);
                        desktop_puts(mon_x + 60, py, proc_table[p].name, 0x07);
                        const char *state_str = "运行";
                        if (proc_table[p].state == 2) state_str = "就绪";
                        else if (proc_table[p].state == 3) state_str = "阻塞";
                        else if (proc_table[p].state == 4) state_str = "睡眠";
                        desktop_puts(mon_x + 200, py, state_str, 0x07);
                        shown++;
                    }
                }
                if (shown == 0) {
                    desktop_puts(mon_x, mon_y + 48, "无运行进程", 0x07);
                }
            }
        } else if (app_id == 9) {
            /* V0.9.0: 截图工具应用 */
            int shot_x = wx + 20;
            int shot_y = content_y + 10;

            /* 标题 */
            desktop_puts(shot_x, shot_y, "截图工具", 0x00);
            gfx_draw_hline(shot_x, shot_y + 18, win_w - 60, 0xE2E8F0);

            /* 截图预览区域 */
            int preview_x = shot_x;
            int preview_y = shot_y + 30;
            int preview_w = win_w - 60;
            int preview_h = 120;
            gfx_fill_rect(preview_x, preview_y, preview_w, preview_h, 0xF0F0F0);
            gfx_draw_rect(preview_x, preview_y, preview_w, preview_h, 0xCCCCCC);

            /* 预览区域显示桌面缩略图示意 */
            gfx_fill_rect(preview_x + 4, preview_y + 4, preview_w - 8, 20, 0x3182CE);
            desktop_puts(preview_x + 8, preview_y + 8, "HepuOS Desktop", 0x0F);
            /* 模拟桌面图标 */
            for (int i = 0; i < 8 && i < (preview_w - 20) / 40; i++) {
                int ix = preview_x + 10 + i * 40;
                int iy = preview_y + 35;
                gfx_fill_rect(ix, iy, 24, 24, app_icons[i % APP_COUNT].color1);
            }
            /* 任务栏 */
            gfx_fill_rect(preview_x + 4, preview_y + preview_h - 20, preview_w - 8, 16, 0x2C2C2C);

            /* 截图信息 */
            int info_y = preview_y + preview_h + 15;
            extern int gfx_refresh_rate;
            char info_str[128];
            snprintf(info_str, sizeof(info_str), "屏幕分辨率: %dx%d @ %dHz",
                     gfx_width, gfx_height, gfx_refresh_rate);
            desktop_puts(shot_x, info_y, info_str, 0x07);
            info_y += 20;
            snprintf(info_str, sizeof(info_str), "色深: %dbpp", gfx_bpp);
            desktop_puts(shot_x, info_y, info_str, 0x07);
            info_y += 20;
            uint32_t file_size = (uint32_t)gfx_width * gfx_height * (gfx_bpp / 8);
            snprintf(info_str, sizeof(info_str), "BMP文件大小: ~%dKB", file_size / 1024);
            desktop_puts(shot_x, info_y, info_str, 0x07);

            /* 截图按钮 */
            int btn_y = wy + win_h - 60;
            ui_draw_button(shot_x, btn_y, 120, 32, "截取屏幕", 0, 0);
            ui_draw_button(shot_x + 130, btn_y, 120, 32, "保存截图", 0, 0);
            ui_draw_button(shot_x + 260, btn_y, 100, 32, "清空", 0, 0);

            /* 使用提示 */
            desktop_puts(shot_x, btn_y + 40, "点击\"截取屏幕\"捕获当前桌面", 0x08);
        } else if (app_id == 10) {
            /* V0.9.0: 音乐播放器应用 */
            int music_x = wx + 20;
            int music_y = content_y + 10;

            /* 标题 */
            desktop_puts(music_x, music_y, "音乐播放器", 0x00);
            gfx_draw_hline(music_x, music_y + 18, win_w - 60, 0xE2E8F0);

            /* 专辑封面区域 */
            int cover_x = music_x;
            int cover_y = music_y + 30;
            int cover_size = 100;
            /* 渐变封面 */
            gfx_fill_rect(cover_x, cover_y, cover_size, cover_size, 0x1A1A2E);
            gfx_fill_rect(cover_x + 10, cover_y + 10, cover_size - 20, cover_size - 20, 0x16213E);
            /* 音符图标 */
            gfx_fill_rect(cover_x + cover_size/2 - 2, cover_y + 25, 4, 40, 0xE94560);
            gfx_fill_circle(cover_x + cover_size/2 - 10, cover_y + 60, 12, 0xE94560);
            gfx_fill_rect(cover_x + cover_size/2, cover_y + 25, 20, 4, 0xE94560);

            /* 歌曲信息 */
            int info_x = cover_x + cover_size + 20;
            desktop_puts(info_x, cover_y + 10, "HepuOS Demo Track", 0x00);
            desktop_puts(info_x, cover_y + 30, "HepuOS System", 0x07);
            desktop_puts(info_x, cover_y + 50, "2026", 0x07);

            /* 进度条 */
            int prog_y = cover_y + cover_size + 20;
            desktop_puts(music_x, prog_y, "0:00", 0x07);
            ui_draw_progress(music_x + 50, prog_y - 2, win_w - 130, 12, 35);
            desktop_puts(music_x + win_w - 80, prog_y, "3:45", 0x07);

            /* 音量控制 */
            int vol_y = prog_y + 30;
            desktop_puts(music_x, vol_y, "音量", 0x07);
            ui_draw_slider(music_x + 60, vol_y - 5, 150, 70, 0, 100);

            /* 频率选择 */
            int freq_y = vol_y + 35;
            desktop_puts(music_x, freq_y, "测试音:", 0x07);
            ui_draw_button(music_x + 70, freq_y - 5, 60, 24, "220Hz", 0, 0);
            ui_draw_button(music_x + 140, freq_y - 5, 60, 24, "440Hz", 1, 0);
            ui_draw_button(music_x + 210, freq_y - 5, 60, 24, "880Hz", 0, 0);

            /* 播放控制按钮 */
            int ctrl_y = wy + win_h - 60;
            ui_draw_button(music_x, ctrl_y, 80, 36, "上一首", 0, 0);
            ui_draw_button(music_x + 90, ctrl_y, 80, 36, "播放", 1, 0);
            ui_draw_button(music_x + 180, ctrl_y, 80, 36, "暂停", 0, 0);
            ui_draw_button(music_x + 270, ctrl_y, 80, 36, "停止", 0, 0);
            ui_draw_button(music_x + 360, ctrl_y, 80, 36, "下一首", 0, 0);

            /* 状态显示 */
            desktop_puts(music_x, ctrl_y + 42, "状态: 已暂停 | AC97声卡驱动就绪", 0x08);
        } else if (app_id == 11) {
            /* V0.9.0: 日历应用 */
            int cal_x = wx + 20;
            int cal_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(cal_x, cal_y, "2026年10月", 0x00);
            ui_draw_button(cal_x + 100, cal_y - 3, 30, 22, "<", 0, 0);
            ui_draw_button(cal_x + 135, cal_y - 3, 30, 22, ">", 0, 0);
            gfx_draw_hline(cal_x, cal_y + 18, win_w - 60, 0xE2E8F0);

            /* 星期标题 */
            const char *weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};
            int cell_w = (win_w - 60) / 7;
            int cell_h = 32;
            for (int i = 0; i < 7; i++) {
                int color = (i == 0 || i == 6) ? 0xE94560 : 0x00;
                desktop_puts(cal_x + i * cell_w + cell_w/2 - 6, cal_y + 28, weekdays[i], color);
            }

            /* 日期网格 */
            /* 2026年10月1日是星期四 */
            int first_day = 4; /* 0=日, 1=一, ..., 4=四 */
            int days_in_month = 31;
            int today = 7; /* 今天是10月7日 */

            for (int day = 1; day <= days_in_month; day++) {
                int pos = first_day + day - 1;
                int row = pos / 7;
                int col = pos % 7;
                int dx = cal_x + col * cell_w;
                int dy = cal_y + 50 + row * cell_h;

                /* 今天高亮 */
                if (day == today) {
                    gfx_fill_rounded_rect(dx + 2, dy + 2, cell_w - 4, cell_h - 4, 4, 0x4A90D9);
                    char day_str[8];
                    snprintf(day_str, sizeof(day_str), "%d", day);
                    desktop_puts(dx + cell_w/2 - 6, dy + cell_h/2 - 6, day_str, 0xFFFFFF);
                } else {
                    int color = (col == 0 || col == 6) ? 0xE94560 : 0x00;
                    char day_str[8];
                    snprintf(day_str, sizeof(day_str), "%d", day);
                    desktop_puts(dx + cell_w/2 - 6, dy + cell_h/2 - 6, day_str, color);
                }
            }

            /* 底部信息 */
            int info_y = cal_y + 50 + 5 * cell_h + 10;
            gfx_draw_hline(cal_x, info_y, win_w - 60, 0xE2E8F0);
            desktop_puts(cal_x, info_y + 10, "今天: 2026年10月7日 星期三", 0x07);
            desktop_puts(cal_x, info_y + 30, "农历: 八月廿七", 0x07);

            /* 快捷按钮 */
            ui_draw_button(cal_x, info_y + 55, 80, 28, "今天", 1, 0);
            ui_draw_button(cal_x + 90, info_y + 55, 80, 28, "新建事件", 0, 0);
            ui_draw_button(cal_x + 180, info_y + 55, 80, 28, "月视图", 0, 0);
        } else if (app_id == 12) {
            /* V0.9.0: 计算器应用 */
            int calc_x = wx + 20;
            int calc_y = content_y + 10;

            /* 标题 */
            desktop_puts(calc_x, calc_y, "计算器", 0x00);
            gfx_draw_hline(calc_x, calc_y + 18, win_w - 60, 0xE2E8F0);

            /* 显示屏 */
            int disp_y = calc_y + 30;
            int disp_w = win_w - 60;
            gfx_fill_rect(calc_x, disp_y, disp_w, 50, 0x1A1A2E);
            gfx_draw_rect(calc_x, disp_y, disp_w, 50, 0x4A90D9);
            /* 显示内容 */
            desktop_puts(calc_x + 10, disp_y + 8, "标准模式", 0x888888);
            desktop_puts(calc_x + disp_w - 80, disp_y + 25, "12345", 0x00FF00);

            /* 按钮区域 */
            int btn_y = disp_y + 65;
            int btn_w = (disp_w - 30) / 4;
            int btn_h = 36;
            int gap = 10;

            /* 第一行：功能键 */
            const char *row1[] = {"MC", "MR", "MS", "M+"};
            for (int i = 0; i < 4; i++) {
                ui_draw_button(calc_x + i*(btn_w+gap), btn_y, btn_w, btn_h, row1[i], 0, 0);
            }

            /* 第二行：清除和操作 */
            const char *row2[] = {"C", "±", "%", "÷"};
            for (int i = 0; i < 4; i++) {
                int selected = (i == 0) ? 1 : 0;
                ui_draw_button(calc_x + i*(btn_w+gap), btn_y + btn_h + 8, btn_w, btn_h, row2[i], selected, 0);
            }

            /* 第三行：数字7-9和乘 */
            const char *row3[] = {"7", "8", "9", "×"};
            for (int i = 0; i < 4; i++) {
                ui_draw_button(calc_x + i*(btn_w+gap), btn_y + 2*(btn_h+8), btn_w, btn_h, row3[i], 0, 0);
            }

            /* 第四行：数字4-6和减 */
            const char *row4[] = {"4", "5", "6", "-"};
            for (int i = 0; i < 4; i++) {
                ui_draw_button(calc_x + i*(btn_w+gap), btn_y + 3*(btn_h+8), btn_w, btn_h, row4[i], 0, 0);
            }

            /* 第五行：数字1-3和加 */
            const char *row5[] = {"1", "2", "3", "+"};
            for (int i = 0; i < 4; i++) {
                ui_draw_button(calc_x + i*(btn_w+gap), btn_y + 4*(btn_h+8), btn_w, btn_h, row5[i], 0, 0);
            }

            /* 第六行：0、小数点、等号 */
            ui_draw_button(calc_x, btn_y + 5*(btn_h+8), btn_w*2+gap, btn_h, "0", 0, 0);
            ui_draw_button(calc_x + 2*(btn_w+gap), btn_y + 5*(btn_h+8), btn_w, btn_h, ".", 0, 0);
            ui_draw_button(calc_x + 3*(btn_w+gap), btn_y + 5*(btn_h+8), btn_w, btn_h, "=", 1, 0);

            /* 底部提示 */
            int tip_y = btn_y + 6*(btn_h+8) + 5;
            desktop_puts(calc_x, tip_y, "支持: 加减乘除、百分比、正负号", 0x07);
        } else if (app_id == 13) {
            /* V0.9.0: 天气应用 */
            int weather_x = wx + 20;
            int weather_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(weather_x, weather_y, "天气", 0x00);
            ui_draw_button(weather_x + 60, weather_y - 3, 60, 22, "刷新", 0, 0);
            ui_draw_button(weather_x + 125, weather_y - 3, 60, 22, "定位", 0, 0);
            gfx_draw_hline(weather_x, weather_y + 18, win_w - 60, 0xE2E8F0);

            /* 当前天气 - 大卡片 */
            int card_y = weather_y + 30;
            int card_w = win_w - 60;
            gfx_fill_rounded_rect(weather_x, card_y, card_w, 100, 8, 0x4A90D9);

            /* 城市名 */
            desktop_puts(weather_x + 15, card_y + 10, "常德市", 0xFFFFFF);
            desktop_puts(weather_x + 15, card_y + 28, "湖南省", 0xCCE5FF);

            /* 大温度 */
            desktop_puts(weather_x + card_w - 80, card_y + 15, "26°C", 0xFFFFFF);
            desktop_puts(weather_x + card_w - 80, card_y + 40, "晴", 0xFFE066);

            /* 天气详情 */
            desktop_puts(weather_x + 15, card_y + 55, "湿度: 65%", 0xFFFFFF);
            desktop_puts(weather_x + 15, card_y + 73, "风速: 东北风 3级", 0xFFFFFF);
            desktop_puts(weather_x + card_w/2, card_y + 55, "空气质量: 良", 0xFFFFFF);
            desktop_puts(weather_x + card_w/2, card_y + 73, "PM2.5: 35", 0xFFFFFF);

            /* 未来5天预报 */
            int forecast_y = card_y + 115;
            desktop_puts(weather_x, forecast_y, "未来5天", 0x00);
            gfx_draw_hline(weather_x, forecast_y + 15, card_w, 0xE2E8F0);

            const char *days[] = {"今天", "明天", "周三", "周四", "周五"};
            const char *weathers[] = {"晴", "多云", "阴", "小雨", "晴"};
            const char *temps[] = {"26°", "24°", "22°", "20°", "25°"};
            const char *temps_low[] = {"18°", "17°", "16°", "15°", "17°"};

            int day_w = card_w / 5;
            for (int i = 0; i < 5; i++) {
                int dx = weather_x + i * day_w;
                /* 日期 */
                desktop_puts(dx + day_w/2 - 12, forecast_y + 25, days[i], 0x00);
                /* 天气 */
                desktop_puts(dx + day_w/2 - 8, forecast_y + 45, weathers[i], 0x4A90D9);
                /* 温度 */
                desktop_puts(dx + day_w/2 - 10, forecast_y + 65, temps[i], 0xE94560);
                desktop_puts(dx + day_w/2 - 10, forecast_y + 82, temps_low[i], 0x888888);
            }

            /* 生活指数 */
            int index_y = forecast_y + 120;
            desktop_puts(weather_x, index_y, "生活指数", 0x00);
            gfx_draw_hline(weather_x, index_y + 15, card_w, 0xE2E8F0);

            /* 指数卡片 */
            int idx_card_w = (card_w - 20) / 3;
            const char *idx_names[] = {"穿衣", "紫外线", "运动"};
            const char *idx_values[] = {"舒适", "中等", "适宜"};
            const char *idx_descs[] = {"建议穿长袖", "注意防晒", "适合户外运动"};

            for (int i = 0; i < 3; i++) {
                int ix = weather_x + i * (idx_card_w + 10);
                gfx_fill_rounded_rect(ix, index_y + 25, idx_card_w, 60, 6, 0xF5F7FA);
                desktop_puts(ix + 8, index_y + 32, idx_names[i], 0x00);
                desktop_puts(ix + 8, index_y + 48, idx_values[i], 0x4A90D9);
                desktop_puts(ix + 8, index_y + 64, idx_descs[i], 0x888888);
            }

            /* 24小时预报 */
            int hour_y = index_y + 100;
            desktop_puts(weather_x, hour_y, "24小时预报", 0x00);
            gfx_draw_hline(weather_x, hour_y + 15, card_w, 0xE2E8F0);

            const char *hours[] = {"现在", "14时", "16时", "18时", "20时", "22时", "00时", "02时"};
            const char *hour_temps[] = {"26°", "27°", "25°", "23°", "21°", "20°", "19°", "18°"};
            const char *hour_weathers[] = {"晴", "晴", "多云", "多云", "阴", "阴", "晴", "晴"};

            int hour_w = card_w / 8;
            for (int i = 0; i < 8; i++) {
                int hx = weather_x + i * hour_w;
                desktop_puts(hx + hour_w/2 - 10, hour_y + 25, hours[i], 0x666666);
                desktop_puts(hx + hour_w/2 - 8, hour_y + 42, hour_weathers[i], 0x4A90D9);
                desktop_puts(hx + hour_w/2 - 10, hour_y + 60, hour_temps[i], 0xE94560);
            }

            /* 日出日落 + 风力详情 */
            int detail_y = hour_y + 85;
            gfx_fill_rounded_rect(weather_x, detail_y, card_w, 55, 6, 0xF5F7FA);
            desktop_puts(weather_x + 12, detail_y + 8, "日出: 06:23", 0xFAAD14);
            desktop_puts(weather_x + 12, detail_y + 28, "日落: 18:15", 0xE94560);
            desktop_puts(weather_x + card_w/3, detail_y + 8, "风向: 东北风", 0x333333);
            desktop_puts(weather_x + card_w/3, detail_y + 28, "风力: 3级 (12km/h)", 0x333333);
            desktop_puts(weather_x + card_w*2/3, detail_y + 8, "气压: 1013hPa", 0x333333);
            desktop_puts(weather_x + card_w*2/3, detail_y + 28, "能见度: 15km", 0x333333);

            /* 更多生活指数 */
            int more_idx_y = detail_y + 70;
            desktop_puts(weather_x, more_idx_y, "更多指数", 0x00);
            gfx_draw_hline(weather_x, more_idx_y + 15, card_w, 0xE2E8F0);

            const char *more_names[] = {"洗车", "感冒", "空气", "防晒", "穿衣", "运动"};
            const char *more_values[] = {"适宜", "较易发", "良", "中等", "舒适", "适宜"};
            const uint32_t more_colors[] = {0x4CAF50, 0xFAAD14, 0x4CAF50, 0xFAAD14, 0x4CAF50, 0x4CAF50};

            int more_w = card_w / 6;
            for (int i = 0; i < 6; i++) {
                int mx = weather_x + i * more_w;
                gfx_fill_rounded_rect(mx + 3, more_idx_y + 22, more_w - 6, 45, 4, 0xFFFFFF);
                gfx_draw_rect(mx + 3, more_idx_y + 22, more_w - 6, 45, 0xE0E0E0);
                desktop_puts(mx + more_w/2 - 10, more_idx_y + 30, more_names[i], 0x666666);
                desktop_puts(mx + more_w/2 - 10, more_idx_y + 48, more_values[i], more_colors[i]);
            }
        } else if (app_id == 14) {
            /* V0.9.0: 备忘录应用 */
            int note_x = wx + 20;
            int note_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(note_x, note_y, "备忘录", 0x00);
            ui_draw_button(note_x + 60, note_y - 3, 70, 22, "新建", 1, 0);
            ui_draw_button(note_x + 135, note_y - 3, 70, 22, "删除", 0, 0);
            ui_draw_button(note_x + 210, note_y - 3, 70, 22, "搜索", 0, 0);
            gfx_draw_hline(note_x, note_y + 18, win_w - 60, 0xE2E8F0);

            /* 备忘录列表 */
            int list_y = note_y + 30;
            int list_w = win_w - 60;

            /* 备忘录条目 */
            const char *note_titles[] = {
                "项目会议记录",
                "购物清单",
                "学习计划",
                "待办事项",
                "读书笔记"
            };
            const char *note_contents[] = {
                "1. 讨论P5优先级开发进度\n2. 安排下周任务\n3. 审查代码质量",
                "- 牛奶\n- 面包\n- 鸡蛋\n- 水果",
                "周一: 学习Rust\n周二: 学习64位汇编\n周三: 复习算法",
                "[ ] 完成文档整理\n[x] 修复编译错误\n[ ] 测试新功能",
                "《操作系统概念》第5章\n重点: 进程调度算法"
            };
            const char *note_dates[] = {
                "2026-10-07 10:30",
                "2026-10-06 18:20",
                "2026-10-05 09:15",
                "2026-10-04 21:00",
                "2026-10-03 14:45"
            };
            const uint32_t note_colors[] = {
                0xFFF3CD, 0xFFE4E1, 0xE8F5E9, 0xE3F2FD, 0xF3E5F5
            };

            for (int i = 0; i < 5; i++) {
                int item_y = list_y + i * 70;
                int selected = (i == 0) ? 1 : 0;

                /* 条目背景 */
                if (selected) {
                    gfx_fill_rounded_rect(note_x, item_y, list_w, 65, 6, 0xE3F2FD);
                    gfx_draw_rect(note_x, item_y, list_w, 65, 0x4A90D9);
                } else {
                    gfx_fill_rounded_rect(note_x, item_y, list_w, 65, 6, note_colors[i]);
                }

                /* 标题 */
                desktop_puts(note_x + 10, item_y + 6, note_titles[i], 0x00);

                /* 日期 */
                desktop_puts(note_x + list_w - 120, item_y + 6, note_dates[i], 0x888888);

                /* 内容预览（前两行） */
                const char *content = note_contents[i];
                int line_count = 0;
                int char_pos = 0;
                char line[64];
                int line_idx = 0;
                while (content[char_pos] && line_count < 2) {
                    if (content[char_pos] == '\n') {
                        line[line_idx] = '\0';
                        desktop_puts(note_x + 10, item_y + 24 + line_count * 16, line, 0x555555);
                        line_count++;
                        line_idx = 0;
                        char_pos++;
                    } else {
                        if (line_idx < 63) line[line_idx++] = content[char_pos];
                        char_pos++;
                    }
                }
                if (line_count < 2 && line_idx > 0) {
                    line[line_idx] = '\0';
                    desktop_puts(note_x + 10, item_y + 24 + line_count * 16, line, 0x555555);
                }
            }

            /* 底部统计 */
            int stat_y = list_y + 5 * 70 + 10;
            gfx_draw_hline(note_x, stat_y, list_w, 0xE2E8F0);
            desktop_puts(note_x, stat_y + 8, "共 5 条备忘录 | 已选中 1 条", 0x07);
            desktop_puts(note_x + list_w - 100, stat_y + 8, "自动保存", 0x4CAF50);
        } else if (app_id == 15) {
            /* V0.9.0: 通讯录应用 */
            int contact_x = wx + 20;
            int contact_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(contact_x, contact_y, "通讯录", 0x00);
            ui_draw_button(contact_x + 60, contact_y - 3, 70, 22, "添加", 1, 0);
            ui_draw_button(contact_x + 135, contact_y - 3, 70, 22, "编辑", 0, 0);
            ui_draw_button(contact_x + 210, contact_y - 3, 70, 22, "搜索", 0, 0);
            gfx_draw_hline(contact_x, contact_y + 18, win_w - 60, 0xE2E8F0);

            /* 联系人列表 */
            int list_y = contact_y + 30;
            int list_w = win_w - 60;

            /* 联系人数据 */
            const char *contact_names[] = {
                "张三", "李四", "王五", "赵六", "钱七", "孙八"
            };
            const char *contact_phones[] = {
                "138-0000-0001", "139-0000-0002", "137-0000-0003",
                "136-0000-0004", "135-0000-0005", "138-0000-0006"
            };
            const char *contact_emails[] = {
                "zhangsan@hepu.os", "lisi@hepu.os", "wangwu@hepu.os",
                "zhaoliu@hepu.os", "qianqi@hepu.os", "sunba@hepu.os"
            };
            const char *contact_groups[] = {
                "同事", "朋友", "家人", "同事", "朋友", "其他"
            };
            const uint32_t avatar_colors[] = {
                0x4A90D9, 0xE94560, 0x52C41A, 0xFAAD14, 0x722ED1, 0x13C2C2
            };

            for (int i = 0; i < 6; i++) {
                int item_y = list_y + i * 55;
                int selected = (i == 0) ? 1 : 0;

                /* 条目背景 */
                if (selected) {
                    gfx_fill_rounded_rect(contact_x, item_y, list_w, 50, 6, 0xE3F2FD);
                    gfx_draw_rect(contact_x, item_y, list_w, 50, 0x4A90D9);
                } else {
                    gfx_draw_hline(contact_x, item_y + 50, list_w, 0xF0F0F0);
                }

                /* 头像 */
                gfx_fill_circle(contact_x + 25, item_y + 25, 16, avatar_colors[i]);
                /* 头像首字母 */
                desktop_puts(contact_x + 21, item_y + 19, &contact_names[i][0], 0xFFFFFF);

                /* 姓名 */
                desktop_puts(contact_x + 55, item_y + 8, contact_names[i], 0x00);

                /* 分组标签 */
                gfx_fill_rounded_rect(contact_x + 100, item_y + 6, 40, 18, 4, avatar_colors[i]);
                desktop_puts(contact_x + 108, item_y + 9, contact_groups[i], 0xFFFFFF);

                /* 电话 */
                desktop_puts(contact_x + 55, item_y + 28, contact_phones[i], 0x555555);

                /* 邮箱 */
                desktop_puts(contact_x + list_w - 160, item_y + 28, contact_emails[i], 0x888888);
            }

            /* 底部统计 */
            int stat_y = list_y + 6 * 55 + 10;
            gfx_draw_hline(contact_x, stat_y, list_w, 0xE2E8F0);
            desktop_puts(contact_x, stat_y + 8, "共 6 位联系人 | 已选中 1 位", 0x07);
            desktop_puts(contact_x + list_w - 120, stat_y + 8, "已同步到云端", 0x4CAF50);
        } else if (app_id == 16) {
            /* V0.9.0: 视频播放器应用 */
            int video_x = wx + 20;
            int video_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(video_x, video_y, "视频播放器", 0x00);
            ui_draw_button(video_x + 80, video_y - 3, 60, 22, "打开", 0, 0);
            ui_draw_button(video_x + 145, video_y - 3, 60, 22, "全屏", 0, 0);
            gfx_draw_hline(video_x, video_y + 18, win_w - 60, 0xE2E8F0);

            /* 视频显示区域 */
            int disp_y = video_y + 30;
            int disp_w = win_w - 60;
            int disp_h = 140;
            gfx_fill_rect(video_x, disp_y, disp_w, disp_h, 0x000000);
            gfx_draw_rect(video_x, disp_y, disp_w, disp_h, 0x333333);

            /* 播放按钮（居中大三角形） */
            int play_cx = video_x + disp_w / 2;
            int play_cy = disp_y + disp_h / 2;
            gfx_fill_circle(play_cx, play_cy, 28, 0xFFFFFF);
            gfx_fill_rect(play_cx - 6, play_cy - 12, 4, 24, 0xE94560);
            gfx_fill_rect(play_cx + 2, play_cy - 12, 4, 24, 0xE94560);

            /* 视频信息 */
            desktop_puts(video_x + 10, disp_y + 8, "HepuOS 演示视频", 0xFFFFFF);
            desktop_puts(video_x + disp_w - 60, disp_y + 8, "1080P", 0x4CAF50);

            /* 控制栏 */
            int ctrl_y = disp_y + disp_h + 10;

            /* 进度条 */
            ui_draw_progress(video_x, ctrl_y, disp_w - 80, 8, 35);
            desktop_puts(video_x + disp_w - 70, ctrl_y - 2, "01:23 / 04:05", 0x555555);

            /* 控制按钮 */
            int btn_y = ctrl_y + 18;
            ui_draw_button(video_x, btn_y, 50, 28, "上一个", 0, 0);
            ui_draw_button(video_x + 55, btn_y, 50, 28, "播放", 1, 0);
            ui_draw_button(video_x + 110, btn_y, 50, 28, "暂停", 0, 0);
            ui_draw_button(video_x + 165, btn_y, 50, 28, "停止", 0, 0);
            ui_draw_button(video_x + 220, btn_y, 50, 28, "下一个", 0, 0);

            /* 音量控制 */
            desktop_puts(video_x + 290, btn_y + 6, "音量", 0x555555);
            ui_draw_slider(video_x + 330, btn_y + 2, 80, 70, 0, 100);

            /* 播放列表 */
            int list_y = btn_y + 45;
            desktop_puts(video_x, list_y, "播放列表", 0x00);
            gfx_draw_hline(video_x, list_y + 15, disp_w, 0xE2E8F0);

            const char *video_titles[] = {
                "HepuOS 系统演示",
                "内核启动过程",
                "图形子系统展示",
                "应用生态介绍",
                "开发教程 - 第一集"
            };
            const char *video_durations[] = {
                "04:05", "02:30", "03:15", "05:20", "08:45"
            };

            for (int i = 0; i < 5; i++) {
                int item_y = list_y + 22 + i * 28;
                int selected = (i == 0) ? 1 : 0;

                if (selected) {
                    gfx_fill_rounded_rect(video_x, item_y, disp_w, 24, 4, 0xE3F2FD);
                }

                /* 序号 */
                char num_str[4];
                snprintf(num_str, sizeof(num_str), "%d.", i + 1);
                desktop_puts(video_x + 8, item_y + 5, num_str, selected ? 0x4A90D9 : 0x888888);

                /* 标题 */
                desktop_puts(video_x + 30, item_y + 5, video_titles[i], selected ? 0x00 : 0x555555);

                /* 时长 */
                desktop_puts(video_x + disp_w - 50, item_y + 5, video_durations[i], 0x888888);
            }
        } else if (app_id == 17) {
            /* V0.9.0: 帮助中心应用 */
            int help_x = wx + 20;
            int help_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(help_x, help_y, "帮助中心", 0x00);
            ui_draw_button(help_x + 70, help_y - 3, 60, 22, "搜索", 0, 0);
            ui_draw_button(help_x + 135, help_y - 3, 60, 22, "反馈", 0, 0);
            gfx_draw_hline(help_x, help_y + 18, win_w - 60, 0xE2E8F0);

            /* 搜索框 */
            int search_y = help_y + 30;
            gfx_fill_rounded_rect(help_x, search_y, win_w - 60, 32, 6, 0xF5F5F5);
            gfx_draw_rect(help_x, search_y, win_w - 60, 32, 0xDDDDDD);
            desktop_puts(help_x + 12, search_y + 8, "搜索帮助文档...", 0x999999);

            /* 快速入口 */
            int quick_y = search_y + 45;
            desktop_puts(help_x, quick_y, "快速入口", 0x00);

            const char *quick_names[] = {
                "入门指南", "常见问题", "系统设置", "应用使用",
                "开发文档", "快捷键", "故障排除", "关于系统"
            };
            const uint32_t quick_colors[] = {
                0x4A90D9, 0xE94560, 0x52C41A, 0xFAAD14,
                0x722ED1, 0x13C2C2, 0xFA541C, 0x8C8C8C
            };

            for (int i = 0; i < 8; i++) {
                int col = i % 4;
                int row = i / 4;
                int qx = help_x + col * ((win_w - 80) / 4);
                int qy = quick_y + 25 + row * 60;
                int qw = (win_w - 100) / 4;

                /* 图标 */
                gfx_fill_circle(qx + qw/2, qy + 16, 14, quick_colors[i]);
                /* 名称 */
                desktop_puts(qx + qw/2 - 24, qy + 36, quick_names[i], 0x555555);
            }

            /* 常见问题 */
            int faq_y = quick_y + 160;
            desktop_puts(help_x, faq_y, "常见问题", 0x00);
            gfx_draw_hline(help_x, faq_y + 15, win_w - 60, 0xE2E8F0);

            const char *faq_questions[] = {
                "如何连接无线网络？",
                "如何安装应用程序？",
                "如何修改系统主题？",
                "如何查看系统信息？",
                "如何更新系统版本？"
            };

            for (int i = 0; i < 5; i++) {
                int item_y = faq_y + 22 + i * 32;
                gfx_draw_hline(help_x, item_y + 28, win_w - 60, 0xF0F0F0);

                /* 问题图标 */
                gfx_fill_circle(help_x + 10, item_y + 12, 8, 0x4A90D9);
                desktop_puts(help_x + 7, item_y + 7, "?", 0xFFFFFF);

                /* 问题 */
                desktop_puts(help_x + 28, item_y + 6, faq_questions[i], 0x333333);

                /* 箭头 */
                desktop_puts(help_x + win_w - 80, item_y + 6, ">", 0x999999);
            }
        } else if (app_id == 18) {
            /* V0.9.0: 设置向导应用 */
            int wizard_x = wx + 20;
            int wizard_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(wizard_x, wizard_y, "设置向导", 0x00);
            desktop_puts(wizard_x + win_w - 120, wizard_y, "步骤 2/5", 0x888888);
            gfx_draw_hline(wizard_x, wizard_y + 18, win_w - 60, 0xE2E8F0);

            /* 进度指示器 */
            int prog_y = wizard_y + 30;
            const char *step_names[] = {"欢迎", "网络", "账户", "外观", "完成"};
            int current_step = 1; /* 0-based, 当前是第2步 */

            for (int i = 0; i < 5; i++) {
                int sx = wizard_x + i * ((win_w - 80) / 4);
                int completed = (i < current_step) ? 1 : 0;
                int active = (i == current_step) ? 1 : 0;

                /* 圆圈 */
                if (completed) {
                    gfx_fill_circle(sx + 12, prog_y + 12, 12, 0x4CAF50);
                    desktop_puts(sx + 9, prog_y + 7, "✓", 0xFFFFFF);
                } else if (active) {
                    gfx_fill_circle(sx + 12, prog_y + 12, 12, 0x4A90D9);
                    char step_num[4];
                    snprintf(step_num, sizeof(step_num), "%d", i + 1);
                    desktop_puts(sx + 9, prog_y + 7, step_num, 0xFFFFFF);
                } else {
                    gfx_fill_circle(sx + 12, prog_y + 12, 12, 0xDDDDDD);
                    char step_num[4];
                    snprintf(step_num, sizeof(step_num), "%d", i + 1);
                    desktop_puts(sx + 9, prog_y + 7, step_num, 0x888888);
                }

                /* 连接线 */
                if (i < 4) {
                    gfx_fill_rect(sx + 24, prog_y + 11, (win_w - 80) / 4 - 24, 2,
                                  completed ? 0x4CAF50 : 0xDDDDDD);
                }

                /* 步骤名称 */
                desktop_puts(sx, prog_y + 30, step_names[i],
                             active ? 0x4A90D9 : (completed ? 0x4CAF50 : 0x888888));
            }

            /* 当前步骤内容 */
            int content_start_y = prog_y + 65;
            desktop_puts(wizard_x, content_start_y, "网络设置", 0x00);
            desktop_puts(wizard_x, content_start_y + 20, "连接到无线网络以获得完整体验", 0x666666);

            /* 网络列表 */
            int net_y = content_start_y + 50;
            const char *net_names[] = {
                "HepuOS_5G", "Home_WiFi", "Office_Net", "Guest_Access"
            };
            const int net_strength[] = {4, 3, 2, 1};
            const int net_secure[] = {1, 1, 0, 1};

            for (int i = 0; i < 4; i++) {
                int item_y = net_y + i * 40;
                int selected = (i == 0) ? 1 : 0;

                if (selected) {
                    gfx_fill_rounded_rect(wizard_x, item_y, win_w - 60, 35, 6, 0xE3F2FD);
                    gfx_draw_rect(wizard_x, item_y, win_w - 60, 35, 0x4A90D9);
                }

                /* WiFi图标（信号强度） */
                for (int j = 0; j < net_strength[i]; j++) {
                    gfx_fill_rect(wizard_x + 10 + j * 4, item_y + 20 - j * 3, 3, j * 3 + 3,
                                  selected ? 0x4A90D9 : 0x666666);
                }

                /* 网络名称 */
                desktop_puts(wizard_x + 35, item_y + 8, net_names[i],
                             selected ? 0x00 : 0x333333);

                /* 安全图标 */
                if (net_secure[i]) {
                    desktop_puts(wizard_x + 35, item_y + 22, "🔒 已加密", 0x888888);
                } else {
                    desktop_puts(wizard_x + 35, item_y + 22, "开放网络", 0xFAAD14);
                }
            }

            /* 底部按钮 */
            int btn_y = net_y + 4 * 40 + 20;
            ui_draw_button(wizard_x, btn_y, 70, 28, "上一步", 0, 0);
            ui_draw_button(wizard_x + 80, btn_y, 70, 28, "下一步", 1, 0);
            ui_draw_button(wizard_x + win_w - 130, btn_y, 70, 28, "跳过", 0, 0);
        } else if (app_id == 19) {
            /* V0.9.0: 反馈中心应用 */
            int fb_x = wx + 20;
            int fb_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(fb_x, fb_y, "反馈中心", 0x00);
            ui_draw_button(fb_x + 70, fb_y - 3, 60, 22, "提交", 1, 0);
            ui_draw_button(fb_x + 135, fb_y - 3, 60, 22, "历史", 0, 0);
            gfx_draw_hline(fb_x, fb_y + 18, win_w - 60, 0xE2E8F0);

            /* 反馈类型选择 */
            int type_y = fb_y + 30;
            desktop_puts(fb_x, type_y, "反馈类型", 0x00);

            const char *type_names[] = {"Bug报告", "功能建议", "性能问题", "其他"};
            const uint32_t type_colors[] = {0xE94560, 0x4A90D9, 0xFAAD14, 0x8C8C8C};

            for (int i = 0; i < 4; i++) {
                int tx = fb_x + i * ((win_w - 80) / 4);
                int tw = (win_w - 100) / 4;
                int selected = (i == 0) ? 1 : 0;

                if (selected) {
                    gfx_fill_rounded_rect(tx, type_y + 18, tw, 32, 6, type_colors[i]);
                    desktop_puts(tx + tw/2 - 24, type_y + 26, type_names[i], 0xFFFFFF);
                } else {
                    gfx_fill_rounded_rect(tx, type_y + 18, tw, 32, 6, 0xF5F5F5);
                    gfx_draw_rect(tx, type_y + 18, tw, 32, 0xDDDDDD);
                    desktop_puts(tx + tw/2 - 24, type_y + 26, type_names[i], 0x555555);
                }
            }

            /* 标题输入 */
            int title_y = type_y + 65;
            desktop_puts(fb_x, title_y, "标题", 0x00);
            gfx_fill_rounded_rect(fb_x, title_y + 18, win_w - 60, 32, 6, 0xFFFFFF);
            gfx_draw_rect(fb_x, title_y + 18, win_w - 60, 32, 0xDDDDDD);
            desktop_puts(fb_x + 12, title_y + 26, "请简要描述问题...", 0x999999);

            /* 详细描述 */
            int desc_y = title_y + 65;
            desktop_puts(fb_x, desc_y, "详细描述", 0x00);
            gfx_fill_rounded_rect(fb_x, desc_y + 18, win_w - 60, 80, 6, 0xFFFFFF);
            gfx_draw_rect(fb_x, desc_y + 18, win_w - 60, 80, 0xDDDDDD);
            desktop_puts(fb_x + 12, desc_y + 26, "请详细描述遇到的问题，包括复现步骤...", 0x999999);

            /* 附件和系统信息 */
            int extra_y = desc_y + 115;
            ui_draw_button(fb_x, extra_y, 80, 28, "添加截图", 0, 0);
            ui_draw_button(fb_x + 90, extra_y, 100, 28, "系统日志", 0, 0);

            /* 系统信息 */
            desktop_puts(fb_x + win_w - 180, extra_y + 6, "系统: HepuOS 0.10.0", 0x888888);

            /* 提交按钮 */
            int submit_y = extra_y + 45;
            ui_draw_button(fb_x + win_w - 130, submit_y, 70, 32, "提交反馈", 1, 0);

            /* 最近反馈 */
            int recent_y = submit_y + 55;
            desktop_puts(fb_x, recent_y, "最近反馈", 0x00);
            gfx_draw_hline(fb_x, recent_y + 15, win_w - 60, 0xE2E8F0);

            const char *recent_titles[] = {
                "应用商店无法下载应用",
                "WiFi连接不稳定",
                "建议增加深色模式"
            };
            const char *recent_status[] = {"处理中", "已修复", "已采纳"};
            const uint32_t status_colors[] = {0xFAAD14, 0x4CAF50, 0x4A90D9};

            for (int i = 0; i < 3; i++) {
                int item_y = recent_y + 22 + i * 30;
                gfx_draw_hline(fb_x, item_y + 26, win_w - 60, 0xF0F0F0);

                desktop_puts(fb_x + 8, item_y + 5, recent_titles[i], 0x333333);

                /* 状态标签 */
                gfx_fill_rounded_rect(fb_x + win_w - 100, item_y + 2, 50, 20, 4, status_colors[i]);
                desktop_puts(fb_x + win_w - 92, item_y + 5, recent_status[i], 0xFFFFFF);
            }
        } else if (app_id == 20) {
            /* V0.9.0: 游戏中心应用 */
            int game_x = wx + 20;
            int game_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(game_x, game_y, "游戏中心", 0x00);
            ui_draw_button(game_x + 70, game_y - 3, 60, 22, "我的", 1, 0);
            ui_draw_button(game_x + 135, game_y - 3, 60, 22, "排行榜", 0, 0);
            ui_draw_button(game_x + 200, game_y - 3, 60, 22, "成就", 0, 0);
            gfx_draw_hline(game_x, game_y + 18, win_w - 60, 0xE2E8F0);

            /* 推荐游戏横幅 */
            int banner_y = game_y + 30;
            gfx_fill_rounded_rect(game_x, banner_y, win_w - 60, 70, 8, 0x1A1A2E);
            /* 横幅装饰 */
            gfx_fill_circle(game_x + 50, banner_y + 35, 25, 0xE94560);
            gfx_fill_circle(game_x + 80, banner_y + 25, 15, 0xFAAD14);
            desktop_puts(game_x + 120, banner_y + 15, "HepuOS 游戏大赛", 0xFFFFFF);
            desktop_puts(game_x + 120, banner_y + 38, "参与赢取限定皮肤", 0xFFD700);
            ui_draw_button(game_x + win_w - 120, banner_y + 22, 60, 28, "立即参与", 1, 0);

            /* 游戏分类 */
            int cat_y = banner_y + 85;
            desktop_puts(game_x, cat_y, "游戏分类", 0x00);

            const char *cat_names[] = {"全部", "休闲", "益智", "动作", "策略", "竞速"};
            for (int i = 0; i < 6; i++) {
                int cx = game_x + i * ((win_w - 80) / 5);
                int selected = (i == 0) ? 1 : 0;

                if (selected) {
                    gfx_fill_rounded_rect(cx, cat_y + 18, 50, 28, 14, 0xE94560);
                    desktop_puts(cx + 14, cat_y + 25, cat_names[i], 0xFFFFFF);
                } else {
                    gfx_fill_rounded_rect(cx, cat_y + 18, 50, 28, 14, 0xF5F5F5);
                    desktop_puts(cx + 14, cat_y + 25, cat_names[i], 0x555555);
                }
            }

            /* 游戏列表 */
            int list_y = cat_y + 65;
            desktop_puts(game_x, list_y, "热门游戏", 0x00);

            const char *game_names[] = {
                "贪吃蛇", "俄罗斯方块", "扫雷", "2048", "五子棋"
            };
            const char *game_desc[] = {
                "经典休闲游戏", "益智消除游戏", "经典逻辑游戏", "数字合成游戏", "策略对弈游戏"
            };
            const char *game_rating[] = {"4.8", "4.9", "4.7", "4.6", "4.8"};
            const uint32_t game_colors[] = {
                0x4CAF50, 0x4A90D9, 0xE94560, 0xFAAD14, 0x722ED1
            };

            for (int i = 0; i < 5; i++) {
                int item_y = list_y + 22 + i * 55;
                gfx_draw_hline(game_x, item_y + 50, win_w - 60, 0xF0F0F0);

                /* 游戏图标 */
                gfx_fill_rounded_rect(game_x, item_y, 44, 44, 8, game_colors[i]);
                desktop_puts(game_x + 14, item_y + 14, &game_names[i][0], 0xFFFFFF);

                /* 游戏名称 */
                desktop_puts(game_x + 55, item_y + 5, game_names[i], 0x00);

                /* 游戏描述 */
                desktop_puts(game_x + 55, item_y + 24, game_desc[i], 0x888888);

                /* 评分 */
                desktop_puts(game_x + 55, item_y + 40, "★", 0xFFD700);
                desktop_puts(game_x + 70, item_y + 40, game_rating[i], 0x555555);

                /* 安装/打开按钮 */
                ui_draw_button(game_x + win_w - 100, item_y + 10, 60, 28, "打开", 1, 0);
            }
        } else if (app_id == 21) {
            /* V0.9.0: 画板应用 */
            int paint_x = wx + 20;
            int paint_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(paint_x, paint_y, "画板", 0x00);
            ui_draw_button(paint_x + 50, paint_y - 3, 50, 22, "新建", 0, 0);
            ui_draw_button(paint_x + 105, paint_y - 3, 50, 22, "保存", 0, 0);
            ui_draw_button(paint_x + 160, paint_y - 3, 50, 22, "撤销", 0, 0);
            gfx_draw_hline(paint_x, paint_y + 18, win_w - 60, 0xE2E8F0);

            /* 工具栏 */
            int tool_y = paint_y + 30;
            desktop_puts(paint_x, tool_y, "工具", 0x00);

            const char *tool_names[] = {"画笔", "橡皮", "直线", "矩形", "圆形", "文字"};
            for (int i = 0; i < 6; i++) {
                int tx = paint_x + i * 55;
                int selected = (i == 0) ? 1 : 0;

                if (selected) {
                    gfx_fill_rounded_rect(tx, tool_y + 18, 48, 32, 6, 0x722ED1);
                    desktop_puts(tx + 12, tool_y + 26, tool_names[i], 0xFFFFFF);
                } else {
                    gfx_fill_rounded_rect(tx, tool_y + 18, 48, 32, 6, 0xF5F5F5);
                    gfx_draw_rect(tx, tool_y + 18, 48, 32, 0xDDDDDD);
                    desktop_puts(tx + 12, tool_y + 26, tool_names[i], 0x555555);
                }
            }

            /* 颜色选择器 */
            int color_y = tool_y + 65;
            desktop_puts(paint_x, color_y, "颜色", 0x00);

            const uint32_t paint_colors[] = {
                0x000000, 0xFFFFFF, 0xE94560, 0xFAAD14, 0x4CAF50,
                0x4A90D9, 0x722ED1, 0x13C2C2, 0xFA541C, 0x8C8C8C
            };

            for (int i = 0; i < 10; i++) {
                int cx = paint_x + i * 32;
                int selected = (i == 0) ? 1 : 0;

                gfx_fill_circle(cx + 12, color_y + 30, 12, paint_colors[i]);
                if (selected) {
                    gfx_draw_circle(cx + 12, color_y + 30, 14, 0x722ED1);
                }
            }

            /* 画笔大小 */
            int size_y = color_y + 55;
            desktop_puts(paint_x, size_y, "画笔大小", 0x00);
            ui_draw_slider(paint_x + 70, size_y - 2, 100, 3, 1, 20);
            desktop_puts(paint_x + 180, size_y, "3px", 0x555555);

            /* 画布区域 */
            int canvas_y = size_y + 35;
            int canvas_w = win_w - 60;
            int canvas_h = 120;

            gfx_fill_rect(paint_x, canvas_y, canvas_w, canvas_h, 0xFFFFFF);
            gfx_draw_rect(paint_x, canvas_y, canvas_w, canvas_h, 0xDDDDDD);

            /* 示例画作（简单图形） */
            /* 太阳 */
            gfx_fill_circle(paint_x + 50, canvas_y + 30, 15, 0xFFD700);
            /* 太阳光芒（8个方向，固定坐标） */
            gfx_fill_rect(paint_x + 49, canvas_y + 5, 2, 8, 0xFFD700); /* 上 */
            gfx_fill_rect(paint_x + 49, canvas_y + 47, 2, 8, 0xFFD700); /* 下 */
            gfx_fill_rect(paint_x + 25, canvas_y + 29, 8, 2, 0xFFD700); /* 左 */
            gfx_fill_rect(paint_x + 67, canvas_y + 29, 8, 2, 0xFFD700); /* 右 */
            gfx_fill_rect(paint_x + 32, canvas_y + 12, 4, 4, 0xFFD700); /* 左上 */
            gfx_fill_rect(paint_x + 64, canvas_y + 12, 4, 4, 0xFFD700); /* 右上 */
            gfx_fill_rect(paint_x + 32, canvas_y + 44, 4, 4, 0xFFD700); /* 左下 */
            gfx_fill_rect(paint_x + 64, canvas_y + 44, 4, 4, 0xFFD700); /* 右下 */
            /* 房子 */
            gfx_fill_rect(paint_x + 120, canvas_y + 50, 50, 40, 0x8B4513);
            gfx_fill_rect(paint_x + 115, canvas_y + 35, 60, 20, 0xE94560);
            gfx_fill_rect(paint_x + 135, canvas_y + 65, 15, 25, 0x4A90D9);
            /* 树 */
            gfx_fill_rect(paint_x + 220, canvas_y + 60, 10, 30, 0x8B4513);
            gfx_fill_circle(paint_x + 225, canvas_y + 50, 20, 0x4CAF50);
            /* 草地 */
            gfx_fill_rect(paint_x, canvas_y + canvas_h - 15, canvas_w, 15, 0x4CAF50);

            /* 底部状态栏 */
            int status_y = canvas_y + canvas_h + 10;
            gfx_draw_hline(paint_x, status_y, canvas_w, 0xE2E8F0);
            desktop_puts(paint_x, status_y + 8, "画布: 800x600 | 工具: 画笔 | 颜色: 黑色", 0x888888);
        } else if (app_id == 22) {
            /* V0.9.0: 时钟应用 */
            int clock_x = wx + 20;
            int clock_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(clock_x, clock_y, "时钟", 0x00);
            ui_draw_button(clock_x + 50, clock_y - 3, 50, 22, "世界时钟", 1, 0);
            ui_draw_button(clock_x + 105, clock_y - 3, 50, 22, "秒表", 0, 0);
            ui_draw_button(clock_x + 160, clock_y - 3, 50, 22, "闹钟", 0, 0);
            gfx_draw_hline(clock_x, clock_y + 18, win_w - 60, 0xE2E8F0);

            /* 模拟时钟 */
            int analog_cx = clock_x + 80;
            int analog_cy = clock_y + 100;
            int analog_r = 60;

            /* 表盘外圈 */
            gfx_fill_circle(analog_cx, analog_cy, analog_r, 0x1A1A2E);
            /* 表盘内圈 */
            gfx_fill_circle(analog_cx, analog_cy, analog_r - 4, 0xFFFFFF);

            /* 刻度 */
            for (int i = 0; i < 12; i++) {
                int angle = i * 30;
                int x1 = analog_cx + (int)((analog_r - 8) * 1);
                int y1 = analog_cy;
                /* 简化：用固定位置绘制12个刻度 */
                int tick_x = analog_cx;
                int tick_y = analog_cy - analog_r + 10;
                if (i % 3 == 0) {
                    gfx_fill_rect(tick_x - 2, tick_y, 4, 6, 0x333333);
                } else {
                    gfx_fill_rect(tick_x - 1, tick_y, 2, 4, 0x888888);
                }
            }

            /* 数字刻度（12, 3, 6, 9） */
            desktop_puts(analog_cx - 4, analog_cy - analog_r + 14, "12", 0x333333);
            desktop_puts(analog_cx + analog_r - 18, analog_cy - 4, "3", 0x333333);
            desktop_puts(analog_cx - 2, analog_cy + analog_r - 20, "6", 0x333333);
            desktop_puts(analog_cx - analog_r + 12, analog_cy - 4, "9", 0x333333);

            /* 时针（指向10点） */
            gfx_fill_rect(analog_cx - 3, analog_cy - 30, 6, 35, 0x333333);
            /* 分针（指向10分） */
            gfx_fill_rect(analog_cx - 2, analog_cy - 45, 4, 50, 0x555555);
            /* 秒针（指向30秒） */
            gfx_fill_rect(analog_cx - 1, analog_cy - 50, 2, 55, 0xE94560);
            /* 中心点 */
            gfx_fill_circle(analog_cx, analog_cy, 5, 0xE94560);

            /* 数字时钟 */
            int digital_x = clock_x + 180;
            int digital_y = clock_y + 50;

            desktop_puts(digital_x, digital_y, "10:10:30", 0x00);
            desktop_puts(digital_x, digital_y + 30, "2026年10月7日 星期三", 0x666666);
            desktop_puts(digital_x, digital_y + 55, "农历八月廿七", 0x888888);

            /* 时区信息 */
            gfx_fill_rounded_rect(digital_x, digital_y + 85, 150, 30, 6, 0xE3F2FD);
            desktop_puts(digital_x + 10, digital_y + 92, "UTC+8 北京时间", 0x4A90D9);

            /* 世界时钟列表 */
            int world_y = clock_y + 170;
            desktop_puts(clock_x, world_y, "世界时钟", 0x00);
            gfx_draw_hline(clock_x, world_y + 15, win_w - 60, 0xE2E8F0);

            const char *city_names[] = {"北京", "东京", "伦敦", "纽约", "悉尼"};
            const char *city_times[] = {"10:10", "11:10", "02:10", "21:10", "13:10"};
            const char *city_zones[] = {"UTC+8", "UTC+9", "UTC+0", "UTC-5", "UTC+10"};

            for (int i = 0; i < 5; i++) {
                int item_y = world_y + 22 + i * 32;
                gfx_draw_hline(clock_x, item_y + 28, win_w - 60, 0xF0F0F0);

                /* 城市名 */
                desktop_puts(clock_x + 8, item_y + 6, city_names[i], 0x00);
                /* 时区 */
                desktop_puts(clock_x + 60, item_y + 6, city_zones[i], 0x888888);
                /* 时间 */
                desktop_puts(clock_x + win_w - 80, item_y + 6, city_times[i], 0x4A90D9);
            }
        } else if (app_id == 23) {
            /* V0.9.0: 系统更新应用 */
            int update_x = wx + 20;
            int update_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(update_x, update_y, "系统更新", 0x00);
            ui_draw_button(update_x + 70, update_y - 3, 60, 22, "检查更新", 1, 0);
            ui_draw_button(update_x + 135, update_y - 3, 60, 22, "更新历史", 0, 0);
            gfx_draw_hline(update_x, update_y + 18, win_w - 60, 0xE2E8F0);

            /* 当前版本信息 */
            int ver_y = update_y + 30;
            gfx_fill_rounded_rect(update_x, ver_y, win_w - 60, 70, 8, 0xE8F5E9);
            gfx_draw_rect(update_x, ver_y, win_w - 60, 70, 0x4CAF50);

            desktop_puts(update_x + 15, ver_y + 10, "当前系统已是最新版本", 0x2E7D32);
            desktop_puts(update_x + 15, ver_y + 32, "HepuOS 0.10.0", 0x00);
            desktop_puts(update_x + 15, ver_y + 50, "发布日期: 2026-10-07 | 内核: 4.0MB", 0x666666);

            /* 检查更新按钮 */
            ui_draw_button(update_x + win_w - 100, ver_y + 20, 70, 32, "检查更新", 1, 0);

            /* 更新设置 */
            int setting_y = ver_y + 85;
            desktop_puts(update_x, setting_y, "更新设置", 0x00);
            gfx_draw_hline(update_x, setting_y + 15, win_w - 60, 0xE2E8F0);

            /* 自动更新开关 */
            int auto_y = setting_y + 25;
            desktop_puts(update_x + 8, auto_y + 5, "自动下载更新", 0x333333);
            desktop_puts(update_x + 8, auto_y + 22, "在WiFi环境下自动下载系统更新", 0x888888);
            /* 开关（开启状态） */
            gfx_fill_rounded_rect(update_x + win_w - 80, auto_y + 8, 50, 26, 13, 0x4CAF50);
            gfx_fill_circle(update_x + win_w - 45, auto_y + 21, 10, 0xFFFFFF);

            /* 通知开关 */
            int notify_y = auto_y + 50;
            desktop_puts(update_x + 8, notify_y + 5, "更新通知", 0x333333);
            desktop_puts(update_x + 8, notify_y + 22, "有新更新时发送通知提醒", 0x888888);
            /* 开关（开启状态） */
            gfx_fill_rounded_rect(update_x + win_w - 80, notify_y + 8, 50, 26, 13, 0x4CAF50);
            gfx_fill_circle(update_x + win_w - 45, notify_y + 21, 10, 0xFFFFFF);

            /* 计量网络开关 */
            int meter_y = notify_y + 50;
            desktop_puts(update_x + 8, meter_y + 5, "计量网络下更新", 0x333333);
            desktop_puts(update_x + 8, meter_y + 22, "允许在计量网络连接下下载更新", 0x888888);
            /* 开关（关闭状态） */
            gfx_fill_rounded_rect(update_x + win_w - 80, meter_y + 8, 50, 26, 13, 0xCCCCCC);
            gfx_fill_circle(update_x + win_w - 65, meter_y + 21, 10, 0xFFFFFF);

            /* 更新历史 */
            int history_y = meter_y + 60;
            desktop_puts(update_x, history_y, "最近更新", 0x00);
            gfx_draw_hline(update_x, history_y + 15, win_w - 60, 0xE2E8F0);

            const char *history_versions[] = {"0.10.0", "0.9.0", "0.8.9"};
            const char *history_dates[] = {"2026-10-07", "2026-10-07", "2026-10-05"};
            const char *history_desc[] = {
                "新增25个桌面应用，完善应用生态",
                "版本号格式统一为三段式",
                "修复引导程序问题，优化内存管理"
            };

            for (int i = 0; i < 3; i++) {
                int item_y = history_y + 22 + i * 45;
                gfx_draw_hline(update_x, item_y + 40, win_w - 60, 0xF0F0F0);

                /* 版本号 */
                desktop_puts(update_x + 8, item_y + 5, history_versions[i], 0x4A90D9);
                /* 日期 */
                desktop_puts(update_x + 60, item_y + 5, history_dates[i], 0x888888);
                /* 描述 */
                desktop_puts(update_x + 8, item_y + 22, history_desc[i], 0x555555);
            }
        } else if (app_id == 24) {
            /* V0.9.0: 文件搜索应用 */
            int search_x = wx + 20;
            int search_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(search_x, search_y, "文件搜索", 0x00);
            ui_draw_button(search_x + 70, search_y - 3, 60, 22, "高级", 0, 0);
            gfx_draw_hline(search_x, search_y + 18, win_w - 60, 0xE2E8F0);

            /* 搜索框 */
            int box_y = search_y + 30;
            gfx_fill_rounded_rect(search_x, box_y, win_w - 60, 40, 8, 0xFFFFFF);
            gfx_draw_rect(search_x, box_y, win_w - 60, 40, 0x4A90D9);
            /* 搜索图标 */
            gfx_fill_circle(search_x + 20, box_y + 20, 8, 0x4A90D9);
            gfx_fill_rect(search_x + 26, box_y + 26, 8, 3, 0x4A90D9);
            /* 搜索文字 */
            desktop_puts(search_x + 40, box_y + 12, "输入文件名或内容搜索...", 0x999999);
            /* 搜索按钮 */
            ui_draw_button(search_x + win_w - 110, box_y + 5, 50, 30, "搜索", 1, 0);

            /* 搜索过滤器 */
            int filter_y = box_y + 55;
            desktop_puts(search_x, filter_y, "文件类型", 0x00);

            const char *filter_names[] = {"全部", "文档", "图片", "视频", "音乐", "其他"};
            for (int i = 0; i < 6; i++) {
                int fx = search_x + i * 55;
                int selected = (i == 0) ? 1 : 0;

                if (selected) {
                    gfx_fill_rounded_rect(fx, filter_y + 18, 48, 26, 13, 0x4A90D9);
                    desktop_puts(fx + 12, filter_y + 24, filter_names[i], 0xFFFFFF);
                } else {
                    gfx_fill_rounded_rect(fx, filter_y + 18, 48, 26, 13, 0xF5F5F5);
                    desktop_puts(fx + 12, filter_y + 24, filter_names[i], 0x555555);
                }
            }

            /* 搜索结果 */
            int result_y = filter_y + 60;
            desktop_puts(search_x, result_y, "搜索结果 (5个文件)", 0x00);
            gfx_draw_hline(search_x, result_y + 15, win_w - 60, 0xE2E8F0);

            const char *result_names[] = {
                "项目计划.docx",
                "会议记录.txt",
                "系统架构.png",
                "演示视频.mp4",
                "背景音乐.mp3"
            };
            const char *result_paths[] = {
                "/home/user/Documents/",
                "/home/user/Documents/Notes/",
                "/home/user/Pictures/",
                "/home/user/Videos/",
                "/home/user/Music/"
            };
            const char *result_sizes[] = {"256 KB", "12 KB", "2.4 MB", "15.6 MB", "4.8 MB"};
            const char *result_dates[] = {
                "2026-10-07 10:30",
                "2026-10-06 15:20",
                "2026-10-05 09:15",
                "2026-10-04 20:00",
                "2026-10-03 14:45"
            };
            const uint32_t result_colors[] = {
                0x4A90D9, 0x52C41A, 0xE94560, 0xFAAD14, 0x722ED1
            };

            for (int i = 0; i < 5; i++) {
                int item_y = result_y + 22 + i * 50;
                gfx_draw_hline(search_x, item_y + 45, win_w - 60, 0xF0F0F0);

                /* 文件图标 */
                gfx_fill_rounded_rect(search_x + 5, item_y + 5, 32, 36, 4, result_colors[i]);
                gfx_fill_rect(search_x + 5, item_y + 5, 12, 10, 0xFFFFFF);

                /* 文件名 */
                desktop_puts(search_x + 45, item_y + 5, result_names[i], 0x00);
                /* 路径 */
                desktop_puts(search_x + 45, item_y + 22, result_paths[i], 0x888888);
                /* 大小 */
                desktop_puts(search_x + win_w - 180, item_y + 5, result_sizes[i], 0x555555);
                /* 日期 */
                desktop_puts(search_x + win_w - 180, item_y + 22, result_dates[i], 0x888888);
            }
        } else if (app_id == 25) {
            /* V0.10.0: 下载管理器应用 */
            int dl_x = wx + 20;
            int dl_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(dl_x, dl_y, "下载管理器", 0x00);
            ui_draw_button(dl_x + 90, dl_y - 3, 60, 22, "全部暂停", 0, 0);
            ui_draw_button(dl_x + 155, dl_y - 3, 60, 22, "清除已完成", 0, 0);
            gfx_draw_hline(dl_x, dl_y + 18, win_w - 60, 0xE2E8F0);

            /* 下载统计 */
            int stat_y = dl_y + 30;
            gfx_fill_rounded_rect(dl_x, stat_y, win_w - 60, 50, 8, 0xE3F2FD);
            desktop_puts(dl_x + 15, stat_y + 8, "正在下载: 2 | 已完成: 5 | 总速度: 2.5 MB/s", 0x1565C0);
            desktop_puts(dl_x + 15, stat_y + 28, "下载目录: /home/user/Downloads/", 0x1976D2);

            /* 下载列表 */
            int list_y = stat_y + 65;
            desktop_puts(dl_x, list_y, "下载任务", 0x00);
            gfx_draw_hline(dl_x, list_y + 15, win_w - 60, 0xE2E8F0);

            /* 下载任务数据 */
            const char *dl_names[] = {
                "HepuOS_0.10.0_ISO.iso",
                "开发文档合集.pdf",
                "示例视频.mp4",
                "音乐专辑.zip",
                "应用安装包.hpkg",
                "系统更新包.bin",
                "字体文件.ttf"
            };
            const char *dl_sizes[] = {
                "101 MB / 101 MB",
                "45 MB / 45 MB",
                "156 MB / 250 MB",
                "78 MB / 78 MB",
                "12 MB / 12 MB",
                "4.0 MB / 4.0 MB",
                "8 MB / 8 MB"
            };
            const int dl_progress[] = {100, 100, 62, 100, 100, 100, 100};
            const char *dl_status[] = {
                "已完成", "已完成", "下载中", "已完成", "已完成", "已完成", "已完成"
            };
            const char *dl_speeds[] = {
                "-", "-", "2.5 MB/s", "-", "-", "-", "-"
            };
            const uint32_t dl_colors[] = {
                0x4CAF50, 0x4CAF50, 0x4A90D9, 0x4CAF50, 0x4CAF50, 0x4CAF50, 0x4CAF50
            };

            for (int i = 0; i < 7; i++) {
                int item_y = list_y + 22 + i * 52;
                gfx_draw_hline(dl_x, item_y + 48, win_w - 60, 0xF0F0F0);

                /* 文件图标 */
                gfx_fill_rounded_rect(dl_x + 5, item_y + 5, 32, 36, 4, dl_colors[i]);
                gfx_fill_rect(dl_x + 5, item_y + 5, 12, 10, 0xFFFFFF);

                /* 文件名 */
                desktop_puts(dl_x + 45, item_y + 5, dl_names[i], 0x00);
                /* 大小 */
                desktop_puts(dl_x + 45, item_y + 22, dl_sizes[i], 0x888888);

                /* 进度条 */
                ui_draw_progress(dl_x + 45, item_y + 38, win_w - 200, 6, dl_progress[i]);

                /* 状态标签 */
                if (dl_progress[i] == 100) {
                    gfx_fill_rounded_rect(dl_x + win_w - 100, item_y + 8, 50, 20, 4, 0x4CAF50);
                    desktop_puts(dl_x + win_w - 92, item_y + 11, dl_status[i], 0xFFFFFF);
                } else {
                    gfx_fill_rounded_rect(dl_x + win_w - 100, item_y + 8, 50, 20, 4, 0x4A90D9);
                    desktop_puts(dl_x + win_w - 92, item_y + 11, dl_status[i], 0xFFFFFF);
                    desktop_puts(dl_x + win_w - 100, item_y + 32, dl_speeds[i], 0x4A90D9);
                }
            }
        } else if (app_id == 26) {
            /* V0.10.0: 打印管理应用 */
            int print_x = wx + 20;
            int print_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(print_x, print_y, "打印管理", 0x00);
            ui_draw_button(print_x + 80, print_y - 3, 60, 22, "添加打印机", 0, 0);
            ui_draw_button(print_x + 145, print_y - 3, 60, 22, "打印测试页", 0, 0);
            gfx_draw_hline(print_x, print_y + 18, win_w - 60, 0xE2E8F0);

            /* 打印机列表 */
            int printer_y = print_y + 30;
            desktop_puts(print_x, printer_y, "打印机", 0x00);
            gfx_draw_hline(print_x, printer_y + 15, win_w - 60, 0xE2E8F0);

            /* 打印机数据 */
            const char *printer_names[] = {
                "HP LaserJet Pro",
                "Canon PIXMA",
                "EPSON L3150",
                "PDF虚拟打印机"
            };
            const char *printer_status[] = {"就绪", "忙碌", "离线", "就绪"};
            const char *printer_queue[] = {"0个任务", "2个任务", "-", "0个任务"};
            const uint32_t status_colors[] = {0x4CAF50, 0xFAAD14, 0x9E9E9E, 0x4CAF50};

            for (int i = 0; i < 4; i++) {
                int item_y = printer_y + 22 + i * 55;
                int selected = (i == 0) ? 1 : 0;

                if (selected) {
                    gfx_fill_rounded_rect(print_x, item_y, win_w - 60, 50, 6, 0xE3F2FD);
                    gfx_draw_rect(print_x, item_y, win_w - 60, 50, 0x4A90D9);
                }

                /* 打印机图标 */
                gfx_fill_rounded_rect(print_x + 8, item_y + 8, 36, 34, 4, 0x607D8B);
                gfx_fill_rect(print_x + 14, item_y + 5, 24, 6, 0xFFFFFF);
                gfx_fill_rect(print_x + 14, item_y + 36, 24, 8, 0xFFFFFF);

                /* 打印机名称 */
                desktop_puts(print_x + 55, item_y + 8, printer_names[i], 0x00);
                /* 状态 */
                gfx_fill_rounded_rect(print_x + 55, item_y + 26, 50, 18, 4, status_colors[i]);
                desktop_puts(print_x + 63, item_y + 29, printer_status[i], 0xFFFFFF);
                /* 队列 */
                desktop_puts(print_x + 120, item_y + 29, printer_queue[i], 0x888888);

                /* 操作按钮 */
                ui_draw_button(print_x + win_w - 130, item_y + 12, 55, 26, "打印", 1, 0);
                ui_draw_button(print_x + win_w - 70, item_y + 12, 55, 26, "属性", 0, 0);
            }

            /* 打印队列 */
            int queue_y = printer_y + 22 + 4 * 55 + 15;
            desktop_puts(print_x, queue_y, "打印队列", 0x00);
            gfx_draw_hline(print_x, queue_y + 15, win_w - 60, 0xE2E8F0);

            const char *queue_docs[] = {
                "项目计划书.docx",
                "系统架构图.pdf"
            };
            const char *queue_status[] = {"打印中", "等待中"};
            const int queue_progress[] = {65, 0};

            for (int i = 0; i < 2; i++) {
                int item_y = queue_y + 22 + i * 40;
                gfx_draw_hline(print_x, item_y + 36, win_w - 60, 0xF0F0F0);

                desktop_puts(print_x + 8, item_y + 5, queue_docs[i], 0x00);
                desktop_puts(print_x + 8, item_y + 20, queue_status[i], 0x888888);

                if (queue_progress[i] > 0) {
                    ui_draw_progress(print_x + 150, item_y + 8, win_w - 250, 6, queue_progress[i]);
                }

                ui_draw_button(print_x + win_w - 70, item_y + 5, 55, 26, "取消", 0, 0);
            }
        } else if (app_id == 27) {
            /* V0.10.0: 磁盘清理应用 */
            int clean_x = wx + 20;
            int clean_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(clean_x, clean_y, "磁盘清理", 0x00);
            ui_draw_button(clean_x + 80, clean_y - 3, 60, 22, "扫描", 1, 0);
            ui_draw_button(clean_x + 145, clean_y - 3, 60, 22, "清理", 0, 0);
            gfx_draw_hline(clean_x, clean_y + 18, win_w - 60, 0xE2E8F0);

            /* 磁盘使用情况 */
            int disk_y = clean_y + 30;
            gfx_fill_rounded_rect(clean_x, disk_y, win_w - 60, 60, 8, 0xF5F5F5);
            desktop_puts(clean_x + 15, disk_y + 8, "系统盘 (C:)", 0x00);
            desktop_puts(clean_x + 15, disk_y + 26, "已使用 85.6 GB / 150 GB", 0x555555);
            /* 进度条 */
            ui_draw_progress(clean_x + 15, disk_y + 44, win_w - 90, 8, 57);
            desktop_puts(clean_x + win_w - 80, disk_y + 42, "57%", 0xFAAD14);

            /* 可清理项列表 */
            int item_y = disk_y + 75;
            desktop_puts(clean_x, item_y, "可清理项", 0x00);
            gfx_draw_hline(clean_x, item_y + 15, win_w - 60, 0xE2E8F0);

            const char *clean_items[] = {
                "临时文件",
                "系统缓存",
                "下载目录",
                "回收站",
                "日志文件",
                "缩略图缓存",
                "更新缓存"
            };
            const char *clean_sizes[] = {
                "256 MB",
                "1.2 GB",
                "3.5 GB",
                "512 MB",
                "128 MB",
                "89 MB",
                "2.1 GB"
            };
            const int clean_checked[] = {1, 1, 0, 1, 1, 0, 1};

            for (int i = 0; i < 7; i++) {
                int row_y = item_y + 22 + i * 36;
                gfx_draw_hline(clean_x, row_y + 32, win_w - 60, 0xF0F0F0);

                /* 复选框 */
                if (clean_checked[i]) {
                    gfx_fill_rounded_rect(clean_x + 5, row_y + 6, 18, 18, 3, 0x4CAF50);
                    gfx_draw_rect(clean_x + 9, row_y + 9, 4, 8, 0xFFFFFF);
                    gfx_draw_rect(clean_x + 13, row_y + 7, 4, 10, 0xFFFFFF);
                } else {
                    gfx_draw_rounded_rect(clean_x + 5, row_y + 6, 18, 18, 3, 0xCCCCCC);
                }

                /* 项目名称 */
                desktop_puts(clean_x + 32, row_y + 8, clean_items[i], 0x00);
                /* 大小 */
                desktop_puts(clean_x + win_w - 100, row_y + 8, clean_sizes[i], 0x888888);
            }

            /* 总计 */
            int total_y = item_y + 22 + 7 * 36 + 10;
            gfx_fill_rounded_rect(clean_x, total_y, win_w - 60, 36, 6, 0xE8F5E9);
            desktop_puts(clean_x + 15, total_y + 10, "可释放空间: 6.1 GB", 0x2E7D32);
            ui_draw_button(clean_x + win_w - 100, total_y + 5, 70, 26, "立即清理", 1, 0);
        } else if (app_id == 28) {
            /* V0.10.0: 备份恢复应用 */
            int bk_x = wx + 20;
            int bk_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(bk_x, bk_y, "备份与恢复", 0x00);
            ui_draw_button(bk_x + 90, bk_y - 3, 60, 22, "立即备份", 1, 0);
            ui_draw_button(bk_x + 155, bk_y - 3, 60, 22, "恢复", 0, 0);
            gfx_draw_hline(bk_x, bk_y + 18, win_w - 60, 0xE2E8F0);

            /* 备份状态 */
            int status_y = bk_y + 30;
            gfx_fill_rounded_rect(bk_x, status_y, win_w - 60, 55, 8, 0xE8F5E9);
            desktop_puts(bk_x + 15, status_y + 8, "上次备份: 2026-10-06 22:30", 0x2E7D32);
            desktop_puts(bk_x + 15, status_y + 28, "备份大小: 12.5 GB | 位置: 外部磁盘", 0x388E3C);

            /* 备份选项 */
            int opt_y = status_y + 70;
            desktop_puts(bk_x, opt_y, "备份内容", 0x00);
            gfx_draw_hline(bk_x, opt_y + 15, win_w - 60, 0xE2E8F0);

            const char *bk_items[] = {
                "系统文件",
                "用户文档",
                "应用设置",
                "邮件和联系人",
                "浏览器数据",
                "桌面配置"
            };
            const char *bk_sizes[] = {
                "8.5 GB",
                "2.3 GB",
                "512 MB",
                "128 MB",
                "256 MB",
                "64 MB"
            };
            const int bk_checked[] = {1, 1, 1, 0, 1, 1};

            for (int i = 0; i < 6; i++) {
                int row_y = opt_y + 22 + i * 34;
                gfx_draw_hline(bk_x, row_y + 30, win_w - 60, 0xF0F0F0);

                /* 复选框 */
                if (bk_checked[i]) {
                    gfx_fill_rounded_rect(bk_x + 5, row_y + 5, 18, 18, 3, 0x4CAF50);
                    gfx_draw_rect(bk_x + 9, row_y + 8, 4, 8, 0xFFFFFF);
                    gfx_draw_rect(bk_x + 13, row_y + 6, 4, 10, 0xFFFFFF);
                } else {
                    gfx_draw_rounded_rect(bk_x + 5, row_y + 5, 18, 18, 3, 0xCCCCCC);
                }

                desktop_puts(bk_x + 32, row_y + 7, bk_items[i], 0x00);
                desktop_puts(bk_x + win_w - 100, row_y + 7, bk_sizes[i], 0x888888);
            }

            /* 备份历史 */
            int hist_y = opt_y + 22 + 6 * 34 + 10;
            desktop_puts(bk_x, hist_y, "备份历史", 0x00);
            gfx_draw_hline(bk_x, hist_y + 15, win_w - 60, 0xE2E8F0);

            const char *hist_dates[] = {
                "2026-10-06 22:30",
                "2026-10-03 20:15",
                "2026-09-30 23:00"
            };
            const char *hist_types[] = {"完整备份", "增量备份", "完整备份"};

            for (int i = 0; i < 3; i++) {
                int row_y = hist_y + 22 + i * 32;
                gfx_draw_hline(bk_x, row_y + 28, win_w - 60, 0xF0F0F0);
                desktop_puts(bk_x + 8, row_y + 6, hist_dates[i], 0x00);
                desktop_puts(bk_x + 160, row_y + 6, hist_types[i], 0x888888);
                ui_draw_button(bk_x + win_w - 80, row_y + 2, 55, 24, "恢复", 0, 0);
            }
        } else if (app_id == 29) {
            /* V0.10.0: 密码管理器应用 */
            int pw_x = wx + 20;
            int pw_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(pw_x, pw_y, "密码管理器", 0x00);
            ui_draw_button(pw_x + 90, pw_y - 3, 60, 22, "添加", 1, 0);
            ui_draw_button(pw_x + 155, pw_y - 3, 60, 22, "生成", 0, 0);
            gfx_draw_hline(pw_x, pw_y + 18, win_w - 60, 0xE2E8F0);

            /* 主密码提示 */
            int lock_y = pw_y + 30;
            gfx_fill_rounded_rect(pw_x, lock_y, win_w - 60, 45, 8, 0xF3E5F5);
            desktop_puts(pw_x + 15, lock_y + 8, "已解锁 | 主密码: 已设置", 0x6A1B9A);
            desktop_puts(pw_x + 15, lock_y + 26, "自动锁定: 5分钟 | 加密: AES-256", 0x8E24AA);

            /* 搜索框 */
            int search_y = lock_y + 60;
            gfx_fill_rounded_rect(pw_x, search_y, win_w - 60, 32, 6, 0xF5F5F5);
            gfx_draw_rect(pw_x, search_y, win_w - 60, 32, 0xE0E0E0);
            desktop_puts(pw_x + 12, search_y + 9, "搜索密码...", 0x999999);

            /* 密码列表 */
            int list_y = search_y + 45;
            desktop_puts(pw_x, list_y, "已保存密码 (8)", 0x00);
            gfx_draw_hline(pw_x, list_y + 15, win_w - 60, 0xE2E8F0);

            const char *pw_names[] = {
                "GitHub",
                "Gmail",
                "微信",
                "支付宝",
                "淘宝",
                "京东",
                "知乎",
                "B站"
            };
            const char *pw_users[] = {
                "hepu_dev",
                "hepu@mail.com",
                "138****8888",
                "hepu@mail.com",
                "hepu_dev",
                "hepu_dev",
                "hepu_dev",
                "hepu_dev"
            };
            const char *pw_strength[] = {
                "强", "强", "中", "强", "中", "强", "中", "强"
            };
            const uint32_t strength_colors[] = {
                0x4CAF50, 0x4CAF50, 0xFAAD14, 0x4CAF50,
                0xFAAD14, 0x4CAF50, 0xFAAD14, 0x4CAF50
            };
            const uint32_t icon_colors[] = {
                0x24292E, 0xEA4335, 0x07C160, 0x1677FF,
                0xFF6A00, 0xE1251B, 0x0084FF, 0xFB7299
            };

            for (int i = 0; i < 8; i++) {
                int item_y = list_y + 22 + i * 42;
                gfx_draw_hline(pw_x, item_y + 38, win_w - 60, 0xF0F0F0);

                /* 图标 */
                gfx_fill_rounded_rect(pw_x + 5, item_y + 5, 32, 32, 6, icon_colors[i]);
                desktop_puts(pw_x + 13, item_y + 12, pw_names[i][0] ? &pw_names[i][0] : "?", 0xFFFFFF);

                /* 名称 */
                desktop_puts(pw_x + 45, item_y + 5, pw_names[i], 0x00);
                /* 用户名 */
                desktop_puts(pw_x + 45, item_y + 22, pw_users[i], 0x888888);

                /* 强度标签 */
                gfx_fill_rounded_rect(pw_x + win_w - 120, item_y + 10, 35, 18, 4, strength_colors[i]);
                desktop_puts(pw_x + win_w - 112, item_y + 13, pw_strength[i], 0xFFFFFF);

                /* 复制按钮 */
                ui_draw_button(pw_x + win_w - 75, item_y + 8, 55, 24, "复制", 0, 0);
            }
        } else if (app_id == 30) {
            /* V0.10.0: VPN客户端应用 */
            int vpn_x = wx + 20;
            int vpn_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(vpn_x, vpn_y, "VPN客户端", 0x00);
            ui_draw_button(vpn_x + 90, vpn_y - 3, 60, 22, "添加", 0, 0);
            gfx_draw_hline(vpn_x, vpn_y + 18, win_w - 60, 0xE2E8F0);

            /* 连接状态 */
            int conn_y = vpn_y + 30;
            gfx_fill_rounded_rect(vpn_x, conn_y, win_w - 60, 70, 8, 0xE8F5E9);
            /* 状态图标 */
            gfx_fill_circle(vpn_x + 35, conn_y + 35, 20, 0x4CAF50);
            gfx_fill_rect(vpn_x + 30, conn_y + 32, 10, 3, 0xFFFFFF);
            gfx_fill_rect(vpn_x + 35, conn_y + 28, 3, 10, 0xFFFFFF);
            /* 状态文字 */
            desktop_puts(vpn_x + 65, conn_y + 15, "已连接", 0x2E7D32);
            desktop_puts(vpn_x + 65, conn_y + 35, "服务器: 日本东京", 0x388E3C);
            desktop_puts(vpn_x + 65, conn_y + 52, "IP: 103.xxx.xxx.xxx | 延迟: 45ms", 0x66BB6A);
            /* 断开按钮 */
            ui_draw_button(vpn_x + win_w - 90, conn_y + 22, 70, 28, "断开", 1, 0);

            /* 服务器列表 */
            int srv_y = conn_y + 85;
            desktop_puts(vpn_x, srv_y, "服务器列表", 0x00);
            gfx_draw_hline(vpn_x, srv_y + 15, win_w - 60, 0xE2E8F0);

            const char *srv_names[] = {
                "日本东京",
                "美国洛杉矶",
                "新加坡",
                "香港",
                "德国法兰克福",
                "英国伦敦"
            };
            const char *srv_ping[] = {"45ms", "120ms", "68ms", "35ms", "156ms", "142ms"};
            const char *srv_load[] = {"32%", "58%", "41%", "67%", "28%", "45%"};
            const int srv_connected[] = {1, 0, 0, 0, 0, 0};

            for (int i = 0; i < 6; i++) {
                int item_y = srv_y + 22 + i * 40;
                gfx_draw_hline(vpn_x, item_y + 36, win_w - 60, 0xF0F0F0);

                if (srv_connected[i]) {
                    gfx_fill_rounded_rect(vpn_x, item_y, win_w - 60, 36, 4, 0xE3F2FD);
                }

                /* 国旗/图标 */
                gfx_fill_rounded_rect(vpn_x + 8, item_y + 6, 28, 24, 4, 0x607D8B);
                /* 服务器名 */
                desktop_puts(vpn_x + 45, item_y + 8, srv_names[i], 0x00);
                /* 延迟 */
                desktop_puts(vpn_x + 160, item_y + 8, srv_ping[i], 0x4CAF50);
                /* 负载 */
                desktop_puts(vpn_x + 220, item_y + 8, "负载:", 0x888888);
                desktop_puts(vpn_x + 255, item_y + 8, srv_load[i], 0xFAAD14);

                /* 连接按钮 */
                if (srv_connected[i]) {
                    ui_draw_button(vpn_x + win_w - 80, item_y + 5, 60, 26, "已连接", 1, 0);
                } else {
                    ui_draw_button(vpn_x + win_w - 80, item_y + 5, 60, 26, "连接", 0, 0);
                }
            }

            /* 设置 */
            int set_y = srv_y + 22 + 6 * 40 + 10;
            desktop_puts(vpn_x, set_y, "设置", 0x00);
            gfx_draw_hline(vpn_x, set_y + 15, win_w - 60, 0xE2E8F0);

            const char *set_items[] = {"自动连接", "Kill Switch", "DNS泄漏保护", "IPv6保护"};
            const int set_checked[] = {1, 1, 1, 0};

            for (int i = 0; i < 4; i++) {
                int row_y = set_y + 22 + i * 30;
                desktop_puts(vpn_x + 8, row_y + 5, set_items[i], 0x00);
                /* 开关 */
                if (set_checked[i]) {
                    gfx_fill_rounded_rect(vpn_x + win_w - 60, row_y + 3, 40, 20, 10, 0x4CAF50);
                    gfx_fill_circle(vpn_x + win_w - 30, row_y + 13, 7, 0xFFFFFF);
                } else {
                    gfx_fill_rounded_rect(vpn_x + win_w - 60, row_y + 3, 40, 20, 10, 0xCCCCCC);
                    gfx_fill_circle(vpn_x + win_w - 50, row_y + 13, 7, 0xFFFFFF);
                }
            }
        } else if (app_id == 31) {
            /* V0.10.0: 远程桌面应用 */
            int rd_x = wx + 20;
            int rd_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(rd_x, rd_y, "远程桌面", 0x00);
            ui_draw_button(rd_x + 80, rd_y - 3, 60, 22, "连接", 1, 0);
            ui_draw_button(rd_x + 145, rd_y - 3, 60, 22, "添加", 0, 0);
            gfx_draw_hline(rd_x, rd_y + 18, win_w - 60, 0xE2E8F0);

            /* 快速连接 */
            int quick_y = rd_y + 30;
            gfx_fill_rounded_rect(rd_x, quick_y, win_w - 60, 50, 8, 0xF5F5F5);
            desktop_puts(rd_x + 15, quick_y + 8, "计算机:", 0x00);
            gfx_fill_rounded_rect(rd_x + 75, quick_y + 5, 150, 26, 4, 0xFFFFFF);
            gfx_draw_rect(rd_x + 75, quick_y + 5, 150, 26, 0xCCCCCC);
            desktop_puts(rd_x + 82, quick_y + 11, "192.168.1.100", 0x333333);
            desktop_puts(rd_x + 15, quick_y + 30, "用户名: hepu_admin", 0x888888);

            /* 连接列表 */
            int list_y = quick_y + 65;
            desktop_puts(rd_x, list_y, "已保存连接", 0x00);
            gfx_draw_hline(rd_x, list_y + 15, win_w - 60, 0xE2E8F0);

            const char *rd_names[] = {
                "办公电脑",
                "家用电脑",
                "开发服务器",
                "测试机"
            };
            const char *rd_addr[] = {
                "192.168.1.100",
                "192.168.1.101",
                "10.0.0.50",
                "192.168.1.200"
            };
            const char *rd_status[] = {"在线", "离线", "在线", "在线"};
            const uint32_t status_colors[] = {0x4CAF50, 0x9E9E9E, 0x4CAF50, 0x4CAF50};

            for (int i = 0; i < 4; i++) {
                int item_y = list_y + 22 + i * 48;
                gfx_draw_hline(rd_x, item_y + 44, win_w - 60, 0xF0F0F0);

                /* 显示器图标 */
                gfx_fill_rounded_rect(rd_x + 5, item_y + 6, 36, 28, 4, 0x607D8B);
                gfx_fill_rect(rd_x + 10, item_y + 10, 26, 18, 0xFFFFFF);
                gfx_fill_rect(rd_x + 15, item_y + 34, 16, 3, 0x607D8B);

                /* 名称 */
                desktop_puts(rd_x + 50, item_y + 6, rd_names[i], 0x00);
                /* 地址 */
                desktop_puts(rd_x + 50, item_y + 24, rd_addr[i], 0x888888);

                /* 状态 */
                gfx_fill_rounded_rect(rd_x + win_w - 120, item_y + 10, 45, 20, 4, status_colors[i]);
                desktop_puts(rd_x + win_w - 112, item_y + 13, rd_status[i], 0xFFFFFF);

                /* 连接按钮 */
                ui_draw_button(rd_x + win_w - 70, item_y + 8, 55, 26, "连接", 0, 0);
            }

            /* 显示设置 */
            int disp_y = list_y + 22 + 4 * 48 + 10;
            desktop_puts(rd_x, disp_y, "显示设置", 0x00);
            gfx_draw_hline(rd_x, disp_y + 15, win_w - 60, 0xE2E8F0);

            const char *disp_items[] = {"全屏模式", "自动调整分辨率", "音频重定向", "剪贴板共享"};
            const int disp_checked[] = {1, 1, 1, 1};

            for (int i = 0; i < 4; i++) {
                int row_y = disp_y + 22 + i * 28;
                desktop_puts(rd_x + 8, row_y + 4, disp_items[i], 0x00);
                if (disp_checked[i]) {
                    gfx_fill_rounded_rect(rd_x + win_w - 60, row_y + 2, 40, 18, 9, 0x4CAF50);
                    gfx_fill_circle(rd_x + win_w - 30, row_y + 11, 6, 0xFFFFFF);
                } else {
                    gfx_fill_rounded_rect(rd_x + win_w - 60, row_y + 2, 40, 18, 9, 0xCCCCCC);
                    gfx_fill_circle(rd_x + win_w - 50, row_y + 11, 6, 0xFFFFFF);
                }
            }
        } else if (app_id == 32) {
            /* V0.10.0: 系统体检应用 */
            int hc_x = wx + 20;
            int hc_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(hc_x, hc_y, "系统体检", 0x00);
            ui_draw_button(hc_x + 70, hc_y - 3, 70, 22, "立即体检", 1, 0);
            gfx_draw_hline(hc_x, hc_y + 18, win_w - 60, 0xE2E8F0);

            /* 体检分数 */
            int score_y = hc_y + 30;
            gfx_fill_rounded_rect(hc_x, score_y, win_w - 60, 80, 8, 0xE8F5E9);
            /* 圆形分数 */
            gfx_fill_circle(hc_x + 45, score_y + 40, 30, 0x4CAF50);
            gfx_fill_circle(hc_x + 45, score_y + 40, 24, 0xFFFFFF);
            desktop_puts(hc_x + 35, score_y + 32, "92", 0x4CAF50);
            desktop_puts(hc_x + 38, score_y + 48, "分", 0x66BB6A);
            /* 状态文字 */
            desktop_puts(hc_x + 90, score_y + 15, "系统状态良好", 0x2E7D32);
            desktop_puts(hc_x + 90, score_y + 35, "上次体检: 2026-10-07 10:30", 0x388E3C);
            desktop_puts(hc_x + 90, score_y + 55, "发现 2 个可优化项", 0x66BB6A);

            /* 体检项目 */
            int item_y = score_y + 95;
            desktop_puts(hc_x, item_y, "体检项目", 0x00);
            gfx_draw_hline(hc_x, item_y + 15, win_w - 60, 0xE2E8F0);

            const char *hc_items[] = {
                "启动项管理",
                "系统垃圾",
                "注册表清理",
                "磁盘错误",
                "内存优化",
                "网络状态",
                "安全防护",
                "驱动更新"
            };
            const char *hc_status[] = {
                "正常", "已清理", "正常", "正常",
                "可优化", "正常", "正常", "可优化"
            };
            const uint32_t hc_colors[] = {
                0x4CAF50, 0x4CAF50, 0x4CAF50, 0x4CAF50,
                0xFAAD14, 0x4CAF50, 0x4CAF50, 0xFAAD14
            };

            for (int i = 0; i < 8; i++) {
                int row_y = item_y + 22 + i * 32;
                gfx_draw_hline(hc_x, row_y + 28, win_w - 60, 0xF0F0F0);

                /* 状态图标 */
                if (hc_colors[i] == 0x4CAF50) {
                    gfx_fill_circle(hc_x + 10, row_y + 10, 8, 0x4CAF50);
                    gfx_draw_rect(hc_x + 6, row_y + 8, 3, 5, 0xFFFFFF);
                    gfx_draw_rect(hc_x + 9, row_y + 5, 3, 8, 0xFFFFFF);
                } else {
                    gfx_fill_circle(hc_x + 10, row_y + 10, 8, 0xFAAD14);
                    gfx_fill_rect(hc_x + 9, row_y + 5, 2, 6, 0xFFFFFF);
                    gfx_fill_rect(hc_x + 9, row_y + 13, 2, 2, 0xFFFFFF);
                }

                /* 项目名 */
                desktop_puts(hc_x + 28, row_y + 6, hc_items[i], 0x00);
                /* 状态 */
                gfx_fill_rounded_rect(hc_x + win_w - 80, row_y + 3, 50, 18, 4, hc_colors[i]);
                desktop_puts(hc_x + win_w - 72, row_y + 6, hc_status[i], 0xFFFFFF);
            }

            /* 优化建议 */
            int opt_y = item_y + 22 + 8 * 32 + 10;
            gfx_fill_rounded_rect(hc_x, opt_y, win_w - 60, 50, 6, 0xFFF8E1);
            desktop_puts(hc_x + 12, opt_y + 8, "优化建议:", 0xE65100);
            desktop_puts(hc_x + 12, opt_y + 26, "1. 关闭3个不必要的启动项可加快启动速度", 0xEF6C00);
            desktop_puts(hc_x + 12, opt_y + 42, "2. 更新2个驱动可提升硬件兼容性", 0xEF6C00);
        } else if (app_id == 33) {
            /* V0.10.0: 任务管理器应用 */
            int tm_x = wx + 20;
            int tm_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(tm_x, tm_y, "任务管理器", 0x00);
            ui_draw_button(tm_x + 85, tm_y - 3, 60, 22, "结束任务", 1, 0);
            ui_draw_button(tm_x + 150, tm_y - 3, 60, 22, "新任务", 0, 0);
            gfx_draw_hline(tm_x, tm_y + 18, win_w - 60, 0xE2E8F0);

            /* 性能概览 */
            int perf_y = tm_y + 30;
            /* CPU */
            gfx_fill_rounded_rect(tm_x, perf_y, (win_w - 70)/2, 55, 6, 0xE3F2FD);
            desktop_puts(tm_x + 10, perf_y + 6, "CPU", 0x1565C0);
            desktop_puts(tm_x + 10, perf_y + 22, "35%", 0x1976D2);
            ui_draw_progress(tm_x + 10, perf_y + 40, (win_w - 90)/2, 6, 35);
            /* 内存 */
            gfx_fill_rounded_rect(tm_x + (win_w - 60)/2 + 5, perf_y, (win_w - 70)/2, 55, 6, 0xE8F5E9);
            desktop_puts(tm_x + (win_w - 60)/2 + 15, perf_y + 6, "内存", 0x2E7D32);
            desktop_puts(tm_x + (win_w - 60)/2 + 15, perf_y + 22, "62% (3.1G/5G)", 0x388E3C);
            ui_draw_progress(tm_x + (win_w - 60)/2 + 15, perf_y + 40, (win_w - 90)/2, 6, 62);

            /* 进程列表 */
            int proc_y = perf_y + 70;
            desktop_puts(tm_x, proc_y, "进程 (12)", 0x00);
            gfx_draw_hline(tm_x, proc_y + 15, win_w - 60, 0xE2E8F0);

            /* 表头 */
            desktop_puts(tm_x + 8, proc_y + 20, "进程名", 0x666666);
            desktop_puts(tm_x + 140, proc_y + 20, "CPU", 0x666666);
            desktop_puts(tm_x + 190, proc_y + 20, "内存", 0x666666);
            desktop_puts(tm_x + 260, proc_y + 20, "状态", 0x666666);

            const char *proc_names[] = {
                "hepu_kernel",
                "desktop_env",
                "file_manager",
                "web_browser",
                "terminal",
                "music_player",
                "system_monitor",
                "app_store",
                "mail_client",
                "calculator",
                "text_editor",
                "idle"
            };
            const char *proc_cpu[] = {
                "5%", "12%", "3%", "8%", "1%", "4%", "2%", "1%", "0%", "0%", "0%", "0%"
            };
            const char *proc_mem[] = {
                "512M", "256M", "64M", "128M", "32M", "48M", "16M", "32M", "24M", "8M", "12M", "0M"
            };
            const char *proc_status[] = {
                "运行", "运行", "运行", "运行", "运行", "运行",
                "运行", "运行", "休眠", "休眠", "休眠", "空闲"
            };

            for (int i = 0; i < 12; i++) {
                int row_y = proc_y + 38 + i * 26;
                gfx_draw_hline(tm_x, row_y + 22, win_w - 60, 0xF5F5F5);

                if (i == 1) {
                    gfx_fill_rect(tm_x, row_y, win_w - 60, 24, 0xE3F2FD);
                }

                desktop_puts(tm_x + 8, row_y + 5, proc_names[i], 0x00);
                desktop_puts(tm_x + 140, row_y + 5, proc_cpu[i], 0x333333);
                desktop_puts(tm_x + 190, row_y + 5, proc_mem[i], 0x333333);
                desktop_puts(tm_x + 260, row_y + 5, proc_status[i], 0x666666);
            }
        } else if (app_id == 34) {
            /* V0.10.0: 记事本增强应用 */
            int note_x = wx + 20;
            int note_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(note_x, note_y, "记事本", 0x00);
            ui_draw_button(note_x + 60, note_y - 3, 50, 22, "新建", 0, 0);
            ui_draw_button(note_x + 115, note_y - 3, 50, 22, "打开", 0, 0);
            ui_draw_button(note_x + 170, note_y - 3, 50, 22, "保存", 1, 0);
            gfx_draw_hline(note_x, note_y + 18, win_w - 60, 0xE2E8F0);

            /* 工具栏 */
            int tool_y = note_y + 25;
            gfx_fill_rounded_rect(note_x, tool_y, win_w - 60, 30, 4, 0xF5F5F5);
            /* 字体选择 */
            gfx_fill_rounded_rect(note_x + 8, tool_y + 5, 80, 20, 3, 0xFFFFFF);
            gfx_draw_rect(note_x + 8, tool_y + 5, 80, 20, 0xCCCCCC);
            desktop_puts(note_x + 14, tool_y + 9, "Sans Serif", 0x333333);
            /* 字号 */
            gfx_fill_rounded_rect(note_x + 95, tool_y + 5, 40, 20, 3, 0xFFFFFF);
            gfx_draw_rect(note_x + 95, tool_y + 5, 40, 20, 0xCCCCCC);
            desktop_puts(note_x + 105, tool_y + 9, "14", 0x333333);
            /* 格式按钮 */
            ui_draw_button(note_x + 145, tool_y + 4, 24, 22, "B", 0, 0);
            ui_draw_button(note_x + 172, tool_y + 4, 24, 22, "I", 0, 0);
            ui_draw_button(note_x + 199, tool_y + 4, 24, 22, "U", 0, 0);
            /* 对齐 */
            ui_draw_button(note_x + 230, tool_y + 4, 24, 22, "L", 0, 0);
            ui_draw_button(note_x + 257, tool_y + 4, 24, 22, "C", 0, 0);
            ui_draw_button(note_x + 284, tool_y + 4, 24, 22, "R", 0, 0);

            /* 编辑区域 */
            int edit_y = tool_y + 40;
            gfx_fill_rounded_rect(note_x, edit_y, win_w - 60, win_h - 180, 4, 0xFFFFFF);
            gfx_draw_rect(note_x, edit_y, win_w - 60, win_h - 180, 0xDDDDDD);

            /* 文本内容 */
            const char *note_lines[] = {
                "赫普OS 开发笔记",
                "",
                "2026-10-07 系统开发记录:",
                "1. 完成34个桌面应用开发",
                "2. 版本号升级到0.10.0",
                "3. 完善图形子系统和应用生态",
                "4. 添加VPN客户端和远程桌面",
                "5. 系统体检和任务管理器",
                "",
                "下一步计划:",
                "- 完善网络端到端验证",
                "- 64位用户态程序支持",
                "- 包管理器功能完善",
                "- 更多第三方库移植",
                "",
                "---",
                "HepuOS - 系统归人，数据归己",
                "自由可控，极致纯净"
            };

            for (int i = 0; i < 18; i++) {
                int line_y = edit_y + 12 + i * 18;
                if (i == 0) {
                    /* 标题加粗 */
                    desktop_puts(note_x + 15, line_y, note_lines[i], 0x00);
                } else if (note_lines[i][0] == '-') {
                    desktop_puts(note_x + 25, line_y, note_lines[i], 0x555555);
                } else if (note_lines[i][0] >= '0' && note_lines[i][0] <= '9') {
                    desktop_puts(note_x + 20, line_y, note_lines[i], 0x333333);
                } else {
                    desktop_puts(note_x + 15, line_y, note_lines[i], 0x444444);
                }
            }

            /* 状态栏 */
            int status_y = edit_y + win_h - 180 + 5;
            gfx_fill_rounded_rect(note_x, status_y, win_w - 60, 22, 3, 0xF5F5F5);
            desktop_puts(note_x + 10, status_y + 5, "行: 18  列: 25  |  字数: 156  |  UTF-8  |  已保存", 0x666666);
        } else if (app_id == 35) {
            /* V0.10.0: 图片编辑器应用 */
            int img_x = wx + 20;
            int img_y = content_y + 10;

            /* 标题栏 */
            desktop_puts(img_x, img_y, "图片编辑器", 0x00);
            ui_draw_button(img_x + 75, img_y - 3, 50, 22, "新建", 0, 0);
            ui_draw_button(img_x + 130, img_y - 3, 50, 22, "打开", 0, 0);
            ui_draw_button(img_x + 185, img_y - 3, 50, 22, "保存", 1, 0);
            gfx_draw_hline(img_x, img_y + 18, win_w - 60, 0xE2E8F0);

            /* 左侧工具栏 */
            int tool_x = img_x;
            int tool_w = 45;
            gfx_fill_rounded_rect(tool_x, img_y + 25, tool_w, win_h - 130, 4, 0xF5F5F5);

            /* 工具按钮 */
            const char *tools[] = {"选择", "画笔", "橡皮", "填充", "文字", "形状", "吸管", "缩放"};
            for (int i = 0; i < 8; i++) {
                int ty = img_y + 32 + i * 32;
                if (i == 1) {
                    gfx_fill_rounded_rect(tool_x + 4, ty, tool_w - 8, 28, 3, 0xE3F2FD);
                    gfx_draw_rect(tool_x + 4, ty, tool_w - 8, 28, 0x4A90D9);
                }
                desktop_puts(tool_x + 8, ty + 8, tools[i], 0x333333);
            }

            /* 画布区域 */
            int canvas_x = img_x + tool_w + 10;
            int canvas_y = img_y + 25;
            int canvas_w = win_w - 60 - tool_w - 10;
            int canvas_h = win_h - 130;
            gfx_fill_rounded_rect(canvas_x, canvas_y, canvas_w, canvas_h, 4, 0xFFFFFF);
            gfx_draw_rect(canvas_x, canvas_y, canvas_w, canvas_h, 0xDDDDDD);

            /* 画布内容 - 示例绘图 */
            /* 太阳 */
            gfx_fill_circle(canvas_x + canvas_w/4, canvas_y + 50, 25, 0xFFD700);
            /* 山 */
            gfx_fill_rect(canvas_x + 50, canvas_y + canvas_h/2, 100, 80, 0x8B4513);
            gfx_fill_rect(canvas_x + 120, canvas_y + canvas_h/2 - 30, 120, 110, 0xA0522D);
            /* 草地 */
            gfx_fill_rect(canvas_x + 20, canvas_y + canvas_h - 60, canvas_w - 40, 40, 0x90EE90);
            /* 房子 */
            gfx_fill_rect(canvas_x + canvas_w - 120, canvas_y + canvas_h/2, 60, 70, 0xDEB887);
            gfx_fill_rect(canvas_x + canvas_w - 130, canvas_y + canvas_h/2 - 25, 80, 25, 0x8B0000);
            /* 云 */
            gfx_fill_circle(canvas_x + canvas_w/2, canvas_y + 40, 15, 0xFFFFFF);
            gfx_fill_circle(canvas_x + canvas_w/2 + 20, canvas_y + 45, 12, 0xFFFFFF);
            gfx_fill_circle(canvas_x + canvas_w/2 - 18, canvas_y + 45, 12, 0xFFFFFF);

            /* 底部颜色面板 */
            int color_y = img_y + 25 + win_h - 130 + 10;
            gfx_fill_rounded_rect(img_x, color_y, win_w - 60, 30, 4, 0xF5F5F5);
            desktop_puts(img_x + 10, color_y + 8, "颜色:", 0x333333);

            const uint32_t colors[] = {
                0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF,
                0xFFFF00, 0xFF00FF, 0x00FFFF, 0xFFA500, 0x800080,
                0xA52A2A, 0x008000, 0x000080, 0x808080, 0xC0C0C0
            };
            for (int i = 0; i < 15; i++) {
                int cx = img_x + 55 + i * 20;
                gfx_fill_rect(cx, color_y + 6, 16, 16, colors[i]);
                gfx_draw_rect(cx, color_y + 6, 16, 16, 0x999999);
                if (i == 2) {
                    gfx_draw_rect(cx - 1, color_y + 5, 18, 18, 0x000000);
                }
            }
            desktop_puts(img_x + win_w - 100, color_y + 8, "粗细: 3px", 0x666666);
        } else {
            desktop_puts(wx + 20, content_y, "Application launched:", 0x07);
            desktop_puts(wx + 20, content_y + 24, app_icons[app_id].name, 0x09);

            /* 应用图标预览 - 扁平化纯色 */
            const app_icon_t *app = &app_icons[app_id];
            gfx_fill_rounded_rect(wx + 20, content_y + 50, 32, 32, 6, app->color1);

            char msg[128];
            snprintf(msg, sizeof(msg), "This is the %s application window.",
                     app_icons[app_id].name);
            desktop_puts(wx + 60, content_y + 55, msg, 0x07);
            desktop_puts(wx + 60, content_y + 75, "Drag titlebar to move.", 0x07);
            desktop_puts(wx + 60, content_y + 95, "Click X to close. Press ESC to close top window.", 0x07);
        }

        /* 状态栏 */
        if (win_h > 180) {
            gfx_draw_hline(wx + 12, wy + win_h - 30, win_w - 24, 0xE2E8F0);
            desktop_puts(wx + 20, wy + win_h - 22, "Ready", 0x07);
            char status[64];
            snprintf(status, sizeof(status), "Window %d of %d", win_idx + 1, window_count);
            desktop_puts(wx + win_w - 100, wy + win_h - 22, status, 0x07);
        }
    }
}

/* 按Z轴顺序绘制所有窗口 */
static void draw_all_windows(void)
{
    /* 收集所有窗口并按z_order排序 */
    int indices[MAX_WINDOWS];
    int count = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].used) {
            indices[count++] = i;
        }
    }

    /* 简单冒泡排序（按z_order从小到大） */
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - 1 - i; j++) {
            if (windows[indices[j]].z_order > windows[indices[j+1]].z_order) {
                int tmp = indices[j];
                indices[j] = indices[j+1];
                indices[j+1] = tmp;
            }
        }
    }

    /* 按顺序绘制 */
    for (int i = 0; i < count; i++) {
        draw_window(indices[i]);
    }
}

void elastic_desktop_render(void)
{
    /* 计算UI缩放因子 */
    calc_ui_scale();

    /* 绘制扁平化背景 */
    draw_modern_background();

    /* 绘制顶部状态栏 */
    draw_statusbar();

    /* 计算图标位置 */
    calc_icon_positions();

    /* 绘制桌面应用图标 */
    int icon_size = SCALE(ICON_SIZE);
    int icon_radius = SCALE(ICON_RADIUS);
    for (int i = 0; i < (int)APP_COUNT && i < 8; i++) {
        int ix = icon_positions[i][0];
        int iy = icon_positions[i][1];
        /* 临时修改图标大小绘制 */
        int old_size = ICON_SIZE;
        int old_radius = ICON_RADIUS;
        /* 直接绘制缩放后的图标 */
        const app_icon_t *app = &app_icons[i];
        int selected = (selected_icon == i);
        int s = icon_size + (selected ? 8 : 0);
        int r = icon_radius + (selected ? 2 : 0);
        int ox = selected ? -4 : 0;
        int oy = selected ? -4 : 0;
        gfx_fill_rounded_rect(ix + ox, iy + oy, s, s, r, app->color1);
        if (selected) {
            gfx_fill_rounded_rect(ix - 2, iy - 2, s + 4, s + 4, r + 2, 0xFFFFFF);
            gfx_fill_rounded_rect(ix - 1, iy - 1, s + 2, s + 2, r + 1, app->color1);
        }
        draw_icon_symbol(ix + s / 2, iy + s / 2 - 4, app->symbol_type, 0xFFFFFF);
        int name_len = strlen(app->name);
        /* V7.56: 使用实际字体宽度计算文字居中
         * 中文字符宽高比1:1，英文字符宽高比1:2
         * 使用utf8_string_width计算实际宽度
         */
        extern uint16_t font_get_size(void);
        extern int utf8_string_width(const char *s, int size);
        uint16_t fsize = font_get_size();
        if (fsize < 16) fsize = 16;
        if (fsize > 32) fsize = 32;
        int text_width = utf8_string_width(app->name, fsize);
        if (text_width <= 0) text_width = name_len * (fsize / 2);
        int text_x = ix + (icon_size - text_width) / 2;
        desktop_puts(text_x, iy + icon_size + 6, app->name, 0x0F);
    }

    /* 绘制底部Dock栏 */
    draw_dock_modern();

    /* 绘制所有应用窗口（按Z轴顺序） */
    draw_all_windows();

    /* 绘制右键菜单 */
    draw_context_menu();

    /* V6.97: 绘制关机/重启对话框 */
    draw_power_dialog();

    /* V8.50: 绘制开始菜单/应用启动器 */
    draw_start_menu();

    /* 绘制鼠标光标 */
    draw_mouse_cursor();
}

/* 找到指定位置最上层的窗口 */
static int find_top_window_at(int mx, int my)
{
    int win_w = gfx_width - 200;
    int win_h = gfx_height - 160;
    int top_idx = -1;
    int top_z = -1;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].used) {
            int wx = windows[i].x;
            int wy = windows[i].y;
            if (mx >= wx && mx <= wx + win_w &&
                my >= wy && my <= wy + win_h) {
                if (windows[i].z_order > top_z) {
                    top_z = windows[i].z_order;
                    top_idx = i;
                }
            }
        }
    }
    return top_idx;
}

/* 创建新窗口 */
static int create_window(int app_id)
{
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].used) {
            windows[i].used = 1;
            windows[i].app_id = app_id;
            windows[i].x = 100 + (window_count % 3) * 30;
            windows[i].y = 80 + (window_count % 3) * 20;
            windows[i].z_order = next_z++;
            windows[i].anim_state = 1;  /* 打开动画 */
            windows[i].anim_frame = 0;
            window_count++;
            return i;
        }
    }
    return -1;
}

/* 关闭窗口 */
static void close_window(int win_idx)
{
    if (win_idx >= 0 && win_idx < MAX_WINDOWS && windows[win_idx].used) {
        windows[win_idx].used = 0;
        window_count--;
    }
}

/* 将窗口置于最上层 */
static void bring_to_front(int win_idx)
{
    if (win_idx >= 0 && win_idx < MAX_WINDOWS && windows[win_idx].used) {
        windows[win_idx].z_order = next_z++;
    }
}

/* ============================================================================
 * 测试函数
 * ========================================================================== */
void desktop_render_test(void)
{
    elastic_desktop_render();
    gfx_flip();
}

/* ============================================================================
 * 桌面主循环 (带鼠标交互)
 * ========================================================================== */
void elastic_desktop_main_loop(void)
{
    extern void serial_puts(uint16_t, const char *);
    serial_puts(0x3F8, "[EDESKTOP] entering main loop with mouse...\n");

    /* 初始化鼠标位置到屏幕中央 */
    desktop_set_mouse(gfx_width / 2, gfx_height / 2);

    /* 鼠标状态 */
    extern int mouse_get_x(void);
    extern int mouse_get_y(void);
    extern uint8_t mouse_get_buttons(void);
    extern int ps2mouse_poll(void *ev);
    uint8_t prev_buttons = 0;
    static int desk_mx = 512, desk_my = 384;
    static uint8_t desk_buttons = 0;

    /* 无限循环 */
    while (1) {
        /* V0.10.1: 使用ps2mouse_poll轮询鼠标（修复鼠标点击不生效） */
        typedef struct { int dx, dy; uint8_t left, right, middle, valid; } ps2ev_t;
        ps2ev_t ev;
        while (ps2mouse_poll(&ev)) {
            if (ev.valid) {
                desk_mx += ev.dx;
                desk_my += ev.dy;
                if (desk_mx < 0) desk_mx = 0;
                if (desk_mx > gfx_width - 1) desk_mx = gfx_width - 1;
                if (desk_my < 0) desk_my = 0;
                if (desk_my > gfx_height - 1) desk_my = gfx_height - 1;
                desk_buttons = (ev.left ? 1 : 0) | (ev.right ? 2 : 0) | (ev.middle ? 4 : 0);
            }
        }

        /* 读取鼠标位置 */
        int mx = desk_mx;
        int my = desk_my;
        uint8_t buttons = desk_buttons;

        /* 限制鼠标在屏幕范围内 */
        if (mx < 0) mx = 0;
        if (mx > gfx_width - 1) mx = gfx_width - 1;
        if (my < 0) my = 0;
        if (my > gfx_height - 1) my = gfx_height - 1;
        desktop_set_mouse(mx, my);

        /* 鼠标悬停检测（仅在无窗口拖动时） */
        if (dragging_window < 0) {
            selected_icon = hit_test_icon(mx, my);
            if (selected_icon < 0) {
                selected_icon = hit_test_dock_icon(mx, my);
            }
        } else {
            selected_icon = -1;
        }

        /* 右键菜单悬停检测 */
        if (context_menu_visible) {
            context_menu_selected = hit_test_context_menu(mx, my);
        }

        /* 鼠标点击检测（左键按下沿） */
        if ((buttons & 1) && !(prev_buttons & 1)) {
            /* 如果右键菜单可见，先处理菜单点击 */
            if (context_menu_visible) {
                int menu_idx = hit_test_context_menu(mx, my);
                if (menu_idx >= 0) {
                    execute_context_menu(menu_idx);
                } else {
                    context_menu_visible = 0;  /* 点击菜单外关闭 */
                }
                prev_buttons = buttons;
                continue;
            }

            /* V6.97: 关机/重启对话框处理 */
            if (power_dialog_visible) {
                handle_power_dialog_click(mx, my);
                prev_buttons = buttons;
                continue;
            }

            /* V8.50: 开始菜单/应用启动器处理 */
            if (start_menu_visible) {
                /* 点击开始菜单外关闭 */
                if (mx > START_MENU_WIDTH || my > STATUSBAR_HEIGHT + START_MENU_HEIGHT) {
                    start_menu_visible = 0;
                    start_menu_search_focus = 0;
                    prev_buttons = buttons;
                    continue;
                }

                /* 搜索框点击 */
                int search_y = STATUSBAR_HEIGHT + 12;
                if (mx >= 12 && mx <= START_MENU_WIDTH - 12 &&
                    my >= search_y && my <= search_y + 32) {
                    start_menu_search_focus = 1;
                    prev_buttons = buttons;
                    continue;
                }

                /* 应用列表点击 */
                int list_y = search_y + 44;
                int item_y = list_y + 28;
                int bottom_y = STATUSBAR_HEIGHT + START_MENU_HEIGHT - 52;
                if (my >= item_y && my < bottom_y) {
                    int item_idx = (my - item_y) / START_MENU_ITEM_H;
                    if (item_idx >= 0 && item_idx < APP_COUNT) {
                        /* 启动应用 */
                        serial_puts(0x3F8, "[STARTMENU] launch app: ");
                        serial_puts(0x3F8, app_icons[item_idx].name);
                        serial_puts(0x3F8, "\n");
                        create_window(item_idx);
                        start_menu_visible = 0;
                        start_menu_search_focus = 0;
                    }
                    prev_buttons = buttons;
                    continue;
                }

                /* 底部关机按钮 */
                if (my >= bottom_y + 10 && my <= bottom_y + 42) {
                    if (mx >= 16 && mx <= 16 + (START_MENU_WIDTH - 40) / 2) {
                        /* 关机 */
                        power_dialog_type = 0;
                        power_dialog_visible = 1;
                        start_menu_visible = 0;
                    } else if (mx >= 16 + (START_MENU_WIDTH - 40) / 2 + 8) {
                        /* 重启 */
                        power_dialog_type = 1;
                        power_dialog_visible = 1;
                        start_menu_visible = 0;
                    }
                    prev_buttons = buttons;
                    continue;
                }

                prev_buttons = buttons;
                continue;
            }

            /* V8.50: 开始按钮点击 */
            if (mx >= 0 && mx <= 44 && my >= 0 && my <= STATUSBAR_HEIGHT) {
                start_menu_visible = !start_menu_visible;
                start_menu_search[0] = 0;
                start_menu_search_focus = 0;
                prev_buttons = buttons;
                continue;
            }

            int win_idx = find_top_window_at(mx, my);

            if (win_idx >= 0) {
                /* 点击了窗口 */
                bring_to_front(win_idx);
                int win_w = gfx_width - 200;
                int wx = windows[win_idx].x;
                int wy = windows[win_idx].y;

                /* V6.98: 最大化时使用全屏宽度 */
                if (windows[win_idx].maximized) {
                    win_w = gfx_width;
                }

                /* 检测是否点击关闭按钮 */
                int close_x = wx + win_w - 40;
                int close_y = wy + 8;
                if (mx >= close_x && mx <= close_x + 24 &&
                    my >= close_y && my <= close_y + 20) {
                    serial_puts(0x3F8, "[EDESKTOP] window closed\n");
                    close_window(win_idx);
                }
                /* V6.98: 检测是否点击最大化按钮 */
                else if (mx >= wx + win_w - 70 && mx <= wx + win_w - 46 &&
                         my >= wy + 8 && my <= wy + 28) {
                    if (windows[win_idx].maximized) {
                        /* 还原窗口 */
                        windows[win_idx].maximized = 0;
                        windows[win_idx].x = windows[win_idx].prev_x;
                        windows[win_idx].y = windows[win_idx].prev_y;
                        serial_puts(0x3F8, "[EDESKTOP] window restored\n");
                    } else {
                        /* 最大化窗口 */
                        windows[win_idx].prev_x = windows[win_idx].x;
                        windows[win_idx].prev_y = windows[win_idx].y;
                        windows[win_idx].maximized = 1;
                        serial_puts(0x3F8, "[EDESKTOP] window maximized\n");
                    }
                }
                /* V6.98: 检测是否点击最小化按钮 */
                else if (mx >= wx + win_w - 100 && mx <= wx + win_w - 76 &&
                         my >= wy + 8 && my <= wy + 28) {
                    windows[win_idx].minimized = 1;
                    serial_puts(0x3F8, "[EDESKTOP] window minimized\n");
                }
                /* 检测是否点击标题栏（开始拖动） */
                else if (mx >= wx && mx <= wx + win_w &&
                         my >= wy && my <= wy + 36) {
                    /* 最大化时拖动标题栏还原窗口 */
                    if (windows[win_idx].maximized) {
                        windows[win_idx].maximized = 0;
                        windows[win_idx].x = mx - (SCALE(gfx_width - 200) / 2);
                        windows[win_idx].y = STATUSBAR_HEIGHT + 10;
                    }
                    dragging_window = win_idx;
                    drag_offset_x = mx - windows[win_idx].x;
                    drag_offset_y = my - windows[win_idx].y;
                    serial_puts(0x3F8, "[EDESKTOP] window drag start\n");
                }
            } else {
                /* 点击桌面，检测图标 */
                int hit = hit_test_icon(mx, my);
                if (hit < 0) {
                    hit = hit_test_dock_icon(mx, my);
                }
                if (hit >= 0) {
                    /* V6.98: 检查是否有该应用的最小化窗口，有则恢复 */
                    int restored = 0;
                    for (int i = 0; i < MAX_WINDOWS; i++) {
                        if (windows[i].used && windows[i].app_id == hit &&
                            windows[i].minimized) {
                            windows[i].minimized = 0;
                            bring_to_front(i);
                            restored = 1;
                            serial_puts(0x3F8, "[EDESKTOP] window restored from dock\n");
                            break;
                        }
                    }
                    if (!restored) {
                        serial_puts(0x3F8, "[EDESKTOP] app launched: ");
                        serial_puts(0x3F8, app_icons[hit].name);
                        serial_puts(0x3F8, "\n");
                        create_window(hit);
                    }
                }
            }
        }

        /* 鼠标释放（结束拖动） */
        if (!(buttons & 1) && (prev_buttons & 1)) {
            if (dragging_window >= 0) {
                serial_puts(0x3F8, "[EDESKTOP] window drag end\n");
                dragging_window = -1;
            }
        }

        /* 右键按下沿 - 显示右键菜单 */
        if ((buttons & 2) && !(prev_buttons & 2)) {
            context_menu_x = mx;
            context_menu_y = my;
            context_menu_visible = 1;
            context_menu_selected = -1;
            /* 确保菜单不超出屏幕 */
            if (context_menu_x + CONTEXT_MENU_WIDTH > gfx_width) {
                context_menu_x = gfx_width - CONTEXT_MENU_WIDTH;
            }
            if (context_menu_y + CONTEXT_MENU_COUNT * CONTEXT_MENU_ITEM_HEIGHT > gfx_height) {
                context_menu_y = gfx_height - CONTEXT_MENU_COUNT * CONTEXT_MENU_ITEM_HEIGHT - 20;
            }
            serial_puts(0x3F8, "[EDESKTOP] context menu shown\n");
        }

        /* 窗口拖动 */
        if (dragging_window >= 0 && windows[dragging_window].used) {
            windows[dragging_window].x = mx - drag_offset_x;
            windows[dragging_window].y = my - drag_offset_y;
            /* 限制窗口在屏幕范围内 */
            int win_w = gfx_width - 200;
            int win_h = gfx_height - 160;
            if (windows[dragging_window].x < 0) windows[dragging_window].x = 0;
            if (windows[dragging_window].y < 0) windows[dragging_window].y = 0;
            if (windows[dragging_window].x > gfx_width - win_w) windows[dragging_window].x = gfx_width - win_w;
            if (windows[dragging_window].y > gfx_height - win_h) windows[dragging_window].y = gfx_height - win_h;
        }

        /* 键盘输入：ESC关闭最上层窗口 */
        extern char keyboard_getchar(void);
        char key = keyboard_getchar();

        /* V8.50: 开始菜单搜索框输入 */
        if (start_menu_visible && start_menu_search_focus && key > 0) {
            int len = 0;
            while (start_menu_search[len]) len++;
            if (key == 8 || key == 127) {  /* Backspace */
                if (len > 0) start_menu_search[len - 1] = 0;
            } else if (key == 27) {  /* ESC关闭菜单 */
                start_menu_visible = 0;
                start_menu_search_focus = 0;
            } else if (key >= 32 && key < 127 && len < 30) {  /* 可打印字符 */
                start_menu_search[len] = key;
                start_menu_search[len + 1] = 0;
            }
            prev_buttons = buttons;
            elastic_desktop_render();
            gfx_flip();
            continue;
        }

        if (key == 27 && window_count > 0) {  /* ESC */
            /* 找到最上层窗口并关闭 */
            int top_idx = -1;
            int top_z = -1;
            for (int i = 0; i < MAX_WINDOWS; i++) {
                if (windows[i].used && windows[i].z_order > top_z) {
                    top_z = windows[i].z_order;
                    top_idx = i;
                }
            }
            if (top_idx >= 0) {
                serial_puts(0x3F8, "[EDESKTOP] ESC pressed, window closed\n");
                close_window(top_idx);
                dragging_window = -1;
            }
        }

        prev_buttons = buttons;

        /* 渲染 */
        elastic_desktop_render();
        gfx_flip();

        /* 简单延时 */
        for (volatile int i = 0; i < 200000; i++);
    }

    serial_puts(0x3F8, "[EDESKTOP] main loop done\n");
}
