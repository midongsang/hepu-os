/* ============================================================================
 * 赫普OS 64位 - 图形桌面环境
 * 基于VBE线性帧缓冲的简单桌面
 * V0.10.96: P3-1图形共享实现，引用shared/gfx/gfx_shared.h
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "../../shared/gfx/gfx_shared.h"

/* VBE信息（从bootloader传递，packed结构保持二进制兼容） */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t pitch;
    uint8_t bpp;
    uint32_t framebuffer;
} __attribute__((packed)) vbe_info_t;

extern void vga_puts(const char *s);
extern void vga_putchar(char c);
extern void gfx64_init_from_bootloader(void);
extern void gfx64_get_info(int *width, int *height, int *bpp);
extern void gfx64_put_pixel(int x, int y, uint32_t color);
extern void gfx64_fill_rect(int x, int y, int w, int h, uint32_t color);
extern int gfx64_put_char(int x, int y, char c, uint32_t color);
extern void gfx64_put_string(int x, int y, const char *s, uint32_t color);
extern void gfx64_clear(uint32_t color);

/* HepuShell 集成 */
extern int hepushell_init(int width, int height);
extern void hepushell_render(void);
extern void hepushell_handle_key(int key);
extern void hepushell_set_theme(int theme);
static int use_hepushell = 0;  /* 0=传统桌面, 1=HepuShell */

/* 前向声明 */
void gfx_terminal_run(void);
void gfx_app_run(const char *title, void (*app_func)(void));
extern int strcmp(const char *s1, const char *s2);
extern int keyboard64_haschar(void);
extern int keyboard64_getchar(void);
static char gfx_app_title[64];
static void app_filemgr(void);
static void app_settings(void);
static void app_notes(void);
static void app_taskmgr(void);
static void app_appstore(void);
static void app_about(void);

/* 鼠标接口 */
extern void mouse64_update(int screen_width, int screen_height);
extern void mouse64_get_pos(int *x, int *y);
extern int mouse64_get_left(void);
extern int mouse64_get_right(void);

/* RTC时间 */
typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint32_t year;
} rtc_time_t;
extern void rtc64_get_time(rtc_time_t *time);

/* 颜色定义 */
#define COLOR_BLACK   0x000000
#define COLOR_WHITE   0xFFFFFF
#define COLOR_BLUE    0x2563EB
#define COLOR_DARK    0x1E1E2E
#define COLOR_GRAY    0x6B7280
#define COLOR_LIGHT   0xE5E7EB
#define COLOR_GREEN   0x10B981
#define COLOR_RED     0xEF4444
#define COLOR_YELLOW  0xF59E0B
#define COLOR_PURPLE  0x8B5CF6
#define COLOR_CYAN    0x06B6D4
#define COLOR_PINK    0xEC4899

/* 桌面配置 */
#define DESKTOP_BG_TOP    0x1E3A5F
#define DESKTOP_BG_BOTTOM 0x0F172A
#define TASKBAR_HEIGHT    32
#define ICON_SIZE         64
#define ICON_SPACING      16
#define ICON_LABEL_HEIGHT 16

/* 应用图标 */
typedef struct {
    const char *name;
    uint32_t color;
    const char *label;
} desktop_icon_t;

static desktop_icon_t desktop_icons[] = {
    {"terminal", COLOR_BLUE, "Terminal"},
    {"files", COLOR_GREEN, "Files"},
    {"browser", COLOR_CYAN, "Browser"},
    {"settings", COLOR_GRAY, "Settings"},
    {"taskmgr", COLOR_YELLOW, "Tasks"},
    {"notepad", COLOR_PURPLE, "Editor"},
    {"appstore", COLOR_PINK, "Store"},
    {"calculator", COLOR_RED, "Calc"},
    {"about", COLOR_PURPLE, "About"},
    {NULL, 0, NULL}
};

static int desktop_initialized = 0;
static int screen_width = 0;
static int screen_height = 0;

