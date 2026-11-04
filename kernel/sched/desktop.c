/* ============================================================================
 * 赫普OS - 桌面环境
 * 创建示例窗口、关于对话框、应用启动器
 * ========================================================================== */

#include <stdint.h>
#include "wm.h"
#include "vga_gfx.h"
#include "vga.h"
#include "hepu.h"
#include "string.h"
#include "appreg.h"

/* V2.41: 窗口按分辨率居中（全局，供desktop_terminal.c等子模块调用） */
wm_window_t *center_create(const char *title, int w, int h)
{
    extern int gfx_width, gfx_height, gfx_scale_x, gfx_scale_y;
    /* V2.41: 窗口尺寸按分辨率缩放 */
    w = (w * gfx_scale_x) / 100;
    h = (h * gfx_scale_y) / 100;
    int cx = (gfx_width - w) / 2;
    int cy = (gfx_height - 12 - h) / 2;
    if (cx < 2) cx = 2;
    if (cy < 2) cy = 2;
    return wm_create_window(title, cx, cy, w, h);
}

static wm_window_t *about_win = NULL;
static wm_window_t *launcher_win = NULL;
static wm_window_t *settings_win = NULL;

/* 终端模拟器已拆分到 desktop_terminal.c */
extern void on_launch_terminal(wm_window_t *win, wm_widget_t *wgt);

/* V2.35: 前向声明 (启动器引用) */
static void on_launch_files(wm_window_t *win, wm_widget_t *wgt);
static void on_launch_settings(wm_window_t *win, wm_widget_t *wgt);

/* 关于窗口的关闭按钮回调 */
static void on_close_about(wm_window_t *win, wm_widget_t *wgt)
{
    (void)wgt;
    wm_destroy_window(win);
}

/* 启动器按钮回调 */
static void on_launch_hello(wm_window_t *win, wm_widget_t *wgt)
{
    (void)win; (void)wgt;
    /* 创建一个" running "提示窗口 */
    wm_window_t *run = center_create("Running", 140, 60);
    if (run) {
        wm_add_label(run, 10, 20, "hello.hug start in ...");
        wm_add_button(run, 40, 40, 60, 14, "OK", on_close_about);
        wm_bring_to_front(run);
    }
}

/* V2.38: Shutdown 确认对话框 */
static wm_window_t *shutdown_win = NULL;

static void on_shutdown_power(wm_window_t *win, wm_widget_t *wgt)
{
    (void)win; (void)wgt;
    extern void machine_poweroff(void);
    vga_puts("[DESKTOP] Power Off...\n");
    machine_poweroff();
}

static void on_shutdown_reboot(wm_window_t *win, wm_widget_t *wgt)
{
    (void)win; (void)wgt;
    extern void machine_reboot(void);
    vga_puts("[DESKTOP] Reboot...\n");
    machine_reboot();
}

static void on_shutdown_cancel(wm_window_t *win, wm_widget_t *wgt)
{
    (void)wgt;
    wm_set_modal(shutdown_win, 0);
    wm_destroy_window(win);
    shutdown_win = NULL;
}

static void on_launch_shutdown(wm_window_t *win, wm_widget_t *wgt)
{
    (void)win; (void)wgt;
    if (shutdown_win && shutdown_win->used) {
        wm_bring_to_front(shutdown_win);
        return;
    }
    shutdown_win = center_create("Power", 180, 80);
    if (shutdown_win) {
        wm_set_modal(shutdown_win, 1);
        wm_add_label(shutdown_win, 40, 20, "Power Off or Reboot?");
        wm_add_button(shutdown_win, 12, 48, 48, 16, "Power", on_shutdown_power);
        wm_add_button(shutdown_win, 66, 48, 48, 16, "Reboot", on_shutdown_reboot);
        wm_add_button(shutdown_win, 120, 48, 48, 16, "Cancel", on_shutdown_cancel);
    }
}

/* 终端模拟器已拆分到 desktop_terminal.c (V6.46 代码重构) */

static void on_launch_about(wm_window_t *win, wm_widget_t *wgt)
{
    (void)win; (void)wgt;
    if (!about_win || !about_win->used) {
        about_win = center_create("About HEPU OS", 200, 142);
        if (about_win) {
            /* V2.36: 中文品牌 (UTF-8 16x16 点阵) */
            wm_add_label(about_win, 44, 18, "赫普超进化系统");
            wm_add_label(about_win, 50, 38, "HEPU HYPER EVOLVE OS");
            {
                static char verstr[24];
                extern const char *hepu_kernel_version;
                verstr[0] = 0;
                strcpy(verstr, "Version ");
                strcat(verstr, hepu_kernel_version);
                wm_add_label(about_win, 60, 50, verstr);
            }
            wm_add_label(about_win, 30, 62, "Microkernel + .hug apps");
            wm_add_label(about_win, 30, 72, "Window Manager: WM v1");
            wm_add_label(about_win, 30, 82, "Graphics: VGA 320x200");
            wm_add_label(about_win, 30, 92, "Input: PS/2 Mouse+Kbd");
            /* V2.35: 图形模式状态进度条 (新控件演示) */
            wm_add_progress(about_win, 30, 104, 140, 10, 100);
            /* V2.36: 中文标语 */
            wm_add_label(about_win, 44, 118, "系统归人 数据归己");
            wm_add_button(about_win, 70, 124, 60, 14, "Close", on_close_about);
        }
    }
    if (about_win) wm_bring_to_front(about_win);
}

