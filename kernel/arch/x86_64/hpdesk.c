/* ============================================================================
 * 赫普OS - Hepu Desktop 桌面环境实现
 * 复现 Quickshell 核心功能
 * V0.10.160
 * ========================================================================== */

#include "hs_ui.h"
#include "hpdesk.h"
#include "spinlock.h"
#include "hs_icons_data.h"   /* V0.57.0: 系统图标位图集(32x32 1bit) */
#include "png64.h"
#include "jpeg64.h"

extern void vga_printf(const char *fmt, ...);
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
static void hs_icon_settings(int x, int y, int size, uint32_t color);
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
extern void gfx64_put_pixel(int x, int y, uint32_t color);
extern void gfx64_draw_rect(int x, int y, int w, int h, uint32_t color);
extern void gfx64_fill_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
extern void gfx64_draw_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
extern void vg_stroke_round_rect(int x, int y, int w, int h, int r, int width, uint32_t color);
extern void gfx64_fill_round_rect_aa(int x, int y, int w, int h, int radius, uint32_t color);
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
/* V0.55.1: 这里的 g_render_lock 从未被任何代码加/解锁, 属残留死代码, 已移除。
 * 桌面为单线程渲染(中断不上桌面 API), 当前无需互斥; 若将来做多线程渲染需重新引入。 */

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

/* 自适应缩放辅助函数（基准1024x768，纯整数运算避免FPU状态问题）
 * g_ui_scale_pct: 用户选择的界面缩放百分比(100/125/150/200), 与分辨率解耦。
 * 物理像素 = 逻辑像素 * (screen_w/1024) * (pct/100)
 * 把缩放做成显式因子后, 分辨率变了不会再顺带改变"组件相对大小",
 * 用户也能在设置里单独放大界面(字体是矢量光栅化, 放大是重画不是拉伸)。 */
int g_ui_scale_pct = 100;
/* 1=缩放跟随分辨率自动(默认), 0=用户在 Display 页手动选过档位 */
int g_disp_scale_auto = 1;
int g_disp_scale_auto_pct = 100;
/* 物理 = 逻辑 * (pct/100), 与分辨率无关 —— 这就是 Windows 的 DIP 语义:
 * 1 DIP 在 150% 下就是 1.5 物理像素, 逻辑画布 = 屏宽/1.5(1920 屏上是 1280)。
 * 旧式 (v * screen_w / 1024 * pct / 100) 把基准分辨率重复乘了一次,
 * 150% 在 1920 屏上实际放大 2.81 倍, 逻辑画布被压到 682x512,
 * 侧栏(500逻辑)+天气卡 根本装不下 —— 200% 装不下的是同一个错。 */
static int hs_sx(int v) { return (v * g_ui_scale_pct) / 100; }
static int hs_sy(int v) { return (v * g_ui_scale_pct) / 100; }
static int hs_s(int v)  {
    int sx = (v * g_ui_scale_pct) / 100;
    int sy = (v * g_ui_scale_pct) / 100;
    return (sx < sy) ? sx : sy;
}

/* 供其他模块(hs_apps.c / hs_systools64.c)使用的缩放入口。
 * hs_sx/hs_sy 是 hpdesk.c 内的 static, 跨文件拿不到;
 * 窗口创建尺寸必须和桌面的缩放语义同源, 否则两套 DPI 会打架。 */
int hs_ui_sx(int v) { return hs_sx(v); }
int hs_ui_sy(int v) { return hs_sy(v); }

/* 全局实例 */
hpdesk_t g_shell;

/* 渲染帧计数 (动画时基, 桌面循环约 60fps) */
static uint32_t g_frame_no;

/* 前向声明（定义在后面） */
static int g_sidebar_tab;
static int g_sidebar_collapsed;   /* 侧栏收起: 只留左边缘一条把手 */
static int g_island_expanded;
/* V0.55.0 P0: 灵动岛展开/收起缓动 (ease-out cubic, 0..100) */
static int g_island_t = 0;        /* 当前进度: 0=收起 100=展开 */
static int g_island_target = 0;   /* 目标进度 */
static int hs_ease_out_cubic(int p)
{
    if (p <= 0) return 0;
    if (p >= 100) return 100;
    int m = 100 - p;
    return 100 - (m * m * m) / 10000;
}

/* 窗口内容缩放的唯一实现。所有 app 的 DS 宏最终都落到这里 ——
 * 各窗口只声明自己的基准宽, 比例规则只有这一处。
 * base_w 传 0 或窗口宽度非法时, 退回 base_w 本身(基准尺寸恒等于原值)。 */
int hs_win_k(hs_window_t *w, int base_w, int v)
{
    int bw = (w && w->width > 0) ? w->width : base_w;
    if (base_w <= 0) return v;
    return v * bw / base_w;
}
/* V0.55.0 P0: 弹出浮层滑入缓动 (0..100, 与灵动岛共用 ease-out cubic) */
static int g_menu_t = 0, g_menu_target = 0;
static int g_color_t = 0, g_color_target = 0;

static void hs_pop_tick(int *t, int *target)
{
    if (*t == *target) return;
    int step = 10;
    if (*t < *target) { *t += step; if (*t > *target) *t = *target; }
    else { *t -= step; if (*t < *target) *t = *target; }
}

static void hs_island_tick(void)
{
    if (g_island_t == g_island_target) return;
    int step = 8;   /* ≈120ms @60fps */
    if (g_island_t < g_island_target) {
        g_island_t += step;
        if (g_island_t > g_island_target) g_island_t = g_island_target;
    } else {
        g_island_t -= step;
        if (g_island_t < g_island_target) g_island_t = g_island_target;
    }
    vga_printf("[P0] island t=%d\n", g_island_t);
}
int g_pending_app;

/* V0.55: 侧栏滚动 (对齐 Clavis SidebarFlickable: 垂直滚动+细滚动条)
 * 基准单位(1024x768), 渲染时经 hs_sy 缩放; 仅内容溢出的标签页生效 */
static int g_sidebar_scroll = 0;

/* V0.56: 程序化壁纸 (对齐 Clavis Wallpaper 切换 + matugen 壁纸取色)
 * 0=Classic 纯色(默认, 保回归稳定) 1=Dusk 紫橙落日 2=Ocean 青蓝渐变;
 * 切换到非 Classic 后下一帧壁纸绘制完成即采样网格提取源色并应用主题 */
/* V0.59.26: 壁纸选择统一到 g_wp_sel(定义在壁纸加载区), 旧的 g_wallpaper_style 已移除 */
static int g_wallpaper_dirty = 0;
#define WP_IMG_COUNT 3

/* V0.55.0: 桌面背景色预设色板 + 持久化 (Settings/Appearance 页与右键面板共用) */
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

/* V0.59.26: 壁纸选择持久化。config.hsqml 里写一行 wallpaper: N (0-5)。
 * 读不到就回 0(默认沿用现在这张 PNG)。 */
static int hs_wp_read_cfg(void)
{
    extern int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size);
    static char buf[1024];
    int n = ext2_64_read_file("/etc/hepushell/config.hsqml", (uint8_t *)buf, 1023);
    if (n <= 0) return 0;
    buf[n] = 0;
    for (int i = 0; i + 9 < n; i++) {
        if (buf[i]=='w'&&buf[i+1]=='a'&&buf[i+2]=='l'&&buf[i+3]=='l'&&
            buf[i+4]=='p'&&buf[i+5]=='a'&&buf[i+6]=='p'&&buf[i+7]=='e'&&
            buf[i+8]=='r'&&buf[i+9]==':') {
            int j = i + 10;
            while (j < n && (buf[j] == ' ' || buf[j] == '\t')) j++;
            int v = 0, got = 0;
            while (j < n && buf[j] >= '0' && buf[j] <= '9') { v = v * 10 + (buf[j]-'0'); j++; got = 1; }
            if (got) return (v >= 0 && v <= WP_IMG_COUNT + 2) ? v : 0;
            return 0;
        }
    }
    return 0;
}

static void hs_wp_save_cfg(int sel)
{
    extern int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size);
    extern int ext2_64_write_file(const char *path, const uint8_t *data, uint32_t size);
    static char buf[4096];
    int n = ext2_64_read_file("/etc/hepushell/config.hsqml", (uint8_t *)buf, 4095);
    if (n <= 0) return;
    buf[n] = 0;
    char digit = (char)('0' + sel);
    for (int i = 0; i + 9 < n; i++) {
        if (buf[i]=='w'&&buf[i+1]=='a'&&buf[i+2]=='l'&&buf[i+3]=='l'&&
            buf[i+4]=='p'&&buf[i+5]=='a'&&buf[i+6]=='p'&&buf[i+7]=='e'&&
            buf[i+8]=='r'&&buf[i+9]==':') {
            int j = i + 10;
            while (j < n && (buf[j] == ' ' || buf[j] == '\t')) j++;
            if (j < n && buf[j] >= '0' && buf[j] <= '9') buf[j] = digit;
            else if (j < n && buf[j] == '\n') {
                buf[j] = digit;   /* 空值: 直接补上 */
            }
            ext2_64_write_file("/etc/hepushell/config.hsqml", (uint8_t *)buf, (uint32_t)n);
            return;
        }
    }
    /* 没有 wallpaper 行: 在 background 行后面插一行。
     * 第一版只改"已存在的行", 而默认配置里根本没有这一行, 切换壁纸悄悄不落盘;
     * 第二版插入时把长度算错, 直接吃掉了下一行的 "panel_bg: ..." —— 现在用
     * memmove 搬一次, 插入定长 18 字节, 不再逐字节错位拼接。 */
    for (int i = 0; i + 12 < n; i++) {
        if (buf[i]=='b'&&buf[i+1]=='a'&&buf[i+2]=='c'&&buf[i+3]=='k'&&
            buf[i+4]=='g'&&buf[i+5]=='r'&&buf[i+6]=='o'&&buf[i+7]=='u'&&
            buf[i+8]=='n'&&buf[i+9]=='d'&&buf[i+10]==':') {
            int j = i;
            while (j < n && buf[j] != '\n') j++;      /* j 指向该行行尾 */
            if (j >= n || n + 19 >= 4095) return;
            /* 插在行尾之后: "\n    wallpaper: N" 共 18 字节 */
            char ins[19];
            ins[0] = '\n';
            ins[1] = ' '; ins[2] = ' '; ins[3] = ' '; ins[4] = ' ';
            ins[5] = 'w'; ins[6] = 'a'; ins[7] = 'l'; ins[8] = 'l';
            ins[9] = 'p'; ins[10] = 'a'; ins[11] = 'p'; ins[12] = 'e';
            ins[13] = 'r'; ins[14] = ':'; ins[15] = ' '; ins[16] = digit;
            ins[17] = '\n'; ins[18] = 0;
            int L = 18;
            for (int k = n; k > j; k--) buf[k + L] = buf[k];
            for (int k = 0; k < L; k++) buf[j + 1 + k] = ins[k];
            n += L;
            buf[n] = 0;
            ext2_64_write_file("/etc/hepushell/config.hsqml", (uint8_t *)buf, (uint32_t)n);
            return;
        }
    }
}

/* 侧栏收起状态持久化：把 config.hsqml 里 sidebar_collapsed: 后的单个数字改掉。
 * 键缺失时不做追加（模板里已含），避免在语句中间插入破坏 hsqml。 */
static void hs_save_sidebar_collapsed(int v)
{
    extern int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size);
    extern int ext2_64_write_file(const char *path, const uint8_t *data, uint32_t size);
    static char sbuf[4096];
    int n = ext2_64_read_file("/etc/hepushell/config.hsqml", (uint8_t *)sbuf, 4095);
    if (n <= 0) return;
    sbuf[n] = 0;
    for (int i = 0; i + 17 < n; i++) {
        if (sbuf[i] == 's' && sbuf[i+1] == 'i' && sbuf[i+2] == 'd' && sbuf[i+3] == 'e' &&
            sbuf[i+4] == 'b' && sbuf[i+5] == 'a' && sbuf[i+6] == 'r' && sbuf[i+7] == '_' &&
            sbuf[i+8] == 'c' && sbuf[i+9] == 'o' && sbuf[i+10] == 'l' && sbuf[i+11] == 'l' &&
            sbuf[i+12] == 'a' && sbuf[i+13] == 'p' && sbuf[i+14] == 's' &&
            sbuf[i+15] == 'e' && sbuf[i+16] == 'd' && sbuf[i+17] == ':') {
            int j = i + 18;
            while (j < n && (sbuf[j] == ' ' || sbuf[j] == '\t')) j++;
            if (j < n && (sbuf[j] == '0' || sbuf[j] == '1')) {
                sbuf[j] = v ? '1' : '0';
                ext2_64_write_file("/etc/hepushell/config.hsqml", (uint8_t *)sbuf, (uint32_t)n);
            }
            break;
        }
    }
}

/* 右键菜单: 由 g_ctx(见下) 统一承载, 旧的 g_context_menu_* 那套已下线 */
/* V0.13.39: 通知位置配置 (0=右上 1=左上 2=右下 3=左下) */
static int g_notif_pos = 0;

/* 窗口拖拽状态（定义在后面） */
/* 右键菜单状态(定义提前: hpdesk_handle_click 在渲染函数之前就要用到) */
#define HS_CTX_MAX 9
static struct {
    int open;
    int x, y, w, h;
    int count;
    int owner;                        /* 0=桌面 1=Files 2=通用窗口 */
    char label[HS_CTX_MAX][28];
    int  id[HS_CTX_MAX];
} g_ctx;

#define HS_CTX_D_REFRESH 0
#define HS_CTX_D_FILES   1
#define HS_CTX_D_SETTING 2
#define HS_CTX_D_TASK    3
#define CUR_ARROW 0
#define CUR_LINK  1
#define CUR_TEXT  2
#define CUR_MOVE  3

static int  hs_ctx_hit_item(int x, int y);
static void hs_ctx_menu_render(void);
void hs_ctx_close(void);
void hs_ctx_open(int x, int y, const char *const *labels, const int *ids, int n, int owner);
void hpdesk_right_click(int x, int y);

static hs_window_t *g_drag_window;
static int g_drag_offset_x;
static int g_drag_offset_y;
static int g_dragging;

/* 设置窗口当前页面 */
static int g_content_dirty;   /* 定义在文件后部, 此处前向声明供设置页使用 */
static int g_settings_page = 0;  /* 0=Appearance, 1=Panels, 2=System, 3=About, 4=Display */
/* Display 页缩放按钮的渲染位置: 命中测试直接复用, 不另算一套坐标 */
static int g_disp_scale_btn_x = 0, g_disp_scale_btn_y = 0;
/* 分辨率档位(点选后写 ESP 的 HEPBOOT.CFG, 重启生效) */
static int g_disp_res_btn_x = 0, g_disp_res_btn_y = 0;
static const int g_disp_res_w[4] = {1024, 1280, 1920, 2048};
static const int g_disp_res_h[4] = {768, 720, 1080, 1536};
static char g_disp_msg[48];
#define HS_DISP_RES_W 132
#define HS_DISP_RES_GAPX 140
#define HS_DISP_RES_GAPY 32
static const int g_disp_scale_pcts[4] = {100, 125, 150, 200};
#define HS_DISP_BTN_W 64
#define HS_DISP_BTN_GAP 72
static int g_disp_shadow_y = 0;     /* Display 页阴影开关行 */
static int g_disp_cursor_y = 0;     /* Display 页光标主题行 */
static int g_settings_storage_ry = 0; /* V0.13.39: 存储管理行点击区域 */
static int g_settings_reg_ry = 0;    /* V0.13.39: 注册表查看行 */
static int g_settings_assoc_ry = 0;  /* V0.13.39: 文件关联查看行 */

/* 内置默认配置（hsqml格式） — 对齐 quickshell/Clavis: M3 tonal-spot dark */
static const char *default_config =
"// Hepu Desktop 默认配置 - 对齐 quickshell/Clavis (Material You tonal-spot dark)\n"
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
"    skip_oobe: 0\n"
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
"    sidebar_x: 10\n"
"    sidebar_y: 50\n"
"    sidebar_width: 280\n"
"    sidebar_height: 500\n"
"    cc_margin_right: 384\n"
"    cc_y: 108\n"
"    cc_width: 280\n"
"    ui_scale: 100\n"
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
"    corner_radius: 12\n"
"    sidebar_collapsed: 0\n"
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

/* V0.55.2: 欢迎引导(OOBE)开关 —— 由 config.hsqml 的 UI.skip_oobe 控制,
 * 置 1 则启动直接进入桌面, 不再弹出首设向导。 */
static int g_skip_oobe = 0;

int hpdesk_skip_oobe(void) { return g_skip_oobe; }

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
    /* V0.59.3: 默认 10 —— 与顶栏胶囊 margin(hs_sx(10)) 同值。
     * 原来是 12, 150% 下侧栏左边落在 x=18、顶栏左边缘在 15, 扫描线实测差 3px。 */
    g_shell.ui.sidebar_x = hsqml_get_number(ui, "sidebar_x", 10);
    g_shell.ui.sidebar_y = hsqml_get_number(ui, "sidebar_y", 50);
    g_shell.ui.sidebar_width = hsqml_get_number(ui, "sidebar_width", 280);
    g_shell.ui.sidebar_height = hsqml_get_number(ui, "sidebar_height", 500);
    g_shell.ui.cc_margin_right = hsqml_get_number(ui, "cc_margin_right", 384);
    g_shell.ui.cc_y = hsqml_get_number(ui, "cc_y", 108);
    g_shell.ui.cc_width = hsqml_get_number(ui, "cc_width", 280);
    g_ui_scale_pct = (int)hsqml_get_number(ui, "ui_scale", 100);
    if (g_ui_scale_pct < 100) g_ui_scale_pct = 100;
    if (g_ui_scale_pct > 250) g_ui_scale_pct = 250;
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
    g_shell.ui.corner_radius = hsqml_get_number(ui, "corner_radius", 12);
    g_sidebar_collapsed = hsqml_get_number(ui, "sidebar_collapsed", 0);
    g_shell.ui.font_small = hsqml_get_number(ui, "font_small", 12);
    g_shell.ui.font_normal = hsqml_get_number(ui, "font_normal", 14);
    g_shell.ui.font_large = hsqml_get_number(ui, "font_large", 18);
    g_skip_oobe = hsqml_get_number(ui, "skip_oobe", 0);
}

static void load_panel_from_config(hsqml_node_t *panel);   /* 前置声明 */

/* 从配置加载面板组：逐个 Panel{} 子节点应用 */
static void load_panels_from_config(hsqml_node_t *panels)
{
    if (!panels) return;
    int n = hsqml_count_children(panels, "Panel");
    for (int i = 0; i < n; i++) {
        hsqml_node_t *panel = hsqml_get_child(panels, "Panel", i);
        if (panel) load_panel_from_config(panel);
    }
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

        hpdesk_add_widget(position, wtype);
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

/* 加载配置: 解析 /etc/hepushell/config.hsqml 并应用 Theme / UI / Panels
 * V0.55.1: 旧实现是空桩(注释写着"完全跳过配置加载"), 于是图形界面写回的
 * 配色与尺寸在重启后完全不生效 —— load_*_from_config 四个函数长期是死代码。
 * 现在真正解析并应用; 无配置文件或解析失败时安全回退到硬编码默认值。 */
static hsqml_result_t g_cfg_res;    /* 静态: 解析结果较大, 不放栈上 */

static int hpdesk_load_config(const char *config)
{
    (void)config;
    static char cfgbuf[4096];
    const char *src;
    int n = hs_read_config_file(cfgbuf, sizeof(cfgbuf));
    if (n > 0) {
        src = cfgbuf;
    } else {
        /* V0.55.1: 配置文件缺失时回退到内置 default_config(此前该常量从未被使用) */
        vga_puts("[Hepu Desktop] config: no file, using built-in defaults\n");
        src = default_config;
    }
    /* 注意: hsqml_parse 成功时返回"根节点数"(正数), 失败返回 -1 或置 error,
     * 不能用 "!=0" 判失败 —— 语义与常见 0=成功 的约定相反。 */
    int pr = hsqml_parse(src, &g_cfg_res);
    if (pr < 0 || g_cfg_res.error || g_cfg_res.root_count <= 0) {
        vga_printf("[Hepu Desktop] config: parse failed rc/roots=%d/%d err=%d line=%d msg=%s n=%d\n",
                   g_cfg_res.root_count, g_cfg_res.node_count, g_cfg_res.error,
                   g_cfg_res.error_line, g_cfg_res.error_msg, n);
        return -1;
    }

    int applied = 0;
    for (int i = 0; i < g_cfg_res.root_count; i++) {
        hsqml_node_t *r = g_cfg_res.roots[i];
        if (!r) continue;
        if (str_cmp(r->type, "Theme") == 0) { load_theme_from_config(r); applied++; }
        else if (str_cmp(r->type, "UI") == 0) { load_ui_from_config(r); applied++; }
        else if (str_cmp(r->type, "Panels") == 0) { load_panels_from_config(r); applied++; }
    }
    vga_printf("[Hepu Desktop] config: %d sections, background=0x%x\n",
               applied, (unsigned)g_shell.colors.bg);
    /* 显示设置(缩放)在配置解析之后覆盖, 优先级高于 config.hsqml 的 ui_scale。
     * 用户没在 Display 页手选过时, 缩放默认跟随分辨率自动。 */
    if (hs_display_load() == 0) {
        int a = hs_display_auto_scale();
        g_ui_scale_pct = a;
        g_disp_scale_auto = 1;
        vga_printf("[DISPLAY] auto scale=%d%% (screen %dx%d)\n",
                   a, g_shell.screen_width, g_shell.screen_height);
    }
    return applied > 0 ? 0 : -1;
}

static void itoa_hs(int value, char *buf);

/* 显示设置持久化: /display.conf (key=value, 与 config.hsqml 分开写, 互不覆盖) */
#define HS_DISPLAY_CFG "/display.conf"
/* 自动档位: 以 1024x768 为 100% 基准, 按屏高(纵向像素密度)推算,
 * 再吸附到 Display 页的四档上。2048x1536 -> 200%。 */
int hs_display_auto_scale(void)
{
    /* 设计基准 1024x768(横向) / 720(纵向预算)。
     * 缩放 pct 的语义是"元素放大 pct/100 倍", 逻辑画布 = 屏宽/pct、屏高/pct,
     * 必须 >= 基准才装得下整版。
     *
     * 纵向基准取 720 而不是 768: 16:9 屏(1920x1080)在 150% 下 logical 是 1280x720,
     * 用 768 会判"装不下"而退回 125% —— 1080p 上永远拿不到 150% 的字号。
     * 同一版式在 720 预算下排布, 4:3 屏上反而更宽松:
     *   1024x768@100%  -> logical 1024x768, 比 720 多 48(留白, 不溢出)
     *   2048x1536@200% -> logical 1024x768, 同上
     *   1920x1080@150% -> logical 1280x720, 刚好卡住
     * 200% 在 1080p 上是 960x540, 改基准也救不回来, 那是物理限制。 */
    int by_h = g_shell.screen_height * 100 / 720;
    int by_w = g_shell.screen_width * 100 / 1024;
    int lim = (by_h < by_w) ? by_h : by_w;
    if (lim < 100) lim = 100;
    int steps[4] = {100, 125, 150, 200};
    int best = steps[0];
    for (int i = 0; i < 4; i++)
        if (steps[i] <= lim) best = steps[i];
    g_disp_scale_auto_pct = best;
    return best;
}

void hs_display_save(void)
{
    extern int ext2_64_write_file(const char *path, const uint8_t *data, uint32_t size);
    char buf[96];
    int p = 0;
    const char *k = "scale=";
    while (k[p]) { buf[p] = k[p]; p++; }
    char n[8]; itoa_hs(g_ui_scale_pct, n);
    int j = 0; while (n[j]) { buf[p++] = n[j++]; }
    buf[p++] = '\n';
    const char *k2 = "resolution=";
    j = 0; while (k2[j]) { buf[p++] = k2[j++]; }
    char a[8]; itoa_hs(g_shell.screen_width, a);
    j = 0; while (a[j]) { buf[p++] = a[j++]; }
    buf[p++] = 'x';
    char b[8]; itoa_hs(g_shell.screen_height, b);
    j = 0; while (b[j]) { buf[p++] = b[j++]; }
    buf[p++] = '\n';
    const char *k3 = "shadow=";
    j = 0; while (k3[j]) { buf[p++] = k3[j++]; }
    buf[p++] = g_shell.ui.window_shadow ? '1' : '0';
    buf[p++] = '\n';
    const char *k4 = "cursor=";
    j = 0; while (k4[j]) { buf[p++] = k4[j++]; }
    buf[p++] = g_shell.ui.cursor_theme ? '1' : '0';
    buf[p++] = '\n'; buf[p] = 0;
    int rc = ext2_64_write_file(HS_DISPLAY_CFG, (const uint8_t *)buf, (uint32_t)p);
    vga_printf("[DISPLAY] save %s rc=%d\n", HS_DISPLAY_CFG, rc);
    (void)rc;
}

/* 把 resolution=WxH 写进 ESP 上的 HEPBOOT.CFG。
 * 分辨率由引导阶段 SetMode 决定(内核已 ExitBootServices, 运行时改不了),
 * 所以这里的语义是"写配置 + 提示重启", 而不是立即切换。
 * 逐行改写而不是整文件覆盖: 原文件里的 timeout/default/kernel_arch/cmdline
 * 必须原样保留, 否则会把引导配置清掉。 */
void hs_display_set_resolution(int ww, int hh)
{
    extern int hs_fat32_read(const char *name83, uint8_t *out, uint32_t max);
    extern int hs_fat32_write(const char *name83, const uint8_t *data, uint32_t len);
    uint8_t in[256], out[256];
    char tail[16];
    const char *key = "resolution=";
    int n, i = 0, p = 0, found = 0;

    n = hs_fat32_read("HEPBOOT CFG", in, sizeof(in) - 8);
    if (n < 0) n = 0;

    while (i < n && p < 200) {
        int ls = i, le, m, is_res = 1, t = 0;
        while (i < n && in[i] != '\n') i++;
        le = i;
        if (i < n) i++;                       /* 吃掉换行 */
        if (le - ls >= 11) {
            for (m = 0; key[m]; m++)
                if (in[ls + m] != (uint8_t)key[m]) { is_res = 0; break; }
        } else {
            is_res = 0;
        }
        if (is_res) {
            /* 命中 resolution 行: 整行换成新的 */
            for (m = 0; key[m]; m++) out[p++] = (uint8_t)key[m];
            itoa_hs(ww, tail);
            for (t = 0; tail[t]; t++) out[p++] = (uint8_t)tail[t];
            out[p++] = 'x';
            itoa_hs(hh, tail);
            for (t = 0; tail[t]; t++) out[p++] = (uint8_t)tail[t];
            out[p++] = '\n';
            found = 1;
        } else {
            for (m = ls; m < le && p < 200; m++) out[p++] = in[m];
            if (i <= n && p < 200) out[p++] = '\n';
        }
    }
    if (!found) {
        int m, t;
        for (m = 0; key[m]; m++) out[p++] = (uint8_t)key[m];
        itoa_hs(ww, tail); for (t = 0; tail[t]; t++) out[p++] = (uint8_t)tail[t];
        out[p++] = 'x';
        itoa_hs(hh, tail); for (t = 0; tail[t]; t++) out[p++] = (uint8_t)tail[t];
        out[p++] = '\n';
    }
    /* 去掉可能的尾部空行 */
    while (p > 0 && out[p - 1] == '\n' && p > 1 && out[p - 2] == '\n') p--;

    if (hs_fat32_write("HEPBOOT CFG", out, (uint32_t)p) == 0) {
        strcpy(g_disp_msg, "Saved. Reboot to apply.");
        vga_printf("[DISPLAY] resolution -> %dx%d written to HEPBOOT.CFG\n", ww, hh);
    } else {
        strcpy(g_disp_msg, "Write failed (ESP not writable)");
        vga_printf("[DISPLAY] resolution write failed\n");
    }
}

/* 返回 1 = 配置文件里有用户显式选择的 scale; 0 = 没有(应走自动) */
int hs_display_load(void)
{
    extern int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size);
    char buf[128];
    int n = ext2_64_read_file(HS_DISPLAY_CFG, (uint8_t *)buf, sizeof(buf) - 1);
    if (n <= 0) return 0;
    buf[n] = 0;
    int found = 0;
    /* 解析 scale=<num> / shadow=0|1 */
    for (int i = 0; buf[i]; i++) {
        if (buf[i] == 's' && buf[i + 1] == 'c' && buf[i + 2] == 'a' &&
            buf[i + 3] == 'l' && buf[i + 4] == 'e' && buf[i + 5] == '=') {
            int v = 0, j = i + 6;
            while (buf[j] >= '0' && buf[j] <= '9') { v = v * 10 + (buf[j] - '0'); j++; }
            if (v >= 100 && v <= 250) { g_ui_scale_pct = v; g_disp_scale_auto = 0; }
            vga_printf("[DISPLAY] loaded scale=%d%% (manual)\n", g_ui_scale_pct);
            found = 1;
        }
        if (buf[i] == 's' && buf[i + 1] == 'h' && buf[i + 2] == 'o' &&
            buf[i + 3] == 'a' && buf[i + 4] == 'd' && buf[i + 5] == 'o' &&
            buf[i + 6] == 'w' && buf[i + 7] == '=') {
            g_shell.ui.window_shadow = (buf[i + 8] == '1');
            vga_printf("[DISPLAY] loaded shadow=%d\n", g_shell.ui.window_shadow);
            found = 1;
            i += 8;
        }
        if (buf[i] == 'c' && buf[i + 1] == 'u' && buf[i + 2] == 'r' &&
            buf[i + 3] == 's' && buf[i + 4] == 'o' && buf[i + 5] == 'r' &&
            buf[i + 6] == '=') {
            g_shell.ui.cursor_theme = (buf[i + 7] == '1');
            vga_printf("[DISPLAY] loaded cursor=%d\n", g_shell.ui.cursor_theme);
            found = 1;
            i += 7;
        }
    }
    return found;
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

void hpdesk_set_theme(hs_theme_t theme)
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
    if (r > 255) r = 255;
    if (r < 0) r = 0;
    if (g > 255) g = 255;
    if (g < 0) g = 0;
    if (b > 255) b = 255;
    if (b < 0) b = 0;
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

/* V0.59.26: 壁纸选择 —— 0..2 是内置图片, 3=Dusk 渐变, 4=Ocean 渐变, 5=纯色(色板)。
 * 默认 0 = 现在这张 PNG。用户可在 Settings/Appearance 的 Wallpaper 行循环切换,
 * 也可以直接点 Desktop 色板, 那就等价于切到纯色模式。 */
static const char *g_wp_files[3] = {
    "/usr/share/wallpapers/wallpaper.png",
    "/usr/share/wallpapers/wallpaper-1.png",
    "/usr/share/wallpapers/wallpaper-2.png",
};
static int g_wp_sel = 0;
static int g_wp_loaded = -1;      /* 当前已解码成功的图片索引, -1 表示没有 */

static png64_image_t g_wp_png;
static jpeg64_image_t g_wp_jpg;
static int g_wp_is_jpeg = 0;

/* 按索引解码一张内置壁纸。切图时先释放上一张, 避免反复切换把内存吃光。 */
static int hs_wp_load(int idx)
{
    extern int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size);
    /* V0.59.27: 1MB —— 512x288 的壁纸放大到 1080p 会糊, 用户放进来的写实图
     * 需要更高分辨率(1024x576 约 530KB), 原 256KB 缓冲读不全。BSS 静态数组,
     * 不计入 1MB 内核体积上限。 */
    static uint8_t wp_raw[2097152];
    if (idx < 0 || idx >= WP_IMG_COUNT) return 0;
    if (g_wp_loaded == idx && g_wp_png.rgba) return 1;

    if (g_wp_png.rgba) { png64_free(&g_wp_png); }
    if (g_wp_jpg.rgba) { jpeg64_free(&g_wp_jpg); }
    g_wp_is_jpeg = 0;

    int n = ext2_64_read_file(g_wp_files[idx], wp_raw, sizeof(wp_raw));
    if (n > 8 && png64_decode(wp_raw, n, &g_wp_png) == 0 && g_wp_png.rgba) {
        g_wp_loaded = idx;
        vga_printf("[PNG64] wallpaper %d ok read=%d %dx%d\n", idx, n,
                   (int)g_wp_png.width, (int)g_wp_png.height);
        return 1;
    }
    g_wp_loaded = -1;
    return 0;
}