/* 绘制渐变背景 */
static void desktop_draw_background(void)
{
    /* 简单的垂直渐变 */
    for (int y = 0; y < screen_height - TASKBAR_HEIGHT; y++) {
        uint32_t ratio = (uint32_t)y * 256 / (screen_height - TASKBAR_HEIGHT);
        uint8_t r = ((DESKTOP_BG_TOP >> 16) & 0xFF) * (256 - ratio) / 256 +
                    ((DESKTOP_BG_BOTTOM >> 16) & 0xFF) * ratio / 256;
        uint8_t g = ((DESKTOP_BG_TOP >> 8) & 0xFF) * (256 - ratio) / 256 +
                    ((DESKTOP_BG_BOTTOM >> 8) & 0xFF) * ratio / 256;
        uint8_t b = (DESKTOP_BG_TOP & 0xFF) * (256 - ratio) / 256 +
                    (DESKTOP_BG_BOTTOM & 0xFF) * ratio / 256;
        uint32_t color = (r << 16) | (g << 8) | b;
        for (int x = 0; x < screen_width; x++) {
            gfx64_put_pixel(x, y, color);
        }
    }
}

/* 绘制任务栏 */
static void desktop_draw_taskbar(void)
{
    int y = screen_height - TASKBAR_HEIGHT;

    /* 任务栏背景 */
    gfx64_fill_rect(0, y, screen_width, TASKBAR_HEIGHT, 0x1E293B);

    /* 开始按钮 */
    gfx64_fill_rect(4, y + 4, 80, TASKBAR_HEIGHT - 8, COLOR_BLUE);
    gfx64_put_string(12, y + 10, "HepuOS", COLOR_WHITE);

    /* 实时时钟 */
    rtc_time_t now;
    rtc64_get_time(&now);
    char time_str[16];
    time_str[0] = '0' + now.hour / 10;
    time_str[1] = '0' + now.hour % 10;
    time_str[2] = ':';
    time_str[3] = '0' + now.minute / 10;
    time_str[4] = '0' + now.minute % 10;
    time_str[5] = 0;
    gfx64_put_string(screen_width - 80, y + 10, time_str, COLOR_LIGHT);

    /* 分隔线 */
    gfx64_fill_rect(0, y, screen_width, 1, 0x334155);
}

/* 绘制鼠标光标 */
static void desktop_draw_cursor(int x, int y)
{
    /* 简单的箭头光标 */
    static const uint8_t cursor_shape[16] = {
        0x80, 0x00,  /* X....... */
        0xC0, 0x00,  /* XX...... */
        0xE0, 0x00,  /* XXX..... */
        0xF0, 0x00,  /* XXXX.... */
        0xF8, 0x00,  /* XXXXX... */
        0xFC, 0x00,  /* XXXXXX.. */
        0xFE, 0x00,  /* XXXXXXX. */
        0xFF, 0x00,  /* XXXXXXXX */
        0xFF, 0x00,  /* XXXXXXXX */
        0xFC, 0x00,  /* XXXXXX.. */
        0xDE, 0x00,  /* XX.XXXX. */
        0x8E, 0x00,  /* X...XX.. */
        0x06, 0x00,  /* .....XX. */
        0x03, 0x00,  /* ......XX */
        0x01, 0x80,  /* .......X */
        0x00, 0xC0   /* ........XX */
    };

    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 16; col++) {
            if (cursor_shape[row] & (0x80 >> col)) {
                int px = x + col;
                int py = y + row;
                if (px >= 0 && px < screen_width && py >= 0 && py < screen_height) {
                    gfx64_put_pixel(px, py, COLOR_WHITE);
                }
            }
        }
    }
}

/* 绘制图标 */
static void desktop_draw_icons(void)
{
    int start_x = 20;
    int start_y = 20;
    int cols = (screen_width - 40) / (ICON_SIZE + ICON_SPACING);

    for (int i = 0; desktop_icons[i].name; i++) {
        int col = i % cols;
        int row = i / cols;
        int x = start_x + col * (ICON_SIZE + ICON_SPACING);
        int y = start_y + row * (ICON_SIZE + ICON_SPACING + ICON_LABEL_HEIGHT);

        /* 图标背景（圆角矩形简化为矩形） */
        gfx64_fill_rect(x, y, ICON_SIZE, ICON_SIZE, desktop_icons[i].color);

        /* 图标内部装饰 */
        gfx64_fill_rect(x + 8, y + 8, ICON_SIZE - 16, ICON_SIZE - 16,
                        desktop_icons[i].color + 0x202020);

        /* 图标标签 */
        gfx64_put_string(x, y + ICON_SIZE + 2, desktop_icons[i].label, COLOR_WHITE);
    }
}