/* 启动器窗口的关闭 */
static void on_close_launcher(wm_window_t *win, wm_widget_t *wgt)
{
    (void)wgt;
    wm_destroy_window(win);
}

/* ============================================================================
 * V2.52: 手机应用子启动器——桌面模式消费统一注册表 (appreg)
 * 桌面启动器 → "Phone Apps" → 手机应用列表 → 点击以手机全屏模式打开
 * ========================================================================== */
extern int hepu_mobile_launch_slot(int slot);

static wm_window_t *phone_apps_win = NULL;

/* 手机应用按钮回调: 从 user_data 取手机槽位, 调用 mobile.c 的全屏 launcher */
static void on_phone_app_click(wm_window_t *win, wm_widget_t *wgt)
{
    (void)win;
    int slot = (int)(intptr_t)wgt->user_data;
    vga_printf("[DESKTOP] launch phone app slot=%d\n", slot);
    /* 手机全屏页运行, ESC 返回后回到 wm_main_loop (桌面重绘恢复) */
    if (hepu_mobile_launch_slot(slot) != 0) {
        vga_puts("[DESKTOP] phone app launch FAILED\n");
    }
}

/* V2.53: 桌面 → 手机模式 (整机切换) */
static void on_launch_phone_mode(wm_window_t *win, wm_widget_t *wgt)
{
    (void)win; (void)wgt;
    vga_puts("[DESKTOP] switch to Phone Mode (G to go back)\n");
    extern void mobile_run(void);
    extern int hepu_ui_mode;
    hepu_ui_mode = 2;
    mobile_run();   /* 手机模式全屏, ESC/G 返回后回 wm_main_loop */
    hepu_ui_mode = 1;
}

/* V2.52: 手机子启动器自身的关闭回调 */
static void on_close_phone_apps(wm_window_t *win, wm_widget_t *wgt)
{
    (void)wgt;
    wm_destroy_window(win);
    phone_apps_win = NULL;
}

/* 打开手机应用子启动器 */
static void on_launch_phone_apps(wm_window_t *win, wm_widget_t *wgt)
{
    (void)win; (void)wgt;
    if (!phone_apps_win || !phone_apps_win->used) {
        phone_apps_win = center_create("Phone Apps", 210, 148);
        if (phone_apps_win) {
            int n = appreg_mobile_count();          /* 8 手机应用 */
            if (n > 8) n = 8;
            for (int i = 0; i < n; i++) {
                int idx = appreg_mobile_slot_to_idx(i);
                const hepu_app_t *a = (idx >= 0) ? appreg_get(idx) : NULL;
                const char *en = a ? a->name_en : "App";
                int bx = 20 + (i % 2) * 95;
                int by = 26 + (i / 2) * 22;
                wm_widget_t *btn = wm_add_button(phone_apps_win, bx, by, 88, 18,
                                                 en, on_phone_app_click);
                if (btn) btn->user_data = (void *)(intptr_t)i;
            }
            wm_add_label(phone_apps_win, 10, 128, "ESC in app returns here");
            wm_add_button(phone_apps_win, 130, 126, 60, 14, "Close", on_close_phone_apps);
        }
    }
    if (phone_apps_win) wm_bring_to_front(phone_apps_win);
}

void desktop_init(void)
{
    /* 创建应用启动器窗口 */
    launcher_win = center_create("App Launcher", 220, 150);
    if (launcher_win) {
        wm_add_label(launcher_win, 10, 16, "Select an application:");

        wm_add_button(launcher_win, 20, 32, 80, 18, "Hello", on_launch_hello);
        wm_add_button(launcher_win, 120, 32, 80, 18, "Terminal", on_launch_terminal);
        wm_add_button(launcher_win, 20, 56, 80, 18, "About", on_launch_about);
        wm_add_button(launcher_win, 120, 56, 80, 18, "Files", on_launch_files);
        wm_add_button(launcher_win, 20, 80, 80, 18, "Settings", on_launch_settings);
        wm_add_button(launcher_win, 120, 80, 80, 18, "Shutdown", on_launch_shutdown);
        /* V2.52: 手机应用子启动器 (双模式: 桌面 → 手机应用) */
        wm_add_button(launcher_win, 20, 102, 80, 18, "PhoneApps", on_launch_phone_apps);
        /* V2.53: 整机切到手机模式 */
        wm_add_button(launcher_win, 120, 102, 80, 18, "PhoneMode", on_launch_phone_mode);

        wm_add_label(launcher_win, 10, 108, "Click titlebar to drag");
        wm_add_label(launcher_win, 10, 118, "Click X to close");
        wm_add_button(launcher_win, 70, 130, 80, 14, "Close", on_close_launcher);
    }

    vga_puts("[DESKTOP] desktop env init done \n");
}