static int hs_wallpaper_png(void)
{
    if (g_wp_sel < 0 || g_wp_sel >= WP_IMG_COUNT) return 0;
    return hs_wp_load(g_wp_sel);
}

static const char *hs_wp_sel_name(void)
{
    if (g_wp_sel == 0) return "Wallpaper 1";
    if (g_wp_sel == 1) return "Wallpaper 2";
    if (g_wp_sel == 2) return "Wallpaper 3";
    if (g_wp_sel == 3) return "Dusk";
    if (g_wp_sel == 4) return "Ocean";
    return "Solid";
}

/* 壁纸预缩放缓存: 512x288 放大到 2048x1536 需要 300 万次「除法+alpha 混合」,
 * 在 QEMU 上一帧画不完, 截屏就抓到只刷了部分行的水平条纹。
 * 改为启动时算一次整屏 XRGB 缓存, 之后每帧只做整行 memcpy。 */
static uint32_t *g_wp_cache;
static int g_wp_cache_w = -1, g_wp_cache_h = -1;

static int hs_wallpaper_build_cache(int w, int h)
{
    uint8_t *src_px = g_wp_is_jpeg ? g_wp_jpg.rgba : g_wp_png.rgba;
    int sw = (int)(g_wp_is_jpeg ? g_wp_jpg.width : g_wp_png.width);
    int sh = (int)(g_wp_is_jpeg ? g_wp_jpg.height : g_wp_png.height);
    if (!src_px || sw <= 0 || sh <= 0) return 0;

    int dw = w, dh = (int)((long)sh * w / (long)sw);
    if (dh < h) { dh = h; dw = (int)((long)sw * h / (long)sh); }
    int dx = (w - dw) / 2, dy = (h - dh) / 2;

    static int xmap_buf[4096];
    if (w > 4096) return 0;
    for (int x = 0; x < w; x++) {
        int sx = (x - dx) * sw / dw;
        xmap_buf[x] = (sx < 0 || sx >= sw) ? -1 : sx;
    }

    extern void *kmalloc(uint64_t size);
    uint32_t *c = (uint32_t *)kmalloc((uint64_t)w * h * 4);
    if (!c) return 0;

    for (int y = 0; y < h; y++) {
        int sy = (y - dy) * sh / dh;
        uint32_t *out = c + (uint64_t)y * w;
        if (sy < 0 || sy >= sh) {
            for (int x = 0; x < w; x++) out[x] = 0;
            continue;
        }
        const uint8_t *row = src_px + (uint64_t)sy * sw * 4;
        for (int x = 0; x < w; x++) {
            int sx = xmap_buf[x];
            if (sx < 0) { out[x] = 0; continue; }
            const uint8_t *p = row + sx * 4;
            out[x] = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
        }
    }
    g_wp_cache = c;
    g_wp_cache_w = w;
    g_wp_cache_h = h;
    return 1;
}

static void hs_wallpaper_render(void)
{
    int w = g_shell.screen_width, h = g_shell.screen_height;
    if (hs_wallpaper_png()) {
        if (!g_wp_cache || g_wp_cache_w != w || g_wp_cache_h != h) {
            if (!hs_wallpaper_build_cache(w, h)) return;   /* 分配失败则不出壁纸 */
        }
        uint32_t *fb = (uint32_t *)gfx64_get_framebuffer();
        if (!fb) return;
        for (int y = 0; y < h; y++) {
            uint32_t *dst = fb + (uint64_t)y * w;
            const uint32_t *src = g_wp_cache + (uint64_t)y * w;
            for (int x = 0; x < w; x++) dst[x] = src[x];
        }
        return;
    }
    if (g_wp_sel == 3) {
        /* Dusk: 紫→橙渐变天空 + 径向渐变落日 */
        hs_wallpaper_vgradient(0x2B1B3D, 0xE8834A);
        gfx64_fill_circle_gradient(w / 2, h * 3 / 4, hs_s(120), 0xFFD680, 0xE85D4A);
    } else if (g_wp_sel == 4) {
        /* Ocean: 青→蓝渐变 */
        hs_wallpaper_vgradient(0x0B3B4F, 0x123E7C);
    } else {
        gfx64_fill_rect(0, 0, w, h, g_shell.colors.bg);
    }
}

/* matugen 式取色: 采样网格 6x4 (边距 12.5%), 取饱和度最高的像素为源色 */
/* P0: 只把壁纸的某块矩形重铺到当前渲染目标(拖动等局部刷新用) */
static void hs_wallpaper_render_rect(int x, int y, int w, int h)
{
    int sw = g_shell.screen_width, sh = g_shell.screen_height;
    /* 非 PNG 壁纸(渐变/纯色)或缓存尺寸不符时退回整屏: 正确性优先 */
    if (!hs_wallpaper_png() || !g_wp_cache || g_wp_cache_w != sw || g_wp_cache_h != sh) {
        hs_wallpaper_render();
        return;
    }
    uint32_t *fb = (uint32_t *)gfx64_get_framebuffer();
    if (!fb || !w || !h) return;
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > sw ? sw : x + w, y1 = y + h > sh ? sh : y + h;
    for (int j = y0; j < y1; j++) {
        uint32_t *dst = fb + (uint64_t)j * sw + x0;
        const uint32_t *src = g_wp_cache + (uint64_t)j * sw + x0;
        int n = x1 - x0;
        for (int i = 0; i < n; i++) dst[i] = src[i];
    }
}

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
    hpdesk_set_theme(g_shell.theme);  /* 重置默认色板再取色, 避免叠加旋转 */
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
    /* 循环: 图0 -> 图1 -> 图2 -> Dusk -> Ocean -> 纯色 -> 图0
     * 图片加载失败就跳过, 不让用户点到一个空白桌面。 */
    int total = WP_IMG_COUNT + 3;
    for (int step = 1; step <= total; step++) {
        int sel = (g_wp_sel + step) % total;
        if (sel < WP_IMG_COUNT && !hs_wp_load(sel)) continue;
        g_wp_sel = sel;
        break;
    }
    /* 换了图, 整屏缓存必须作废, 下一次全量重绘会重建 */
    if (g_wp_cache) { kfree(g_wp_cache); g_wp_cache = 0; g_wp_cache_w = g_wp_cache_h = 0; }
    if (g_wp_sel >= WP_IMG_COUNT && g_wp_png.rgba) { png64_free(&g_wp_png); g_wp_loaded = -1; }
    g_content_dirty = 1;
    hs_wp_save_cfg(g_wp_sel);
    vga_printf("[WALLPAPER] sel=%d\n", g_wp_sel);
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

void hpdesk_add_widget(hs_panel_pos_t panel, hs_widget_type_t type)
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

void hpdesk_toggle_launcher(void)
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

/* SB-16: 窗口管理器锁。
 * 当前所有窗口操作（创建/置顶/关闭/焦点）都由 BSP 桌面主循环驱动，
 * 并发窗口尚未打开。这把锁为 SB-25 预埋：窗口槽扫描 + 占位、z_order
 * 重排必须原子，否则两个绘制者会拿到同一个槽或让 z 序出现重复 id。
 * 只锁簿记，不锁窗口内容渲染。 */
static spinlock_t g_wm_lock;

static inline uint64_t wm_lock(void) { return spinlock_lock_irqsave(&g_wm_lock); }
static inline void wm_unlock(uint64_t f) { spinlock_unlock_irqrestore(&g_wm_lock, f); }

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
    /* 窗口几何统一到真实屏尺寸(第 1/4 步: 只动 auto_pos)。
     * 旧实现硬编码 1024/768: 窗口宽 1270 时 nx = 1024-1270 = -246, 窗口被甩出屏外,
     * 渲染成一整块压在桌面上的色块。任何 >1024 的屏、任何大体量窗口都会踩这个坑。 */
    int scr_w = g_shell.screen_width > 0 ? g_shell.screen_width : 1024;
    int scr_h = g_shell.screen_height > 0 ? g_shell.screen_height : 768;
    if (*x != -1 && *y != -1) {
        /* 已显式指定坐标: 只做屏内兜底, 不再套 1024 假设 */
        if (*x <= 0) *x = 1;
        if (*x + w > scr_w) *x = scr_w - w;
        if (*y <= 0) *y = 1;
        if (*y + h > scr_h) *y = scr_h - h;
        return;
    }
    /* 默认位置: 居中偏上。左右面板在高缩放下会占掉大部分横向空间, 居中是最不
     * 偏向任何一边的选择(与面板的重叠面积两侧均摊), 级联时再轻微错开。 */
    int idx = g_wm.cascade_index;
    int nx = (scr_w - w) / 2 + (idx % 6) * hs_sx(24);
    int ny = scr_h / 6 + (idx % 6) * hs_sy(20);
    if (nx + w > scr_w) nx = scr_w - w;
    if (nx < 0) nx = 0;
    if (ny + h > scr_h) ny = scr_h - h;
    if (ny < 0) ny = 0;
    *x = nx;
    *y = ny;
    g_wm.cascade_index = (idx + 1) % 6;
}

/* 创建窗口 */
hs_window_t *hs_window_create_inner(hs_window_type_t type, const char *title,
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
    win->surface = 0;
    win->surface_w = win->surface_h = 0;
    win->content_dirty = 1;
    win->hover_rel_x = win->hover_rel_y = -1;
    win->hover_inside = 0;

    /* 复制标题 */
    int i = 0;
    while (title && title[i] && i < 63) {
        win->title[i] = title[i];
        i++;
    }
    win->title[i] = 0;

    /* 窗口几何统一入口(单点钳制 + 可观测)。
     * 各 app 传的是物理像素基准值(520x420 等), 内容渲染也按同一基准画,
     * 所以这里不再二次缩放 —— 只做钳制, 并打印实际尺寸, 避免再出现
     * "窗口尺寸改了但内容不缩放, 看起来像没画满"那类问题。
     * 要真正按 DPI 放大窗口, 必须连同该窗口的内容渲染一起改(见 Settings 的做法)。 */
    {
        int max_w = g_shell.screen_width - hs_sx(40);
        int max_h = g_shell.screen_height - hs_sy(80);
        if (max_w > 0 && w > max_w) w = max_w;
        if (max_h > 0 && h > max_h) h = max_h;
        if (w < hs_sx(200)) w = hs_sx(200);
        if (h < hs_sy(120)) h = hs_sy(120);
    }
    {
        char nb[16];
        vga_puts("[WM] create ");
        vga_puts(title ? title : "?");
        vga_puts(" ");
        hs_itoa(w, nb);
        vga_puts(nb);
        vga_puts("x");
        hs_itoa(h, nb);
        vga_puts(nb);
        vga_puts("\n");
    }

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

/* SB-16 wrapper：槽扫描 + 占位 + z_order 入表在锁内完成 */
hs_window_t *hs_window_create(hs_window_type_t type, const char *title,
                              int x, int y, int w, int h)
{
    uint64_t wf = wm_lock();
    hs_window_t *r = hs_window_create_inner(type, title, x, y, w, h);
    wm_unlock(wf);
    return r;
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
void hs_window_raise_inner(hs_window_t *win)
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

/* SB-16 wrapper：z_order 重排整段持锁（两核同时置顶会产生重复 id） */
void hs_window_raise(hs_window_t *win)
{
    uint64_t wf = wm_lock();
    hs_window_raise_inner(win);
    wm_unlock(wf);
}

void hs_window_lower_inner(hs_window_t *win)
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

/* SB-16 wrapper：z_order 重排整段持锁（两核同时置顶会产生重复 id） */
void hs_window_lower(hs_window_t *win)
{
    uint64_t wf = wm_lock();
    hs_window_lower_inner(win);
    wm_unlock(wf);
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
void hpdesk_tile_focused(int dir)
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

/* V0.57.0: 颜色混合(按百分比向 b 靠拢), 用于悬停底色与投影渐变 */
/* V0.57.0: 百分比文本 —— 旧的固定三位写法("000"/"003"/"009")在侧栏 Info 页上
 * 看着像码表而不是百分比, 改成 "0%"/"3%"/"100%"。 */
static void hs_pct_str(int v, char *out)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    if (v >= 100) { out[0] = '1'; out[1] = '0'; out[2] = '0'; out[3] = '%'; out[4] = 0; return; }
    if (v >= 10)  { out[0] = (char)('0' + v / 10); out[1] = (char)('0' + v % 10); out[2] = '%'; out[3] = 0; return; }
    out[0] = (char)('0' + v); out[1] = '%'; out[2] = 0;
}

static uint32_t hs_mix_color(uint32_t a, uint32_t b, int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    int br = (b >> 16) & 0xFF, bg2 = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = (ar * (100 - pct) + br * pct) / 100;
    int g = (ag * (100 - pct) + bg2 * pct) / 100;
    int bl = (ab * (100 - pct) + bb * pct) / 100;
    return (uint32_t)((r << 16) | (g << 8) | bl);
}

/* V0.59.8: 窗口投影改成真正的 alpha 混合。
 *
 * 之前的路子是"多层不透明色块叠出渐变"：越靠近窗口的色块越深。层数再多也只是骗
 * 眼睛, 层间一定有台阶; 而且基色必须猜(取窗口下方像素), 一旦那点落在别的窗口上
 * 阴影就偏色。0.59.7 把 3 层做到 9 层只是把台阶变细, 问题本身没解决。
 *
 * 现在直接逐像素混黑:
 *   - 只遍历窗口外侧的那一圈(内部跳过, 窗口自己会盖住), 800x640 的窗口约 90k 像素,
 *     和原来 9 次整块圆角填充(每次 50 万像素写)是同一个量级, 甚至更省;
 *   - 每个像素按它到窗口边界的距离算 alpha, 平方衰减 —— 连续, 没有台阶;
 *   - 因为是真的混合, 底下是什么颜色都自然变暗, 不再需要采样基色, 也就不存在偏色;
 *   - 四角用 math64_isqrt 取欧氏距离, 阴影跟着圆角走。
 * 光源假定在上方: 阴影矩形整体下移 6px, 所以上边只露 10px、下边 22px。 */
/* ---------- 阴影贴图 (V0.60.1) ----------
 * 解析距离场只能生成"等距同心环": 角部和边上按同一个速率衰减, 而真阴影的形状是
 * **遮挡体的卷积** —— 窗口的凸角漏光更多, 影子在角上拖得更长。这就是无论怎么调
 * 半径/系数都觉得"假"的数学原因。
 *
 * 所以改成真的做卷积: 把窗口的实心圆角矩形当 mask, 做盒式模糊(中心极限定理: N 遍
 * 盒式模糊逼近高斯), 得到 alpha 图, 再逐像素 blend。
 *
 * 而且按窗口**尺寸**缓存: mask 内容只依赖尺寸, 窗口移动不用重算, 只有改尺寸才重算。
 * 模糊走分离式(水平一趟 + 垂直一趟), 复杂度 O(像素), 不是 gfx64_blur_region 的
 * O(像素 × r²) —— 后者 800x640 + blur10 要 2 亿次像素读, 一次重绘就卡死桌面。
 *
 * 内存: (w+2pad) x (h+2pad) 字节 × 2(工作 + 临时), 900x700 的窗口约 1.6MB, 复用不释放。
 */
#define SH_BLUR_R     24    /* 单遍盒式模糊半径(缩放前); 3 遍 -> std ≈ r*sqrt(3) */
#define SH_PASSES     3
/* V0.59.24 拖动位移拷贝: 记录上一次真正画到屏幕的窗口/阴影位置。
 * 与当前帧同一窗口、同尺寸、同 y、x 只差 dx(<=64) 时, 与上一帧重叠的那一大块
 * 在后台缓冲里已经是对的, 只需刷移动前沿那条 dx 宽窄带。 */
static int g_shd_ok, g_shd_id, g_shd_x, g_shd_y, g_shd_w, g_shd_h;
static int g_wn_ok, g_wn_id, g_wn_x, g_wn_y, g_wn_w, g_wn_h;
static int g_band_blits, g_band_reason;
#define SH_STRENGTH   255   /* 3 遍盒式模糊把边缘削到约 1/4, 这里补回来 */

/* V0.59.24 位移拷贝的安全前提: 拖动窗口不能被别的窗口压住/压到别人。
 * 因为"与上一帧重叠的那一大块已经是对的"这条假设, 只在没有任何其他窗口会在这
 * 块上重画时才成立 —— hs_window_render_all 每帧会把别的窗口也 blend 一遍,
 * 下层窗口的像素会覆盖掉拖动窗口在重叠区的内容, 产生穿透。
 * 这里用最简单的保守判据: 只要有一个可见窗口与拖动窗口(含阴影 pad)相交, 就不走
 * 带路径。窗口数很少, 每帧几次矩形相交判断, 成本可忽略。 */
static void cc_geometry(int *cx, int *cy, int *cw, int *ch);

/* 位移拷贝的安全前提还有一条: 重叠区里"压在拖动窗口之上"的图元必须没变。
 * 面板(顶栏/侧栏/Dock/时钟/岛/天气)是在窗口**之前**绘制的(见 prof5 的 ts3 段
 * 早于 ts4), 所以它们在窗口之下, 不影响重叠区 —— 不必排除。
 * 真正压在窗口之上、且每帧重新 blend 的只有控制中心和通知(都在 ts4 之后画),
 * 它们若与拖动窗口相交, 重叠区就是它们的像素, 窗口移走后本该露出窗口内容却没露。
 * 所以只排除这两块。 */
/* 窗口本体与控制中心/通知卡的相交矩形(并集)。这两块是在窗口**之后**绘制的上层图元,
 * 每帧都会把重叠区盖成它们的样子; 拖动窗口移走后, 那块本该露出窗口内容, 却因为
 * 位移拷贝只刷前沿窄带而没有被重画 —— 所以要单独把这小块补画一次。 */
static int hs_upper_overlap_rect(const hs_window_t *w, int *x0, int *y0, int *x1, int *y1)
{
    int has = 0, ux0 = 0, uy0 = 0, ux1 = 0, uy1 = 0;
    int wx0 = w->x, wy0 = w->y, wx1 = w->x + w->width, wy1 = w->y + w->height;
    if (g_shell.panel_registry[HS_PANEL_CONTROL_CENTER].enabled) {
        int cx = 0, cy = 0, cw = 0, ch = 0;
        cc_geometry(&cx, &cy, &cw, &ch);
        int ix0 = wx0 > cx ? wx0 : cx, iy0 = wy0 > cy ? wy0 : cy;
        int ix1 = wx1 < cx + cw ? wx1 : cx + cw;
        int iy1 = wy1 < cy + ch ? wy1 : cy + ch;
        if (ix1 > ix0 && iy1 > iy0) {
            ux0 = ix0; uy0 = iy0; ux1 = ix1; uy1 = iy1; has = 1;
        }
    }
    {
        int nwd = hs_sx(460), nhd = hs_sy(300);
        int nx = g_shell.screen_width - nwd;
        int ix0 = wx0 > nx ? wx0 : nx, iy0 = wy0 > 0 ? wy0 : 0;
        int ix1 = wx1 < nx + nwd ? wx1 : nx + nwd;
        int iy1 = wy1 < nhd ? wy1 : nhd;
        if (ix1 > ix0 && iy1 > iy0) {
            if (has) {
                if (ix0 < ux0) ux0 = ix0; if (iy0 < uy0) uy0 = iy0;
                if (ix1 > ux1) ux1 = ix1; if (iy1 > uy1) uy1 = iy1;
            } else { ux0 = ix0; uy0 = iy0; ux1 = ix1; uy1 = iy1; }
            has = 1;
        }
    }
    if (!has) return 0;
    *x0 = ux0; *y0 = uy0; *x1 = ux1; *y1 = uy1;
    return 1;
}

static int hs_no_other_window_overlap(const hs_window_t *w, int pad)
{
    for (int i = 0; i < g_wm.window_count; i++) {
        hs_window_t *o = &g_wm.windows[i];
        if (o == w || !o->visible) continue;
        int ax0 = w->x - pad, ay0 = w->y - pad;
        int ax1 = w->x + w->width + pad, ay1 = w->y + w->height + pad;
        int bx0 = o->x, by0 = o->y;
        int bx1 = o->x + o->width, by1 = o->y + o->height;
        if (ax0 < bx1 && bx0 < ax1 && ay0 < by1 && by0 < ay1) return 0;
    }
    return 1;
}

static uint8_t *sh_mask = 0;
static uint8_t *sh_tmp = 0;
static uint32_t *sh_argb = 0;      /* 预渲染的投影 ARGB(黑底+掩膜 alpha), 供批量合成 */
static uint32_t sh_argb_cap = 0;
static uint32_t sh_cap = 0;
static int sh_mw = 0, sh_mh = 0;
static int sh_kw = -1, sh_kh = -1;      /* 缓存 key: 窗口宽高 */
static int sh_kpk = -1;                  /* 缓存 key: 半径/圆角/pad 指纹 */

/* 圆角矩形内点判定(矩形外一律 0) */
static int sh_in_rounded(long px, long py, long r_x0, long r_y0, long r_x1, long r_y1, long rc)
{
    if (px < r_x0 || px > r_x1 || py < r_y0 || py > r_y1) return 0;
    long cx = px, cy = py;
    if (px < r_x0 + rc) cx = r_x0 + rc; else if (px > r_x1 - rc) cx = r_x1 - rc;
    if (py < r_y0 + rc) cy = r_y0 + rc; else if (py > r_y1 - rc) cy = r_y1 - rc;
    long dx = px - cx, dy = py - cy;
    return (dx * dx + dy * dy) <= rc * rc;
}

/* 水平/垂直盒式模糊(分离式, 滑动窗口 O(像素)) */
static void sh_box_h(const uint8_t *src, uint8_t *dst, int w, int h, int r)
{
    for (int y = 0; y < h; y++) {
        const uint8_t *sr = src + (long)y * w;
        uint8_t *dr = dst + (long)y * w;
        long sum = 0, n = 0;
        for (int i = 0; i <= r && i < w; i++) { sum += sr[i]; n++; }
        for (int x = 0; x < w; x++) {
            dr[x] = (uint8_t)(sum / n);
            int add = x + r + 1, sub = x - r;
            if (add < w) { sum += sr[add]; n++; }
            if (sub >= 0) { sum -= sr[sub]; n--; }
        }
    }
}

static void sh_box_v(const uint8_t *src, uint8_t *dst, int w, int h, int r)
{
    for (int x = 0; x < w; x++) {
        long sum = 0, n = 0;
        for (int i = 0; i <= r && i < h; i++) { sum += src[(long)i * w + x]; n++; }
        for (int y = 0; y < h; y++) {
            dst[(long)y * w + x] = (uint8_t)(sum / n);
            int add = y + r + 1, sub = y - r;
            if (add < h) { sum += src[(long)add * w + x]; n++; }
            if (sub >= 0) { sum -= src[(long)sub * w + x]; n--; }
        }
    }
}

/* 生成 mask: 返回 1 成功, 0 表示内存不够(调用方回退到解析式) */
static int sh_build(int ww, int hh, int pad, int rc, int r, int off)
{
    extern void *kmalloc(uint64_t size);
    extern void kfree(void *ptr);

    int mw = ww + 2 * pad, mh = hh + 2 * pad + off;
    if (mw <= 0 || mh <= 0 || mw > 2048 || mh > 1400) return 0;
    uint32_t need = (uint32_t)mw * (uint32_t)mh;
    uint32_t need4 = need * 4;
    if (need > sh_cap) {
        if (sh_mask) kfree(sh_mask);
        if (sh_tmp) kfree(sh_tmp);
        sh_mask = (uint8_t *)kmalloc((uint64_t)need);
        sh_tmp = (uint8_t *)kmalloc((uint64_t)need);
        sh_cap = (sh_mask && sh_tmp) ? need : 0;
        if (!sh_cap) return 0;
    }
    if (need4 > sh_argb_cap) {
        if (sh_argb) kfree(sh_argb);
        sh_argb = (uint32_t *)kmalloc((uint64_t)need4);
        sh_argb_cap = sh_argb ? need4 : 0;
        if (!sh_argb) return 0;
    }
    /* 实心圆角矩形当遮挡体 */
    for (long y = 0; y < mh; y++) {
        uint8_t *row = sh_mask + y * mw;
        for (long x = 0; x < mw; x++) {
            row[x] = sh_in_rounded(x, y, pad, pad + off, pad + ww - 1, pad + off + hh - 1, rc) ? 255 : 0;
        }
    }
    if (r < 1) r = 1;
    for (int pass = 0; pass < SH_PASSES; pass++) {
        sh_box_h(sh_mask, sh_tmp, mw, mh, r);
        sh_box_v(sh_tmp, sh_mask, mw, mh, r);
    }
    /* 预渲染 ARGB: 窗口内部透明(窗口自己会盖住), 环带写黑底 + 掩膜 alpha,
     * 每帧直接走批量 gfx64_blend_image, 不再逐像素 blend_pixel。 */
    for (long y = 0; y < mh; y++) {
        const uint8_t *row = sh_mask + y * mw;
        uint32_t *arow = sh_argb + y * mw;
        for (long x = 0; x < mw; x++) {
            if (sh_in_rounded(x, y, pad, pad + off, pad + ww - 1, pad + off + hh - 1, rc)) {
                arow[x] = 0;
            } else {
                arow[x] = (uint32_t)row[x] << 24;   /* RGB=0 黑, alpha=掩膜 */
            }
        }
    }
    sh_mw = mw; sh_mh = mh; sh_kw = ww; sh_kh = hh;
    return 1;
}

/* 回退: 解析距离场(内存不够时用, 或者 mask 尺寸超限) */
static void hs_window_shadow_analytic(const hs_window_t *w)
{
    extern void gfx64_blend_pixel(int x, int y, uint32_t fg, int alpha);
    extern uint32_t math64_isqrt(uint32_t v);
    extern int gfx64_get_width(void);
    extern int gfx64_get_height(void);
    int d1 = hs_s(16), a1 = 26;
    int d2 = hs_s(44), a2 = 16;
    int dmax = d2;
    int off = hs_sy(14);
    int x0 = w->x, x1 = w->x + w->width - 1;
    int y0 = w->y + off, y1 = w->y + w->height - 1 + off;
    int ybeg = y0 - dmax, xbeg = x0 - dmax;
    if (ybeg < 0) ybeg = 0;
    if (xbeg < 0) xbeg = 0;
    int ylim = (y1 + dmax < gfx64_get_height()) ? y1 + dmax : gfx64_get_height() - 1;
    int xlim = (x1 + dmax < gfx64_get_width()) ? x1 + dmax : gfx64_get_width() - 1;
    for (int y = ybeg; y <= ylim; y++) {
        for (int x = xbeg; x <= xlim; x++) {
            if (x >= x0 && x <= x1 && y >= y0 && y <= y1) continue;
            int dx = 0, dy = 0;
            if (x < x0) dx = x0 - x; else if (x > x1) dx = x - x1;
            if (y < y0) dy = y0 - y; else if (y > y1) dy = y - y1;
            int dist;
            if (dx > 0 && dy > 0) dist = (int)math64_isqrt((uint32_t)(dx * dx + dy * dy));
            else dist = dx ? dx : dy;
            int a = 0;
            if (dist < d1) { long u = d1 - dist; a += (int)(u * u * a1 / ((long)d1 * d1)); }
            if (dist < d2) { long u = d2 - dist; a += (int)(u * u * a2 / ((long)d2 * d2)); }
            if (a > 90) a = 90;
            if (a > 0) gfx64_blend_pixel(x, y, 0x000000, a);
        }
    }
}

/* OBS-03: 窗口本体与投影掩膜必须用同一个圆角半径，否则四个角会错位 1-2px
 * （窗口是尖角、阴影已经削圆，底角看起来"压"在阴影上）。
 * 圆角随窗口短边自适应，公式与投影保持一致。 */
static int hs_window_corner_radius(const hs_window_t *w)
{
    int mn = w->width < w->height ? w->width : w->height;
    int rc = hs_s(g_shell.ui.corner_radius);
    if (rc < 4) rc = 16;
    if (rc > mn / 8) rc = mn / 8;
    if (rc < 5) rc = 5;
    return rc;
}

/* OBS-03: 窗口圆角的统一几何 —— 圆心在 (x+r, y+r)，判定 d2 <= r^2，
 * 与投影掩膜 sh_in_rounded() 完全同一套。逐行/逐列算"圆内最外侧像素"的
 * 水平内缩量，填充与描边共用这张表，所以：
 *   · 窗口背景与边框是同一条曲线（不会露壁纸缝）
 *   · 边框 1px 线逐行连续（不会像 SDF 环带那样在 45 度处断成缺口，
 *     也不会与直边重叠出 2px 粗块）
 *   · 与阴影掩膜的曲线也一致（内缩量逐像素相同）
 * isqrt 只在半径变化时算一遍，建成查表。 */
static int g_inset_r = -1;
static int g_inset_tbl[260];

static int hs_round_inset(int dy, int r)
{
    extern uint32_t math64_isqrt(uint32_t v);
    if (r > 255) r = 255;
    if (dy < 0) dy = -dy;
    if (dy >= r) return 0;
    if (g_inset_r != r) {
        for (int i = 0; i <= r; i++) {
            int v = r * r - i * i;
            if (v < 0) v = 0;
            g_inset_tbl[i] = r - (int)math64_isqrt((uint32_t)v);
        }
        g_inset_r = r;
    }
    return g_inset_tbl[dy];
}

/* 圆角矩形 1px 描边 —— 画"圆内最外侧"那一圈像素 */
static void hs_stroke_round(int x, int y, int w, int h, int r, uint32_t color)
{
    extern void gfx64_put_pixel(int x, int y, uint32_t color);
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r <= 0) {
        extern void gfx64_draw_rect(int x, int y, int w, int h, uint32_t color);
        gfx64_draw_rect(x, y, w, h, color);
        return;
    }
    for (int yy = 0; yy < h; yy++) {
        int dy = 0;
        if (yy < r)              dy = r - yy;
        else if (yy > h - 1 - r) dy = yy - (h - 1 - r);
        int inset = dy ? hs_round_inset(dy, r) : 0;
        if (w - 2 * inset <= 0) continue;
        gfx64_put_pixel(x + inset, y + yy, color);
        gfx64_put_pixel(x + w - 1 - inset, y + yy, color);
    }
    /* 上下两条直边只画中段：圆角区（x < r 或 x > w-1-r）若也按 inset 画，
     * 得到的是从角点出发的 45 度斜线（因为那里边界是圆弧，不是水平线），
     * 会在角外拖出一条斜痕。圆角区的像素已经由上面的逐行左右边界覆盖完了。 */
    for (int xx = r; xx <= w - 1 - r; xx++) {
        gfx64_put_pixel(x + xx, y, color);
        gfx64_put_pixel(x + xx, y + h - 1, color);
    }
}

static inline uint64_t hs_rdtsc(void);
static uint64_t g_cyc_shadow, g_cyc_shadow_build, g_cyc_shadow_blit, g_cyc_title, g_cyc_border;

static void hs_window_shadow(const hs_window_t *w)
{
    if (w->width <= 0 || w->height <= 0) return;
    if (!g_shell.ui.window_shadow) return;   /* Display 页可关闭 */

    /* 注意: hs_s() 依赖 g_ui_scale_pct, 而 Settings/任务管理器窗口在 hpdesk_init
     * 里就建好了, 那时缩放还没从配置读出来 —— 早先版本因此把半径算成 1px, mask 糊
     * 成一团废图, 又按尺寸命中了缓存, 于是一直是废的。这里做保底, 并把半径/圆角/
     * pad 一起纳入缓存指纹, 缩放变化后会重建。 */
    /* 随窗口尺寸自适应: 圆角/模糊半径/光源偏移都按短边比例取, 再夹到区间内。
     * 小窗口用 24px 半径会糊成一团, 大窗口用 12px 又干瘪 —— 定值都不合适。 */
    int mn = w->width < w->height ? w->width : w->height;
    int rc = hs_window_corner_radius(w);
    int r = mn / 18;                 /* 短边 1/18 左右的模糊 */
    if (r < 10) r = 10;
    if (r > 40) r = 40;
    if (r > mn / 4) r = mn / 4;      /* 极小窗口别让阴影比窗口还大 */
    int pad = 3 * r + 6;             /* 模糊支撑 3r, 必须 >= 它, 否则边带会切出硬边 */
    /* 光源偏移只做"轻微下移"：off 是遮挡体在 mask 里的下移量，而"窗口内部"一律按
     * 遮挡体判定，所以 off 越大、上/左/右三边被吃掉的就越多 —— 取 rc/2+4(最大 12)
     * 会让三边阴影整圈消失，只剩下方远处一圈，看起来就是"阴影没补上"。
     * 保持 3px 的量，四边都有近场暗部，下方再多一点点即可。 */
    int off = 3;
    int pk = ((rc * 4096 + pad) * 4096 + r) * 512 + off;

    uint64_t sb0 = hs_rdtsc();
    if (sh_kw != w->width || sh_kh != w->height || sh_kpk != pk) {
        if (!sh_build(w->width, w->height, pad, rc, r, off)) {
            hs_window_shadow_analytic(w);   /* 内存不够, 退回解析式 */
            return;
        }
    }
    uint64_t sb1 = hs_rdtsc();
    g_cyc_shadow_build += sb1 - sb0;
    g_cyc_shadow += sb1 - sb0;
    int mx0 = w->x - pad;
    /* 阴影源在 mask 里下移了 off, 但 blit 原点必须对齐真实窗口, 否则窗口正下方
     * 那 off 一段既没阴影、又被当成"窗口内部"跳过, 会出现一条空缝。 */
    int my0 = w->y - pad;

    sh_kpk = pk;
    uint64_t sb2 = hs_rdtsc();
    /* V0.59.24 位移拷贝: 拖动中同一窗口只横向移动且位移 <=64 时, 只刷移动前沿
     * 那一条新出现的窄带 —— 阴影整体平移 dx, 与上一帧重叠的部分已经是对的。
     * 阴影缓冲面积是 (w+2pad)*(h+2pad)(Settings 约 836x811), 而窄带只有
     * dx*(h+2pad)(dx=10 时约 8K), 差两个数量级。 */
    int s_band = 0, s_dx = 0;
    /* 诊断: 哪个条件把位移拷贝挡掉了(1=没在拖 2=无基线 3=换窗口 4=尺寸变
     * 5=y 变了 6=与其它窗口/上层图元相交 7=没有位移 8=位移过大 9=通过) */
    int s_reason = 9;
    if (!g_dragging) s_reason = 1;
    else if (!g_shd_ok) s_reason = 2;
    else if (g_shd_id != w->id) s_reason = 3;
    else if (g_shd_w != w->width || g_shd_h != w->height) s_reason = 4;
    else if (g_shd_y != w->y) s_reason = 5;
    else if (!hs_no_other_window_overlap(w, pad)) s_reason = 6;
    else {
        int d = w->x - g_shd_x;
        int ad = d < 0 ? -d : d;
        if (ad == 0) s_reason = 7;
        else if (!(ad <= 64 && ad <= sh_mw)) s_reason = 8;
        else { s_band = 1; s_dx = d; }
    }
    g_band_reason = s_reason;
    if (s_band) {
        if (s_dx > 0)
            gfx64_blend_image_sub(sh_argb, sh_mw, sh_mh, sh_mw - s_dx, 0,
                                  s_dx, sh_mh, mx0 + sh_mw - s_dx, my0, 255);
        else
            gfx64_blend_image_sub(sh_argb, sh_mw, sh_mh, 0, 0,
                                  -s_dx, sh_mh, mx0, my0, 255);
        g_band_blits++;
    } else {
        gfx64_blend_image(sh_argb, sh_mw, sh_mh, mx0, my0, 255);
    }
    g_shd_ok = 1; g_shd_id = w->id; g_shd_x = w->x; g_shd_y = w->y;
    g_shd_w = w->width; g_shd_h = w->height;
    uint64_t sb3 = hs_rdtsc();
    g_cyc_shadow_blit += sb3 - sb2;
    g_cyc_shadow += sb3 - sb2;
}

/* ===== P0 脏区框架 (V0.59.5) =====
 * 拖动这类局部变化不该重铺整屏壁纸(每帧 8MB 拷贝)也不该整帧提交。
 * 这里累积一个脏矩形(并集), 局部路径只重铺这块壁纸、重画可能压在上面的面板与窗口,
 * present 也只提交这块。单矩形并集对"拖一个窗口"足够(两块本来就相邻)。 */
static int g_dirty_x0 = 0, g_dirty_y0 = 0, g_dirty_x1 = -1, g_dirty_y1 = -1;
static uint32_t g_dirty_frames = 0;

static void hs_dirty_add(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    if (g_dirty_x1 < 0) { g_dirty_x0 = x; g_dirty_y0 = y; g_dirty_x1 = x + w; g_dirty_y1 = y + h; return; }
    if (x < g_dirty_x0) g_dirty_x0 = x;
    if (y < g_dirty_y0) g_dirty_y0 = y;
    if (x + w > g_dirty_x1) g_dirty_x1 = x + w;
    if (y + h > g_dirty_y1) g_dirty_y1 = y + h;
}

static int hs_dirty_rect(int *x, int *y, int *w, int *h)
{
    if (g_dirty_x1 < 0) return 0;
    int x0 = g_dirty_x0 < 0 ? 0 : g_dirty_x0;
    int y0 = g_dirty_y0 < 0 ? 0 : g_dirty_y0;
    int x1 = g_dirty_x1 > g_shell.screen_width ? g_shell.screen_width : g_dirty_x1;
    int y1 = g_dirty_y1 > g_shell.screen_height ? g_shell.screen_height : g_dirty_y1;
    if (x1 <= x0 || y1 <= y0) return 0;
    *x = x0; *y = y0; *w = x1 - x0; *h = y1 - y0;
    return 1;
}

static void hs_dirty_reset(void) { g_dirty_x1 = -1; }

/* 该矩形是否与脏区相交; 脏区为空时返回 1(保守: 全画) */
static int hs_dirty_hits(int x, int y, int w, int h)
{
    if (g_dirty_x1 < 0) return 1;
    if (x + w <= g_dirty_x0 || x >= g_dirty_x1) return 0;
    if (y + h <= g_dirty_y0 || y >= g_dirty_y1) return 0;
    return 1;
}

/* 窗口 surface(V0.59.11): 每个窗口一块常驻 ARGB 缓冲(0xAARRGGBB)。
 * 内容先画进这块, 再由合成器 gfx64_blend_image 贴到屏幕。圆角外是"真透明"
 * (alpha=0), 合成时透出壁纸 —— 不再是"贴一块背景补角"。
 * 分配失败返回 0, 调用方退回直接画屏幕的老路径。 */
extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);
extern void gfx64_target_begin(void *buf, int w, int h, int ox, int oy);
extern void gfx64_target_end(void);
extern void gfx64_fill_round_rect_argb(void *buf, int bw, int bh, int x, int y, int w, int h,
                                        int r, uint32_t color, int corners);