/* 绘制桌面 */
void desktop64_draw(void)
{
    if (!desktop_initialized) return;

    desktop_draw_background();
    desktop_draw_icons();
    desktop_draw_taskbar();

    /* 绘制鼠标光标 */
    int mx, my;
    mouse64_get_pos(&mx, &my);
    desktop_draw_cursor(mx, my);
}

/* 检测图标点击 */
static int desktop_check_icon_click(int x, int y)
{
    int start_x = 20;
    int start_y = 20;
    int cols = (screen_width - 40) / (ICON_SIZE + ICON_SPACING);

    for (int i = 0; desktop_icons[i].name; i++) {
        int col = i % cols;
        int row = i / cols;
        int ix = start_x + col * (ICON_SIZE + ICON_SPACING);
        int iy = start_y + row * (ICON_SIZE + ICON_SPACING + ICON_LABEL_HEIGHT);

        if (x >= ix && x < ix + ICON_SIZE &&
            y >= iy && y < iy + ICON_SIZE + ICON_LABEL_HEIGHT) {
            return i;
        }
    }
    return -1;
}

/* 初始化桌面 */
int desktop64_init(void)
{
    int bpp;
    gfx64_init_from_bootloader();
    gfx64_get_info(&screen_width, &screen_height, &bpp);

    if (screen_width == 0 || screen_height == 0) {
        vga_puts("[DESKTOP64] No VBE mode, using text mode\n");
        return -1;
    }

    vga_puts("[DESKTOP64] Desktop initialized: ");
    char buf[16];
    int n = screen_width, i = 0;
    char tmp[8];
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j++] = 'x';
    n = screen_height; i = 0;
    do { tmp[i++] = n % 10 + '0'; } while ((n /= 10) > 0);
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = 0;
    vga_puts(buf);
    vga_puts("\n");

    desktop_initialized = 1;
    desktop64_draw();

    return 0;
}