/* ============================================================================
 * V2.31: 桌面图标启动——桌面图标点击入口
 * 0=Apps(启动器) 1=Files(文件) 2=Term(终端) 3=About(关于)
 * ========================================================================== */

/* V2.35: 文件浏览窗口——用 List + Scrollbar 控件 (可滚动) */
static const char *file_items[12] = {
    "/dev        [DIR]",
    "/initrd     [DIR]",
    "/etc        [DIR]",
    "hello.hug  [APP]",
    "readme.txt [TXT]",
    "logo.bmp   [IMG]",
    "notes.hug  [APP]",
    "sysinfo    [APP]",
    "kernel.elf [BIN]",
    "docs/      [DIR]",
    "usr/       [DIR]",
    "var/       [DIR]"
};

static void on_launch_files(wm_window_t *win, wm_widget_t *wgt)
{
    (void)win; (void)wgt;
    static wm_window_t *files_win = NULL;
    if (!files_win || !files_win->used) {
        files_win = center_create("Files", 210, 155);
        if (files_win) {
            wm_add_label(files_win, 8, 14, "HEPU OS File Browser");
            wm_widget_t *list = wm_add_list(files_win, 8, 26, 150, 110, 10);
            if (list) {
                for (int i = 0; i < 12 && i < WM_LIST_MAX_ITEMS; i++)
                    list->u.list.items[i] = file_items[i];
                list->u.list.count = 12;
                list->u.list.selected = -1;
                list->u.list.item_h = 10;
            }
            wm_add_scrollbar(files_win, 162, 26, 8, 110, 8, 0);
            wm_add_label(files_win, 8, 138, "Select item, scroll to browse");
            wm_add_button(files_win, 90, 138, 60, 14, "Close", on_close_about);
        }
    }
    if (files_win) wm_bring_to_front(files_win);
}

/* V2.35: 设置窗口——Checkbox + List (主题选择) */
/* V2.38: Settings 主题切换回调 */
static void on_theme_select(wm_window_t *win, wm_widget_t *wgt)
{
    (void)win;
    if (!wgt || wgt->type != WM_WIDGET_LIST) return;
    int sel = wgt->u.list.selected;
    if (sel < 0 || sel >= 4) return;
    extern const char *theme_apply(int);
    theme_apply(sel);
}

static void on_launch_settings(wm_window_t *win, wm_widget_t *wgt)
{
    (void)win; (void)wgt;
    if (!settings_win || !settings_win->used) {
        settings_win = center_create("Settings", 200, 150);
        if (settings_win) {
            wm_add_label(settings_win, 10, 14, "System Settings");
            wm_add_checkbox(settings_win, 10, 28, "Double-click maximize", 1);
            wm_add_checkbox(settings_win, 10, 42, "Show taskbar clock", 1);
            wm_add_checkbox(settings_win, 10, 56, "Mouse cursor", 1);
            wm_add_label(settings_win, 10, 76, "Theme:");
            wm_widget_t *list = wm_add_list(settings_win, 10, 86, 110, 40, 10);
            if (list) {
                list->u.list.items[0] = "Hepu Blue";
                list->u.list.items[1] = "Dark";
                list->u.list.items[2] = "OLED Black";
                list->u.list.items[3] = "TRON";
                list->u.list.count = 4;
                list->u.list.selected = 0;
                list->u.list.item_h = 10;
                list->on_click = on_theme_select;
            }
            wm_add_button(settings_win, 55, 130, 60, 14, "Close", on_close_about);
        }
    }
    if (settings_win) wm_bring_to_front(settings_win);
}

/* V2.36: 桌面右键菜单动作 (wm.c 调用) */
void desktop_menu_action(int idx)
{
    switch (idx) {
        case 0: on_launch_terminal(NULL, NULL); break;
        case 1: on_launch_files(NULL, NULL); break;
        case 2: on_launch_settings(NULL, NULL); break;
        case 3: on_launch_about(NULL, NULL); break;
        case 4: on_launch_shutdown(NULL, NULL); break;
        default: break;
    }
}

/* 桌面图标启动入口 (wm.c 调用) */
void desktop_launch_icon(int idx)
{
    switch (idx) {
        case 0:  /* Apps → 启动器 */
            on_launch_hello(NULL, NULL);
            break;
        case 1:  /* Files */
            on_launch_files(NULL, NULL);
            break;
        case 2:  /* Term */
            on_launch_terminal(NULL, NULL);
            break;
        case 3:  /* About */
            on_launch_about(NULL, NULL);
            break;
        default:
            break;
    }
}