extern void gfx64_blend_image(void *src, int sw, int sh, int dx, int dy, int opacity);
extern void gfx64_blend_image_sub(void *src, int sw, int sh, int sx, int sy,
                              int w, int h, int dx, int dy, int opacity);
extern void gfx64_clip_set(int x, int y, int w, int h);
extern void gfx64_clip_disable(void);

static int g_surf_reuse, g_surf_repaint;
/* V0.59.17: 当前帧是否走局部脏区路径(面板/控制中心/通知据此决定是否按矩形跳过) */
static int g_in_partial_frame;
/* V0.59.23 诊断: 拖动期间走了多少次局部帧/全量帧, 面板被跳过多少次。
 * 面板段那 21.6% 到底是"每帧全量重画"还是"整块 blend", 由这两个计数决定。 */
static int g_partial_frames, g_full_frames, g_panel_skips;
static int g_last_dirty[4];
static uint64_t g_t_drag_start;
/* V0.59.13 分段计时: rdtsc 周期数, 定位瓶颈用 */
static inline uint64_t hs_rdtsc(void)
{
    uint32_t a, d;
    __asm__ volatile("rdtsc" : "=a"(a), "=d"(d));
    return ((uint64_t)d << 32) | a;
}
static uint64_t g_cyc_render_all, g_cyc_blend, g_cyc_panel;
/* V0.59.16 二级切分: 把 render_all 与 other 各自再拆一层 */
static uint64_t g_cyc_wall, g_cyc_panels, g_cyc_cc, g_cyc_notify, g_cyc_cursor;
static uint64_t g_cyc_memset, g_cyc_content;
static uint64_t g_cyc_bgfill, g_cyc_surfens, g_cyc_cursordraw, g_cyc_ctxmenu, g_cyc_loophdr;
static uint64_t g_seg_pre, g_seg_mid, g_seg_post;
static uint64_t g_seg_lp, g_seg_lm, g_seg_lq;
/* 拖动帧各阶段时间戳(相对本帧入口) */
static uint64_t g_ts1, g_ts2, g_ts3, g_ts4, g_ts5, g_ts6;
static int g_prof_frames;
extern uint64_t gfx64_prof_present;

static void *hs_window_surface_ensure(hs_window_t *w)
{
    int ww = w->width, hh = w->height;
    if (ww <= 0 || hh <= 0) return 0;
    if (w->surface && w->surface_w == ww && w->surface_h == hh) return w->surface;
    if (w->surface) { kfree(w->surface); w->surface = 0; }
    void *p = kmalloc((uint64_t)ww * hh * 4);
    if (!p) { w->surface_w = w->surface_h = 0; return 0; }
    memset(p, 0, (size_t)ww * hh * 4);
    w->surface = p;
    w->surface_w = ww;
    w->surface_h = hh;
    return p;
}

void hs_window_render_all(void)
{
    uint64_t __ra0 = hs_rdtsc();
    /* 按z_order从底到顶渲染 */
    for (int zi = 0; zi < g_wm.z_count; zi++) {
        int id = g_wm.z_order[zi];
        if (id < 0 || id >= g_wm.window_count) continue;
        hs_window_t *w = &g_wm.windows[id];
        if (!w->visible || w->workspace != g_shell.current_workspace) continue;

        /* 面板窗口不渲染（由面板系统渲染） */
        if (w->type == HS_WINDOW_PANEL) continue;

        /* P0 脏区: 局部刷新时, 与脏区不相交的窗口整窗跳过(含文字栅格, 比绘图更贵)。
         * 外扩 hs_s(96) 覆盖投影(3*R+6, R=24)与圆角外扩, 宁可多画不漏画。 */
        if (g_dirty_x1 >= 0) {
            int pad = hs_s(96);
            if (!hs_dirty_hits(w->x - pad, w->y - pad, w->width + pad * 2, w->height + pad * 2))
                continue;
        }

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

        /* V0.57.0: 投影(仅浮动窗口, 最大化/面板不画) */
        if (w->type == HS_WINDOW_FLOATING && w->state != HS_WINDOW_STATE_MAXIMIZED) {
            hs_window_shadow(w);
        }

        /* 窗口背景 */
        /* OBS-03: 浮动窗口走圆角，半径与投影掩膜完全一致；最大化窗口与面板
           保持直角（贴边圆角反而突兀）。
           边框用「外圆角填边框色 + 内缩 1px 圆角填背景色」两笔成形：两笔走
           同一条矢量圆角路径，半径只差 1，所以弧段与直边粗细一致、首尾闭合，
           不会出现「直边粗、弧段细」或「角上露壁纸缝」。 */
        int wrc = 0;
        if (w->type == HS_WINDOW_FLOATING && w->state != HS_WINDOW_STATE_MAXIMIZED)
            wrc = hs_window_corner_radius(w);
        /* V0.59.11: 内容画进窗口自己的 ARGB surface, 帧末再合成到屏幕。
         * 底色用"带 alpha 的圆角"写入 —— 圆角外必须真透明, 合成时才透出壁纸。 */
        /* V0.59.12: 鼠标相对窗口的位置变了才可能改变窗口外观(hover 高亮),
         * 绝对位置变化不算 —— 拖动时两者相同, 这类帧可以直接复用 surface。 */
        {
            extern int hs_window_is_dragging(void);
            int rx = g_shell.mouse_x - w->x, ry = g_shell.mouse_y - w->y;
            int inside = (rx >= 0 && rx < w->width && ry >= 0 && ry < w->height);
            /* V0.59.21: 拖动由标题栏发起, 鼠标相对窗口的偏移不变、外观不变; 窗口 x
             * 每帧 +10 会让 rx 每帧都变, 若照常置脏会击穿 surface 复用。拖动期间
             * 不因 hover_rel/inside 变化置脏。 */
            if (!hs_window_is_dragging()) {
                if (rx != w->hover_rel_x || ry != w->hover_rel_y) w->content_dirty = 1;
                if (inside != w->hover_inside) w->content_dirty = 1;
            }
            w->hover_rel_x = rx; w->hover_rel_y = ry; w->hover_inside = inside;
        }

        uint64_t se0 = hs_rdtsc();
        void *tsurf = hs_window_surface_ensure(w);
        g_cyc_surfens += hs_rdtsc() - se0;
        if (tsurf && !w->content_dirty) {
            /* 内容没变: 直接贴上一帧的 surface, 跳过 memset + 全部绘制 */
            { uint64_t b0 = hs_rdtsc();
              /* V0.59.24 位移拷贝: 拖动中横向连续移动时, 窗口内容整体平移,
               * 与上一帧重叠的部分已经在后台缓冲里, 只需刷前沿那条 dx 宽窄带
               * (宽度 dx 而非整个 w, 高度 h 不变)。 */
              int w_band = 0, w_dx = 0;
              if (g_dragging && g_wn_ok && g_wn_id == w->id &&
                  g_wn_w == w->width && g_wn_h == w->height && g_wn_y == w->y &&
                  hs_no_other_window_overlap(w, 0)) {
                  int d = w->x - g_wn_x;
                  int ad = d < 0 ? -d : d;
                  if (ad > 0 && ad <= 64 && ad <= w->width) { w_band = 1; w_dx = d; }
              }
              if (w_band) {
                  if (w_dx > 0)
                      gfx64_blend_image_sub(tsurf, w->width, w->height,
                                            w->width - w_dx, 0, w_dx, w->height,
                                            w->x + w->width - w_dx, w->y, 255);
                  else
                      gfx64_blend_image_sub(tsurf, w->width, w->height,
                                            0, 0, -w_dx, w->height,
                                            w->x, w->y, 255);
                  g_band_blits++;
                  /* 补画: 窗口本体与上层图元(控制中心/通知卡)相交的那一小块。
                   * 那块在本帧之前显示的是上层图元, 窗口移走后若已离开图元范围, 就该
                   * 露出窗口内容; 只刷前沿条的话这里会缺一块。交集通常只有几十像素宽,
                   * 比整块 445K 像素便宜得多。 */
                  int ox0 = 0, oy0 = 0, ox1 = 0, oy1 = 0;
                  if (hs_upper_overlap_rect(w, &ox0, &oy0, &ox1, &oy1)) {
                      int rx0 = w->x > g_wn_x ? w->x : g_wn_x;
                      int rx1 = (w->x + w->width) < (g_wn_x + w->width)
                                ? (w->x + w->width) : (g_wn_x + w->width);
                      int bx0 = ox0 > rx0 ? ox0 : rx0;
                      int bx1 = ox1 < rx1 ? ox1 : rx1;
                      if (bx1 > bx0) {
                          int by0 = oy0 > w->y ? oy0 : w->y;
                          int by1 = oy1 < (w->y + w->height) ? oy1 : (w->y + w->height);
                          if (by1 > by0)
                              gfx64_blend_image_sub(tsurf, w->width, w->height,
                                                    bx0 - w->x, by0 - w->y,
                                                    bx1 - bx0, by1 - by0,
                                                    bx0, by0, 255);
                      }
                  }
              } else {
                  gfx64_blend_image(tsurf, w->width, w->height, w->x, w->y, 255);
              }
              g_wn_ok = 1; g_wn_id = w->id; g_wn_x = w->x; g_wn_y = w->y;
              g_wn_w = w->width; g_wn_h = w->height;
              g_cyc_blend += hs_rdtsc() - b0; }
            g_surf_reuse++;
            continue;
        }
        if (tsurf) {
            { uint64_t m0 = hs_rdtsc();
              memset(tsurf, 0, (size_t)w->width * w->height * 4);
              g_cyc_memset += hs_rdtsc() - m0; }
            { uint64_t b0 = hs_rdtsc();
              gfx64_fill_round_rect_argb(tsurf, w->width, w->height, 0, 0,
                                       w->width, w->height, wrc, w->bg_color, 15);
              g_cyc_bgfill += hs_rdtsc() - b0; }
            gfx64_target_begin(tsurf, w->width, w->height, w->x, w->y);
        } else {
            if (wrc > 0) gfx64_fill_round_rect_aa(w->x, w->y, w->width, w->height, wrc, w->bg_color);
            else gfx64_fill_rect(w->x, w->y, w->width, w->height, w->bg_color);
        }

        /* 标题栏（浮动窗口） */
        uint64_t tt0 = hs_rdtsc();
        if (w->type == HS_WINDOW_FLOATING) {
            /* 标题栏高度跟随 DPI: 内容已按窗口尺寸放大, 标题栏若留 24 物理像素,
             * 125% 下相对就变窄, 标题与按钮会挤到一起。 */
            int th = hs_sy(24);
            /* OBS-03: 标题栏顶部两角跟随窗口圆角，底部两角保持直角（贴内容区） */
            if (wrc > 0) {
                int trc = wrc < th ? wrc : th;
                if (tsurf) {
                    /* 只圆顶部两角(1|2=3), 底部贴内容区保持直角 */
                    gfx64_fill_round_rect_argb(tsurf, w->width, w->height, 0, 0,
                                               w->width, th, trc,
                                               g_shell.colors.titlebar_bg, 3);
                } else {
                    gfx64_fill_round_rect_aa(w->x, w->y, w->width, th, trc,
                                             g_shell.colors.titlebar_bg);
                    if (th > trc)
                        gfx64_fill_rect(w->x, w->y + trc, w->width, th - trc,
                                        g_shell.colors.titlebar_bg);
                }
            } else {
                gfx64_fill_rect(w->x, w->y, w->width, th,
                                g_shell.colors.titlebar_bg);
            }
            /* V0.55: 标题栏前景跟随主题, 深色主题下不再不可见 */
            uint32_t tb_fg = g_shell.colors.window_fg;
            int by = w->y + hs_sy(5);
            int bw24 = hs_sx(24), bh20 = hs_sy(20);
            int cmin = w->x + w->width - hs_sx(68);
            int cmax = w->x + w->width - hs_sx(44);
            int cclo = w->x + w->width - hs_sx(20);
            int mx = g_shell.mouse_x, my = g_shell.mouse_y;
            int hov_min = (my >= w->y && my < w->y + th &&
                           mx >= cmin && mx < cmin + bw24);
            int hov_max = (my >= w->y && my < w->y + th &&
                           mx >= cmax && mx < cmax + bw24);
            int hov_clo = (my >= w->y && my < w->y + th &&
                           mx >= cclo && mx < cclo + bw24);
            /* 悬停底色(关闭键红色) */
            if (hov_min)
                gfx64_fill_rounded_rect(cmin, w->y + hs_sy(2), bw24, bh20, hs_sx(4),
                                        g_shell.colors.panel_bg);
            if (hov_max)
                gfx64_fill_rounded_rect(cmax, w->y + hs_sy(2), bw24, bh20, hs_sx(4),
                                        g_shell.colors.panel_bg);
            if (hov_clo)
                gfx64_fill_rounded_rect(cclo, w->y + hs_sy(2), bw24, bh20, hs_sx(4), 0xE5484D);
            uint32_t btnc = hov_clo ? 0xFFFFFF : tb_fg;
            /* 最小化: 水平粗线 */
            gfx64_fill_rect(cmin + hs_sx(6), by + hs_sy(6), hs_sx(11), hs_sy(2), btnc);
            /* 最大化: 方框; 已最大化时画双层表示还原 */
            gfx64_draw_rect(cmax + hs_sx(6), by + hs_sy(2), hs_sx(10), hs_sy(10), btnc);
            if (w->state == HS_WINDOW_STATE_MAXIMIZED) {
                gfx64_draw_rect(cmax + hs_sx(8), by + hs_sy(4), hs_sx(10), hs_sy(10), btnc);
                gfx64_draw_rect(cmax + hs_sx(10), by + hs_sy(6), hs_sx(6), hs_sy(6), btnc);
            }
            /* 关闭: ×(硬边对角线, 逐像素) */
            {
                int xsz = hs_sx(10);
                for (int i = 0; i <= xsz; i++) {
                    gfx64_put_pixel(cclo + hs_sx(4) + i, by + hs_sy(2) + i, btnc);
                    gfx64_put_pixel(cclo + hs_sx(14) - i, by + hs_sy(2) + i, btnc);
                }
            }
        }
        g_cyc_title += hs_rdtsc() - tt0;

        /* 边框 */
        { uint64_t bd0 = hs_rdtsc();
        uint32_t border = w->focus ?
                          g_shell.colors.accent : w->border_color;
        if (wrc > 0)
            hs_stroke_round(w->x, w->y, w->width, w->height, wrc, border);
        else
            gfx64_draw_rect(w->x, w->y, w->width, w->height, border);
        g_cyc_border += hs_rdtsc() - bd0; }

        /* 标题（跟随主题前景色） */
        uint64_t tt2 = hs_rdtsc();
        if (w->type != HS_WINDOW_POPUP || w->title[0]) {
            void *fb = gfx64_get_framebuffer();
            uint32_t tc = g_shell.colors.window_fg;
            int tx = w->x + 8;
            /* 浮动窗口标题栏左侧应用图标(单色矢量) */
            if (w->type == HS_WINDOW_FLOATING) {
                if (strcmp(w->title, "Terminal") == 0)
                    hs_icon_terminal(w->x + hs_sx(5), w->y + hs_sy(4), hs_s(16), tc);
                else if (strcmp(w->title, "Files") == 0)
                    hs_icon_folder(w->x + hs_sx(5), w->y + hs_sy(4), hs_s(16), tc);
                else if (strcmp(w->title, "Task Manager") == 0)
                    hs_icon_task(w->x + hs_sx(5), w->y + hs_sy(4), hs_s(16), tc);
                else if (strcmp(w->title, "Settings") == 0)
                    hs_icon_settings(w->x + hs_sx(5), w->y + hs_sy(4), hs_s(16), tc);
                else if (strcmp(w->title, "Device Manager") == 0)
                    hs_icon_info(w->x + hs_sx(5), w->y + hs_sy(4), hs_s(16), tc);
                else if (strcmp(w->title, "Disk Manager") == 0)
                    hs_icon_store(w->x + hs_sx(5), w->y + hs_sy(4), hs_s(16), tc);
                else if (strcmp(w->title, "Log Viewer") == 0)
                    hs_icon_doc(w->x + hs_sx(5), w->y + hs_sy(4), hs_s(16), tc);
                else if (strcmp(w->title, "Calculator") == 0)
                    hs_icon_calc(w->x + hs_sx(5), w->y + hs_sy(4), hs_s(16), tc);
                else if (strcmp(w->title, "Editor") == 0)
                    hs_icon_editor(w->x + hs_sx(5), w->y + hs_sy(4), hs_s(16), tc);
                else if (strcmp(w->title, "Browser") == 0)
                    hs_icon_browser(w->x + hs_sx(5), w->y + hs_sy(4), hs_s(16), tc);
                else if (strcmp(w->title, "App Store") == 0)
                    hs_icon_store(w->x + hs_sx(5), w->y + hs_sy(4), hs_s(16), tc);
                else if (strcmp(w->title, "About") == 0)
                    hs_icon_info(w->x + hs_sx(5), w->y + hs_sy(4), hs_s(16), tc);
                tx = w->x + hs_sx(26);
            }
            /* 标题过长时截断, 避免压住右侧窗口按钮 */
            char tbuf[40];
            int maxc = (w->width - hs_sx(150)) / hs_s(7);   /* 7 = 半角字符宽 */
            if (maxc < 4) maxc = 4;                          /* 4 = 字符列下限 */
            int n = 0;
            while (w->title[n] && n < maxc && n < 39) { tbuf[n] = w->title[n]; n++; }
            if (w->title[n] && n > 1) { tbuf[n - 1] = '.'; tbuf[n] = '.'; n++; }
            tbuf[n] = 0;
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               tx, w->y + hs_sy(6), tbuf,
                               tc, hs_s(12));
        }
        g_cyc_title += hs_rdtsc() - tt2;

        /* 自定义渲染回调 */
        if (w->render) {
            /* V0.59.8: 窗口内容统一裁剪到窗口矩形。终端/列表这类"内容比窗口高"的
             * 界面以前会画到窗口外(覆盖下面的壁纸/别的窗口), 现在被裁在这里。 */
            gfx64_clip_set(w->x, w->y, w->width, w->height);
            { uint64_t c0 = hs_rdtsc();
              w->render(w);
              g_cyc_content += hs_rdtsc() - c0; }
            gfx64_clip_disable();
        }

        /* 合成: surface 贴到屏幕。整体 opacity 255, 透明度由每像素 alpha 决定 */
        if (tsurf) {
            gfx64_target_end();
            { uint64_t b0 = hs_rdtsc();
              gfx64_blend_image(tsurf, w->width, w->height, w->x, w->y, 255);
              /* 刚重画了整块 surface, 这一帧是整块贴上去的, 记录位置供下一帧做带 */
              g_wn_ok = 1; g_wn_id = w->id; g_wn_x = w->x; g_wn_y = w->y;
              g_wn_w = w->width; g_wn_h = w->height;
              g_cyc_blend += hs_rdtsc() - b0; }
            w->content_dirty = 0;
            g_surf_repaint++;
        }

        /* 还原动画临时偏移, 保证命中测试用持久坐标 */
        w->x = save_x;
        w->y = save_y;
    }
    g_cyc_render_all += hs_rdtsc() - __ra0;
}