/* 桌面主循环（带鼠标交互和实时更新） */
void desktop64_run(void)
{
    if (!desktop_initialized) {
        if (desktop64_init() != 0) {
            vga_puts("[DESKTOP64] Failed to initialize desktop\n");
            return;
        }
    }

    vga_puts("[DESKTOP64] Desktop running with mouse support\n");

    static int last_left = 0;
    static int last_right = 0;
    static int last_minute = -1;

    /* 事件循环 */
    while (1) {
        /* 更新鼠标 */
        mouse64_update(screen_width, screen_height);

        /* 检查键盘输入（ESC退出到Shell） */
        extern int keyboard64_haschar(void);
        extern int keyboard64_getchar(void);
        if (keyboard64_haschar()) {
            int key = keyboard64_getchar();
            if (key == 27) {  /* ESC */
                vga_puts("[DESKTOP64] ESC pressed, returning to Shell...\n");
                extern void shell64_run(void);
                shell64_run();
                return;
            }
            /* F1 (0x3B scancode) 切换 HepuShell / 传统桌面 */
            if (key == 0x3B || key == 59) {
                use_hepushell = !use_hepushell;
                if (use_hepushell) {
                    vga_puts("[DESKTOP64] Switching to HepuShell...\n");
                    hepushell_init(screen_width, screen_height);
                } else {
                    vga_puts("[DESKTOP64] Switching to classic desktop...\n");
                }
            }
            /* F5 (0x3F scancode) 重新加载 HepuShell 配置 */
            if (key == 0x3F || key == 63) {
                if (use_hepushell) {
                    vga_puts("[DESKTOP64] Reloading HepuShell config...\n");
                    hepushell_reload();
                }
            }
            /* HepuShell 模式下传递按键 */
            if (use_hepushell) {
                hepushell_handle_key(key);
            }
        }

        /* 渲染 */
        if (use_hepushell) {
            int mx, my;
            mouse64_get_pos(&mx, &my);
            hepushell_update_mouse(mx, my);
            hepushell_render();
        } else {
            desktop64_draw();
        }

        /* 检测左键按下（下降沿）：用于窗口拖拽 */
        int left = mouse64_get_left();
        if (left && !last_left) {
            int mx, my;
            mouse64_get_pos(&mx, &my);
            if (use_hepushell) {
                extern void hepushell_mouse_down(int x, int y);
                hepushell_mouse_down(mx, my);
            }
        }

        /* 检测左键释放（上升沿）：点击或拖拽结束 */
        if (!left && last_left) {
            int mx, my;
            mouse64_get_pos(&mx, &my);
            if (use_hepushell) {
                extern void hepushell_mouse_up(int x, int y);
                extern void hepushell_handle_click(int x, int y);
                hepushell_mouse_up(mx, my);
                hepushell_handle_click(mx, my);
            } else {
                int icon_idx = desktop_check_icon_click(mx, my);
                if (icon_idx >= 0) {
                vga_puts("[DESKTOP64] Launching: ");
                vga_puts(desktop_icons[icon_idx].label);
                vga_puts("\n");

                /* 应用启动：切换到文本模式执行对应命令 */
                extern void shell64_run(void);
                extern void vga_set_text_mode(void);

                /* 简单应用映射 */
                switch (icon_idx) {
                    case 0: /* 终端 */
                        vga_puts("[DESKTOP64] Opening Terminal...\n");
                        gfx_terminal_run();
                        desktop64_draw();
                        break;
                    case 1: /* 文件管理器 */
                        vga_puts("[DESKTOP64] Opening File Manager...\n");
                        gfx_app_run("File Manager", app_filemgr);
                        desktop64_draw();
                        break;
                    case 2: /* 浏览器 */
                        vga_puts("[DESKTOP64] Opening Browser...\n");
                        gfx_app_run("Web Browser", app_about);
                        desktop64_draw();
                        break;
                    case 3: /* 系统设置 */
                        vga_puts("[DESKTOP64] Opening Settings...\n");
                        gfx_app_run("System Settings", app_settings);
                        desktop64_draw();
                        break;
                    case 4: /* 任务管理器 */
                        vga_puts("[DESKTOP64] Opening Task Manager...\n");
                        gfx_app_run("Task Manager", app_taskmgr);
                        desktop64_draw();
                        break;
                    case 5: /* 记事本 */
                        vga_puts("[DESKTOP64] Opening Notepad...\n");
                        gfx_app_run("Notepad", app_notes);
                        desktop64_draw();
                        break;
                    case 6: /* 应用商店 */
                        vga_puts("[DESKTOP64] Opening App Store...\n");
                        gfx_app_run("App Store", app_appstore);
                        desktop64_draw();
                        break;
                    case 7: /* 计算器 */
                        vga_puts("[DESKTOP64] Opening Calculator...\n");
                        gfx_app_run("Calculator", app_about);
                        desktop64_draw();
                        break;
                    case 8: /* 关于 */
                        gfx_app_run("About HepuOS", app_about);
                        desktop64_draw();
                        break;
                    default:
                        vga_puts("[DESKTOP64] App not implemented yet\n");
                        break;
                }
                vga_puts("[DESKTOP64] Returning to Shell...\n");
            }
            }  /* end else (classic desktop) */
        }
        last_left = left;

        /* 检测右键点击（上升沿） */
        int right = mouse64_get_right();
        if (right && !last_right) {
            int mx, my;
            mouse64_get_pos(&mx, &my);
            if (use_hepushell) {
                extern void hepushell_handle_right_click(int x, int y);
                hepushell_handle_right_click(mx, my);
            }
        }
        last_right = right;

        /* 检测HepuShell Dock图标点击的应用启动请求 */
        if (use_hepushell) {
            extern int g_pending_app;
            if (g_pending_app != 0) {
                int app = g_pending_app;
                g_pending_app = 0;
                vga_puts("[DESKTOP64] Launching app from Dock: ");
                switch (app) {
                    case 1: vga_puts("Terminal\n"); gfx_terminal_run(); break;
                    case 2: vga_puts("Files\n"); gfx_app_run("File Manager", app_filemgr); break;
                    case 3: vga_puts("Browser\n"); gfx_app_run("Web Browser", app_about); break;
                    case 4: vga_puts("Settings\n"); gfx_app_run("System Settings", app_settings); break;
                    case 5: vga_puts("Editor\n"); gfx_app_run("Text Editor", app_notes); break;
                    case 6: vga_puts("Calculator\n"); gfx_app_run("Calculator", app_about); break;
                    case 7: vga_puts("Notes\n"); gfx_app_run("Notepad", app_notes); break;
                    case 8: vga_puts("About\n"); gfx_app_run("About HepuOS", app_about); break;
                }
            }
        }

        /* 每分钟重绘一次（更新时钟） */
        rtc_time_t now;
        rtc64_get_time(&now);
        if (now.minute != last_minute) {
            last_minute = now.minute;
            desktop64_draw();
        }

        /* 短暂延迟，减少CPU占用 */
        for (volatile int i = 0; i < 100000; i++) { asm volatile("nop"); }
    }
}