/* 设置窗口渲染回调 */
/* 设置窗口内容按窗口实际宽度等比缩放(基准 460x430)。
 * 渲染与命中测试必须共用这一套公式, 否则点击会落空。 */
static int hs_set_k(hs_window_t *w, int v) { return hs_win_k(w, 460, v); }
#define SETK(v) hs_set_k(w, (v))

/* V0.59.6 试点: 左侧导航改用布局容器(hs_ui)。
 * 原来 5 个按钮的位置是 cy + i * SETK(32), 间距 32 这个常数直接写在循环里;
 * 现在容器管排列与间距, paint 回调只拿到"已经算好的矩形", 自己不再加偏移。
 * 视觉结果与旧实现逐像素一致(28 + gap 4 = 32), 但改间距只需改一处。
 * 事件命中仍走原逻辑(g_settings_page 由 hpdesk 输入处理设置), 本步不动。 */
static hs_window_t *g_nav_win = 0;
static hs_ui_box_t *g_nav_root = 0;
static hs_ui_box_t *g_nav_items = 0;
static int g_nav_count = 0;

static void hs_settings_nav_paint(hs_ui_box_t *b);
static void hs_settings_nav_ensure(int count);
static void hs_settings_nav_layout(int cx, int cy, int w_px, int item_h, int gap);

static hs_ui_box_t g_nav_root_storage;
static hs_ui_box_t g_nav_item_storage[8];
static int g_nav_ready = 0;

static void hs_settings_nav_paint(hs_ui_box_t *b)
{
    hs_window_t *w = g_nav_win;    /* SETK 宏需要名为 w 的局部变量 */
    void *fb = gfx64_get_framebuffer();
    static const char *menu[5] = {"Appearance", "Panels", "System", "About", "Display"};
    int page = b->tag;
    if (page < 0 || page > 4) return;
    uint32_t bg = (page == g_settings_page) ? g_shell.colors.accent : g_shell.colors.panel_bg;
    uint32_t fg = (page == g_settings_page) ? hs_on_accent() : g_shell.colors.panel_fg;
    gfx64_fill_rounded_rect(b->x, b->y, b->w, b->h, SETK(6), bg);
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       b->x + SETK(10), b->y + SETK(7), menu[page], fg, SETK(13));
}

static void hs_settings_nav_ensure(int count)
{
    if (count > 8) count = 8;
    hs_ui_init_box(&g_nav_root_storage, HS_UI_COL, 0, 0);
    for (int i = 0; i < count; i++) {
        hs_ui_init_box(&g_nav_item_storage[i], HS_UI_COL, 0, 0);
        g_nav_item_storage[i].tag = i;
        g_nav_item_storage[i].paint = hs_settings_nav_paint;
        hs_ui_add(&g_nav_root_storage, &g_nav_item_storage[i]);
    }
    g_nav_root = &g_nav_root_storage;
    g_nav_items = g_nav_item_storage;
    g_nav_count = count;
    g_nav_ready = 1;
}

static void hs_settings_nav_layout(int cx, int cy, int w_px, int item_h, int gap)
{
    if (!g_nav_root || g_nav_count <= 0) return;
    g_nav_root->gap = gap;
    for (int i = 0; i < g_nav_count; i++) g_nav_items[i].fixed_h = item_h;
    hs_ui_layout(g_nav_root, cx, cy, w_px,
                 g_nav_count * item_h + (g_nav_count - 1) * gap);
}

static void hs_settings_window_render(hs_window_t *w)
{
    void *fb = gfx64_get_framebuffer();
    int cx = w->x + SETK(16);
    int cy = w->y + SETK(36);  /* 跳过标题栏 */

    /* 左侧菜单 —— 由布局容器排列(hs_ui), 这里只给根矩形与尺寸参数 */
    g_nav_win = w;
    hs_settings_nav_ensure(5);
    hs_settings_nav_layout(cx, cy, SETK(120), SETK(28), SETK(4));

    /* 右侧内容 */
    int rx = cx + SETK(140);
    int ry = cy;

    if (g_settings_page == 0) {
        /* Appearance */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Appearance", g_shell.colors.panel_fg, SETK(16));
        ry += SETK(30);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Theme: Light / Dark", g_shell.colors.panel_fg, SETK(12));
        ry += SETK(24);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Accent Color (click to apply):", g_shell.colors.panel_fg, SETK(12));
        ry += SETK(22);
        /* 8 个预设色板圆点，点击即应用 */
        for (int i = 0; i < HS_THEME_PRESET_COUNT; i++) {
            int sx = rx + i * SETK(28);
            int sy = ry;
            gfx64_fill_circle(sx + SETK(10), sy + SETK(10), SETK(10), g_theme_presets[i]);
            /* 当前生效色描边 */
            if (g_theme_presets[i] == g_shell.theme_source) {
                gfx64_draw_rounded_rect(sx - SETK(2), sy - SETK(2), SETK(24), SETK(24), SETK(12), g_shell.colors.panel_fg);
            }
        }
        ry += SETK(28);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Font: DejaVu Sans", g_shell.colors.panel_fg, SETK(12));
        ry += SETK(24);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Clock Size: 40px", g_shell.colors.panel_fg, SETK(12));
        ry += SETK(24);
        /* V0.56: 壁纸切换行 (点击循环 Classic/Dusk/Ocean) */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Wallpaper:", g_shell.colors.panel_fg, SETK(12));
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx + SETK(84), ry, hs_wp_sel_name(),
                           g_shell.colors.accent, SETK(12));
        /* V0.55.0: 桌面背景色 3x3 色板 (点击应用并写回 config.hsqml) */
        ry += SETK(24);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Desktop:", g_shell.colors.panel_fg, SETK(12));
        for (int i = 0; i < 9; i++) {
            int bx = rx + SETK(76) + (i % 3) * SETK(22);
            int by = ry - SETK(4) + (i / 3) * SETK(22);
            gfx64_fill_rect(bx, by, SETK(16), SETK(16), g_color_presets[i]);
            uint32_t bc = (g_shell.colors.bg == g_color_presets[i])
                        ? g_shell.colors.panel_fg : g_shell.colors.border;
            gfx64_draw_rounded_rect(bx, by, SETK(16), SETK(16), SETK(3), bc);
        }
    } else if (g_settings_page == 1) {
        /* Panels */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Panels", g_shell.colors.panel_fg, SETK(16));
        ry += SETK(30);
        const char *panels[] = {"Top Bar", "Dynamic Island", "Dock", "Clock",
                                "Weather", "Sidebar", "Control Center"};
        for (int i = 0; i < 7; i++) {
            int enabled = hs_panel_is_enabled(
                (const char*[]){"top_bar","dynamic_island","dock","desktop_clock",
                                "weather","sidebar","control_center"}[i]);
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               rx, ry + i * SETK(22), enabled ? "[x] " : "[ ] ",
                               enabled ? g_shell.colors.accent : g_shell.colors.panel_fg, SETK(12));
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               rx + SETK(30), ry + i * SETK(22), panels[i], g_shell.colors.panel_fg, SETK(12));
        }
    } else if (g_settings_page == 2) {
        /* System */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "System Information", g_shell.colors.panel_fg, SETK(16));
        ry += SETK(30);
        const char *labels[] = {"OS:", "Kernel:", "CPU:", "Memory:", "Graphics:", "Filesystem:"};
        /* V0.56.1: "4 cores" / "2GB" 原是写死文案, 与实际机器无关。
         * 改为读 SMP CPU 数与 PMM 物理内存总量。 */
        extern uint32_t smp_get_cpu_count(void);
        extern void pmm64_get_stats(uint64_t *total, uint64_t *used, uint64_t *free);
        static char cpu_str[16];
        static char mem_str[16];
        {
            uint32_t nc = smp_get_cpu_count();
            uint64_t mt = 0, mu = 0, mf = 0;
            pmm64_get_stats(&mt, &mu, &mf);
            hs_itoa((int)(nc ? nc : 1), cpu_str);
            int k = strlen(cpu_str);
            strcpy(cpu_str + k, (nc == 1) ? " core" : " cores");
            hs_itoa((int)(mt / (1024ULL * 1024ULL)), mem_str);
            k = strlen(mem_str);
            strcpy(mem_str + k, " MB");
        }
        const char *values[] = {"Hepu OS v" HEPU_OS_VERSION, "64-bit SMP", cpu_str, mem_str,
                                "VBE 1024x768", "ext2"};
        for (int i = 0; i < 6; i++) {
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               rx, ry + i * SETK(24), labels[i], g_shell.colors.panel_fg, SETK(12));
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               rx + SETK(80), ry + i * SETK(24), values[i], g_shell.colors.accent, SETK(12));
        }
        /* V0.13.39: 语言切换 + 驱动管理入口 */
        ry += SETK(6) * SETK(24) + SETK(10);
        gfx64_fill_rect(rx, ry, w->width - SETK(180), 1, g_shell.colors.border);
        ry += SETK(8);
        extern int l10n64_get_lang(void);
        extern int drv_get_count(void);
        char lb[64];
        strcpy(lb, "Language: ");
        strcpy(lb + strlen(lb), l10n64_get_lang() ? "English  [switch]" : "中文  [切换]");
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, lb, g_shell.colors.panel_fg, SETK(12));
        ry += SETK(24);
        char db[64];
        strcpy(db, "Drivers: ");
        hs_itoa(drv_get_count(), db + strlen(db));
        strcpy(db + strlen(db), "  [Refresh]");
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, db, g_shell.colors.panel_fg, SETK(12));
        ry += SETK(24);
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
                           rx, ry, npb, g_shell.colors.panel_fg, SETK(12));
        ry += SETK(24);
        /* V0.13.39: 存储管理区 */
        gfx64_fill_rect(rx, ry, w->width - SETK(180), 1, g_shell.colors.border);
        ry += SETK(8);
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
                           rx, ry, sb, g_shell.colors.panel_fg, SETK(12));
        ry += SETK(24);
        char sb2[80];
        strcpy(sb2, storage_hal_get_device() ? "Mounted on USB ext2  [Unmount]" : "System disk ext2  [Mount USB]");
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, sb2, g_shell.colors.accent, SETK(12));
        g_settings_storage_ry = ry;
        ry += SETK(24);
        gfx64_fill_rect(rx, ry, w->width - SETK(180), 1, g_shell.colors.border);
        ry += SETK(8);
        /* V0.13.39: 注册表/文件关联管理 */
        extern int reg64_count(void);
        extern int file_assoc_count(void);
        char rb[80];
        strcpy(rb, "Registry: ");
        hs_itoa(reg64_count(), rb + strlen(rb));
        strcpy(rb + strlen(rb), "  [View]");
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, rb, g_shell.colors.panel_fg, SETK(12));
        g_settings_reg_ry = ry;
        ry += SETK(24);
        char ab[80];
        strcpy(ab, "Associations: ");
        hs_itoa(file_assoc_count(), ab + strlen(ab));
        strcpy(ab + strlen(ab), "  [View]");
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, ab, g_shell.colors.panel_fg, SETK(12));
        g_settings_assoc_ry = ry;
    } else if (g_settings_page == 4) {
        /* Display */
        char b1[16], b2[16];
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Display", g_shell.colors.panel_fg, SETK(16));
        ry += SETK(30);
        itoa_hs(g_shell.screen_width, b1);
        itoa_hs(g_shell.screen_height, b2);
        char res[64];
        int p = 0;
        const char *pre = "Resolution: ";
        while (pre[p]) { res[p] = pre[p]; p++; }
        int k = 0; while (b1[k]) { res[p++] = b1[k++]; }
        res[p++] = ' '; res[p++] = 'x'; res[p++] = ' ';
        k = 0; while (b2[k]) { res[p++] = b2[k++]; }
        res[p] = 0;
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, res, g_shell.colors.panel_fg, SETK(12));
        ry += SETK(22);
        /* 引导器枚举到的模式表(0x6100: count + N*(w,h)), 只读展示。
         * 分辨率由引导阶段 SetMode 决定(内核已过 ExitBootServices, 运行时切不了)。 */
        {
            volatile uint32_t *ml = (volatile uint32_t *)0x6100;
            uint32_t cnt = ml[0];
            if (cnt > 16) cnt = 0;
            int shown = 0;
            char mb[128];
            int q = 0;
            const char *pre2 = "Available: ";
            while (pre2[q]) { mb[q] = pre2[q]; q++; }
            for (uint32_t mi = 0; mi < cnt && shown < 3 && q < 100; mi++) {
                uint32_t mw = ml[1 + mi * 2], mh = ml[2 + mi * 2];
                if (mw < 100 || mw > 6000 || mh < 100 || mh > 6000) continue;
                char t1[8], t2[8];
                itoa_hs((int)mw, t1); itoa_hs((int)mh, t2);
                int k1 = 0;
                while (t1[k1] && q < 110) mb[q++] = t1[k1++];
                mb[q++] = 'x';
                k1 = 0;
                while (t2[k1] && q < 110) mb[q++] = t2[k1++];
                if (shown < 2 && mi + 1 < cnt) { mb[q++] = ','; mb[q++] = ' '; }
                shown++;
            }
            if (cnt > 3) {
                mb[q++] = '.'; mb[q++] = '.'; mb[q++] = '.';
            }
            mb[q] = 0;
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               rx, ry, mb, g_shell.colors.panel_fg, SETK(11));
            ry += SETK(18);
        }
        /* 分辨率档位: 点选后写 ESP 上的 HEPBOOT.CFG, 重启生效。
         * 内核已 ExitBootServices, 运行时没有 SetMode 能力, 所以这里的交互
         * 只能是"落配置 + 提示重启", 不做假的立即切换。 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Resolution (reboot to apply)",
                           g_shell.colors.panel_fg, SETK(11));
        ry += SETK(20);
        g_disp_res_btn_x = rx;
        g_disp_res_btn_y = ry;
        for (int ri = 0; ri < 4; ri++) {
            int col = ri % 2, row = ri / 2;
            int bx = rx + col * SETK(HS_DISP_RES_GAPX);
            int by2 = ry + row * SETK(HS_DISP_RES_GAPY);
            int on = (g_disp_res_w[ri] == g_shell.screen_width &&
                      g_disp_res_h[ri] == g_shell.screen_height);
            char rl[16];
            int q = 0, t4 = 0;
            char t3[8];
            gfx64_fill_rounded_rect(bx, by2, SETK(HS_DISP_RES_W), SETK(26), SETK(6),
                                    on ? g_shell.colors.accent : g_shell.colors.panel_bg);
            itoa_hs(g_disp_res_w[ri], rl);
            while (rl[q]) q++;
            rl[q++] = 'x';
            itoa_hs(g_disp_res_h[ri], t3);
            while (t3[t4] && q < 15) rl[q++] = t3[t4++];
            rl[q] = 0;
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               bx + SETK(10), by2 + SETK(7), rl,
                               on ? 0x2B2930 : g_shell.colors.panel_fg, SETK(10));
        }
        /* 两行按钮的实际占用: 行距 + 按钮高 + 行间留白 */
        ry += SETK(HS_DISP_RES_GAPY + 26) + SETK(8);
        if (g_disp_msg[0]) {
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               rx, ry, g_disp_msg,
                               g_shell.colors.text_secondary, SETK(10));
            ry += SETK(16);
        }

        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, g_disp_scale_auto ? "UI Scale: Auto (from resolution)"
                                                     : "UI Scale: Manual (click to change)",
                           g_shell.colors.panel_fg, SETK(12));
        ry += SETK(24);
        g_disp_scale_btn_x = rx;
        g_disp_scale_btn_y = ry;
        for (int i = 0; i < 4; i++) {
            int bx = rx + i * SETK(HS_DISP_BTN_GAP);
            int on = (g_disp_scale_pcts[i] == g_ui_scale_pct);
            gfx64_fill_rounded_rect(bx, ry, SETK(HS_DISP_BTN_W), SETK(28), SETK(6),
                                    on ? g_shell.colors.accent : g_shell.colors.panel_bg);
            char lab[16];
            itoa_hs(g_disp_scale_pcts[i], lab);
            int q = 0; while (lab[q]) q++;
            lab[q] = '%'; lab[q + 1] = 0;
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               bx + SETK(14), ry + SETK(6), lab,
                               on ? hs_on_accent() : g_shell.colors.panel_fg, SETK(12));
        }
        ry += SETK(44);

        /* 窗口投影开关 */
        {
            char sb[40];
            int q = 0;
            const char *sp = "Window Shadows: ";
            while (sp[q]) { sb[q] = sp[q]; q++; }
            const char *sv = g_shell.ui.window_shadow ? "On" : "Off";
            int q2 = 0; while (sv[q2]) sb[q++] = sv[q2++];
            const char *sh = "  [click to toggle]";
            q2 = 0; while (sh[q2]) sb[q++] = sh[q2++];
            sb[q] = 0;
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               rx, ry, sb,
                               g_shell.ui.window_shadow ? g_shell.colors.accent
                                                        : g_shell.colors.text_secondary,
                               SETK(12));
            g_disp_shadow_y = ry;
            ry += SETK(24);
        }

        /* 光标主题 */
        {
            char cb[48];
            int q = 0;
            const char *cp = "Cursor Theme: ";
            while (cp[q]) { cb[q] = cp[q]; q++; }
            const char *cv = g_shell.ui.cursor_theme ? "Modern" : "Classic";
            int q2 = 0; while (cv[q2]) cb[q++] = cv[q2++];
            const char *ch = "  [click to toggle]";
            q2 = 0; while (ch[q2]) cb[q++] = ch[q2++];
            cb[q] = 0;
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               rx, ry, cb,
                               g_shell.ui.cursor_theme ? g_shell.colors.accent
                                                       : g_shell.colors.text_secondary,
                               SETK(12));
            g_disp_cursor_y = ry;
            ry += SETK(24);
        }

        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Refresh rate: firmware-controlled", g_shell.colors.panel_fg, SETK(11));
        ry += SETK(20);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "(no EDID/CRTC driver yet)", g_shell.colors.panel_fg, SETK(11));
    } else {
        /* About */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "About Hepu OS", g_shell.colors.panel_fg, SETK(16));
        ry += SETK(30);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "A self-developed 64-bit OS", g_shell.colors.panel_fg, SETK(12));
        ry += SETK(24);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Inspired by Quickshell", g_shell.colors.panel_fg, SETK(12));
        ry += SETK(24);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "Hepu Desktop Environment", g_shell.colors.panel_fg, SETK(12));
        ry += SETK(24);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, ry, "(c) 2026 Hepu OS Project", g_shell.colors.accent, SETK(12));
    }
}
#undef SETK

/* ============================================================================
 * 工作区
 * ========================================================================== */

void hpdesk_switch_workspace(int ws)
{
    if (ws < 0 || ws >= HS_MAX_WORKSPACES) return;
    g_shell.current_workspace = ws;
    hs_window_tile();
}

/* ============================================================================
 * 应用注册
 * ========================================================================== */

void hpdesk_add_app(const char *name, const char *command, const char *icon)
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

/* V0.59.17 面板包围盒: 局部脏区帧只重画与脏区相交的面板。
 * 矩形一律取"比实际绘制区域略大"的保守值 —— 多画一像素只是浪费, 少画就是残影。 */
static int hs_panel_bounds(int idx, int *ox, int *oy, int *ow, int *oh)
{
    int sw = g_shell.screen_width, sh = g_shell.screen_height;
    switch (idx) {
    case HS_PANEL_TOP_BAR:
        *ox = 0; *oy = 0;
        *ow = sw; *oh = hs_sy(8) + hs_sy(g_shell.ui.bar_height) + hs_sy(16);
        return 1;
    case HS_PANEL_DYNAMIC_ISLAND: {
        int iw = hs_sx(g_shell.ui.island_width), ih = hs_sy(g_shell.ui.island_height);
        *ox = (sw - iw) / 2 - hs_sx(8);
        *oy = hs_sy(g_shell.ui.island_y) - hs_sy(8);
        *ow = iw + hs_sx(16); *oh = ih + hs_sy(16);
        return 1;
    }
    case HS_PANEL_DOCK: {
        int dw = hs_sx(g_shell.ui.dock_width), dh = hs_sy(g_shell.ui.dock_height);
        *ox = (sw - dw) / 2 - hs_sx(4);
        *oy = sh - dh - hs_sy(40); if (*oy < 0) *oy = 0;
        *ow = dw + hs_sx(8); *oh = sh - *oy;
        return 1;
    }
    case HS_PANEL_DESKTOP_CLOCK: {
        /* 时钟按 ui.clock_x/clock_y 定位(逻辑坐标), 字号可达 clock_font_size。
         * 宽度取到天气卡左缘, 高度按最大字号留足。 */
        int cx = hs_sx(g_shell.ui.clock_x);
        int cy = hs_sy(g_shell.ui.clock_y);
        int wx = sw - hs_sx(g_shell.ui.weather_margin_right) - hs_sx(380);
        if (cx > wx) cx = wx;
        if (cx < hs_sx(20)) cx = hs_sx(20);
        int cw = sw - cx;
        int ch = hs_sy(300) + hs_sy(g_shell.ui.clock_font_size) * 2;
        *ox = cx - hs_sx(12); if (*ox < 0) *ox = 0;
        *oy = cy - hs_sy(20); if (*oy < 0) *oy = 0;
        *ow = cw; *oh = ch;
        if (*oy + *oh > sh) *oh = sh - *oy;
        return 1;
    }
    case HS_PANEL_WEATHER: {
        /* 实测桌面天气卡在右下角(不是右上), 见 docs 里 V0.59.19 的截图 */
        int ww = hs_sx(g_shell.ui.weather_width), wh = hs_sy(g_shell.ui.weather_height);
        *ox = sw - ww - hs_sx(g_shell.ui.weather_margin_right) - hs_sx(24);
        *oy = sh - wh - hs_sy(140);
        *ow = ww + hs_sx(48); *oh = wh + hs_sy(160);
        if (*ox < 0) *ox = 0;
        if (*oy < 0) *oy = 0;
        if (*oy + *oh > sh) *oh = sh - *oy;
        return 1;
    }
    case HS_PANEL_SIDEBAR: {
        *ox = hs_sx(g_shell.ui.sidebar_x) - hs_sx(8);
        *oy = hs_sy(g_shell.ui.sidebar_y) - hs_sy(8);
        *ow = hs_sx(g_shell.ui.sidebar_width) + hs_sx(16);
        *oh = hs_sy(g_shell.ui.sidebar_height) + hs_sy(16);
        if (*ox < 0) *ox = 0;
        if (*oy < 0) *oy = 0;
        return 1;
    }
    }
    return 0;
}

/* V0.59.18 面板层表面(单层试验: 先只做控制中心, 验证隔离机制本身是否成立) */
typedef struct { void *surf; int w, h, x, y; int valid; } hs_layer_t;
static hs_layer_t g_ly_cc, g_ly_notify;
static hs_layer_t g_ly_tb, g_ly_island, g_ly_dock, g_ly_clock, g_ly_sidebar;

static void *hs_layer_ensure(hs_layer_t *L, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return 0;
    if (L->surf && L->w == w && L->h == h && L->x == x && L->y == y) return L->surf;
    if (L->surf) { kfree(L->surf); L->surf = 0; }
    void *p = kmalloc((uint64_t)w * h * 4);
    if (!p) { L->w = L->h = 0; L->valid = 0; return 0; }
    L->surf = p; L->w = w; L->h = h; L->x = x; L->y = y; L->valid = 0;
    return p;
}

static void hs_layer_draw_a(hs_layer_t *L, int x, int y, int w, int h,
                           void (*fn)(void), int need_draw, int opacity)
{
    void *s = hs_layer_ensure(L, x, y, w, h);
    if (!s) { if (fn) fn(); return; }
    if (need_draw) {
        memset(s, 0, (size_t)w * h * 4);
        gfx64_target_begin(s, w, h, x, y);
        fn();
        gfx64_target_end();
        L->valid = 1;
    }
    /* V0.59.28: 整层不透明度 —— 顶栏用它做成半透明, 让壁纸从后面透出来。
     * 表面本身是 memset 0(全透明) 再画的, 所以合成时逐像素 alpha 是干净的。 */
    gfx64_blend_image(s, w, h, x, y, opacity);
}

static void hs_layer_draw(hs_layer_t *L, int x, int y, int w, int h,
                         void (*fn)(void), int need_draw)
{
    hs_layer_draw_a(L, x, y, w, h, fn, need_draw, 255);
}

/* 脏区(单矩形)与给定矩形是否相交; 任一无矩形返回 1(保守: 视为相交, 照画) */
static int hs_dirty_intersects(int x, int y, int w, int h)
{
    if (g_dirty_x1 < 0) return 1;
    if (x >= g_dirty_x1 || y >= g_dirty_y1) return 0;
    if (x + w <= g_dirty_x0 || y + h <= g_dirty_y0) return 0;
    return 1;
}

void hs_panel_render_all(void)
{
    /* 按层级顺序渲染所有启用的面板（顶部面板优先，避免累积corruption）
     * 控制中心是常驻侧栏，绘制在窗口之上，见 hpdesk_render */
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
        if (!g_shell.panel_registry[idx].enabled || !g_shell.panel_registry[idx].render) continue;
        int px = 0, py = 0, pw = 0, ph = 0;
        if (!hs_panel_bounds(idx, &px, &py, &pw, &ph) || pw <= 0 || ph <= 0) {
            g_shell.panel_registry[idx].render();
            continue;
        }
        /* V0.59.23 实测: "面板与脏区不相交就跳过" 依然无效, 回退了。
         * 26 个局部帧 × 6 个面板 = 156 次机会, 只有 4 次命中; panelK 34533 -> 35422,
         * 没有下降。原因不在面板而在脏区本身: 拖动时脏区是"旧位置 ∪ 新位置 + pad",
         * 实测 x=308 y=36 w=1421 h=1044 —— 覆盖 77% 屏幕, 顶栏/Dock/侧栏几乎必然相交。
         * 这条负结果解释了 V0.59.13 当年"77809→77505 无效"的实测, 两轮独立数据一致:
         * 在这条拖动路径上做矩形相交判断没有收益, 别再试第三次。
         * 面板 21.6% 的真实构成是"六个面板表面各一块整块 blend", 要从 blend 原语下手。 */
        hs_layer_t *L = 0;
        switch (idx) {
        case HS_PANEL_TOP_BAR:        L = &g_ly_tb; break;
        case HS_PANEL_DYNAMIC_ISLAND: L = &g_ly_island; break;
        case HS_PANEL_DOCK:           L = &g_ly_dock; break;
        case HS_PANEL_DESKTOP_CLOCK:  L = &g_ly_clock; break;
        case HS_PANEL_SIDEBAR:        L = &g_ly_sidebar; break;
        /* 天气卡定位字段(weather_width/margin_right)与实际绘制区域对不上, 纳入层会裁边,
         * 收益仅 0.6%, 保持直绘。 */
        }
        if (L) {
            /* 顶栏半透明(88%), 其余面板保持不透明 */
            /* 顶栏与时钟胶囊同一透明度: 胶囊嵌在顶栏里, 两者 alpha 不一致
             * 会露出一块比顶栏更"实"的补丁。其余面板保持不透明。 */
            int op = (idx == HS_PANEL_TOP_BAR ||
                      idx == HS_PANEL_DYNAMIC_ISLAND) ? 224 : 255;
            hs_layer_draw_a(L, px, py, pw, ph, g_shell.panel_registry[idx].render,
                            !(g_in_partial_frame && L->valid), op);
        }
        else   g_shell.panel_registry[idx].render();
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
    /* V0.59.29: 胶囊垂直居中于顶栏。原来 y 直接取 config 的 island_y(12),
     * 实测胶囊 12..52 / 顶栏 12..70 —— 贴上沿, 下面空 18px, 偏上 9px。
     * 居中口径: bar_top + (bar_h - island_h)/2; 展开时 island_h 超过 bar_h,
     * 钳到 bar_top 让它往下长, 而不是上下同时超出。 */
    int bar_top = hs_sy(8);
    int bar_h = hs_sy(g_shell.ui.bar_height);
    int island_y = bar_top + (bar_h - island_h) / 2;
    if (island_y < bar_top) island_y = bar_top;
    {
        int max_h = g_shell.screen_height - island_y - hs_sy(8);
        if (max_h > 0 && island_h > max_h) island_h = max_h;
    }

    {
        /* P0: 展开/收起尺寸连续插值(缓动), 不再是瞬跳 */
        int e = hs_ease_out_cubic(g_island_t);
        int ww = hs_sx(g_shell.ui.island_width), hh = hs_sy(g_shell.ui.island_height);
        int tw = hs_sx(360), th = hs_sy(120);
        island_w = ww + (tw - ww) * e / 100;
        island_h = hh + (th - hh) * e / 100;
        island_x = (g_shell.screen_width - island_w) / 2;
        /* 高度变了, y 跟着重算, 保证动画过程中也始终居中 */
        island_y = bar_top + (bar_h - island_h) / 2;
        if (island_y < bar_top) island_y = bar_top;
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

    if (g_island_t < 50) {
        /* 收起状态：只显示时间 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           island_x + (island_w - text_w) / 2, island_y + (island_h - font_size) / 2,
                           time, g_shell.colors.panel_fg, font_size);
    } else {
        /* 展开状态：时间+日期+天气+系统状态
         * 改为整块在岛内垂直居中: 旧实现固定 +hs_sy(12) 偏移,
         * 放大后文字块比岛还高, 上半被裁。 */
        int f_time = hs_s(28), f_date = hs_s(14), f_stat = hs_s(12);
        int blk = f_time + hs_sy(8) + f_date + hs_sy(8) + f_stat;
        /* font64_draw_string 的 y 是基线(y+font_size 才是块底), 居中排版必须把
         * 首行基线放在"块顶 + 字高"处, 否则整块会向上顶出岛外、时间上半被裁。 */
        int cy = island_y + (island_h - blk) / 2 + f_time;
        if (cy - f_time < island_y + hs_sy(4)) cy = island_y + hs_sy(4) + f_time;
        /* 时间（大字号） */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           island_x + hs_sx(20), cy, time, g_shell.colors.panel_fg, f_time);
        cy += f_time + hs_sy(8);
        /* 日期 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           island_x + hs_sx(20), cy, "October 15, Thursday", g_shell.colors.text_secondary, f_date);
        /* 右侧天气 */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           island_x + island_w - hs_sx(100), cy, "26C Cloudy", g_shell.colors.text_secondary, f_date);
        cy += f_date + hs_sy(8);
        /* 系统状态条 (V0.56.2: 原来是写死的 "CPU 35%  MEM 60%", 与任务管理器/Info 页
         * 的真实数据对不上; 改读同一套统计源, 避免同屏两套数值) */
        {
            extern int hs_process_get_cpu_usage(void);
            extern int hs_process_get_mem_usage(void);
            char sbuf[64];
            char num[12];
            int l = 0;
            const char *p = 0;
#define HS_CAT(s) for (p = (s); *p && l < 63; p++) sbuf[l++] = *p;
            HS_CAT("CPU ");
            hs_itoa(hs_process_get_cpu_usage(), num);
            HS_CAT(num);
            HS_CAT("%   MEM ");
            hs_itoa(hs_process_get_mem_usage(), num);
            HS_CAT(num);
            HS_CAT("%   WiFi On");
#undef HS_CAT
            sbuf[l] = 0;
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               island_x + hs_sx(20), cy, sbuf, g_shell.colors.text_secondary, f_stat);
        }
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



/* 设置: 齿轮(8 齿 + 齿体圆 + 中心孔) */
/* V0.57.1: 64x64 版位图绘制, 按目标像素做覆盖采样 —— 每个目标像素统计它覆盖到的
 * 源像素里有多少是墨, 过半就点亮。原来的 32->size 直接映射在缩小时会整像素跳过,
 * 2px 宽的线条会成段消失(状态栏 20px 时尤其明显)。同行连续点亮的像素合并成一段再画,
 * 避免 24x24 逐像素调用 576 次 fill_rect。 */
static void hs_icon_bmp64(int x, int y, int size, uint32_t color, uint32_t bg,
                          const uint64_t *bits)
{
    if (!bits || size <= 0) return;
    for (int ty = 0; ty < size; ty++) {
        int sy0 = ty * 64 / size, sy1 = (ty + 1) * 64 / size;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        int run = -1, run_level = 0;
        for (int tx = 0; tx <= size; tx++) {
            int level = 0;
            if (tx < size) {
                int sx0 = tx * 64 / size, sx1 = (tx + 1) * 64 / size;
                if (sx1 <= sx0) sx1 = sx0 + 1;
                int ink = 0, tot = 0;
                for (int sy = sy0; sy < sy1; sy++)
                    for (int sx = sx0; sx < sx1; sx++) {
                        tot++;
                        if ((bits[sy] >> (63 - sx)) & 1u) ink++;
                    }
                /* 4 级覆盖率 + 混色: 1-bit 二值化在 24px 下边缘全是台阶, 用覆盖比例
                 * 分档混背景色, 相当于给位图做了抗锯齿。 */
                if (ink * 4 >= tot * 3) level = 3;
                else if (ink * 2 >= tot) level = 2;
                else if (ink > 0) level = 1;
            }
            if (level != run_level || (level && run < 0)) {
                if (run >= 0) {
                    uint32_t c = (run_level >= 3) ? color
                               : (run_level == 2) ? hs_mix_color(bg, color, 62)
                               : hs_mix_color(bg, color, 30);
                    gfx64_fill_rect(x + run, y + ty, tx - run, 1, c);
                }
                run = level ? tx : -1;
                run_level = level;
            }
        }
    }
}

/* V0.57.0: 位图图标绘制。数据是 32x32 1bit(bit31=最左), 按 size 缩放,
 * 目标色由调用方给 —— 这样同一套图标在浅/深主题、选中/未选中状态下都能用。
 * 每像素落成一个矩形块, 缩小时按 >=1px 保证细线不被吃掉。 */
static void hs_icon_bmp(int x, int y, int size, uint32_t color, const uint32_t *bits)
{
    if (!bits || size <= 0) return;
    for (int py = 0; py < 32; py++) {
        uint32_t row = bits[py];
        if (!row) continue;
        for (int px = 0; px < 32; px++) {
            if (!(row & (1u << (31 - px)))) continue;
            int rx0 = x + px * size / 32;
            int ry0 = y + py * size / 32;
            int rx1 = x + (px + 1) * size / 32;
            int ry1 = y + (py + 1) * size / 32;
            if (rx1 <= rx0) rx1 = rx0 + 1;
            if (ry1 <= ry0) ry1 = ry0 + 1;
            gfx64_fill_rect(rx0, ry0, rx1 - rx0, ry1 - ry0, color);
        }
    }
}

/* Files 类型图标: 单色线性套的统一入口。
 * kind: 0=目录 1=图片 2=视频 3=音频 4=文档 5=其它(未知)。
 * 目录/音频/未知没有语义完全对应的线性图标, 回落到原有矢量描边 —— 宁可少一种细分,
 * 也不拿"造型像但语义不对"的图标硬配(比如用齿轮代表可执行文件)。
 * 图片/视频/文档用第二批线性位图(hs_ico_photo / hs_ico_video / hs_ico_doc_list)。 */
void hs_file_type_icon(int x, int y, int size, uint32_t color, int kind)
{
    switch (kind) {
    case 1: hs_icon_bmp(x, y, size, color, hs_ico_photo);    return;
    case 2: hs_icon_bmp(x, y, size, color, hs_ico_video);    return;
    case 4: hs_icon_bmp(x, y, size, color, hs_ico_doc_list); return;
    case 0: hs_icon_folder(x, y, size, color); return;
    case 3: hs_icon_media(x, y, size, color);  return;
    default: hs_icon_file(x, y, size, color);  return;
    }
}

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

    /* 从右到左排列：电源、设置、音量、电池、WiFi
     * V0.57.1: 改用与 App 图标同一套线性风格位图, 不再用各自一套分别手绘的矢量 —— 那排
     * 图标原来线宽/圆角/重心各不相同, 并排放着像拼的。尺寸提到 18px: 32px 源图减半到
     * 16px 时 2.5px 的线条会塌, 18px 能保住笔画。 */
    icon_size = hs_s(24);   /* 状态栏高 36px, 24px 图标上下各留 6px */
    icon_y = bar_top + (bar_h - icon_size) / 2;
    icon_x -= icon_size; hs_icon_bmp64(icon_x, icon_y, icon_size, fg, g_shell.colors.panel_bg, hs_ico64_power64);
    icon_x -= hs_sx(6) + icon_size; hs_icon_bmp64(icon_x, icon_y, icon_size, fg, g_shell.colors.panel_bg, hs_ico64_gear64);
    icon_x -= hs_sx(6) + icon_size; hs_icon_bmp64(icon_x, icon_y, icon_size, fg, g_shell.colors.panel_bg, hs_ico64_speaker64);
    icon_x -= hs_sx(6) + icon_size; hs_icon_bmp64(icon_x, icon_y, icon_size, fg, g_shell.colors.panel_bg, hs_ico64_battery64);
    /* wifi 与旁边四个同源: 都走 64x64 那套(hs_icon_bmp64)。
     * 旧实现这里单独用 32px 的 hs_ico_wifi_std, 24px 显示时笔画比邻居细一档,
     * 并排放着像两套图标拼的 —— 与 V0.57.1 修掉的"各自一套手绘矢量"是同一类问题。 */
    icon_x -= hs_sx(6) + icon_size; hs_icon_bmp64(icon_x, icon_y, icon_size, fg, g_shell.colors.panel_bg, hs_ico64_wifi64);
}

/* 桌面大时钟 */
static void hs_desktop_clock_render(void)
{
    void *fb = gfx64_get_framebuffer();
    int clock_x = hs_sx(g_shell.ui.clock_x);
    int clock_y = hs_sy(g_shell.ui.clock_y);

    /* 时钟区域不得撞进天气卡: clock_x 是 1024 基准逻辑值, 200% 下放大成 1520,
     * 而天气卡左缘在 1488 —— 两者重叠, 日期文本被卡片盖住只露出尾巴 "…ay"。
     * 这里把时钟整体钳在"天气卡左缘减去时钟区域宽"以内。 */
    {
        int wx = g_shell.screen_width - hs_sx(g_shell.ui.weather_margin_right);
        int max_x = wx - hs_sx(380);
        if (clock_x > max_x) clock_x = max_x;
        if (clock_x < hs_sx(20)) clock_x = hs_sx(20);
    }

    uint8_t h = rtc64_get_hours();
    uint8_t m = rtc64_get_minutes();
    static char time[16];
    time[0] = '0' + h / 10; time[1] = '0' + h % 10; time[2] = ':';
    time[3] = '0' + m / 10; time[4] = '0' + m % 10; time[5] = 0;

    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       clock_x, clock_y, time, g_shell.colors.accent, hs_s(g_shell.ui.clock_font_size));

    static const char date[] = "October 15, Thursday";
    /* 日期不得越过天气卡左缘: 200% 下 20 字符按 date_font_size 直接画会溢出卡片,
     * 截图表现为卡片右侧外面露出半截 "...ay"。按可用宽反推字号上限。 */
    {
        int date_x = clock_x + hs_sx(10);
        int avail = clock_x + hs_sx(350) - date_x;
        int len = 0; while (date[len]) len++;
        int ds = hs_s(g_shell.ui.date_font_size);
        int need = (int)(len * 0.55f * ds);
        if (avail > 0 && need > avail) ds = (int)(avail / (len * 0.55f));
        if (ds < hs_s(10)) ds = hs_s(10);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           date_x, clock_y + hs_sy(85), (char*)date,
                           g_shell.colors.panel_fg, ds);
    }
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

    /* V0.56.1: 取数失败/尚未返回时给明确占位。旧实现直接画空字符串,
     * 卡片只剩一块纯色方块, 看不出是"没数据"还是"渲染坏了"。 */
    /* V0.57.0: 取到响应但字段没解析出来时, g_weather_ok 已是 1 而两个字符串还是空串,
     * 卡片就退化成一块纯色方块 —— 看起来像"没接数据"或"渲染坏了"。按字段回退占位。 */
    const char *tstr = (g_weather_ok && g_weather_temp[0]) ? g_weather_temp : "--";
    const char *dstr = (g_weather_ok && g_weather_desc[0]) ? g_weather_desc : "N/A";

    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       wx + hs_sx(20), wy + hs_sy(15), (char *)tstr, hs_on_accent(), hs_s(g_shell.ui.weather_temp_font));

    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       wx + hs_sx(15), wy + hs_sy(70), (char *)dstr, hs_on_accent(), hs_s(g_shell.ui.weather_desc_font));
}

/* 左侧三标签侧边栏 */
/* 侧边栏当前标签（定义在文件开头前向声明处初始化） */

/* 锁屏状态 */
static int g_locked = 0;       /* 0=桌面, 1=锁屏, 2=登录 */
static char g_password[32] = {0};
static int g_password_len = 0;
static int g_vkb_visible = 0;  /* V0.13.39: 锁屏虚拟键盘 */
/* V0.55.1: 虚拟键盘尚未实现 shift 状态(该变量从未被读写), 已移除声明。 */

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
        int bsw = hs_sx(76);
        int bsx = kb_x + kb_w - bsw;
        int bsy = kb_y + 3 * hs_sy(38);
        gfx64_fill_rounded_rect(bsx, bsy, bsw, hs_sy(32), hs_s(6), g_shell.colors.accent_pressed);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           bsx + hs_sx(8), bsy + hs_sy(7), "DEL", g_shell.colors.panel_fg, hs_s(16));
    }
}

/* V0.59.2: 通用细滚动条。
 * 之前只有侧栏 Apps 页手绘了一处滑块, 其余窗口(终端/文件管理器/任务管理器)
 * 全靠滚轮或键盘滚, 屏幕上没有任何"还能往下滚"的提示, 用户看窗口以为内容
 * 就这么多。抽成公共函数后每个窗口调用同一份画法, 粗细/颜色/圆角一致。
 *
 * thumb 高度按 视口/(视口+可滚量) 折算, 最小 16px 基准, 否则内容一长滑块
 * 会细到看不见。 */
void hs_scrollbar_draw(int x, int track_y, int track_h, int offset, int max_scroll)
{
    if (max_scroll <= 0 || track_h <= 8) return;
    void *fb = gfx64_get_framebuffer();
    int W = g_shell.screen_width, H = g_shell.screen_height;
    int bw = hs_sx(3);
    gfx64_fill_rounded_rect(x, track_y, bw, track_h, hs_s(2), g_shell.colors.border);
    int thumb_h = track_h * track_h / (track_h + hs_sy(max_scroll));
    int min_h = hs_sy(16);
    if (thumb_h < min_h) thumb_h = min_h;
    if (thumb_h > track_h) thumb_h = track_h;
    int span = track_h - thumb_h;
    int thumb_y = track_y + (max_scroll > 0 ? span * offset / max_scroll : 0);
    if (thumb_y < track_y) thumb_y = track_y;
    if (thumb_y > track_y + span) thumb_y = track_y + span;
    gfx64_fill_rounded_rect(x, thumb_y, bw, thumb_h, hs_s(2), g_shell.colors.accent);
}

/* V0.55: 侧栏内容高度(基准单位) — 内容超出可视区的高度即可滚动量 */
static int hs_sidebar_max_scroll(int tab)
{
    int content;
    if (tab == 0) {
        /* Apps: 标题 35 + 行数*88 (行高对齐 Clavis cell 间距) */
        int rows = (g_shell.app_count + 2) / 3;
        content = 35 + rows * 88;
    } else if (tab == 1) {
        content = 35 + 28 + 28 + 40 + 24 + 20 + 20 + 20;  /* Weather 固定行 */
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
    /* 高倍缩放下外框会超出屏幕(150% 时 sy=150 + sh=1500 > 1536),
     * 导致面板底部被裁、内容被迫滚动、并挤压底部 dock。外框必须钳制在屏内。
     * 宽度同理: 侧栏 + Quick Controls 两栏在高倍缩放下会重叠。 */
    {
        int max_sh = g_shell.screen_height - sy - hs_sy(16);
        if (max_sh > 0 && sh > max_sh) sh = max_sh;
        int max_sw = g_shell.screen_width * 2 / 5;
        if (max_sw > 0 && sw > max_sw) sw = max_sw;
    }
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

/* V0.56.2: Apps 页图标网格的单元位置。
 * 渲染与点击必须用同一套公式 —— 否则会出现"看得见却点不到"的错位。
 * 返回 1 表示该单元完全落在可视区域内(可点击/可绘制)。 */
static int hs_apps_cell_pos(int i, int *ax, int *ay)
{
    int sx = hs_sx(g_shell.ui.sidebar_x);
    int sy = hs_sy(g_shell.ui.sidebar_y);
    int sh = hs_sy(g_shell.ui.sidebar_height);
    /* tab_y(12) + 40(内容起点) + 35(标题 "Applications" 占位) */
    int content_y = sy + hs_sy(12) + hs_sy(40) + hs_sy(35);

    int scroll = hs_sy(g_sidebar_scroll);
    int clip_top = content_y;
    int clip_bot = sy + sh - hs_sy(8);
    int row_h = hs_sy(88);

    int x = sx + hs_sx(20) + (i % 3) * hs_sx(80);
    int y = content_y + (i / 3) * row_h - scroll;
    if (ay) *ay = y;
    if (ax) *ax = x;
    return (y >= clip_top && y + hs_sy(70) <= clip_bot);
}

static void hs_sidebar_render(void)
{
    int sx = hs_sx(g_shell.ui.sidebar_x);
    int sy = hs_sy(g_shell.ui.sidebar_y);
    int sw = hs_sx(g_shell.ui.sidebar_width);
    int sh = hs_sy(g_shell.ui.sidebar_height);

    /* 外框必须落在屏内。注意: 命中测试函数里此前已有一份同样的钳制, 但渲染
     * 这一份之前漏了 —— 150% 时面板仍是 150+1500=1650 > 1536, 底部被屏裁,
     * 只是没有滚动条所以看不出来。两处必须用一致的公式。 */
    {
        int max_sh = g_shell.screen_height - sy - hs_sy(16);
        if (max_sh > 0 && sh > max_sh) sh = max_sh;
        int max_sw = g_shell.screen_width * 2 / 5;
        if (max_sw > 0 && sw > max_sw) sw = max_sw;
    }

    /* 收起态：只画贴左边缘的一条圆角把手（点它展开）。面板本身仍 enabled，
     * 因此不需要动 panel_registry，也不用担心 F7 全隐与折叠两种状态打架。 */
    if (g_sidebar_collapsed) {
        void *fbc = gfx64_get_framebuffer();
        int hw = hs_sx(16), hh = hs_sy(104);
        int hx = sx, hy = sy + sh / 2 - hh / 2;
        /* 把手固定用浅紫：强调色来自壁纸取色，深色壁纸下会被取成近黑，
         * 混白也不够（实测仍只剩一道暗影），所以这里不走主题色。 */
        gfx64_fill_rounded_rect(hx, hy, hw, hh, hs_s(7), 0xFFE7E3F6u);
        gfx64_draw_rounded_rect(hx, hy, hw, hh, hs_s(7), 0xFFB9B2D6u);
        font64_draw_string(fbc, g_shell.screen_width, g_shell.screen_height,
                           hx + hs_sx(5), hy + hh / 2 - hs_sy(9), ">",
                           0xFF2A2A33u, hs_s(15));
        return;
    }

    /* 渲染侧探针: 第 60 帧打一次(避开首帧 pct 尚未稳定的阶段)。
     * 首帧 [GEOM] 按配置算出 200% / 侧栏 819x1272 / 底 1472, 但截图里面板底边
     * 只到 y~1105 —— 说明渲染时的 pct 与该帧不同。这里把渲染函数自己的
     * pct 与四个边界一起打出来, 三组数字对齐后差异才有解释。 */
    {
        static int probed = 0;
        if (!probed && g_frame_no >= 60) {
            probed = 1;
            vga_printf("[GEOM-RENDER] frame=%d pct=%d sidebar sx=%d sy=%d sw=%d sh=%d bottom=%d\n",
                       g_frame_no, g_ui_scale_pct, sx, sy, sw, sh, sy + sh);
        }
    }

    /* 侧边栏背景（毛玻璃效果） */
    gfx64_fill_rounded_rect(sx, sy, sw, sh, hs_s(g_shell.ui.corner_radius), g_shell.colors.window_bg);
    gfx64_draw_rounded_rect(sx, sy, sw, sh, hs_s(g_shell.ui.corner_radius), g_shell.colors.border);

    void *fb = gfx64_get_framebuffer();

    /* 标签栏 */
    int tab_y = sy + hs_sy(12);
    int tab_w = (sw - hs_sx(24) - hs_sx(28)) / 2;
    const char *tabs[] = {"Apps", "Weather"};
    for (int i = 0; i < 2; i++) {
        int tx = sx + hs_sx(12) + i * tab_w;
        /* V0.57.0: 悬停反馈 —— 未选中的标签在指针下轻微染上强调色 */
        int tab_hov = (g_shell.mouse_x >= tx && g_shell.mouse_x < tx + tab_w - hs_sx(4) &&
                       g_shell.mouse_y >= tab_y && g_shell.mouse_y < tab_y + hs_sy(28));
        uint32_t sel = g_shell.colors.accent;
        {
            /* 壁纸采样在深色壁纸下会给出很暗的 accent, 选中块与面板底几乎同色,
             * 深色字压深色底 -> 150% 实测整块"消失"。这里对过暗的 accent 亮化,
             * 并按其最终亮度选字色, 保证选中态始终可分辨。 */
            int r0 = (sel >> 16) & 0xFF, g0 = (sel >> 8) & 0xFF, b0 = sel & 0xFF;
            int lum0 = (r0 * 77 + g0 * 151 + b0 * 28) >> 8;
            if (lum0 < 80) sel = hs_mix_color(sel, 0xFFFFFF, 55);
        }
        uint32_t bg = (i == g_sidebar_tab) ? sel
                     : (tab_hov ? hs_mix_color(g_shell.colors.panel_bg, g_shell.colors.accent, 28)
                                : g_shell.colors.panel_bg);
        uint32_t fg = g_shell.colors.panel_fg;
        if (i == g_sidebar_tab) {
            int r1 = (sel >> 16) & 0xFF, g1 = (sel >> 8) & 0xFF, b1 = sel & 0xFF;
            int lum1 = (r1 * 77 + g1 * 151 + b1 * 28) >> 8;
            fg = (lum1 > 140) ? 0x003544 : 0xF2F5F7;
        }
        /* 色板里的 accent 是 6 位色(alpha 字节 0), 而 vg 填充走 alpha 混合:
         * alpha=0 被当作全透明, 选中块整块不落像素 —— 文字却按"亮底"选了深色,
         * 结果 150% 下整个 tab 像消失。这里统一补不透明位。 */
        gfx64_fill_rounded_rect(tx, tab_y, tab_w - hs_sx(4), hs_sy(28), hs_s(8),
                                bg | 0xFF000000u);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           tx + (tab_w - hs_sx(4) - hs_sx(32)) / 2, tab_y + hs_sy(7),
                           tabs[i], fg, hs_s(14));
    }

    /* 收起按钮：tab 行最右端一枚小方块「<」 */
    {
        int bw = hs_sx(20), bh = hs_sy(20);
        int bx = sx + sw - hs_sx(26);
        int byy = tab_y + hs_sy(4);
        int hov = (g_shell.mouse_x >= bx && g_shell.mouse_x < bx + bw &&
                   g_shell.mouse_y >= byy && g_shell.mouse_y < byy + bh);
        gfx64_fill_rounded_rect(bx, byy, bw, bh, hs_s(6),
                                (hov ? hs_mix_color(g_shell.colors.panel_bg, g_shell.colors.accent, 28)
                                     : g_shell.colors.panel_bg) | 0xFF000000u);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           bx + hs_sx(7), byy + hs_sy(2), "<",
                           g_shell.colors.panel_fg, hs_s(13));
    }

    /* 内容区域 */
    int content_y = tab_y + hs_sy(40);

    /* V0.59.0: 侧栏去掉 Info 标签页。
     * 里面放的是 Uptime / 进程数 / 网络 / 版本 / 规格 / 内存 / 磁盘 ——
     * 这组数据在任务管理器里本来就有位置（曲线、进程表、内核服务都在那儿），
     * 在侧栏再单开一页等于同一组数据维护两份，而且侧栏版既没有曲线也没有明细。
     * 这些行已整体搬到任务管理器顶部的系统摘要条。 */
    if (g_sidebar_tab == 0) {
            /* 应用抽屉 (V0.55: 真实注册应用列表 + Clavis 式垂直滚动) */
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           sx + hs_sx(20), content_y, "Applications", g_shell.colors.panel_fg, hs_s(18));
        content_y += hs_sy(35);

        int clip_top = content_y;                 /* 网格视口顶 (标题固定) */
        int clip_bot = sy + sh - hs_sy(8);        /* 侧栏底内边距 */
        for (int i = 0; i < g_shell.app_count; i++) {
            int ax = 0, ay = 0;
            /* 无裁剪原语: 整行剔除 — 行不完全落在视口内时跳过 */
            if (!hs_apps_cell_pos(i, &ax, &ay)) continue;
            /* V0.57.0: 悬停高亮 —— 指针在单元内时垫一层淡强调色底座 */
            if (g_shell.mouse_x >= ax && g_shell.mouse_x < ax + hs_s(48) &&
                g_shell.mouse_y >= ay && g_shell.mouse_y < ay + hs_sy(70)) {
                /* 底座比图标大一圈(每边 6px)并加外描边, 否则底座会被图标完全盖住看不出变化 */
                gfx64_fill_rounded_rect(ax - hs_s(6), ay - hs_s(6), hs_s(60), hs_s(60),
                                        hs_s(14),
                                        hs_mix_color(g_shell.colors.panel_bg, g_shell.colors.accent, 26));
                gfx64_draw_rounded_rect(ax - hs_s(6), ay - hs_s(6), hs_s(60), hs_s(60),
                                        hs_s(14), g_shell.colors.accent);
            }
            /* V0.13.15: 应用矢量图标(白, 透明底) */
            gfx64_fill_rounded_rect(ax, ay, hs_s(48), hs_s(48), hs_s(10), g_shell.colors.accent);
            uint32_t ic = 0xFDF8EF;
            /* V0.57.0: 改用统一线性图标位图集(hs_icons_data.h)。
             * 映射按语义尽量贴近, 注释写在每行后面 —— 有错配的地方是"造型可用但语义近似",
             * 不是照着应用名字硬配的。 */
            {
                const uint32_t *ico = hs_ico_menu;
                switch (i) {
                    case 0:  ico = hs_ico_menu_bar; break;  /* Terminal: 命令行横幅 */
                    case 1:  ico = hs_ico_home_v2;  break;  /* Files: 主目录 */
                    case 2:  ico = hs_ico_search;   break;  /* Browser: 检索 */
                    case 3:  ico = hs_ico_settings; break;  /* Settings: 齿轮 */
                    case 4:  ico = hs_ico_mail;     break;  /* Editor: 文档/信件 */
                    case 5:  ico = hs_ico_add;      break;  /* Calculator: 运算 */
                    case 6:  ico = hs_ico_doc_list; break;  /* Notes: 带字文档 */
                    case 7:  ico = hs_ico_clock;    break;  /* Task Manager: 性能时钟 */
                    case 8:  ico = hs_ico_grid_all; break;  /* Store: 应用网格 */
                    case 9:  ico = hs_ico_sliders;  break;  /* Info: 参数面板 */
                    case 10: ico = hs_ico_settings; break;
                    case 11: ico = hs_ico_download; break;  /* Disk Manager: 存储 */
                    default: ico = hs_ico_menu;     break;
                }
                hs_icon_bmp(ax + hs_s(8), ay + hs_s(8), hs_s(32), ic, ico);
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
/* 控制中心默认收起, 由顶栏右上角齿轮切换 */
static int g_cc_open = 0;

#define CC_RAIL_W 104        /* 导航栏宽度 */
#define CC_PAD    16         /* 内容区内边距 */

static void cc_geometry(int *cx, int *cy, int *cw, int *ch)
{
    /* 宽度跟随 hs_sx() 缩放, 与左侧 sidebar 同一基准(280), 两栏在高 DPI 下等宽。
     * 高度与内部纵向坐标保持基准像素, 避免卡片被压扁重叠。 */
    int w = hs_sx(g_shell.ui.cc_width);
    /* 高度必须与宽度同源缩放, 否则面板被拉成宽扁条(150% 实测 787x440) */
    int h = hs_sy(g_shell.ui.cc_height);
    if (w < hs_sx(280)) w = hs_sx(280);
    if (h < hs_sy(320)) h = hs_sy(440);
    int max_w = g_shell.screen_width * 3 / 5;
    if (max_w > 0 && w > max_w) w = max_w;
    int max_h = g_shell.screen_height * 19 / 20;
    if (max_h > 0 && h > max_h) h = max_h;

    /* 两栏不得重叠: 侧栏被外框钳制(屏宽 2/5)后右缘可能比 QC 的 cx 更靠右,
     * 200% 时实测侧栏右缘 867 > QC 左缘 832, QC 压进侧栏 35px。 */
    {
        int sb_x = hs_sx(g_shell.ui.sidebar_x);
        int sb_w = hs_sx(g_shell.ui.sidebar_width);
        int sb_cap = g_shell.screen_width * 2 / 5;
        if (sb_cap > 0 && sb_w > sb_cap) sb_w = sb_cap;
        int min_cx = sb_x + sb_w + hs_sx(24);
        /* 右缘与顶栏同一 margin(hs_sx(10)), 否则 QC 会浮在页面中间、和右上状态栏
         * 差出 20 多逻辑像素, 看起来像两套布局(实测 150%: QC 右缘 1853 / 顶栏 1892)。 */
        int cx_try = g_shell.screen_width - w - hs_sx(10);
        if (cx_try < min_cx) {
            cx_try = min_cx;
            w = g_shell.screen_width - cx_try - hs_sx(10);
            if (w < hs_sx(200)) w = hs_sx(200);
        }
        *cx = cx_try;
    }
    *cw = w;
    *ch = h;
    if (*cx < 0) *cx = 0;
    *cy = hs_sy(g_shell.ui.cc_y);
    /* 先压高度再兜底挪位置: 面板变高后若把 cy 往上推, 会盖住右上通知卡 */
    if (*cy + h > g_shell.screen_height) {
        h = g_shell.screen_height - *cy - hs_sy(8);
        if (h < hs_sy(300)) h = hs_sy(300);
        *ch = h;
    }
    if (*cy + *ch > g_shell.screen_height) *cy = g_shell.screen_height - *ch;
    if (*cy < 0) *cy = 0;

    /* 右上/右下通知常驻时, 控制中心从其下方起算, 否则面板标题会被通知卡压住
     * (150% 实测: 通知区 65~195, QC 顶 162)。 */
    {
        int nvis = 0;
        for (int i = 0; i < g_shell.notifications.count; i++)
            if (g_shell.notifications.notifications[i].visible) nvis++;
        if (nvis > 3) nvis = 3;
        int right_side = (g_notif_pos == 0 || g_notif_pos == 2);
        if (right_side && nvis > 0) {
            int nb = hs_sy(g_shell.ui.bar_height) + hs_sy(8) +
                     nvis * (hs_sy(64) + hs_sy(10));
            if (*cy < nb + hs_sy(8)) *cy = nb + hs_sy(8);
            if (*cy + *ch > g_shell.screen_height) {
                *ch = g_shell.screen_height - *cy - hs_sy(8);
                if (*ch < hs_sy(300)) *ch = hs_sy(300);
            }
        }
    }
}

/* 开关卡片：开启态用强调色填充，关闭态用次要表面 */
/* 粗线段: 用 fill_rect 采样绘制。
 * 不用 gfx64_draw_line_aa —— 实测它在面板层表面上下文里画不出像素,
 * WiFi 两个弧完全缺失, 只剩中间那个点。 */
static void cc_seg(int x0, int y0, int x1, int y1, uint32_t c, int t)
{
    int dx = x1 - x0, dy = y1 - y0;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    int n = ax > ay ? ax : ay;
    if (n < 1) n = 1;
    for (int i = 0; i <= n; i++)
        gfx64_fill_rect(x0 + dx * i / n - t / 2, y0 + dy * i / n - t / 2, t, t, c);
}

/* 控制中心开关图标(简笔手绘, 不依赖位图资源):
 * 0 WiFi  1 BT  2 Air  3 DND  4 Loc  5 Torch */
static void cc_icon_draw(int kind, int cx, int cy, int r, uint32_t fg, uint32_t bg)
{
    int t = hs_s(3);
    switch (kind) {
    case 0: /* WiFi: 底部圆点 + 两道上凸折线 */
        gfx64_fill_circle(cx, cy + r / 2, t, fg);
        /* 内层弧(更平缓, 避免看起来像屋顶) */
        cc_seg(cx - r * 3 / 5, cy + r / 3, cx, cy - r / 8, fg, t);
        cc_seg(cx, cy - r / 8, cx + r * 3 / 5, cy + r / 3, fg, t);
        /* 外层弧 */
        cc_seg(cx - r, cy + r / 3, cx, cy - r * 2 / 5, fg, t);
        cc_seg(cx, cy - r * 2 / 5, cx + r, cy + r / 3, fg, t);
        break;
    case 1: /* Bluetooth: 中竖线 + 右侧菱形折线(经典 ᛒ 形) */
        cc_seg(cx, cy - r, cx, cy + r, fg, t);
        cc_seg(cx, cy - r, cx + r / 2, cy - r / 2, fg, t);
        cc_seg(cx + r / 2, cy - r / 2, cx, cy, fg, t);
        cc_seg(cx, cy, cx + r / 2, cy + r / 2, fg, t);
        cc_seg(cx + r / 2, cy + r / 2, cx, cy + r, fg, t);
        break;
    case 2: /* Air: 三条长短横线 */
        gfx64_fill_rect(cx - r, cy - r * 2 / 3, r * 2, t, fg);
        gfx64_fill_rect(cx - r * 3 / 4, cy, r * 3 / 2, t, fg);
        gfx64_fill_rect(cx - r / 2, cy + r * 2 / 3, r, t, fg);
        break;
    case 3: /* DND: 月牙 */
        gfx64_fill_circle(cx, cy, r, fg);
        gfx64_fill_circle(cx + r / 2, cy - r / 3, r, bg);
        break;
    case 4: /* Loc: 定位针 */
        gfx64_fill_circle(cx, cy - r / 3, r * 2 / 3, fg);
        gfx64_fill_circle(cx, cy - r / 3, r / 3, bg);
        {
            int xs[3] = {cx - r * 2 / 3, cx + r * 2 / 3, cx};
            int ys[3] = {cy, cy, cy + r};
            gfx64_fill_polygon(xs, ys, 3, fg);
        }
        break;
    case 5: /* Torch: 机身 + 光束 */
        gfx64_fill_rect(cx - r / 2, cy - r / 2, r, r, fg);
        {
            int xs[4] = {cx - r * 3 / 4, cx + r * 3 / 4, cx + r / 2, cx - r / 2};
            int ys[4] = {cy - r, cy - r, cy - r / 2, cy - r / 2};
            gfx64_fill_polygon(xs, ys, 4, fg);
        }
        break;
    default:
        break;
    }
}

/* 方形开关卡片: 图标居中, 名称在下 */
static void cc_toggle_card(void *fb, int x, int y, int w, int h,
                           int on, const char *label, int icon_kind)
{
    uint32_t bg = on ? g_shell.colors.accent : g_shell.colors.titlebar_bg;
    uint32_t fg = on ? hs_on_accent() : g_shell.colors.panel_fg;
    gfx64_fill_rounded_rect(x, y, w, h, hs_s(12), bg);
    if (!on) gfx64_draw_rounded_rect(x, y, w, h, hs_s(12), g_shell.colors.border);

    int r = hs_s(18);
    int icx = x + w / 2;
    int icy = y + h / 2 - hs_sy(12);
    cc_icon_draw(icon_kind, icx, icy, r, fg, bg);

    int cw = hs_s(9), fs = hs_s(13);
    int tw_ = (int)strlen(label) * cw;
    int lx = x + (w - tw_) / 2;
    if (lx < x + hs_sx(4)) lx = x + hs_sx(4);
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       lx, y + h - hs_sy(30), label, fg, fs);
}

/* 滑块卡片：标题 + 百分比 + 轨道 */
static void cc_slider_card(void *fb, int x, int y, int w, int h,
                           const char *label, int val)
{
    gfx64_fill_rounded_rect(x, y, w, h, hs_s(10), g_shell.colors.titlebar_bg);
    gfx64_draw_rounded_rect(x, y, w, h, hs_s(10), g_shell.colors.border);

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
                       x + hs_sx(14), y + hs_sy(12), label,
                       g_shell.colors.panel_fg, hs_s(13));
    font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                       x + w - hs_sx(52), y + hs_sy(12), vb,
                       g_shell.colors.text_secondary, hs_s(12));

    int tx = x + hs_sx(14);
    int tw = w - hs_sx(28);
    int ty = y + h - hs_sy(22);
    int fill = tw * val / 100;
    int barh = hs_sy(8);
    gfx64_fill_rounded_rect(tx, ty, tw, barh, hs_s(4), g_shell.colors.border);
    if (fill > hs_sx(8))
        gfx64_fill_rounded_rect(tx, ty, fill, barh, hs_s(4), g_shell.colors.accent);
    gfx64_fill_circle(tx + fill - hs_sx(8), ty + barh / 2, hs_s(7),
                      g_shell.colors.accent);
}