/* ============================================================================
 * 图形模式终端
 * ========================================================================== */
#define TERM_X 50
#define TERM_Y 50
#define TERM_W 700
#define TERM_H 500
#define TERM_BG 0x1E1E1E
#define TERM_BORDER 0x4A90D9
#define TERM_TEXT 0x00FF00
#define TERM_TITLE 0xFFFFFF
#define TERM_MAX_LINES 25
#define TERM_MAX_CHARS 80

static char term_lines[TERM_MAX_LINES][TERM_MAX_CHARS];
static int term_line_count = 0;
static int term_cursor_x = 0;
static int term_cursor_y = 0;

static void term_clear(void) {
    for (int i = 0; i < TERM_MAX_LINES; i++) {
        term_lines[i][0] = 0;
    }
    term_line_count = 0;
    term_cursor_x = 0;
    term_cursor_y = 0;
}

static void term_print(const char *s) {
    while (*s) {
        if (*s == '\n') {
            term_lines[term_cursor_y][term_cursor_x] = 0;
            term_cursor_y++;
            term_cursor_x = 0;
            if (term_cursor_y >= TERM_MAX_LINES) {
                /* 滚动 */
                for (int i = 0; i < TERM_MAX_LINES - 1; i++) {
                    for (int j = 0; j < TERM_MAX_CHARS; j++) {
                        term_lines[i][j] = term_lines[i + 1][j];
                    }
                }
                term_lines[TERM_MAX_LINES - 1][0] = 0;
                term_cursor_y = TERM_MAX_LINES - 1;
            }
            term_line_count = term_cursor_y + 1;
        } else if (term_cursor_x < TERM_MAX_CHARS - 1) {
            term_lines[term_cursor_y][term_cursor_x++] = *s;
            term_lines[term_cursor_y][term_cursor_x] = 0;
        }
        s++;
    }
}

static void term_draw(void) {
    /* 窗口背景 */
    gfx64_fill_rect(TERM_X, TERM_Y, TERM_W, TERM_H, TERM_BG);
    /* 边框 */
    gfx64_fill_rect(TERM_X, TERM_Y, TERM_W, 3, TERM_BORDER);
    gfx64_fill_rect(TERM_X, TERM_Y + TERM_H - 3, TERM_W, 3, TERM_BORDER);
    gfx64_fill_rect(TERM_X, TERM_Y, 3, TERM_H, TERM_BORDER);
    gfx64_fill_rect(TERM_X + TERM_W - 3, TERM_Y, 3, TERM_H, TERM_BORDER);
    /* 标题栏 */
    gfx64_fill_rect(TERM_X + 3, TERM_Y + 3, TERM_W - 6, 25, TERM_BORDER);
    if (gfx_app_title[0]) {
        gfx64_put_string(TERM_X + 10, TERM_Y + 8, gfx_app_title, TERM_TITLE);
    } else {
        gfx64_put_string(TERM_X + 10, TERM_Y + 8, "HepuOS Terminal (ESC to close)", TERM_TITLE);
    }
    /* 文本内容 */
    int y = TERM_Y + 35;
    for (int i = 0; i < term_line_count && i < TERM_MAX_LINES; i++) {
        gfx64_put_string(TERM_X + 10, y, term_lines[i], TERM_TEXT);
        y += 16;
    }
    /* 光标 */
    gfx64_fill_rect(TERM_X + 10 + term_cursor_x * 8, y - 2, 8, 14, TERM_TEXT);
}

/* 输出重定向钩子：Shell输出到图形终端 */
static void term_output_hook(const char *s, int len) {
    for (int i = 0; i < len; i++) {
        char c = s[i];
        if (c == '\n') {
            term_lines[term_cursor_y][term_cursor_x] = 0;
            term_cursor_y++;
            term_cursor_x = 0;
            if (term_cursor_y >= TERM_MAX_LINES) {
                for (int j = 0; j < TERM_MAX_LINES - 1; j++) {
                    for (int k = 0; k < TERM_MAX_CHARS; k++) {
                        term_lines[j][k] = term_lines[j + 1][k];
                    }
                }
                term_lines[TERM_MAX_LINES - 1][0] = 0;
                term_cursor_y = TERM_MAX_LINES - 1;
            }
            term_line_count = term_cursor_y + 1;
        } else if (c == '\r') {
            term_cursor_x = 0;
        } else if (c == '\b') {
            if (term_cursor_x > 0) term_cursor_x--;
            term_lines[term_cursor_y][term_cursor_x] = 0;
        } else if (term_cursor_x < TERM_MAX_CHARS - 1) {
            term_lines[term_cursor_y][term_cursor_x++] = c;
            term_lines[term_cursor_y][term_cursor_x] = 0;
        }
    }
}