/* 把颜色按比例压暗(保留 k%, 用于绘制浮层投影的阶梯暗边)。
 * 面板底色本身接近纯黑, 直接给纯黑投影在深色壁纸上看不出来,
 * 所以用"面板色 × 系数"而非固定黑色, 保证在任何主题下都能拉开层次。 */
static uint32_t hs_dim(uint32_t c, int k)
{
    int r = (int)((c >> 16) & 0xFF) * k / 100;
    int g = (int)((c >> 8) & 0xFF) * k / 100;
    int b = (int)(c & 0xFF) * k / 100;
    return (uint32_t)(0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
}

static void hs_control_center_render(void)
{
    if (!g_cc_open) return;
    int cx, cy, cw, ch;
    cc_geometry(&cx, &cy, &cw, &ch);

    /* 浮层层次: QC 会压住天气卡(设计稿里两者纵向本就叠 60 逻辑像素),
     * 之前两块同色卡片叠在一起像"画错了"。这里加两层阶梯暗边做投影,
     * 让被压住的组件自然退到后面 —— 不改任何几何, 只改观感。 */
    gfx64_fill_rounded_rect(cx - hs_s(1), cy + hs_s(6), cw + hs_s(5), ch + hs_s(8),
                            hs_s(15), hs_dim(g_shell.colors.window_bg, 22));
    gfx64_fill_rounded_rect(cx, cy + hs_s(3), cw + hs_s(2), ch + hs_s(4),
                            hs_s(13), hs_dim(g_shell.colors.window_bg, 48));
    gfx64_fill_rounded_rect(cx, cy, cw, ch, hs_s(12), g_shell.colors.window_bg);
    gfx64_draw_rounded_rect(cx, cy, cw, ch, hs_s(12), g_shell.colors.border);

    void *fb = gfx64_get_framebuffer();
    int rail_w = hs_sx(CC_RAIL_W);
    int pad = hs_sx(CC_PAD);

    /* 导航栏（Clavis NavigationRail） */
    const char *nav[] = {"Quick", "Devices", "Audio"};
    for (int i = 0; i < 3; i++) {
        int nx = cx + hs_sx(8);
        int ny = cy + hs_sy(14) + i * hs_sy(46);
        int nw = rail_w - hs_sx(16);
        int nh = hs_sy(38);
        uint32_t bg = (i == g_cc_page) ? g_shell.colors.accent : g_shell.colors.window_bg;
        uint32_t fg = (i == g_cc_page) ? hs_on_accent() : g_shell.colors.panel_fg;
        if (i != g_cc_page)
            gfx64_fill_rounded_rect(nx, ny, nw, nh, hs_s(8), g_shell.colors.titlebar_bg);
        else
            gfx64_fill_rounded_rect(nx, ny, nw, nh, hs_s(8), bg);
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           nx + hs_sx(12), ny + hs_sy(12), nav[i], fg, hs_s(13));
    }

    int rx = cx + rail_w + pad;
    int rw = cw - rail_w - pad * 2;
    const char *toggles[] = {"WiFi", "BT", "Air", "DND", "Loc", "Torch"};

    if (g_cc_page == 0) {
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, cy + hs_sy(18), "Quick Controls",
                           g_shell.colors.panel_fg, hs_s(16));
        /* 3 列 2 行正方形: 图标 + 名称, 不再是六个文字长条 */
        int gap = hs_sx(10);
        int card_w = (rw - 2 * gap) / 3;
        int card_h = card_w;
        for (int i = 0; i < 6; i++) {
            int tx = rx + (i % 3) * (card_w + gap);
            int ty = cy + hs_sy(48) + (i / 3) * (card_h + gap);
            cc_toggle_card(fb, tx, ty, card_w, card_h, g_shell.cc.toggles[i],
                           toggles[i], i);
        }
        cc_slider_card(fb, rx, cy + hs_sy(258), rw, hs_sy(58), "Volume",
                       g_shell.cc.volume);
        cc_slider_card(fb, rx, cy + hs_sy(322), rw, hs_sy(58), "Brightness",
                       g_shell.cc.brightness);
    } else if (g_cc_page == 1) {
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, cy + hs_sy(18), "Devices",
                           g_shell.colors.panel_fg, hs_s(16));
        for (int i = 0; i < 6; i++) {
            int ry = cy + hs_sy(48) + i * hs_sy(54);
            gfx64_fill_rounded_rect(rx, ry, rw, hs_sy(48), hs_s(10),
                                    g_shell.colors.titlebar_bg);
            gfx64_draw_rounded_rect(rx, ry, rw, hs_sy(48), hs_s(10),
                                    g_shell.colors.border);
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               rx + hs_sx(14), ry + hs_sy(16), toggles[i],
                               g_shell.colors.panel_fg, hs_s(13));
            const char *stt = g_shell.cc.toggles[i] ? "On" : "Off";
            uint32_t stc = g_shell.cc.toggles[i] ? g_shell.colors.accent
                                                : g_shell.colors.text_secondary;
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               rx + rw - hs_sx(44), ry + hs_sy(16), stt, stc,
                               hs_s(12));
            gfx64_fill_circle(rx + rw - hs_sx(16), ry + hs_sy(24), hs_s(5), stc);
        }
    } else {
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, cy + hs_sy(18), "Audio & Display",
                           g_shell.colors.panel_fg, hs_s(16));
        cc_slider_card(fb, rx, cy + hs_sy(52), rw, hs_sy(96), "Volume",
                       g_shell.cc.volume);
        cc_slider_card(fb, rx, cy + hs_sy(160), rw, hs_sy(96), "Brightness",
                       g_shell.cc.brightness);
        const char *note = "Adjust output level and panel backlight.";
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           rx, cy + hs_sy(276), note,
                           g_shell.colors.text_secondary, hs_s(11));
        cc_toggle_card(fb, rx, cy + hs_sy(300), hs_sy(80), hs_sy(80),
                       g_shell.cc.toggles[3], "DND", 3);
        cc_toggle_card(fb, rx + hs_sy(90), cy + hs_sy(300), hs_sy(80), hs_sy(80),
                       g_shell.cc.toggles[5], "Torch", 5);
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

    int rail_w = hs_sx(CC_RAIL_W);
    int pad = hs_sx(CC_PAD);

    /* 导航栏切页 */
    for (int i = 0; i < 3; i++) {
        int nx = cx + hs_sx(8);
        int ny = cy + hs_sy(14) + i * hs_sy(46);
        if (x >= nx && x <= nx + rail_w - hs_sx(16) &&
            y >= ny && y <= ny + hs_sy(38)) {
            g_cc_page = i;
            return 1;
        }
    }

    int rx = cx + rail_w + pad;
    int rw = cw - rail_w - pad * 2;
    int gap = hs_sx(10);
    int card_w = (rw - 2 * gap) / 3;

    if (g_cc_page == 0) {
        for (int i = 0; i < 6; i++) {
            int tx = rx + (i % 3) * (card_w + gap);
            int ty = cy + hs_sy(48) + (i / 3) * (card_w + gap);
            if (x >= tx && x <= tx + card_w && y >= ty && y <= ty + card_w) {
                g_shell.cc.toggles[i] = !g_shell.cc.toggles[i];
                return 1;
            }
        }
        {
            int ty = cy + hs_sy(258) + hs_sy(58) - hs_sy(22);
            if (y >= ty - hs_sy(10) && y <= ty + hs_sy(20)) {
                cc_slider_set(x, rx + hs_sx(14), rw - hs_sx(28), &g_shell.cc.volume);
                return 1;
            }
        }
        {
            int ty = cy + hs_sy(322) + hs_sy(58) - hs_sy(22);
            if (y >= ty - hs_sy(10) && y <= ty + hs_sy(20)) {
                cc_slider_set(x, rx + hs_sx(14), rw - hs_sx(28), &g_shell.cc.brightness);
                return 1;
            }
        }
    } else if (g_cc_page == 1) {
        for (int i = 0; i < 6; i++) {
            int ry = cy + hs_sy(48) + i * hs_sy(54);
            if (x >= rx && x <= rx + rw && y >= ry && y <= ry + hs_sy(48)) {
                g_shell.cc.toggles[i] = !g_shell.cc.toggles[i];
                return 1;
            }
        }
    } else {
        {
            int ty = cy + hs_sy(52) + hs_sy(96) - hs_sy(22);
            if (y >= ty - hs_sy(12) && y <= ty + hs_sy(24)) {
                cc_slider_set(x, rx + hs_sx(14), rw - hs_sx(28), &g_shell.cc.volume);
                return 1;
            }
        }
        {
            int ty = cy + hs_sy(160) + hs_sy(96) - hs_sy(22);
            if (y >= ty - hs_sy(12) && y <= ty + hs_sy(24)) {
                cc_slider_set(x, rx + hs_sx(14), rw - hs_sx(28), &g_shell.cc.brightness);
                return 1;
            }
        }
        if (y >= cy + hs_sy(300) && y <= cy + hs_sy(360)) {
            g_shell.cc.toggles[3] = !g_shell.cc.toggles[3];
            return 1;
        }
        if (y >= cy + hs_sy(368) && y <= cy + hs_sy(428)) {
            g_shell.cc.toggles[5] = !g_shell.cc.toggles[5];
            return 1;
        }
    }
    return 1;
}

/* 右键菜单渲染 */
static int g_menu_file_mode = 0;   /* V0.13.39: 1=文件管理器右键菜单 */

/* ============================================================================
 * V0.55.0: 桌面配色面板 — 图形化更换桌面背景色, 并持久化到 config.hsqml
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
    /* P0: 弹出滑入 */
    y += (int)(12 * (100 - hs_ease_out_cubic(g_color_t)) / 100);
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
            g_wp_sel = 5;               /* 纯色桌面(色板) */
            hs_save_bg_color(g_color_presets[i]);
            g_content_dirty = 1;  /* 同上: 强制全量重绘 */
            g_color_panel_visible = 0;
            return 1;
        }
    }
    return 1;
}

/* Hepu Desktop鼠标点击处理（由desktop64主循环调用） */
static int g_files_drag_consumed;   /* Files 拖拽已消费本次按下-抬起 */

void hpdesk_handle_click(int x, int y)
{
    /* 拖拽已消费这次按下-抬起: 不能再走点击/双击路径(否则会��接打开源文件) */
    if (g_files_drag_consumed) { g_files_drag_consumed = 0; return; }

    hpdesk_mark_dirty();   /* P0: 交互发生, 内容需重绘 */
    /* V0.55 P3-GUI: 首启向导模态拦截点击 */
    {
        extern int hs_oobe_active(void);
        extern int hs_oobe_handle_click(int x, int y);
        if (hs_oobe_active()) { hs_oobe_handle_click(x, y); return; }
    }

    /* 右键菜单开着: 无论点哪儿都先归菜单, 点空白则关闭 */
    if (g_ctx.open) {
        int idx = hs_ctx_hit_item(x, y);
        int owner = g_ctx.owner, cid = (idx >= 0) ? g_ctx.id[idx] : -1;
        hs_ctx_close();
        if (idx >= 0 && owner == 0) {
            if (cid == HS_CTX_D_REFRESH) hpdesk_mark_dirty();
            else if (cid == HS_CTX_D_FILES) g_pending_app = HS_APP_FILES;
            else if (cid == HS_CTX_D_SETTING) g_pending_app = HS_APP_SETTINGS;
            else if (cid == HS_CTX_D_TASK) g_pending_app = HS_APP_TASK;
        } else if (idx >= 0 && owner == 1) {
            extern void hs_files_menu_action(int id);
            hs_files_menu_action(cid);
        } else if (idx >= 0 && owner == 2) {
            /* 通用窗口菜单: 0=Close */
            if (cid == 0) {
                for (int zi = g_wm.z_count - 1; zi >= 0; zi--) {
                    int wid = g_wm.z_order[zi];
                    if (wid < 0 || wid >= g_wm.window_count) continue;
                    hs_window_t *w2 = &g_wm.windows[wid];
                    if (!w2->visible || w2->type != HS_WINDOW_FLOATING) continue;
                    if (x < w2->x || x > w2->x + w2->width || y < w2->y || y > w2->y + w2->height) continue;
                    hs_window_hide(w2);
                    break;
                }
            }
        }
        return;
    }

    /* V0.55.0: 桌面配色面板优先接管点击 */
    if (hs_color_panel_handle_click(x, y)) return;

    /* 控制中心绘制在窗口之上，优先接管该区域点击 */
    if (!g_locked && g_shell.panel_registry[HS_PANEL_CONTROL_CENTER].enabled && 1) {
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
            int input_h = hs_sy(36);
            int avatar_size = hs_s(96);
            int avatar_y = hs_sy(180);
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
            int menu_x = w->x + hs_set_k(w, 16);
            int menu_y = w->y + hs_set_k(w, 36);
            if (x >= menu_x && x <= menu_x + hs_set_k(w, 120) && y >= menu_y) {
                int item = (y - menu_y) / hs_set_k(w, 32);
                if (item >= 0 && item < 5) {
                    g_settings_page = item;
                    /* V0.13.39: System tab 内可点击操作 (语言切换 / 驱动刷新) */
                    if (item == 2 && g_settings_page == 2) {
                        int lang_y = w->y + hs_set_k(w, 36) + 6 * hs_set_k(w, 24) +
                                     hs_set_k(w, 18);
                        int drv_y = lang_y + hs_set_k(w, 24);
                        if (y >= lang_y && y <= lang_y + hs_set_k(w, 20)) {
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
                        if (y >= drv_y && y <= drv_y + hs_set_k(w, 20)) {
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
                            int npy = drv_y + hs_set_k(w, 24);
                            if (y >= npy && y <= npy + hs_set_k(w, 20)) {
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
                            if (sry && y >= sry && y <= sry + hs_set_k(w, 20)) {
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
                            if (rry && y >= rry && y <= rry + hs_set_k(w, 20)) {
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
            /* Display 页: 分辨率档位点击 -> 写 HEPBOOT.CFG(重启生效) */
            if (g_settings_page == 4 && y >= g_disp_res_btn_y) {
                int rx_ = x - g_disp_res_btn_x;
                int ry_ = y - g_disp_res_btn_y;
                int gw_ = hs_set_k(w, HS_DISP_RES_GAPX);
                int gv_ = hs_set_k(w, HS_DISP_RES_GAPY);
                int bw_ = hs_set_k(w, HS_DISP_RES_W);
                int bh_ = hs_set_k(w, 26);
                if (rx_ >= 0 && ry_ >= 0 && gw_ > 0 && gv_ > 0) {
                    int col = rx_ / gw_, row = ry_ / gv_;
                    if (col < 2 && row < 2 &&
                        (rx_ % gw_) <= bw_ && (ry_ % gv_) <= bh_) {
                        int idx = row * 2 + col;
                        extern void hs_display_set_resolution(int ww, int hh);
                        hs_display_set_resolution(g_disp_res_w[idx], g_disp_res_h[idx]);
                        g_content_dirty = 1;
                        return;
                    }
                }
            }
            /* Display 页: 缩放档位点击 -> 立即生效 + 持久化 */
            if (g_settings_page == 4) {
                if (y >= g_disp_scale_btn_y && y <= g_disp_scale_btn_y + 28) {
                    int bi = (x - g_disp_scale_btn_x);
                    if (bi >= 0 && bi < 4 * hs_set_k(w, HS_DISP_BTN_GAP)) {
                        int idx = bi / hs_set_k(w, HS_DISP_BTN_GAP);
                        if (idx >= 0 && idx < 4) {
                            g_ui_scale_pct = g_disp_scale_pcts[idx];
                            g_disp_scale_auto = 0;   /* 手动选择后不再自动跟随 */
                            g_content_dirty = 1;
                            extern void hs_display_save(void);
                            hs_display_save();
                            vga_printf("[SETTINGS] ui scale = %d%%\n", g_ui_scale_pct);
                            return;
                        }
                    }
                }
            }
            /* Display 页: 窗口投影开关 */
            if (g_settings_page == 4 && g_disp_shadow_y > 0 &&
                y >= g_disp_shadow_y - 4 && y <= g_disp_shadow_y + 22 &&
                x >= w->x + hs_set_k(w, 16) && x <= w->x + w->width - hs_set_k(w, 16)) {
                g_shell.ui.window_shadow = g_shell.ui.window_shadow ? 0 : 1;
                g_content_dirty = 1;
                extern void hs_display_save(void);
                hs_display_save();
                vga_printf("[SETTINGS] window shadow = %d\n", g_shell.ui.window_shadow);
                return;
            }

            /* Display 页: 光标主题切换 */
            if (g_settings_page == 4 && g_disp_cursor_y > 0 &&
                y >= g_disp_cursor_y - 4 && y <= g_disp_cursor_y + 22 &&
                x >= w->x + hs_set_k(w, 16) && x <= w->x + w->width - hs_set_k(w, 16)) {
                g_shell.ui.cursor_theme = g_shell.ui.cursor_theme ? 0 : 1;
                { extern void cursor64_set_theme(int t);
                  cursor64_set_theme(g_shell.ui.cursor_theme); }
                g_content_dirty = 1;
                extern void hs_display_save(void);
                hs_display_save();
                vga_printf("[SETTINGS] cursor theme = %d\n", g_shell.ui.cursor_theme);
                return;
            }

            /* V0.54.7: Appearance 页色板点击 — 取色器应用源色 */
            if (g_settings_page == 0) {
                int sw_x = w->x + 156;       /* rx = cx+140 = w->x+16+140 */
                int sw_y = w->y + 112;       /* cy+30+24+22 = w->y+36+76 */
                for (int i = 0; i < HS_THEME_PRESET_COUNT; i++) {
                    int bx = sw_x + i * 28;
                    if (x >= bx && x <= bx + 20 && y >= sw_y && y <= sw_y + 20) {
                        hpdesk_set_theme(g_shell.theme);  /* 重置为当前主题默认色板 */
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
                /* V0.55.0: 桌面背景色 3x3 色板点击 */
                for (int i = 0; i < 9; i++) {
                    int bx = sw_x + 76 + (i % 3) * 22;
                    int by = w->y + 208 + (i / 3) * 22;
                    if (x >= bx && x <= bx + 16 && y >= by && y <= by + 16) {
                        g_shell.colors.bg = g_color_presets[i];
                        g_wp_sel = 5;
                        hs_save_bg_color(g_color_presets[i]);
                        /* 必须强制全量重绘: 局部路径只重铺脏区壁纸, 屏幕其余部分会留着
                         * 旧底色, 表现为"设置里选了 A 桌面还是 B"或出现色块接缝。 */
                        g_content_dirty = 1;
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
        g_island_target = g_island_expanded ? 100 : 0;   /* P0: 触发缓动 */
        hpdesk_mark_dirty();
        return;
    }

    /* 侧边栏标签点击切换 */
    int sx = hs_sx(g_shell.ui.sidebar_x);
    int sy = hs_sy(g_shell.ui.sidebar_y);
    int sw = hs_sx(g_shell.ui.sidebar_width);
    /* 侧栏收起/展开命中（必须先判，否则会落到下面的 tab 区域里） */
    {
        int shh = hs_sy(g_shell.ui.sidebar_height);
        int sww = hs_sx(g_shell.ui.sidebar_width);
        int max_sh = g_shell.screen_height - sy - hs_sy(16);
        if (max_sh > 0 && shh > max_sh) shh = max_sh;
        int max_sw = g_shell.screen_width * 2 / 5;
        if (max_sw > 0 && sww > max_sw) sww = max_sw;
        if (g_sidebar_collapsed) {
            int hw = hs_sx(14), hh = hs_sy(96);
            int hy = sy + shh / 2 - hh / 2;
            if (x >= sx && x <= sx + hw && y >= hy && y <= hy + hh) {
                g_sidebar_collapsed = 0;
                hs_save_sidebar_collapsed(0);
                hpdesk_mark_dirty();
                return;
            }
        } else if (hs_panel_is_enabled("sidebar")) {
            int bw = hs_sx(20), bh = hs_sy(20);
            int bx = sx + sww - hs_sx(26);
            int byy = sy + hs_sy(12) + hs_sy(4);
            if (x >= bx && x <= bx + bw && y >= byy && y <= byy + bh) {
                g_sidebar_collapsed = 1;
                hs_save_sidebar_collapsed(1);
                hpdesk_mark_dirty();
                return;
            }
        }
    }
    int tab_y = sy + hs_sy(12);
    int tab_h = hs_sy(28);
    int tab_w = (sw - hs_sx(24) - hs_sx(28)) / 2;
    if (y >= tab_y && y <= tab_y + tab_h) {
        for (int i = 0; i < 2; i++) {
            int tx = sx + hs_sx(12) + i * tab_w;
            if (x >= tx && x <= tx + tab_w - hs_sx(4)) {
                g_sidebar_tab = i;
                g_sidebar_scroll = 0;   /* V0.55: 切页重置滚动 */
                /* V0.57.0: 切页必须显式请求重绘 —— 按需合成下不然页面外观不变,
                 * 看起来就像"点了标签没反应"。 */
                hpdesk_mark_dirty();
                return;
            }
        }
    }

    /* V0.56.2: Apps 页图标点击。
     * 之前该页只有渲染没有命中测试 —— 图标看得见, 点上去毫无反应,
     * 这是"鼠标点了没反应"的真实缺陷之一。坐标复用渲染所用的
     * hs_apps_cell_pos(), 避免两套公式漂移。 */
    if (g_sidebar_tab == 0 && hs_panel_is_enabled("sidebar")) {
        for (int i = 0; i < g_shell.app_count; i++) {
            int ax = 0, ay = 0;
            if (!hs_apps_cell_pos(i, &ax, &ay)) continue;
            if (x >= ax - hs_sx(4) && x <= ax + hs_s(48) + hs_sx(4) &&
                y >= ay - hs_sy(4) && y <= ay + hs_sy(70)) {
                static const int app_map[13] = {
                    HS_APP_TERMINAL, HS_APP_FILES, HS_APP_BROWSER, HS_APP_SETTINGS,
                    HS_APP_EDITOR, HS_APP_CALCULATOR, HS_APP_NOTES, HS_APP_TASK,
                    HS_APP_ABOUT, HS_APP_ABOUT, HS_APP_DEVMGR, HS_APP_DISKMGR, HS_APP_LOGVIEW
                };
                g_pending_app = (i < 13) ? app_map[i] : HS_APP_ABOUT;
                hpdesk_mark_dirty();
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
/* V0.57.0: 悬停目标编号 —— 0..2 侧栏标签, 100+i Apps 图标, -1 无。
 * 用途: 只有在悬停目标变化时才请求重绘, 既保住按需渲染的收益(不再全屏 60fps 重画),
 * 又让悬停高亮真的能出现 —— 之前 update_mouse 完全不 mark_dirty,
 * 按需合成下"移动指针"不会触发任何重绘, 悬停状态永远刷不出来。 */
static int hs_hover_id(int x, int y)
{
    if (!hs_panel_is_enabled("sidebar")) return -1;
    int sx = hs_sx(g_shell.ui.sidebar_x), sy = hs_sy(g_shell.ui.sidebar_y);
    int sw = hs_sx(g_shell.ui.sidebar_width), sh = hs_sy(g_shell.ui.sidebar_height);
    if (x < sx || x > sx + sw || y < sy || y > sy + sh) return -1;

    int tab_y = sy + hs_sy(12), tab_h = hs_sy(28), tab_w = (sw - hs_sx(24)) / 2;
    if (y >= tab_y && y <= tab_y + tab_h) {
        for (int i = 0; i < 2; i++) {
            int tx = sx + hs_sx(12) + i * tab_w;
            if (x >= tx && x <= tx + tab_w - hs_sx(4)) return i;
        }
        return -1;
    }
    if (g_sidebar_tab == 0) {
        for (int i = 0; i < g_shell.app_count; i++) {
            int ax = 0, ay = 0;
            if (!hs_apps_cell_pos(i, &ax, &ay)) continue;
            if (x >= ax && x < ax + hs_s(48) && y >= ay && y < ay + hs_sy(70)) return 100 + i;
        }
    }
    return -1;
}

void hpdesk_update_mouse(int x, int y)
{
    g_shell.mouse_x = x;
    g_shell.mouse_y = y;
    /* 窗口拖拽移动 */
    hs_window_drag_move(x, y);
    /* Files 列表项拖拽(越过阈值才真正进入拖拽态) */
    {
        extern void hs_files_drag_move(int x, int y);
        hs_files_drag_move(x, y);
    }

    /* V0.57.0: 悬停目标变化 → 触发一次重绘 */
    static int last_hover = -2;
    int now_hover = hs_hover_id(x, y);
    /* 指针处于侧栏内时也持续重绘: 光线/时钟之外的"指针在面板上"这一状态本身就是脏的 */
    int in_sidebar = (now_hover != -1);
    if (now_hover != last_hover || in_sidebar) {
        last_hover = now_hover;
        hpdesk_mark_dirty();
    }
}

/* 鼠标按下：检测窗口标题栏启动拖拽 */
void hpdesk_mouse_down(int x, int y)
{
    /* V0.55 P3-GUI: 首启向导期间禁止拖拽桌面窗口 */
    {
        extern int hs_oobe_active(void);
        if (hs_oobe_active()) return;
    }
    /* 顶栏右上角齿轮 = 控制中心开关。齿轮是从右数第二个图标(power 最右)。
     * 命中区必须跟 hs_top_bar_render 里同一套几何算, 否则缩放后点不中。 */
    {
        int margin = hs_sx(10);
        int icon_size = hs_s(24);
        int power_x = g_shell.screen_width - margin - hs_sx(14) - icon_size;
        int gear_x = power_x - hs_sx(6) - icon_size;
        int bar_top = hs_sy(8);
        int bar_h = hs_sy(g_shell.ui.bar_height);
        int gear_y = bar_top + (bar_h - icon_size) / 2;
        if (x >= gear_x && x <= gear_x + icon_size &&
            y >= gear_y && y <= gear_y + icon_size) {
            g_cc_open = !g_cc_open;
            g_content_dirty = 1;
            return;
        }
    }
    /* Files 列表项拖拽: 必须命中最顶层窗口是 Files, 否则被压住的窗口会"隔空"接管 */
    {
        extern hs_window_t *hs_window_at(int x, int y);
        extern int hs_files_drag_begin(int x, int y);
        hs_window_t *tw = hs_window_at(x, y);
        if (tw && tw->title[0] == 'F' && strcmp(tw->title, "Files") == 0) {
            g_files_drag_consumed = 0;
            if (hs_files_drag_begin(x, y)) return;
        }
    }
    for (int zi = g_wm.z_count - 1; zi >= 0; zi--) {
        int id = g_wm.z_order[zi];
        if (id < 0 || id >= g_wm.window_count) continue;
        hs_window_t *w = &g_wm.windows[id];
        if (!w->visible || w->type != HS_WINDOW_FLOATING) continue;
        /* 命中区必须与绘制用同一套缩放, 否则放大后按钮画得大、点得小 */
        if (x >= w->x && x <= w->x + w->width &&
            y >= w->y && y <= w->y + hs_sy(24)) {
            /* Files 多标签: 标签就画在标题栏里, 命中优先于"拖窗口/双击最大化" */
            if (strcmp(w->title, "Files") == 0) {
                extern int hs_files_tab_click(int x, int y);
                if (hs_files_tab_click(x, y)) return;
            }
            /* V0.59.4: 窗口内容区的点击派发。原来只有标题栏有处理, 内容区
             * 的点击直接落到下层 —— 任务管理器的标签页点了没反应。 */
            if (x >= w->x && x <= w->x + w->width &&
                y > w->y + hs_sy(24) && y <= w->y + w->height) {
                if (strcmp(w->title, "Task Manager") == 0) {
                    extern int hs_taskmgr_click(int x, int y);
                    if (hs_taskmgr_click(x, y)) return;
                }
            }

            /* V0.13.16: 标题栏三按钮: 最小化/最大化/关闭 (无背景) */
            int bw24 = hs_sx(24);
            int min_x = w->x + w->width - hs_sx(68);
            int max_x = w->x + w->width - hs_sx(44);
            int close_x = w->x + w->width - hs_sx(20);
            if (x >= min_x && x <= min_x + bw24) {
                hs_window_minimize(w);
                return;
            }
            if (x >= max_x && x <= max_x + bw24) {
                hs_window_toggle_maximize(w);
                return;
            }
            if (x >= close_x && x <= close_x + bw24) {
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
void hpdesk_mouse_up(int x, int y)
{
    hpdesk_mark_dirty();   /* P0: 交互发生, 内容需重绘 */
    {
        extern int hs_files_drag_end(int x, int y);
        if (hs_files_drag_end(x, y)) g_files_drag_consumed = 1;
    }
    hs_window_drag_end();
}

/* ============================================================================
 * 主渲染
 * ========================================================================== */

extern void gfx64_set_target_back(void);
extern void gfx64_present(void);
extern void cursor64_draw(int x, int y);
extern int cursor64_load_file(const char *path, int want);
extern int cursor64_ready(void);
extern int cursor64_nominal(void);
extern int cursor64_hotspot_x(void);
extern int cursor64_hotspot_y(void);
extern int cursor64_pick_size(int w, int h);

/* ============================================================================
 * V0.55.0 P0: 按需合成（离屏缓冲按需提交 + 光标独立层）
 *
 * 旧行为: 每帧无条件把壁纸/面板/窗口/文字全量重画到后台缓冲再 present,
 *         60fps 下即使画面完全静止也在满负荷跑软件光栅。
 * 新行为:
 *   1) 内容只在 dirty(交互/动画/兜底节流) 时重绘到后台缓冲;
 *   2) 提交(present) 每帧把后台内容整块拷到前台, 顺带覆盖上一帧光标残影;
 *   3) 光标只画在前台, 不进入后台缓冲 —— 鼠标移动不再触发内容重绘;
 *   4) 内容不动且鼠标不动时直接 return, 本轮零绘制。
 * ========================================================================== */
static int g_content_dirty = 1;
static int g_last_mx = -1, g_last_my = -1;
/* P0 观测: 重绘/跳过帧计数, 用于证明按需合成生效 */
static uint32_t g_redraw_frames = 0, g_skip_frames = 0;
static uint32_t g_cursor_frames = 0;   /* P0: 只刷光标的帧数(未整帧重绘) */
/* P0 演示: 启动时灵动岛展开->收回一次, 展示缓动 */
static int g_welcome_frame = -1;

/* 供 hpdesk_mark_dirty 遍历窗口用 */
hs_window_t *hs_wm_windows(int *count)
{
    if (count) *count = g_wm.window_count;
    return g_wm.windows;
}

void hpdesk_mark_dirty(void)
{
    g_content_dirty = 1;
    /* 全局"有东西变了"无法定位到具体窗口, 保守起见把所有窗口标脏。 */
    extern hs_window_t *hs_wm_windows(int *count);
    int n = 0;
    hs_window_t *ws = hs_wm_windows(&n);
    for (int i = 0; i < n; i++) ws[i].content_dirty = 1;
}

/* V0.55.0 P0: PNG 解码链路自检 (ext2 读取 -> pmm 分配 -> inflate -> 反滤波)
 * 首次渲染时跑一次, 串口打印尺寸与首像素, 供无人值守验证使用 */
static void hs_png_selfcheck(void)
{
    static int done = 0;
    if (done) return;
    done = 1;
    extern int ext2_64_read_file(const char *path, uint8_t *buffer, uint32_t max_size);
    static uint8_t raw[131072];
    int n = ext2_64_read_file("/usr/share/samples/sample.png", raw, sizeof(raw));
    if (n <= 0) {
        vga_puts("[PNG64] no sample image\n");
        return;
    }
    vga_printf("[PNG64] read n=%d sig=%d,%d,%d,%d\n",
               n, raw[0], raw[1], raw[2], raw[3]);
    png64_image_t img;
    img.width = img.height = 0;
    img.rgba = 0;
    int rc = png64_decode(raw, n, &img);
    if (rc != 0 || !img.rgba || img.width == 0) {
        vga_printf("[PNG64] decode FAILED rc=%d\n", rc);
        return;
    }
    vga_printf("[PNG64] decoded %dx%d px0=%d,%d,%d,%d\n",
               (int)img.width, (int)img.height,
               img.rgba[0], img.rgba[1], img.rgba[2], img.rgba[3]);
}

/* ============================================================================
 * 右键上下文菜单(V0.57.1)
 *
 * 此前 right 位从 usb/xhci/ps2 三路都读出来了, 但没有任何消费者 —— 全系统零右键。
 * 这里落一个通用小浮层: 调用方给标签与 id, 菜单负责定位(贴边翻转)、渲染、
 * 命中与关闭, 点击派发回各自的 owner。
 * ========================================================================== */
static int hs_ctx_item_h(void) { return hs_sy(24); }

void hs_ctx_close(void)
{
    if (!g_ctx.open) return;
    g_ctx.open = 0;
    g_content_dirty = 1;
}

void hs_ctx_open(int x, int y, const char *const *labels, const int *ids, int n, int owner)
{
    if (n <= 0) return;
    if (n > HS_CTX_MAX) n = HS_CTX_MAX;
    int maxlen = 0;
    for (int i = 0; i < n; i++) {
        /* hpdesk.c 里没有 hs_strcpyn(它是 hs_apps.c 的 static), 就地按长度拷贝 */
        int k = 0;
        while (labels[i][k] && k < 27) { g_ctx.label[i][k] = labels[i][k]; k++; }
        g_ctx.label[i][k] = 0;
        g_ctx.id[i] = ids[i];
        int l = (int)strlen(labels[i]);
        if (l > maxlen) maxlen = l;
    }
    g_ctx.count = n;
    g_ctx.owner = owner;
    g_ctx.w = maxlen * hs_s(7) + hs_s(26);
    g_ctx.h = n * hs_ctx_item_h() + hs_sy(8);
    /* 贴边翻转: 右/下空间不足时向左/向上展开, 保证整块菜单在屏内 */
    int sw = g_shell.screen_width, shh = g_shell.screen_height;
    g_ctx.x = x;
    g_ctx.y = y;
    if (g_ctx.x + g_ctx.w > sw - hs_s(4)) g_ctx.x = x - g_ctx.w;
    if (g_ctx.y + g_ctx.h > shh - hs_s(4)) g_ctx.y = y - g_ctx.h;
    if (g_ctx.x < hs_s(4)) g_ctx.x = hs_s(4);
    if (g_ctx.y < hs_s(4)) g_ctx.y = hs_s(4);
    g_ctx.open = 1;
    g_content_dirty = 1;
    vga_printf("[CTX] open n=%d at %d,%d w=%d h=%d owner=%d\n", n, g_ctx.x, g_ctx.y, g_ctx.w, g_ctx.h, owner);
}

int hs_ctx_is_open(void) { return g_ctx.open; }

/* 返回菜单项下标, -1 = 没点在菜单上 */
static int hs_ctx_hit_item(int x, int y)
{
    if (!g_ctx.open) return -1;
    if (x < g_ctx.x || x > g_ctx.x + g_ctx.w) return -1;
    if (y < g_ctx.y || y > g_ctx.y + g_ctx.h) return -1;
    int rel = y - g_ctx.y - hs_sy(4);
    if (rel < 0) return -1;
    int idx = rel / hs_ctx_item_h();
    if (idx < 0 || idx >= g_ctx.count) return -1;
    return idx;
}

static void hs_ctx_menu_render(void)
{
    if (!g_ctx.open) return;
    void *fb = gfx64_get_framebuffer();
    int W = g_shell.screen_width, H = g_shell.screen_height;
    int ih = hs_ctx_item_h();
    int mx = g_shell.mouse_x, my = g_shell.mouse_y;
    /* 两层暗边做投影(自研管线无模糊, 用叠加半透明矩形近似) */
    gfx64_fill_rounded_rect(g_ctx.x + hs_s(4), g_ctx.y + hs_sy(6), g_ctx.w, g_ctx.h, hs_s(8), 0x40000000);
    gfx64_fill_rounded_rect(g_ctx.x, g_ctx.y, g_ctx.w, g_ctx.h, hs_s(8), g_shell.colors.panel_bg);
    gfx64_draw_rounded_rect(g_ctx.x, g_ctx.y, g_ctx.w, g_ctx.h, hs_s(8), 0x44FFFFFF);
    for (int i = 0; i < g_ctx.count; i++) {
        int iy = g_ctx.y + hs_sy(4) + i * ih;
        int hov = (mx > g_ctx.x && mx < g_ctx.x + g_ctx.w && my >= iy && my < iy + ih);
        if (hov)
            gfx64_fill_rounded_rect(g_ctx.x + hs_s(4), iy, g_ctx.w - hs_s(8), ih, hs_s(4),
                                    g_shell.colors.accent);
        font64_draw_string(fb, W, H, g_ctx.x + hs_s(12), iy + hs_sy(5), g_ctx.label[i],
                           hov ? 0xFFFFFF : g_shell.colors.window_fg, hs_s(12));
    }
}

void hpdesk_right_click(int x, int y)
{
    vga_printf("[RCLICK] %d,%d open=%d\n", x, y, g_ctx.open);
    g_content_dirty = 1;
    if (g_ctx.open) { hs_ctx_close(); return; }

    /* Files 窗口: 文件操作菜单(内容随选中/剪贴板状态变化, 由 Files 自己构建) */
    {
        extern int hs_files_hit(int x, int y);
        extern int hs_files_open_ctx(int x, int y);
        if (hs_files_hit(x, y) && hs_files_open_ctx(x, y)) return;
    }
    /* 其它窗口标题栏: 给一个最小窗口菜单(关闭) */
    for (int zi = g_wm.z_count - 1; zi >= 0; zi--) {
        int id = g_wm.z_order[zi];
        if (id < 0 || id >= g_wm.window_count) continue;
        hs_window_t *w = &g_wm.windows[id];
        if (!w->visible || w->type != HS_WINDOW_FLOATING) continue;
        if (x < w->x || x > w->x + w->width || y < w->y || y > w->y + w->height) continue;
        static const char *labs[2] = {"Close window", "Cancel"};
        static const int ids[2] = {0, 1};
        hs_ctx_open(x, y, labs, ids, 2, 2);   /* owner 2 = 通用窗口 */
        return;
    }
    /* 桌面 */
    {
        static const char *labs[4] = {"Refresh desktop", "Open Files",
                                      "Display settings", "Task manager"};
        static const int ids[4] = {HS_CTX_D_REFRESH, HS_CTX_D_FILES,
                                   HS_CTX_D_SETTING, HS_CTX_D_TASK};
        hs_ctx_open(x, y, labs, ids, 4, 0);
    }
}

static int hs_cursor_kind_at(int x, int y)
{
    if (g_dragging) return CUR_MOVE;
    if (g_ctx.open && hs_ctx_hit_item(x, y) >= 0) return CUR_LINK;
    {
        extern int hs_oobe_active(void);
        if (hs_oobe_active()) return CUR_LINK;
    }
    {
        extern int hs_files_cursor_kind(int x, int y);
        int k = hs_files_cursor_kind(x, y);
        if (k >= 0) return k;
    }
    /* 其余窗口: 标题栏箭头, 窗口内容手型 —— 内容区里基本都是可点控件 */
    for (int zi = g_wm.z_count - 1; zi >= 0; zi--) {
        int id = g_wm.z_order[zi];
        if (id < 0 || id >= g_wm.window_count) continue;
        hs_window_t *w = &g_wm.windows[id];
        if (!w->visible || w->type != HS_WINDOW_FLOATING) continue;
        if (x < w->x || x > w->x + w->width || y < w->y || y > w->y + w->height) continue;
        if (y <= w->y + hs_sy(26)) return CUR_ARROW;
        return CUR_LINK;
    }
    return CUR_ARROW;
}

static void hs_cursor_init_once(void)
{
    static int inited = 0;
    if (inited) return;
    inited = 1;
    /* 指针尺寸跟随 UI 缩放(Windows 语义: 指针是 DIP 尺寸, 高 DPI 下应同步变大)。
     * 加载失败必须留痕 —— 旧实现静默回退内置位图, 看起来就像"光标包没生效"。 */
    int want = cursor64_pick_size(g_shell.screen_width, g_shell.screen_height);
    want = want * (g_ui_scale_pct > 0 ? g_ui_scale_pct : 100) / 100;
    extern int cursor64_load_kind(int kind, const char *path, int want);
    int rc  = cursor64_load_kind(CUR_ARROW, "/usr/share/cursors/default.xcur", want);
    /* 悬停形态沿用素材包自带的 link(一支竖直箭头) —— 自备的 hand.xcur 试过,
     * 按用户意见回退, 保持与其余 17 个素材同一套视觉 */
    int rc1 = cursor64_load_kind(CUR_LINK,  "/usr/share/cursors/link.xcur",    want);
    int rc2 = cursor64_load_kind(CUR_TEXT,  "/usr/share/cursors/text.xcur",    want);
    int rc3 = cursor64_load_kind(CUR_MOVE,  "/usr/share/cursors/move.xcur",    want);

    vga_printf("[CURSOR] load arrow=%d link=%d text=%d move=%d want=%d ready=%d nominal=%d hot=(%d,%d)\n",
               rc, rc1, rc2, rc3, want, cursor64_ready(), cursor64_nominal(),
               cursor64_hotspot_x(), cursor64_hotspot_y());
}

/* 是否有进行中的动画(有则必须持续重绘) */
static int hs_anim_active(void)
{
    if (g_island_t != g_island_target) return 1;
    if (g_menu_t != g_menu_target) return 1;
    if (g_color_t != g_color_target) return 1;
    for (int i = 0; i < g_wm.window_count; i++) {
        hs_window_t *w = &g_wm.windows[i];
        if (w->visible && w->anim_dur) return 1;
    }
    return 0;
}

/* 提交一帧: 后台内容→前台, 再在前台叠加光标 */
static void hs_present_with_cursor(int mx, int my)
{
    gfx64_present();
    gfx64_set_target_front();
    hs_cursor_init_once();
    { uint64_t k0 = hs_rdtsc();
      hs_ctx_menu_render();     /* 菜单与光标同为前台叠加层 */
      g_cyc_ctxmenu += hs_rdtsc() - k0; }
    {
        extern void cursor64_set_kind(int kind);
        extern void cursor64_set_theme(int t);
        extern void cursor64_set_scale(int pct);
        cursor64_set_theme(g_shell.ui.cursor_theme);
        cursor64_set_scale(g_ui_scale_pct);
        cursor64_set_kind(hs_cursor_kind_at(mx, my));
    }
    { uint64_t k0 = hs_rdtsc();
      cursor64_draw(mx, my);
      g_cyc_cursordraw += hs_rdtsc() - k0; }
}

/* 拖动自测(V0.59.5 验证脏区局部路径用)。默认关闭; 需要复验时把 0 改成 1:
 * 启动第45帧自动拖第一个浮动窗口 28 帧, 串口打印 dirty_region_frames。 */
#if 0
extern void hs_window_drag_start(int x, int y);
extern void hs_window_drag_move(int x, int y);
extern void hs_window_drag_end(void);
static int g_st_on = 0, g_st_step = 0, g_st_sx = 0, g_st_sy = 0;
void hs_drag_selftest(void)
{
    if (g_frame_no < 45) return;
    if (g_st_step >= 90) return;
    if (!g_st_on) {
        vga_printf("[ST] fno=%d wc=%d zc=%d\n", (int)g_frame_no, g_wm.window_count, g_wm.z_count);
        for (int i = 0; i < g_wm.window_count && i < 8; i++)
            vga_printf("[ST]  w%d type=%d vis=%d st=%d at %d,%d %dx%d\n", i,
                       (int)g_wm.windows[i].type, g_wm.windows[i].visible,
                       (int)g_wm.windows[i].state, g_wm.windows[i].x, g_wm.windows[i].y,
                       g_wm.windows[i].width, g_wm.windows[i].height);
        for (int i = 0; i < g_wm.window_count; i++) {
            hs_window_t *w = &g_wm.windows[i];
            if (w->visible && w->type == HS_WINDOW_FLOATING &&
                w->state != HS_WINDOW_STATE_MAXIMIZED) {
                g_st_on = 1;
                g_st_sx = w->x + 60;
                g_st_sy = w->y + 16;
                hs_window_drag_start(g_st_sx, g_st_sy);
                g_cyc_render_all = g_cyc_blend = g_cyc_wall = g_cyc_panels = g_cyc_cc = 0;
        g_cyc_notify = g_cyc_cursor = g_cyc_shadow = g_cyc_shadow_build = g_cyc_shadow_blit = g_cyc_memset = g_cyc_content = 0;
        g_cyc_bgfill = g_cyc_surfens = g_cyc_title = g_cyc_border = g_cyc_cursordraw = g_cyc_ctxmenu = g_cyc_loophdr = 0;
        gfx64_prof_present = 0;
        g_seg_lp = g_seg_pre; g_seg_lm = g_seg_mid; g_seg_lq = g_seg_post;
        g_t_drag_start = hs_rdtsc();
        g_partial_frames = 0; g_full_frames = 0; g_panel_skips = 0;
        {
            extern uint64_t g_cyc_net, g_cyc_dhcp, g_cyc_input;
            extern uint64_t g_cyc_render_call, g_cyc_loop, g_loop_iters;
            g_cyc_net = g_cyc_dhcp = g_cyc_input = 0;
            g_cyc_render_call = g_cyc_loop = g_loop_iters = 0;
        }
        vga_printf("[SELFTEST] drag begin win=%d from %d,%d\n", w->id, w->x, w->y);
                break;
            }
        }
        return;
    }
    g_st_step++;
    if (g_st_step <= 28) {
        g_st_sx += 10;
        hs_window_drag_move(g_st_sx, g_st_sy);
    } else if (g_st_step == 29) {
        hs_window_drag_end();
        {
        uint64_t span = hs_rdtsc() - g_t_drag_start;
        int nf = 28;
        uint32_t frameK  = (uint32_t)((span / nf) / 1000);
        uint32_t rendK   = (uint32_t)((g_cyc_render_all / nf) / 1000);
        uint32_t blendK  = (uint32_t)((g_cyc_blend / nf) / 1000);
        uint32_t presK   = (uint32_t)((gfx64_prof_present / nf) / 1000);
        /* V0.59.22: 旧式 otherK = frameK - rendK - presK 是错的 —— 它把壁纸/面板/
         * 控制中心/通知这些「不在 renderAll 区间内」的段全算进了"其它", 于是
         * 102K 的 otherK 里约 71% 其实是面板与壁纸绘制, 看起来像不明开销。
         * 也不能用 PROF2 的累加器(g_cyc_wall 等)去扣: 那是"blit 阶段"口径,
         * 与 PROF5 的 ts 时间段口径差一个量级(g_cyc_wall 1811 vs PROF5 wallK 19073)。
         * 正确做法: 用 PROF5 时间戳链的总和 g_ts6 作为"已测部分",
         * otherK 才是真正没被任何段覆盖的残差(循环轮询/调度/时间戳间隙)。 */
        uint32_t segK    = (uint32_t)(g_ts6 / 1000);
        uint32_t otherK  = (frameK > segK) ? (frameK - segK) : 0;
        vga_printf("[PROF] drag %d frames: frameK=%d renderAllK=%d blendK=%d presentK=%d segK=%d otherK=%d\n",
                   nf, frameK, rendK, blendK, presK, segK, otherK);
        vga_printf("[PROF0] partial_frames=%d full_frames=%d panel_skips=%d (per partial frame skips=%d)\n",
                   g_partial_frames, g_full_frames, g_panel_skips,
                   g_partial_frames ? g_panel_skips / g_partial_frames : 0);
        vga_printf("[PROF0] last_dirty x=%d y=%d w=%d h=%d band_blits=%d band_reason=%d\n",
                   g_last_dirty[0], g_last_dirty[1], g_last_dirty[2], g_last_dirty[3],
                   g_band_blits, g_band_reason);
        {
            extern void cc_geometry(int *cx, int *cy, int *cw, int *ch);
            int cx = 0, cy = 0, cw = 0, ch = 0;
            cc_geometry(&cx, &cy, &cw, &ch);
            int nwd = hs_sx(460), nhd = hs_sy(300);
            vga_printf("[PROF0] cc x=%d y=%d w=%d h=%d | notify x=%d y=%d %dx%d\n",
                       cx, cy, cw, ch, g_shell.screen_width - nwd, 0, nwd, nhd);
        }
        {
            /* V0.59.25: 把"真残差"按主循环分段拆开。这些计数从拖动开始清零,
             * 覆盖 28 帧拖动期间的全部循环轮次。 */
            extern uint64_t g_cyc_net, g_cyc_dhcp, g_cyc_input;
            extern uint64_t g_cyc_render_call, g_cyc_loop, g_loop_iters;
            if (g_loop_iters) {
                uint64_t n = g_loop_iters;
                vga_printf("[PROF7] loop_iters=%d loopK=%d netK=%d dhcpK=%d inK=%d renderK=%d\n",
                           (int)n,
                           (uint32_t)((g_cyc_loop / n) / 1000),
                           (uint32_t)((g_cyc_net / n) / 1000),
                           (uint32_t)((g_cyc_dhcp / n) / 1000),
                           (uint32_t)((g_cyc_input / n) / 1000),
                           (uint32_t)((g_cyc_render_call / n) / 1000));
            }
        }
        {
            uint32_t a1 = (uint32_t)((g_ts1) / 1000), a2 = (uint32_t)((g_ts2) / 1000);
            uint32_t a3 = (uint32_t)((g_ts3) / 1000), a4 = (uint32_t)((g_ts4) / 1000);
            uint32_t a5 = (uint32_t)((g_ts5) / 1000), a6 = (uint32_t)((g_ts6) / 1000);
            vga_printf("[PROF5] hdrK=%d wallK=%d panelK=%d winK=%d ccNotifyK=%d presentK=%d\n",
                       a1, a2 - a1, a3 - a2, a4 - a3, a5 - a4, a6 - a5);
        }
        vga_printf("[PROF4] preK=%d midK=%d postK=%d\n",
                   (uint32_t)(((g_seg_pre - g_seg_lp) / nf) / 1000),
                   (uint32_t)(((g_seg_mid - g_seg_lm) / nf) / 1000),
                   (uint32_t)(((g_seg_post - g_seg_lq) / nf) / 1000));
        vga_printf("[PROF3] bgfillK=%d surfensK=%d cursorK=%d ctxmenuK=%d\n",
                   (uint32_t)((g_cyc_bgfill / nf) / 1000), (uint32_t)((g_cyc_surfens / nf) / 1000),
                   (uint32_t)((g_cyc_cursordraw / nf) / 1000), (uint32_t)((g_cyc_ctxmenu / nf) / 1000));
        vga_printf("[PROF2] wallK=%d panelsK=%d shadowK=%d memsetK=%d contentK=%d ccK=%d notifyK=%d\n",
                   (uint32_t)((g_cyc_wall / nf) / 1000), (uint32_t)((g_cyc_panels / nf) / 1000),
                   (uint32_t)((g_cyc_shadow / nf) / 1000), (uint32_t)((g_cyc_memset / nf) / 1000),
                   (uint32_t)((g_cyc_content / nf) / 1000), (uint32_t)((g_cyc_cc / nf) / 1000),
                   (uint32_t)((g_cyc_notify / nf) / 1000));
        vga_printf("[PROF6] shadowBuildK=%d shadowBlitK=%d titleK=%d borderK=%d\n",
                   (uint32_t)((g_cyc_shadow_build / nf) / 1000),
                   (uint32_t)((g_cyc_shadow_blit / nf) / 1000),
                   (uint32_t)((g_cyc_title / nf) / 1000),
                   (uint32_t)((g_cyc_border / nf) / 1000));
        g_cyc_render_all = g_cyc_blend = 0; gfx64_prof_present = 0;
    }
    vga_printf("[SELFTEST] drag end, dirty_region_frames=%d surface_reuse=%d surface_repaint=%d\n",
                   (int)g_dirty_frames, g_surf_reuse, g_surf_repaint);
    }
}
#endif

static void hpdesk_render_inner(void)
{
    uint64_t __rs0 = hs_rdtsc();
    {
    }
    /* V0.13.11: 双缓冲原子帧 — 全部绘制到后台缓冲, 帧末一次性提交(消除撕裂/半帧) */
    g_frame_no++;

    /* 几何探针: 只打印第一帧。布局问题一直靠看截图估算, 而截图与配置值经常对不上,
     * 这里把每个面板的真实几何 + 当前缩放吐到串口, 后续改造全部以这些数字为准。 */
    if (g_frame_no == 1) {
        /* 真实逻辑画布 = 物理尺寸 ÷ 缩放因子(Windows DIP 语义)。
         * 旧实现按 "1024*100/pct" 算, 把 1024 基准当成物理宽, 打印出来是 819x614,
         * 而 1920x1080@125% 的实际逻辑画布是 1536x864 —— 这个误导曾导致
         * "逻辑宽装不下双栏"的错误结论(见 §1.51)。 */
        int lw = g_shell.screen_width * 100 / (g_ui_scale_pct > 0 ? g_ui_scale_pct : 100);
        int lh = g_shell.screen_height * 100 / (g_ui_scale_pct > 0 ? g_ui_scale_pct : 100);
        int sxs = hs_sx(g_shell.ui.sidebar_x), sys_ = hs_sy(g_shell.ui.sidebar_y);
        int sws = hs_sx(g_shell.ui.sidebar_width), shs = hs_sy(g_shell.ui.sidebar_height);
        int ccx = 0, ccy = 0, ccw = 0, cch = 0;
        cc_geometry(&ccx, &ccy, &ccw, &cch);
        vga_printf("[GEOM] pct=%d screen=%dx%d logical=%dx%d | sidebar x=%d y=%d w=%d h=%d | "
                   "cc x=%d y=%d w=%d h=%d | gap_x=%d\n",
                   g_ui_scale_pct, g_shell.screen_width, g_shell.screen_height, lw, lh,
                   sxs, sys_, sws, shs, ccx, ccy, ccw, cch, ccx - (sxs + sws));
        {
            int cap_w = g_shell.screen_width * 2 / 5;
            int cap_h = g_shell.screen_height - sys_ - hs_sy(16);
            vga_printf("[GEOM] theme accent=0x%x panel_bg=0x%x panel_fg=0x%x tab=%d\n",
                       g_shell.colors.accent, g_shell.colors.panel_bg,
                       g_shell.colors.panel_fg, g_sidebar_tab);
            vga_printf("[GEOM] sidebar clamped: w=%d h=%d right=%d bottom=%d (caps %d/%d)\n",
                       sws > cap_w ? cap_w : sws, shs > cap_h ? cap_h : shs,
                       sxs + (sws > cap_w ? cap_w : sws), sys_ + (shs > cap_h ? cap_h : shs),
                       cap_w, cap_h);
        }
    }
    /* P0 演示: 启动第 30 帧起展开灵动岛, 第 120 帧收回 (展示 ease-out 缓动) */
    if (g_welcome_frame >= 0) {
        g_welcome_frame++;
        if (g_welcome_frame == 30) { g_island_expanded = 1; g_island_target = 100; }
        if (g_welcome_frame == 120) { g_island_expanded = 0; g_island_target = 0; }
        if (g_welcome_frame == 150) { g_welcome_frame = -1; }
    } else if (g_frame_no == 1) {
        g_welcome_frame = 0;
    }
    hs_island_tick();
    hs_pop_tick(&g_menu_t, &g_menu_target);
    hs_pop_tick(&g_color_t, &g_color_target);

    int mx = g_shell.mouse_x, my = g_shell.mouse_y;
    int mouse_moved = (mx != g_last_mx || my != g_last_my);
    int animating = hs_anim_active();
    /* 兜底: 每 60 帧(约1s@60fps) 强制全量一次, 防止漏标 dirty 造成画面停更 */
    if ((g_frame_no % 30) == 0) g_content_dirty = 1;

    /* 完全静止: 不重绘、不提交、不画光标 */
#if 0   /* 必须在跳帧判断之前 —— 静止桌面每帧 return, 放后面则永远轮不到它产生第一个脏区 */
    {
        extern void hs_drag_selftest(void);
        hs_drag_selftest();
    }
#endif

    /* 有脏矩形时不能跳帧(否则局部路径永远轮不到) */
    if (!g_content_dirty && !animating && !mouse_moved && g_dirty_x1 < 0) {
        g_skip_frames++;
        if (g_redraw_frames + g_skip_frames >= 1800) {
            vga_printf("[P0] frames: redraw=%d skip=%d skip_pct=%d\n",
                       (int)g_redraw_frames, (int)g_skip_frames,
                       (int)(g_skip_frames * 100 / (g_redraw_frames + g_skip_frames + 1)));
            vga_printf("[P0] cursor-only frames=%d\n", (int)g_cursor_frames);
            vga_printf("[P0] dirty-region frames=%d\n", (int)g_dirty_frames);
            g_dirty_frames = 0;

            g_cursor_frames = 0;
            g_redraw_frames = g_skip_frames = 0;
        }
        return;
    }
    /* P0 脏区: 光标局部刷新快速路径。
     * 鼠标每移动一次都会进重绘, 原实现要重画壁纸+全部窗口+面板, 再整帧拷 8MB(1920x1080x4),
     * 目的只是把一个 64x64 的光标挪个位置。这里只做三件事:
     *   1) 用后台缓冲(干净桌面, 不含光标/菜单)盖掉旧光标矩形
     *   2) 同理铺一下新位置
     *   3) 只在前台画光标
     * 前提: 无内容变化、无动画、右键菜单未展开(g_ctx.open 时菜单在 as 前台叠加层, 不能只刷光标)。
     * 光标最大 64x64(CUR_MAX_DIM), 150% 缩放约 96, 取 128 留余量。 */
    if (!g_content_dirty && !animating && mouse_moved && !g_ctx.open) {
        const int curb = 128, cboff = 16;
        int ox = g_last_mx - cboff, oy = g_last_my - cboff;
        int nx = mx - cboff, ny = my - cboff;
        if (g_last_mx >= 0)
            gfx64_present_region(ox, oy, curb, curb);   /* 擦旧光标 */
        gfx64_present_region(nx, ny, curb, curb);       /* 铺新背景 */
        gfx64_set_target_front();
        hs_cursor_init_once();
        {
            extern void cursor64_set_kind(int kind);
            extern void cursor64_set_theme(int t);
            extern void cursor64_set_scale(int pct);
            cursor64_set_theme(g_shell.ui.cursor_theme);
            cursor64_set_scale(g_ui_scale_pct);
            cursor64_set_kind(hs_cursor_kind_at(mx, my));
        }
        cursor64_draw(mx, my);
        g_last_mx = mx; g_last_my = my;
        g_cursor_frames++;
        if (g_cursor_frames == 1)
            vga_printf("[P0] cursor-only fast path active (x=%d y=%d)\n", mx, my);
        return;
    }
    g_redraw_frames++;

    /* P0 脏区局部路径: 无内容变化、无动画, 但有脏矩形(典型=拖动窗口) ——
     * 只重铺脏区壁纸 + 重画面板/窗口(与脏区不相交的窗口内部会跳过), present 只提脏区。
     * 前提是脏区外的后台缓冲与前台一致(上一次提交是全量或同样只提脏区, 保持成立)。 */
    {
        int dx_ = 0, dy_ = 0, dw_ = 0, dh_ = 0;
        if (!g_content_dirty && !animating && hs_dirty_rect(&dx_, &dy_, &dw_, &dh_)) {
            gfx64_set_target_back();
            g_in_partial_frame = 1; g_partial_frames++;
            g_last_dirty[0] = dx_; g_last_dirty[1] = dy_; g_last_dirty[2] = dw_; g_last_dirty[3] = dh_;
            g_ts1 = hs_rdtsc() - __rs0;
            hs_wallpaper_render_rect(dx_, dy_, dw_, dh_);
            g_ts2 = hs_rdtsc() - __rs0;
            hs_panel_render_all();
            g_ts3 = hs_rdtsc() - __rs0;
            hs_window_render_all();
            g_ts4 = hs_rdtsc() - __rs0;
            if (g_shell.panel_registry[HS_PANEL_CONTROL_CENTER].enabled && g_cc_open) {
                int ccx = 0, ccy = 0, ccw = 0, cch = 0;
                cc_geometry(&ccx, &ccy, &ccw, &cch);
                hs_layer_draw(&g_ly_cc, ccx, ccy, ccw, cch, hs_control_center_render,
                              !(g_in_partial_frame && g_ly_cc.valid));
            }
            {
                int nw = hs_sx(460), nh = hs_sy(300);
                hs_layer_draw(&g_ly_notify, g_shell.screen_width - nw, 0, nw, nh,
                              hs_notification_render, !(g_in_partial_frame && g_ly_notify.valid));
            }
            g_ts5 = hs_rdtsc() - __rs0;
            gfx64_present_region(dx_, dy_, dw_, dh_);
            g_ts6 = hs_rdtsc() - __rs0;
            gfx64_set_target_front();
            hs_cursor_init_once();
            {
                extern void cursor64_set_kind(int kind);
                extern void cursor64_set_theme(int t);
                extern void cursor64_set_scale(int pct);
                cursor64_set_theme(g_shell.ui.cursor_theme);
                cursor64_set_scale(g_ui_scale_pct);
                cursor64_set_kind(hs_cursor_kind_at(mx, my));
            }
            cursor64_draw(mx, my);
            g_last_mx = mx; g_last_my = my;
            hs_dirty_reset();
            g_in_partial_frame = 0;
            g_dirty_frames++;
            return;
        }
    }
    hs_dirty_reset();   /* 走全量路径时脏区已被整屏覆盖, 清掉避免累积 */

    if (g_content_dirty || animating) {
    g_full_frames++;
    hs_png_selfcheck();
    gfx64_set_target_back();

    /* 检测g_shell是否被野指针写入破坏 */
    if (g_shell.canary_start != 0xDEADBEEF || g_shell.canary_end != 0xCAFEBABE) {
        extern void vga_puts(const char *s);
        vga_puts("[HPDESK] g_shell corruption detected! reinit\n");
        hpdesk_init(g_shell.screen_width, g_shell.screen_height);
    }

    /* 每帧重新初始化面板注册表，防止野指针破坏render函数指针 */
    hs_panel_registry_init();

    /* 锁屏/登录界面优先 */
    if (g_locked == 1) {
        hs_lock_screen_render();
        hs_present_with_cursor(mx, my);
        g_content_dirty = 0; g_last_mx = mx; g_last_my = my;
        return;
    }
    if (g_locked == 2) {
        hs_login_screen_render();
        hs_present_with_cursor(mx, my);
        g_content_dirty = 0; g_last_mx = mx; g_last_my = my;
        return;
    }

    g_seg_pre += hs_rdtsc() - __rs0;
    { uint64_t w0 = hs_rdtsc();
      hs_wallpaper_render();
      g_cyc_wall += hs_rdtsc() - w0; }
    /* V0.56: 切壁纸后首帧 — 壁纸绘制完成即采样提取源色 (窗口/面板尚未绘制) */
    if (g_wallpaper_dirty) {
        g_wallpaper_dirty = 0;
        hs_wallpaper_sample_theme();
    }

    /* 通过面板注册表渲染所有启用的面板 */
    { uint64_t p0 = hs_rdtsc();
      hs_panel_render_all();
      g_cyc_panels += hs_rdtsc() - p0; }

    hs_window_render_all();
    /* 控制中心绘制在窗口之上，避免被最大化窗口遮挡 */
    if (g_shell.panel_registry[HS_PANEL_CONTROL_CENTER].enabled && g_cc_open) {
        uint64_t cc0 = hs_rdtsc();
        hs_control_center_render();
        g_cyc_cc += hs_rdtsc() - cc0;
    }
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
    {
        /* P0: CPU/内存/USB 等状态数值 0.5s 采样一次, 避免每帧无效重绘 */
        static uint32_t last_status_frame = 0;
        if (g_frame_no - last_status_frame >= 30) {
            last_status_frame = g_frame_no;
            hs_status_update();
            g_content_dirty = 1;
        }
    }
    hs_notification_render();
    /* V0.13.39: 网络就绪后自动检查驱动仓库(系统更新通知) */
    {
        extern int dhcp64_is_bound(void);
        static int up_checked = 0;
        /* V0.59.15: 这里在渲染路径里做同步网络 I/O —— 启动期一旦网络就绪, 这一帧
         * 就被卡住(原 15 秒)。两处改动: 挪到启动 30 秒之后(错开桌面首屏), 超时降 3 秒
         * (见 drv_mgr_update)。彻底异步需要把 HTTP 拆成状态机, 留到后面做。 */
        if (!up_checked && dhcp64_is_bound() && g_frame_no > 1800) {
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

    /* V0.55.0: 桌面配色面板 */
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

    } /* end of: 内容层重绘 (dirty/动画时) */

    /* 提交: 后台内容→前台, 光标单独叠加在前台, 不污染后台缓冲 */
    g_seg_mid += hs_rdtsc() - __rs0;
    hs_present_with_cursor(mx, my);
    g_content_dirty = 0;
    g_last_mx = mx;
    g_last_my = my;
    g_seg_post += hs_rdtsc() - __rs0;
}

/* V0.59.13 分段计时包装: 整帧 / render_all / blend / present 各自 cycles。
 * 每 60 帧打印一次(单位: 千周期), 用于定位到底哪一段在吃时间。 */
void hpdesk_render(void)
{
    uint64_t t0 = hs_rdtsc();
    hpdesk_render_inner();
    g_cyc_panel += hs_rdtsc() - t0;
    g_prof_frames++;
}


/* ============================================================================
 * 键盘处理
 * ========================================================================== */

void hpdesk_handle_key(int key)
{
    hpdesk_mark_dirty();   /* P0: 交互发生, 内容需重绘 */
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
        hpdesk_set_theme(g_shell.theme);  /* 重置为当前主题默认色板 */
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
        g_sidebar_tab = (g_sidebar_tab + 1) % 2;
        g_sidebar_scroll = 0;   /* V0.55: 切页重置滚动 */
        return;
    }

    /* 工作区切换 Alt+1..9 简化为数字键 */
    if (key >= '1' && key <= '9') {
        hpdesk_switch_workspace(key - '1');
        return;
    }

    /* Super/Win 键切换启动器（简化为空格） */
    if (key == ' ') {
        hpdesk_toggle_launcher();
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

int hpdesk_init(int width, int height)
{
    g_wp_sel = hs_wp_read_cfg();
    spinlock_init(&g_wm_lock);
    g_shell.canary_start = 0xDEADBEEF;
    g_shell.canary_end = 0xCAFEBABE;
    g_shell.initialized = 0;
    g_shell.screen_width = width;
    g_shell.screen_height = height;
    /* 自适应缩放因子（横向基准 1024 / 纵向预算 720，与 hs_display_auto_scale 同源） */
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
    hpdesk_set_theme(HS_THEME_DARK);
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
    g_shell.ui.cc_width = 280;
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
    g_shell.ui.corner_radius = 12;
    g_shell.ui.window_shadow = 1;
    g_shell.ui.cursor_theme = 0;
    g_shell.ui.font_small = 12;
    g_shell.ui.font_normal = 14;
    g_shell.ui.font_large = 18;

    /* 初始化面板（配置加载会覆盖） */
    hs_panel_init(&g_shell.top_panel, HS_PANEL_TOP, HS_PANEL_HEIGHT);
    hs_panel_init(&g_shell.bottom_panel, HS_PANEL_BOTTOM, HS_PANEL_HEIGHT);

    /* 从配置加载主题和面板 */
    hpdesk_load_config(NULL);

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
    hpdesk_add_app("Terminal", "terminal", "[>]");
    hpdesk_add_app("Files", "files", "[/]");
    hpdesk_add_app("Browser", "browser", "[@]");
    hpdesk_add_app("Settings", "settings", "[*]");
    hpdesk_add_app("Editor", "editor", "[~]");
    hpdesk_add_app("Calculator", "calc", "[=]");
    hpdesk_add_app("Notes", "notes", "[#]");
    hpdesk_add_app("Task Manager", "taskmgr", "[!]");
    hpdesk_add_app("App Store", "store", "[$]");
    hpdesk_add_app("About", "about", "[?]");
    /* V0.55: 系统工具 */
    hpdesk_add_app("Device Manager", "devmgr", "[D]");
    hpdesk_add_app("Disk Manager", "diskmgr", "[S]");
    hpdesk_add_app("Log Viewer", "logview", "[L]");

    g_shell.initialized = 1;

    /* V0.59.3: 启动即执行 /etc/hepushell/autostart.cmd（自动化取证通道） */
    {
        extern int ext2_64_read_file(const char *path, uint8_t *out, uint32_t max);
        static char abuf[256];
        int an = ext2_64_read_file("/etc/hepushell/autostart.cmd", (uint8_t *)abuf, 255);
        if (an > 0) {
            /* 支持多行: 一行一条命令, 方便一次取证多条 */
            abuf[an] = 0;
            for (int i = 0; i < an; i++) if (abuf[i] == '\r') abuf[i] = '\n';
            int pos = 0;
            while (pos < an) {
                int q = pos;
                while (q < an && abuf[q] != '\n') q++;
                if (q < an) abuf[q] = 0;
                if (abuf[pos]) {
                    vga_puts("[Autostart] "); vga_puts(abuf + pos); vga_puts("\n");
                    hs_terminal_run_line(abuf + pos);
                }
                pos = q + 1;
            }
        }
    }

    vga_puts("[Hepu Desktop] Initialized (");
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

int hpdesk_get_tick(void) { return g_frame_tick; }

void hpdesk_run(void)
{
    if (!g_shell.initialized) return;

    /* 主循环由外部调用渲染和键盘处理 */
    g_frame_tick++;
    hpdesk_render();
}

/* 重新加载配置 */
int hpdesk_reload(void)
{
    if (!g_shell.initialized) return -1;

    vga_puts("[Hepu Desktop] Reloading configuration...\n");

    /* 重置面板 */
    hs_panel_init(&g_shell.top_panel, HS_PANEL_TOP, HS_PANEL_HEIGHT);
    hs_panel_init(&g_shell.bottom_panel, HS_PANEL_BOTTOM, HS_PANEL_HEIGHT);

    /* 重新加载配置 */
    int result = hpdesk_load_config(NULL);

    if (result == 0) {
        vga_puts("[Hepu Desktop] Configuration reloaded successfully\n");
    } else {
        vga_puts("[Hepu Desktop] Configuration reload failed, using defaults\n");
        /* 回退到默认小部件 */
        hpdesk_add_widget(HS_PANEL_TOP, HS_WIDGET_WORKSPACES);
        hpdesk_add_widget(HS_PANEL_TOP, HS_WIDGET_CPU);
        hpdesk_add_widget(HS_PANEL_TOP, HS_WIDGET_MEMORY);
        hpdesk_add_widget(HS_PANEL_BOTTOM, HS_WIDGET_CLOCK);
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
    hs_notification_send("Welcome to Hepu OS", "Hepu Desktop loaded", 0, 5000);
    /* SB-22 实证：启动期累计的跨核 TLB shootdown 次数 */
    {
        extern uint64_t vmm64_tlb_shootdown_count(void);
        extern void vga_puts(const char *);
        extern void vga_put_hex(uint64_t);
        vga_puts("[SB22] tlb shootdowns=");
        vga_put_hex(vmm64_tlb_shootdown_count());
        vga_puts("\n");
    }

    /* 创建设置浮动窗口 */
    hs_window_manager_init();
    hs_window_t *settings_win = hs_window_create(HS_WINDOW_FLOATING, "Settings",
                                                  -1, -1, hs_sx(460), hs_sy(430));
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
    int w = hs_sx(288);
    int h = hs_sy(64);
    int gap = hs_sy(10);

    int popup_count = 0;

    for (int i = g_shell.notifications.count - 1; i >= 0 && popup_count < 3; i--) {
        hs_notification_t *n = &g_shell.notifications.notifications[i];
        if (!n->visible) continue;

        /* 到期隐藏 (timeout 毫秒, 桌面循环约 60fps) */
        uint32_t age = g_frame_no - n->born_frame;
        uint32_t life = (uint32_t)n->timeout * 60 / 1000;
        if (n->timeout > 0 && age > life) { n->visible = 0; continue; }

        int x, y;
        /* 顶部锚点必须用"顶栏实际高度"(bar_height 经 hs_sy 缩放), 而不是常量
         * HS_PANEL_HEIGHT=40。200% 下顶栏实际 160px, 通知卡却从 y=48 起,
         * 正好盖在右上角 wifi/电池/音量图标上。 */
        int top_bottom = hs_sy(g_shell.ui.bar_height) + hs_sy(8);
        int right_side = (g_notif_pos == 0 || g_notif_pos == 2);
        if (g_notif_pos == 0) {      /* 右上: 向下堆叠 */
            /* 与顶栏/QC 同一 margin, 否则卡身冲出屏幕右缘(150% 下 06:2x 被裁) */
            x = g_shell.screen_width - w - hs_sx(10);
            y = top_bottom + popup_count * (h + gap);
        } else if (g_notif_pos == 1) { /* 左上 */
            x = 8;
            y = top_bottom + popup_count * (h + gap);
        } else if (g_notif_pos == 2) { /* 右下: 向上堆叠 */
            /* 与顶栏/QC 同一 margin, 否则卡身冲出屏幕右缘(150% 下 06:2x 被裁) */
            x = g_shell.screen_width - w - hs_sx(10);
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

        /* 动画期间必须夹在屏内: 旧实现从右侧滑入时 x 会临时越出右边界,
         * 卡片右边框被裁掉、贴死在屏幕边缘(截图里右上角卡片右边看不到描边)。 */
        int n_margin = hs_sx(10);   /* 与顶栏胶囊同一 margin, 右端才对得齐 */
        if (x + w > g_shell.screen_width - n_margin) x = g_shell.screen_width - n_margin - w;
        if (x < n_margin) x = n_margin;
        if (y + h > g_shell.screen_height) y = g_shell.screen_height - h;

        /* 卡片底: surfaceContainerHigh + 细描边, 圆角 10 */
        gfx64_fill_rounded_rect(x, y, w, h, HS_RD_WIN, g_shell.colors.titlebar_bg);
        gfx64_draw_rounded_rect(x, y, w, h, HS_RD_WIN, g_shell.colors.border);

        /* 图标: 普通=accent 圆形, 紧急=红色圆角方块, 内绘标题首字母 */
        uint32_t icon_bg = (n->urgency == 2) ? 0xE5484D : g_shell.colors.accent;
        uint32_t icon_fg = (n->urgency == 2) ? 0xFFFFFF : hs_on_accent();
        int icx = x + hs_sx(28), icy = y + hs_sy(30), ir = hs_s(15);
        if (n->urgency == 2)
            gfx64_fill_rounded_rect(icx - ir, icy - ir, ir * 2, ir * 2, hs_s(8), icon_bg);
        else
            gfx64_fill_circle(icx, icy, ir, icon_bg);
        if (n->title[0]) {
            char ch[2] = { n->title[0], 0 };
            if (ch[0] >= 'a' && ch[0] <= 'z') ch[0] -= 32;
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               icx - hs_s(4), icy - hs_s(7), ch, icon_fg, hs_s(14));
        }

        /* 标题 (12px) 与正文 (10px), 按宽度截断 */
        int tx = x + hs_sx(52);
        int max_chars = (w - hs_sx(52) - hs_sx(12)) / hs_s(7);
        char tb[64];
        int ti = 0;
        while (n->title[ti] && ti < max_chars && ti < 62) { tb[ti] = n->title[ti]; ti++; }
        tb[ti] = 0;
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           tx, y + hs_sy(10), tb, g_shell.colors.window_fg, hs_s(12));
        max_chars = (w - hs_sx(52) - hs_sx(12)) / hs_s(6);
        ti = 0;
        while (n->body[ti] && ti < max_chars && ti < 62) { tb[ti] = n->body[ti]; ti++; }
        tb[ti] = 0;
        font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                           tx, y + hs_sy(30), tb, g_shell.colors.panel_fg, hs_s(10));

        /* 时间戳 (RTC 真实时间), 卡片右上角 */
        {
            uint8_t th = rtc64_get_hours();
            uint8_t tm = rtc64_get_minutes();
            char ts[8];
            ts[0] = '0' + th / 10; ts[1] = '0' + th % 10; ts[2] = ':';
            ts[3] = '0' + tm / 10; ts[4] = '0' + tm % 10; ts[5] = 0;
            font64_draw_string(fb, g_shell.screen_width, g_shell.screen_height,
                               x + w - hs_sx(40), y + hs_sy(8), ts,
                               g_shell.colors.panel_fg, hs_s(9));
        }

        /* 到期进度条: 底部 2px, accent 剩余比例 (对齐 Clavis progressTrack) */
        if (n->timeout > 0 && life > 0) {
            int track_w = w - hs_sx(16);
            int remain = (int)((life - age) * track_w / life);
            gfx64_fill_rounded_rect(x + hs_sx(8), y + h - hs_sy(5), track_w,
                                    hs_sy(2), hs_s(1), g_shell.colors.border);
            if (remain > hs_sx(2))
                gfx64_fill_rounded_rect(x + hs_sx(8), y + h - hs_sy(5), remain,
                                        hs_sy(2), hs_s(1), g_shell.colors.accent);
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

    if (new_x != g_drag_window->x || new_y != g_drag_window->y) {
        /* P0 脏区: 旧位置 ∪ 新位置, 两处都要重铺(旧处恢复壁纸, 新处画窗口) */
        int pad = hs_s(96);
        hs_dirty_add(g_drag_window->x - pad, g_drag_window->y - pad,
                     g_drag_window->width + pad * 2, g_drag_window->height + pad * 2);
        hs_dirty_add(new_x - pad, new_y - pad,
                     g_drag_window->width + pad * 2, g_drag_window->height + pad * 2);
    }
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
static int hs_find_key_from(const uint8_t *b, int len, const char *key, int start)
{
    int klen = 0;
    while (key[klen]) klen++;
    if (start < 0) start = 0;
    for (int i = start; i + klen < len; i++) {
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
int hpdesk_fetch_weather(void)
{
    extern int http64_get(const char *url, uint8_t *response, int max_response);
    extern int http64_get_status(const uint8_t *response);
    extern int http64_get_body(const uint8_t *response, int response_len,
                               uint8_t *body, int max_body);
    static const char *url =
        "http://api.open-meteo.com/v1/forecast?latitude=29.03&longitude=111.69&current_weather=true";
    uint8_t resp[2048];
    uint8_t body[1024];
    /* V0.56.1: 失败要能让调用方知道(此前失败静默 return, 桌面只在第 60 帧试一次,
     * 网络没就绪就永久没有天气) */
    /* 天气是"拿不到就拉倒"的请求: 3 秒拿不到就放弃, 别把桌面主循环堵住 */
    extern int http64_get_timeout(const char *url, uint8_t *response, int max_response, int timeout_s);
    int rlen = http64_get_timeout(url, resp, sizeof(resp), 3);
    if (rlen < 0) return 0;
    int st = http64_get_status(resp);
    if (st < 200 || st >= 300) return 0;
    /* V0.57.0: 这里必须传"真实响应长度" rlen, 而不是缓冲区大小 sizeof(resp)。
     * 旧实现传 2048, 而实际响应只有 ~730 字节, get_body 于是算出
     * body_len = 2048 - body_start ≈ 1900, 再被 max_body 截到 1024 ——
     * 拷进 body 的后半截全是缓冲区里的旧垃圾, 返回长度也虚高,
     * 后续按长度扫描 JSON 自然解析不出 temperature/weathercode。 */
    int blen = http64_get_body(resp, rlen, body, sizeof(body));
    if (blen <= 0) return 0;

    /* 温度: open-meteo 的响应里 "temperature" 会先出现在 current_weather_units 段
     * (值是单位字符串 "..C"), 旧实现只找第一个、解析失败就放弃, 于是永远取不到温度。
     * 改为从上一个命中点继续往下找, 直到某个值真的是数字为止。 */
    int t = -999;
    int tp = 0;
    while ((tp = hs_find_key_from(body, blen, "\"temperature\":", tp)) >= 0) {
        if (hs_parse_int_at(body, tp, blen, &t) == 0) { hs_itoa(t, g_weather_temp); break; }
        t = -999;
        tp += 1;
    }
    /* 天气码 → 描述 (同样跳过 units 段里的字符串值) */
    int wc = -1;
    int wp = 0;
    while ((wp = hs_find_key_from(body, blen, "\"weathercode\":", wp)) >= 0) {
        if (hs_parse_int_at(body, wp, blen, &wc) == 0) break;
        wc = -1;
        wp += 1;
    }
    if (wc >= 0) {
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
    /* V0.57.0: 数据到手必须请重绘 —— 按需合成下不标脏, 桌面右下角组件会一直
     * 停在抓取前的 "-- / N/A" 占位(而 Weather 页因切页重绘能显示, 两处观感不一致)。 */
    hpdesk_mark_dirty();
    vga_puts("[WX] weather fetched\n");
    return 1;
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

int hpdesk_switcher_active(void) { return g_sw_active; }

void hpdesk_switcher_next(void)
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

void hpdesk_switcher_close(void)
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
void hpdesk_cycle_window_focus(void)
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
void hpdesk_close_focused_window(void)
{
    if (g_wm.focus_window < 0) return;
    hs_window_recycle(g_wm.focus_window);
}

/* ---- 主题切换 (Light → Dark → Mono 循环) ---- */
void hpdesk_toggle_theme(void)
{
    g_shell.theme = (hs_theme_t)(((int)g_shell.theme + 1) % 3);
    hs_theme_init(&g_shell.colors, g_shell.theme);
    vga_puts("[Theme] toggled\n");
}

/* ---- 锁屏 ---- */
void hpdesk_lock(void)
{
    if (g_locked == 0) g_locked = 1;
}

/* ---- 工作区相对切换 ---- */
void hpdesk_switch_workspace_delta(int delta)
{
    int ws = g_shell.current_workspace + delta;
    if (ws < 0) ws = 0;
    if (ws >= HS_MAX_WORKSPACES) ws = HS_MAX_WORKSPACES - 1;
    if (ws != g_shell.current_workspace) hpdesk_switch_workspace(ws);
}

/* ---- 显示桌面 (最小化全部窗口) ---- */
void hpdesk_show_desktop(void)
{
    for (int i = 0; i < g_wm.window_count; i++) {
        if (g_wm.windows[i].visible) g_wm.windows[i].visible = 0;
    }
    g_wm.focus_window = -1;
}

/* ---- 滚轮 (桌面缩放占位: 无终端滚动目标时调节 UI 缩放) ---- */
static int g_ui_scale_v = 100;
void hpdesk_update_wheel(int wheel)
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
                       "App content rebuilding (V0.55.0)", g_shell.colors.text_secondary, 12);
}

void hpdesk_handle_pending_app(void)
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
                                          -1, -1, hs_sx(460), hs_sy(300));
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