void gfx_terminal_run(void) {
    extern void shell_execute(char *line);
    extern void set_output_hook(void (*hook)(const char *s, int len));

    gfx_app_title[0] = 0;  /* 清空标题，显示默认终端标题 */
    term_clear();

    /* 设置输出重定向钩子 */
    set_output_hook(term_output_hook);

    vga_puts("HepuOS 64-bit Terminal v2.0\n");
    vga_puts("Full Shell integration - 75+ commands\n");
    vga_puts("Type 'help' for commands, ESC to close.\n\n");

    char cmd_buf[TERM_MAX_CHARS];
    int cmd_pos = 0;
    cmd_buf[0] = 0;

    vga_puts("hepu@os:~$ ");
    term_draw();

    while (1) {
        if (keyboard64_haschar()) {
            int c = keyboard64_getchar();
            if (c == 27) {  /* ESC */
                set_output_hook(0);  /* 清除钩子 */
                return;
            } else if (c == '\n' || c == '\r') {
                vga_putchar('\n');
                cmd_buf[cmd_pos] = 0;
                if (cmd_buf[0]) {
                    shell_execute(cmd_buf);
                }
                cmd_pos = 0;
                cmd_buf[0] = 0;
                vga_puts("hepu@os:~$ ");
            } else if (c == '\b' || c == 0x7F) {
                if (cmd_pos > 0) {
                    cmd_pos--;
                    cmd_buf[cmd_pos] = 0;
                    vga_putchar('\b');
                }
            } else if (c >= 0x20 && c < 0x7F && cmd_pos < TERM_MAX_CHARS - 3) {
                cmd_buf[cmd_pos++] = (char)c;
                cmd_buf[cmd_pos] = 0;
                vga_putchar((char)c);
            }
            term_draw();
        }
        /* 短暂延迟 */
        for (volatile int i = 0; i < 10000; i++) { asm volatile("nop"); }
    }
}

/* ============================================================================
 * 通用图形应用查看器
 * ========================================================================== */

void gfx_app_run(const char *title, void (*app_func)(void)) {
    extern void set_output_hook(void (*hook)(const char *s, int len));

    /* 保存标题 */
    int i = 0;
    while (title[i] && i < 63) {
        gfx_app_title[i] = title[i];
        i++;
    }
    gfx_app_title[i] = 0;

    term_clear();
    set_output_hook(term_output_hook);

    /* 调用应用函数 */
    if (app_func) {
        app_func();
    }

    vga_puts("\n[Press ESC to return to desktop]\n");

    /* 等待ESC */
    while (1) {
        if (keyboard64_haschar()) {
            int c = keyboard64_getchar();
            if (c == 27) {  /* ESC */
                break;
            }
        }
        term_draw();
        for (volatile int i = 0; i < 10000; i++) { asm volatile("nop"); }
    }

    set_output_hook(0);
}

/* 应用包装函数 */
void app_filemgr(void) {
    extern void fm64_init(void);
    fm64_init();
}

void app_settings(void) {
    extern void syssettings64_show(void);
    syssettings64_show();
}

void app_notes(void) {
    extern void notes64_show(void);
    notes64_show();
}

void app_taskmgr(void) {
    extern void taskmgr64_init(void);
    taskmgr64_init();
}

void app_appstore(void) {
    extern void appstore64_show_list(void);
    appstore64_show_list();
}

void app_about(void) {
    vga_puts("========================================\n");
    vga_puts("  HepuOS 64-bit Microkernel OS\n");
    vga_puts("  Version: 0.10.117\n");
    vga_puts("  Architecture: x86_64 (Long Mode)\n");
    vga_puts("  Graphics: VBE 1024x768x32\n");
    vga_puts("  Shell: 75+ commands\n");
    vga_puts("  Filesystem: ext2\n");
    vga_puts("========================================\n");
}
