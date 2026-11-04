/* ============================================================================
 * 赫普OS - 手机模式 (mobile.c)
 * V2.42: 竖屏优先的全屏手机 UI —— 状态栏 + 应用网格 + 底部导航 + 滑动翻页
 * 触控模拟: QEMU 下用鼠标模拟触摸 (按下=down, 拖动=滑动, 释放=up)
 * 进入: shell 命令 `phone`; 返回: ESC
 * ========================================================================== */

#include "mobile.h"
#include "serial.h"
#include "ime.h"
#include <stdint.h>
#include "vga_gfx.h"
#include "vga.h"
#include "mouse.h"
#include "keyboard.h"
#include "pit.h"
#include "string.h"
#include "font_zh.h"
#include "wm.h"
#include "appreg.h"
#include "ide_ext2.h"
#include "ext2.h"
#include "permission_manager.h"

/* V2.67: 权限请求前向声明 (phone/camera 等在其定义前调用) */
int mobile_perm_request(const char *app, const char *pkg, int perm);
void mobile_perm_denied_page(const char *app, const char *perm);
/* V2.70: 权限管理界面前向声明 (设置中心先调用) */
void mobile_run_perm_mgr(void);
/* V2.71: 隐私看板前向声明 */
void mobile_run_privacy(void);
/* V2.86: 时钟应用前向声明 */
int mob_datefmt;   /* V3.09: 前向声明 (定义在设置区) */
void mobile_run_clock(void);
void mobile_run_contacts(void);   /* V3.35 */
void mobile_run_ai(void);         /* V3.79: AI 助手 */
void mobile_run_sms(void);        /* V3.36: 通讯录联动前向声明 */
void mobile_run_compose(void);    /* V3.36 */
const char *contacts_name_of(const char *num);   /* V3.35: 前向声明 (短信联动) */

/* ---- 应用列表 (图标+中文名) ----
 * V2.51: 应用双模式注册——8 原生手机应用 + 3 双模式桌面应用(终端/文件/关于),
 * 手机桌面第 2 页即可进入桌面应用. 元数据见 appreg.c 统一注册表. */
#define MOBILE_APPS 14   /* V3.79: +AI助手 */
const char *mob_app_names[MOBILE_APPS] = {
    "电话", "短信", "相机", "相册",
    "设置", "音乐", "浏览器", "计算器",
    "终端", "文件", "关于", "时钟",
    "通讯录", "助手"
};
const uint8_t mob_app_colors[MOBILE_APPS] = {
    0x0A, 0x09, 0x0C, 0x0D,
    0x0B, 0x0E, 0x02, 0x0F,
    0x07, 0x06, 0x05, 0x03,
    0x0E, 0x0A
};
/* V3.63: 英文应用名 (多语言) */
const char *mob_app_names_en[MOBILE_APPS] = {
    "Phone", "SMS", "Camera", "Album",
    "Settings", "Music", "Browser", "Calc",
    "Terminal", "Files", "About", "Clock",
    "Contacts", "AI"
};

/* V2.65: 应用安全 id (供沙箱域注册/切换) */
const char *mob_app_ids[MOBILE_APPS] = {
    "phone", "sms", "camera", "gallery",
    "settings", "music", "browser", "calc",
    "terminal", "files", "about", "clock",
    "contacts", "assistant"
};

/* ---- 页面状态 ---- */
#define MOBILE_PAGES 3          /* V3.35: 3 页应用网格 (每页 6 个) */
#define MOBILE_COLS 3
#define MOBILE_ROWS 2
#define MOBILE_APPS_PER_PAGE (MOBILE_COLS * MOBILE_ROWS)
int mob_page = 0;
int mob_running = 0;

/* V2.42: 键盘导航焦点 (QEMU 无鼠标时可键盘操作) */
int mob_focus = 0;

/* V2.43: 下拉通知面板 */
int mob_notify_open = 0;
/* V2.80: 短信未读计数 (前向声明, 供通知面板使用) */
int sms_unread_count = 0;
/* V3.67: 手机桌面 ESC 二次确认——防止误触一次即退出手机模式(退回文本 shell 被误认作"黑屏/条纹") */
#define MOB_ESC_CONFIRM_TICKS 600  /* V3.67: 约30-60秒确认窗口, 足够看提示后手动再按 */
int mob_esc_arm = 0;
int mob_esc_left = 0;

/* V2.89: 强调色 (定义在设置区, 此处前向声明供全部手机 UI 使用) */
uint8_t mob_accent;
int accent_idx = 0;
int ui_tron = 0;   /* V3.49: TRON 未来感主题 (1=开) */
/* V3.63: 系统语言 (0=中文 1=English, CMOS 0x3D 持久化) */
int sys_lang_en = 0;
void lang_save(void)
{
    extern void cmos_write_byte(uint8_t, uint8_t);
    cmos_write_byte(0x3D, (uint8_t)(sys_lang_en & 1));
}
void lang_load(void)
{
    extern uint8_t cmos_read_byte(uint8_t);
    sys_lang_en = (int)(cmos_read_byte(0x3D) & 1);
}
const char *ui_tr(const char *zh, const char *en) { return sys_lang_en ? en : zh; }
/* V3.64: 应用标题多语言 (中文 gfx_puts_zh / 英文 gfx_puts 居中) */
void ui_title(const char *zh, const char *en, int y)
{
    if (sys_lang_en) gfx_puts((gfx_width - (int)strlen(en) * 8) / 2, y, en, 0x0B);
    else gfx_puts_zh((gfx_width - zh_str_width(zh)) / 2, y, zh, 0x0B);
}
uint8_t ui_border(void) { return ui_tron ? 0x0B : 0x0F; }   /* V3.49: 面板边框色 */
/* V3.50: 应用内页背景 (TRON 下青色网格) */
void ui_draw_app_bg(void)
{
    gfx_fillrect(0, 0, gfx_width, gfx_height, 0x00);
    if (ui_tron) {
        for (int x = 16; x < gfx_width; x += 16) gfx_vline(x, 0, gfx_height, 0x02);
        for (int y = 16; y < gfx_height; y += 16) gfx_hline(0, y, gfx_width, 0x02);
        gfx_hline(0, 0, gfx_width, 0x0B);
        /* V3.54: 应用内页 TRON 扫描线 */
        static int tron_app_scan = 0;
        tron_app_scan = (tron_app_scan + 2) % gfx_height;
        gfx_hline(0, tron_app_scan, gfx_width, 0x0B);
    }
}
/* V2.98: 12 小时制 (定义在设置区, 前向声明供状态栏/时钟/通知使用) */
int mob_time12;
/* V2.99: 浅色主题反色开关 (定义在设置区, 前向声明) */
int mob_invert;

/* V2.98: 时间格式化 (24h: HH:MM; 12h: hh:MM AM/PM) */
void mob_time_str(char *out, int h, int m)
{
    if (mob_time12) {
        int h12 = h % 12;
        if (h12 == 0) h12 = 12;
        out[0] = '0' + h12 / 10; out[1] = '0' + h12 % 10;
        out[2] = ':';
        out[3] = '0' + m / 10; out[4] = '0' + m % 10;
        out[5] = ' ';
        out[6] = (h < 12) ? 'A' : 'P';
        out[7] = 'M';
        out[8] = 0;
    } else {
        out[0] = '0' + h / 10; out[1] = '0' + h % 10;
        out[2] = ':';
        out[3] = '0' + m / 10; out[4] = '0' + m % 10;
        out[5] = 0;
    }
}

/* V2.43: 计算器 (虚拟键盘演示) */
#define CALC_BUF_MAX 24
/* V3.25: 运算精度保护——int 最大 2147483647(10位)，限 9 位输入保证
 * 解析/中间运算不溢出（999999999 < INT_MAX） */
#define CALC_INT_MAX_DIGITS 9
char calc_buf[CALC_BUF_MAX];
int calc_rad = 0;   /* V3.03: 0=度 1=弧度 */
int calc_len = 0;
const char *calc_keys[16] = {
    "7", "8", "9", "+",
    "4", "5", "6", "-",
    "1", "2", "3", "=",
    "0", "C", ".", "/"
};
#define MOB_TOGGLE_NUM 4
int mob_toggles[MOB_TOGGLE_NUM] = {1, 0, 0, 0};  /* WiFi/蓝牙/飞行/省电 */
const char *mob_toggle_names[MOB_TOGGLE_NUM] = {"WiFi", "Blueto", "Airplane", "Power"};

/* V2.49: 长按上下文菜单 (-1=无) */
int mob_menu = -1;       /* 菜单对哪个应用打开 */
int mob_menu_sel = 0;    /* 菜单选中项 */
#define MOB_MENU_ITEMS 3
const char *mob_menu_names[MOB_MENU_ITEMS] = {"Open", "Info", "Uninstall"};

/* 触摸模拟状态 */
int touch_down = 0;
int touch_start_x = 0, touch_start_y = 0;
int touch_cur_x = 0, touch_cur_y = 0;
int touch_dragging = 0;
int touch_moved = 0;
int touch_hold_count = 0;   /* V2.49: 长按计时 */
int touch_hold_done = 0;

/* ---- 布局 (按当前分辨率缩放) ---- */
int sb_h;    /* 状态栏高度 */
int nb_h;    /* 导航栏高度 */
int app_area_x, app_area_y, app_area_w, app_area_h;
int cell_w, cell_h;
int icon_w, icon_h;

void mobile_layout(void)
{
    sb_h = 14;
    nb_h = 24;
    app_area_x = 0;
    app_area_y = sb_h;
    app_area_w = gfx_width;
    app_area_h = gfx_height - sb_h - nb_h;
    cell_w = app_area_w / MOBILE_COLS;
    cell_h = app_area_h / MOBILE_ROWS;
    icon_w = cell_w / 3;
    if (icon_w > 40) icon_w = 40;
    icon_h = icon_w;
}

/* ---- 绘制 ---- */
/* V3.57: TRON 发光文字 (定义在 mobile_draw_all 前, 状态栏先声明) */
void gfx_puts_glow(int x, int y, const char *s, int c);

/* V3.75: WiFi 扇形图标 (5 点弧近似, 两圈 + 底部连接点) */
void gfx_wifi_icon(int cx, int cy, uint8_t col)
{
    static const int wx8[5] = {6, 3, 0, -3, -6};
    static const int wy8[5] = {-6, -7, -8, -7, -6};
    for (int r = 3; r <= 5; r += 2) {
        for (int i = 0; i < 4; i++) {
            gfx_line(cx + wx8[i] * r / 8, cy + wy8[i] * r / 8,
                     cx + wx8[i + 1] * r / 8, cy + wy8[i + 1] * r / 8, col);
        }
    }
    gfx_fillrect(cx - 1, cy + 6, 3, 2, col);
}

void mobile_draw_statusbar(void)
{
    gfx_fillrect(0, 0, gfx_width, sb_h, mob_accent);
    /* 时钟 */
    {
        extern uint32_t pit_get_seconds(void);
        uint32_t sec = pit_get_seconds();
        int hh = (int)(sec / 3600) % 24;
        int mm = (int)((sec % 3600) / 60);
        char tbuf[16];
        mob_time_str(tbuf, hh, mm);   /* V2.98: 24h/12h */
        gfx_puts_glow(4, 3, tbuf, 0x0F);   /* V3.57: TRON 时间发光 */
    }
    /* V3.75: 飞行/蓝牙/WiFi 状态指示 (mob_toggles: 0=WiFi 1=蓝牙 2=飞行) */
    int sx = gfx_width - 30;
    int tx = sx - 34;
    if (mob_toggles[2]) {   /* 飞行: 小飞机三角 */
        gfx_line(tx, 4, tx + 5, 9, 0x0F);
        gfx_line(tx + 5, 9, tx, 9, 0x0F);
        gfx_line(tx, 9, tx, 4, 0x0F);
        tx -= 14;
    }
    if (mob_toggles[1]) {   /* 蓝牙: 蓝色 B */
        gfx_putchar(tx, 3, 'B', 0x09);
        tx -= 14;
    }
    if (mob_toggles[0]) {   /* WiFi: 青色扇形 / 关: 灰 x */
        gfx_wifi_icon(tx, 7, 0x0B);
    } else {
        gfx_putchar(tx, 3, 'x', 0x08);
    }
    /* 信号 (4 格) */
    for (int i = 0; i < 4; i++) {
        gfx_fillrect(sx + i * 3, sb_h - 4 - i * 2, 2, i * 2 + 2, 0x0F);
    }
    /* 电量 (电池框) —— V2.55: 动态显示真实电量 (pm_get_battery) */
    int bx = gfx_width - 16;
    gfx_rect(bx, 4, 10, 6, 0x0F);
    gfx_putchar(bx + 10, 4, '|', 0x0F);
    {
        extern int pm_get_battery(void *);
        typedef struct { int present, charging, capacity, voltage, current,
                         temperature, design_capacity, full_capacity, remaining_capacity; } pmb;
        pmb b;
        int pct = 100;
        if (pm_get_battery(&b) == 0) pct = b.capacity;
        if (pct > 100) pct = 100;
        if (pct < 0) pct = 0;
        uint8_t col = (pct > 20) ? 0x0A : (pct > 5) ? 0x0E : 0x0C;  /* 绿/黄/红 */
        int fill = (pct * 6) / 100;
        if (fill < 1) fill = 1;
        gfx_fillrect(bx + 1, 5, fill, 4, col);
    }
    (void)pit_get_seconds;
}

void mobile_draw_grid(void)
{
    for (int i = 0; i < MOBILE_APPS_PER_PAGE; i++) {
        int idx = mob_page * MOBILE_APPS_PER_PAGE + i;
        if (idx >= MOBILE_APPS) break;
        int col = i % MOBILE_COLS;
        int row = i / MOBILE_COLS;
        int cx = app_area_x + col * cell_w + (cell_w - icon_w) / 2;
        int cy = app_area_y + row * cell_h + (cell_h - icon_h - 18) / 2;

        /* 图标: 圆角方块渐变 */
        for (int r = 0; r < icon_h; r++) {
            uint8_t c = ui_tron ? 0x0B : mob_app_colors[idx];   /* V3.56: TRON 图标青色化 */
            if (r >= icon_h / 2 && c >= 8) c = (uint8_t)(c - 8);
            gfx_hline(cx, cy + r, icon_w, c);
        }
        gfx_rect(cx, cy, icon_w, icon_h, ui_border());   /* V3.51: TRON 青色图标描边 */
        /* 高光 */
        gfx_hline(cx + 2, cy + 2, icon_w / 3, 0x0F);

        /* 应用名 (V3.63: 中文/英文) */
        if (sys_lang_en) {
            const char *en = mob_app_names_en[idx];
            int ew = (int)strlen(en) * 8;
            gfx_puts(cx + (icon_w - ew) / 2, cy + icon_h + 2, en, 0x0F);
        } else {
            int tw = zh_str_width(mob_app_names[idx]);
            gfx_puts_zh(cx + (icon_w - tw) / 2, cy + icon_h + 2,
                        mob_app_names[idx], 0x0F);
        }
        /* V2.42: 键盘焦点高亮 */
        if (idx == mob_focus) {
            gfx_rect(cx - 2, cy - 2, icon_w + 4, icon_h + 4, 0x0F);
            gfx_rect(cx - 4, cy - 4, icon_w + 8, icon_h + 8, 0x08);
        }
        /* V3.75: 未读角标 (短信图标右上角红底白字) */
        if (idx == 1 && sms_unread_count > 0) {
            int bx = cx + icon_w - 6;
            int by = cy - 3;
            gfx_fillrect(bx, by, 10, 9, 0x0C);
            gfx_rect(bx, by, 10, 9, 0x0F);
            int nu = sms_unread_count > 9 ? 9 : sms_unread_count;
            char bc[2];
            bc[0] = '0' + nu;
            bc[1] = 0;
            gfx_puts(bx + 1, by + 1, bc, 0x0F);
            if (sms_unread_count > 9) gfx_putchar(bx + 7, by + 1, '+', 0x0F);
        }
    }
}

void mobile_draw_dots(void)
{
    int total = MOBILE_PAGES;
    int dot_gap = 10;
    int x0 = (gfx_width - (total - 1) * dot_gap) / 2;
    int y = app_area_y + app_area_h + 6;
    for (int i = 0; i < total; i++) {
        gfx_fillrect(x0 + i * dot_gap, y, 4, 4, (i == mob_page) ? 0x0F : 0x08);
    }
}

void mobile_draw_navbar(void)
{
    int y0 = gfx_height - nb_h;
    gfx_fillrect(0, y0, gfx_width, nb_h, mob_accent);
    /* Home 键 (圆形) */
    int hx = gfx_width / 2;
    int hy = y0 + nb_h / 2;
    gfx_rect(hx - 8, hy - 8, 16, 16, 0x0F);
    gfx_rect(hx - 6, hy - 6, 12, 12, 0x07);
    /* 返回 (左) */
    gfx_puts(hx - gfx_width / 4, hy - 4, "<", 0x0F);
    /* 最近任务 (右) */
    gfx_puts(hx + gfx_width / 4, hy - 4, "[]", 0x0F);
}

void mobile_draw_notify(void);  /* V2.43 前向声明 */

void mobile_app_info(int idx);  /* V2.49 前向声明 */
/* V2.51 前向声明: 双模式桌面应用手机全屏页 */
void mobile_run_terminal(void);
void mobile_run_files(void);
/* V2.90/V3.02: 多闹钟 (6 槽, 文件级, 供落盘函数与时钟应用共用; V3.02 支持增删) */
#define ALARM_MAX 6
int al_h[ALARM_MAX] = {7, 8, 0, 0, 0, 0};
int al_m[ALARM_MAX] = {0, 30, 0, 0, 0, 0};
int al_armed[ALARM_MAX] = {0, 0, 0, 0, 0, 0};
int al_sel = 0;
int al_full = 0;   /* V3.02: 槽满提示帧 */

/* V2.90: 闹钟落盘 (/data/clock/alarms.txt, 每行 HH:MM|armed) */
void alarm_save(void)
{
    extern int ide_ext2_writefile(const char *, const char *);
    char buf[ALARM_MAX * 8 + 1];
    for (int i = 0; i < ALARM_MAX; i++) {
        buf[i * 8 + 0] = '0' + al_h[i] / 10;
        buf[i * 8 + 1] = '0' + al_h[i] % 10;
        buf[i * 8 + 2] = ':';
        buf[i * 8 + 3] = '0' + al_m[i] / 10;
        buf[i * 8 + 4] = '0' + al_m[i] % 10;
        buf[i * 8 + 5] = '|';
        buf[i * 8 + 6] = al_armed[i] ? '1' : '0';
        buf[i * 8 + 7] = '\n';
    }
    buf[ALARM_MAX * 8] = 0;
    ide_ext2_writefile("/data/clock/alarms.txt", buf);
    ide_ext2_sync();
}
void alarm_load(void)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    char buf[64];
    int n = ide_ext2_read_file("/data/clock", "alarms.txt", buf, 63);
    if (n <= 0) {
        /* V2.90: 迁移旧单闹钟 alarm.txt -> 槽 0 */
        char ob[16];
        int on = ide_ext2_read_file("/data/clock", "alarm.txt", ob, 15);
        if (on >= 6) {
            ob[on] = 0;
            al_h[0] = (ob[0] - '0') * 10 + (ob[1] - '0');
            al_m[0] = (ob[3] - '0') * 10 + (ob[4] - '0');
            al_armed[0] = (on > 6 && ob[6] == '1') ? 1 : 0;
            alarm_save();
        }
        return;
    }
    buf[n] = 0;
    int li = 0;
    for (int i = 0; i < ALARM_MAX && li + 7 <= n; i++) {
        al_h[i] = (buf[li] - '0') * 10 + (buf[li + 1] - '0');
        al_m[i] = (buf[li + 3] - '0') * 10 + (buf[li + 4] - '0');
        al_armed[i] = (buf[li + 6] == '1') ? 1 : 0;
        li += 8;
    }
}

/* V3.06: 世界时钟城市 (可增删, 相对上海 UTC+8 偏移, cities.txt 落盘) */
#define WC_MAX 8
char wc_name[WC_MAX][9];
int wc_off[WC_MAX];
int wc_cnt = 0;
int wc_sel = 0;
const char *wc_pool_name[12] = {"Shanghai", "Beijing", "Tokyo", "Seoul",
    "Singapore", "London", "Paris", "NewYork", "LA", "Sydney", "Dubai", "Moscow"};
const int wc_pool_off[12] = {0, 0, 1, 1, 0, -8, -7, -13, -16, 2, -4, -5};
void wc_save(void)
{
    extern int ide_ext2_writefile(const char *, const char *);
    char buf[WC_MAX * 12 + 1];
    int nb = 0;
    for (int i = 0; i < wc_cnt; i++) {
        const char *nm = wc_name[i];
        for (int j = 0; nm[j] && nb < WC_MAX * 12; j++) buf[nb++] = nm[j];
        buf[nb++] = '|';
        int v = wc_off[i], tj = 0;
        char tmp[4];
        if (v < 0) { buf[nb++] = '-'; v = -v; }
        do { tmp[tj++] = '0' + (v % 10); v /= 10; } while (v > 0);
        while (tj > 0) buf[nb++] = tmp[--tj];
        buf[nb++] = '\n';
    }
    buf[nb] = 0;
    ide_ext2_writefile("/data/clock/cities.txt", buf);
    ide_ext2_sync();
}
void wc_load(void)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    char buf[100];
    int n = ide_ext2_read_file("/data/clock", "cities.txt", buf, 99);
    wc_cnt = 0;
    if (n >= 3) {
        buf[n] = 0;
        int li = 0;
        while (wc_cnt < WC_MAX && li < n) {
            int ni = 0, off = 0, neg = 0;
            while (li < n && buf[li] != '|' && ni < 8) wc_name[wc_cnt][ni++] = buf[li++];
            wc_name[wc_cnt][ni] = 0;
            if (li < n && buf[li] == '|') li++;
            if (li < n && buf[li] == '-') { neg = 1; li++; }
            while (li < n && buf[li] >= '0' && buf[li] <= '9') { off = off * 10 + (buf[li] - '0'); li++; }
            if (li < n && buf[li] == '\n') li++;
            wc_off[wc_cnt] = neg ? -off : off;
            wc_cnt++;
        }
    } else {
        /* 默认 5 城 */
        static const char *dname[5] = {"Shanghai", "Tokyo", "London", "NewYork", "LA"};
        static const int doff[5] = {0, 1, -8, -13, -16};
        for (int i = 0; i < 5; i++) {
            int j = 0;
            while (dname[i][j] && j < 8) { wc_name[wc_cnt][j] = dname[i][j]; j++; }
            wc_name[wc_cnt][j] = 0;
            wc_off[wc_cnt] = doff[i];
            wc_cnt++;
        }
        wc_save();
    }
}

/* V2.86: 时钟应用 (时钟/闹钟/计时器) */
/* V2.96: 月天数校验 (闰年: 2000-2099 内 y%4==0) */
int calc_days_in_month(int y, int m)
{
    static const int dim[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && (y % 4 == 0)) return 29;
    return (m >= 1 && m <= 12) ? dim[m] : 31;
}

/* V3.05: 蔡勒公式推星期 (0=周日..6=周六; 1582-10-15 后) */
int calc_wday(int y, int m, int d)
{
    if (m < 3) { m += 12; y--; }
    int w = (d + 2 * m + 3 * (m + 1) / 5 + y + y / 4 - y / 100 + y / 400 + 1) % 7;
    if (w < 0) w += 7;
    return w;
}
const char *calc_wday_name(int w)
{
    static const char *names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    if (w < 0 || w > 6) return "???";
    return names[w];
}


void mobile_run_about(void);
int hepu_mobile_launch_slot(int slot);

/* V2.49: 应用信息页 */
void mobile_app_info(int idx)
{
    int running = 1;
    while (running) {
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        if (sys_lang_en) gfx_puts((gfx_width - 48) / 2, 18, "App Info", 0x0B);
        else gfx_puts_zh((gfx_width - 48) / 2, 18, "应用信息", 0x0B);
        int tw = zh_str_width(mob_app_names[idx]);
        gfx_puts_zh((gfx_width - tw) / 2, 40, mob_app_names[idx], 0x0B);
        gfx_puts((gfx_width - 90) / 2, 62, "App Info", 0x0F);
        gfx_puts((gfx_width - 150) / 2, 80, "Version 1.0.2  |  HEPU", 0x08);
        gfx_puts((gfx_width - 130) / 2, 94, "GPLv3  |  x86", 0x08);
        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) { running = 0; break; }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}

/* V2.49: 长按上下文菜单绘制 */
void mobile_draw_menu(void)
{
    if (mob_menu < 0) return;
    /* 在焦点应用图标下方弹出 */
    int col = (mob_menu % MOBILE_COLS);
    int row = (mob_menu / MOBILE_COLS) % 2;
    int cx = app_area_x + col * cell_w;
    int mw = 90, mh = MOB_MENU_ITEMS * 16 + 4;
    int mx = cx + (cell_w - mw) / 2;
    int my = app_area_y + row * cell_h + icon_h + 4;
    gfx_fillrect(mx, my, mw, mh, 0x00);
    gfx_rect(mx, my, mw, mh, 0x0F);
    for (int i = 0; i < MOB_MENU_ITEMS; i++) {
        int y = my + 2 + i * 16;
        if (i == mob_menu_sel) gfx_fillrect(mx + 1, y, mw - 2, 14, 0x01);
        gfx_puts(mx + 6, y + 2, mob_menu_names[i], 0x0F);
    }
}

int mob_wallpaper = 0;   /* V3.07: 壁纸索引 */

/* V3.57: TRON 发光文字 (青色外发光, 经典主题回退原样) */
void gfx_puts_glow(int x, int y, const char *s, int c)
{
    if (!ui_tron) { gfx_puts(x, y, s, c); return; }
    gfx_puts(x - 1, y, s, 0x0B);
    gfx_puts(x + 1, y, s, 0x0B);
    gfx_puts(x, y - 1, s, 0x0B);
    gfx_puts(x, y + 1, s, 0x0B);
    gfx_puts(x, y, s, c);
}

void mobile_draw_all(void)
{
    /* V2.93/V3.07: 背景 (壁纸 0=渐变 1=夜空 2=斜纹 3=网格) */
    if (mob_wallpaper == 1) {
        for (int y = 0; y < gfx_height; y++) {
            uint8_t c = (y < 30) ? 0x03 : ((y < gfx_height / 2) ? 0x01 : 0x00);
            gfx_hline(0, y, gfx_width, c);
        }
        static const uint8_t stars[10][2] = {{12,8},{30,14},{52,6},{70,20},{90,10},
                                             {110,16},{140,8},{170,22},{200,12},{230,18}};
        for (int si = 0; si < 10; si++)
            gfx_fillrect(stars[si][0], stars[si][1], 1, 1, 0x0F);
    } else if (mob_wallpaper == 2) {
        for (int y = 0; y < gfx_height; y++)
            for (int x = 0; x < gfx_width; x++)
                if (((x + y) / 8) % 2) gfx_fillrect(x, y, 1, 1, mob_accent);
    } else if (mob_wallpaper == 3 || ui_tron) {
        /* V3.49: TRON 主题下强制青色网格背景 */
        for (int y = 0; y < gfx_height; y++) {
            uint8_t c = ((y / 16) % 2) ? (ui_tron ? 0x01 : 0x08) : 0x00;
            gfx_hline(0, y, gfx_width, c);
        }
        for (int x = 16; x < gfx_width; x += 16)
            gfx_vline(x, 0, gfx_height, mob_accent);
        gfx_hline(0, 0, gfx_width, mob_accent);
        if (ui_tron) {   /* V3.49: TRON 横向网格线 */
            for (int y = 16; y < gfx_height; y += 16)
                gfx_hline(0, y, gfx_width, 0x02);
            gfx_puts_glow(gfx_width - 56, gfx_height - 44, "HEPU", 0x0F);   /* V3.57: 发光角标 */
        }
    } else {
        for (int y = 0; y < gfx_height; y++) {
            uint8_t c = (y < gfx_height / 2) ? mob_accent : 0x00;
            gfx_hline(0, y, gfx_width, c);
        }
    }
    mobile_draw_statusbar();
    mobile_draw_grid();
    mobile_draw_dots();
    mobile_draw_navbar();
    mobile_draw_notify();   /* V2.43: 通知面板在最上层 */
    mobile_draw_menu();     /* V2.49: 长按菜单在最上层 */
    if (ui_tron) {   /* V3.53: TRON 扫描线动画 (每帧下移 2px) */
        static int tron_scan = 0;
        tron_scan = (tron_scan + 2) % gfx_height;
        gfx_hline(0, tron_scan, gfx_width, 0x0B);
    }
}

/* ---- V2.43: 下拉通知面板 ---- */
void mobile_draw_notify(void)
{
    if (!mob_notify_open) return;
    int nw = gfx_width;
    int nh = gfx_height / 2;
    /* 半透明深色背景 */
    for (int y = 0; y < nh; y++) {
        gfx_hline(0, y, nw, (y < nh / 2) ? mob_accent : 0x08);
    }
    gfx_rect(0, 0, nw, nh, ui_border());   /* V3.51: TRON 通知面板边框 */
    /* 日期时间 */
    {
        extern uint32_t pit_get_seconds(void);
        uint32_t sec = pit_get_seconds();
        int hh = (int)(sec / 3600) % 24;
        int mm = (int)((sec % 3600) / 60);
        char tbuf[16];
        mob_time_str(tbuf, hh, mm);   /* V2.98: 24h/12h */
        gfx_puts_glow(6, 4, tbuf, 0x0F);
        gfx_puts(nw - 60, 4, "2026-09-03", ui_tron ? 0x0B : 0x0F);
    }
    /* 快捷开关 2x2 */
    int tw_ = nw / 2;
    int th_ = 30;
    for (int i = 0; i < MOB_TOGGLE_NUM; i++) {
        int tx = (i % 2) * tw_;
        int ty = 16 + (i / 2) * (th_ + 2);
        uint8_t bg = mob_toggles[i] ? 0x0A : 0x08;  /* 开=绿 关=灰 */
        gfx_fillrect(tx + 4, ty, tw_ - 8, th_ - 2, bg);
        gfx_rect(tx + 4, ty, tw_ - 8, th_ - 2, 0x0F);
        gfx_puts(tx + 10, ty + (th_ - 2) / 2 - 4, mob_toggle_names[i], ui_tron ? 0x0B : 0x0F);
    }
    /* 通知列表 */
    gfx_puts_glow(6, 16 + 2 * (th_ + 2) + 2, "Notifications:  Enter: SMS", 0x0B);
    gfx_fillrect(4, 16 + 2 * (th_ + 2) + 14, nw - 8, 14, 0x01);
    /* V2.80: 短信未读真实计数 (N 键打开时刷新) */
    {
        char ub[24];
        int ui = 0, un = sms_unread_count;
        char u2[6]; int uj = 0;
        if (un == 0) u2[uj++] = '0';
        while (un > 0) { u2[uj++] = '0' + (un % 10); un /= 10; }
        while (uj > 0) ub[ui++] = u2[--uj];
        ub[ui++] = ' '; ub[ui++] = 'u'; ub[ui++] = 'n'; ub[ui++] = 'r';
        ub[ui++] = 'e'; ub[ui++] = 'a'; ub[ui++] = 'd'; ub[ui++] = ' ';
        ub[ui++] = 'm'; ub[ui++] = 'e'; ub[ui++] = 's'; ub[ui++] = 's';
        ub[ui++] = 'a'; ub[ui++] = 'g'; ub[ui++] = 'e'; ub[ui++] = 's';
        ub[ui] = 0;
        gfx_puts(8, 16 + 2 * (th_ + 2) + 16, ub, 0x0F);
    }
    gfx_fillrect(4, 16 + 2 * (th_ + 2) + 30, nw - 8, 14, 0x01);
    gfx_puts(8, 16 + 2 * (th_ + 2) + 32, " System update ready", 0x0F);
}

/* ---- V2.88: 计算器科学模式 (整数定点函数) ---- */
/* V2.92: 错误哨兵 (函数越界/非法输入) */
#define CALC_ERR (-999999999)
/* V2.92: sin 查表 (0-90 度, x100 定点) */
static const int calc_sin_tab[91] = {
    0, 2, 3, 5, 7, 9, 10, 12, 14, 16, 17, 19, 21, 22, 24,
    26, 28, 29, 31, 33, 34, 36, 37, 39, 41, 42, 44, 45, 47, 48,
    50, 52, 53, 54, 56, 57, 59, 60, 62, 63, 64, 66, 67, 68, 69,
    71, 72, 73, 74, 75, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86,
    87, 87, 88, 89, 90, 91, 91, 92, 93, 93, 94, 95, 95, 96, 96,
    97, 97, 97, 98, 98, 98, 99, 99, 99, 99, 100, 100, 100, 100, 100,
    100
};

static const int calc_tan_tab[90] = {
    0, 2, 3, 5, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 25,
    27, 29, 31, 32, 34, 36, 38, 40, 42, 45, 47, 49, 51, 53, 55,
    58, 60, 62, 65, 67, 70, 73, 75, 78, 81, 84, 87, 90, 93, 97,
    100, 104, 107, 111, 115, 119, 123, 128, 133, 138, 143, 148, 154, 160, 166,
    173, 180, 188, 196, 205, 214, 225, 236, 248, 261, 275, 290, 308, 327, 349,
    373, 401, 433, 470, 514, 567, 631, 712, 814, 951, 1143, 1430, 1908, 2864, 5729,
};
int calc_sin(int deg)
{
    deg = ((deg % 360) + 360) % 360;
    int q = deg / 90, r = deg % 90;
    if (q == 0) return calc_sin_tab[r];
    if (q == 1) return calc_sin_tab[90 - r];
    if (q == 2) return -calc_sin_tab[r];
    return -calc_sin_tab[90 - r];
}
int calc_cos(int deg)
{
    return calc_sin(deg + 90);
}
/* V3.03: tan 度数查表 (x100 定点, 0-89 度) */
int calc_tan(int deg)
{
    deg = ((deg % 360) + 360) % 360;
    int q = deg / 90, r = deg % 90;
    if (q == 0) return calc_tan_tab[r];
    if (q == 1) return -calc_tan_tab[90 - r];
    if (q == 2) return calc_tan_tab[r];
    return -calc_tan_tab[90 - r];
}
/* V3.03: 反三角 (输入 sin/cos/tan x100 定点, 输出角度) */
int calc_asin(int v)
{
    if (v < -100 || v > 100) return CALC_ERR;
    if (v < 0) return -calc_asin(-v);
    for (int d = 0; d <= 90; d++)
        if (calc_sin(d) >= v) return d;
    return 90;
}
int calc_acos(int v)
{
    if (v < -100 || v > 100) return CALC_ERR;
    return 90 - calc_asin(v);
}
int calc_atan(int v)
{
    if (v < -5729 || v > 5729) return CALC_ERR;
    if (v < 0) return -calc_atan(-v);
    for (int d = 0; d < 90; d++)
        if (calc_tan(d) >= v) return d;
    return 89;
}

int calc_log10(int x)
{
    if (x <= 0) return CALC_ERR;
    int n = 0;
    while (x >= 10) { x /= 10; n++; }
    return n;
}
int calc_ln(int x)
{
    int l = calc_log10(x);
    if (l == CALC_ERR) return CALC_ERR;
    return l * 230 / 100;   /* x100 定点 */
}
int calc_isqrt(int x)
{
    if (x <= 0) return CALC_ERR;
    int r = x;
    while (r > x / r) r = (r + x / r) / 2;
    return r;
}
int calc_fact(int x)
{
    if (x < 0 || x > 12) return CALC_ERR;
    int r = 1;
    for (int i = 2; i <= x; i++) r *= i;
    return r;
}
/* 写 ERR 到 calc_buf */
void calc_put_err(void)
{
    calc_len = 0;
    calc_buf[0] = 'E'; calc_buf[1] = 'R'; calc_buf[2] = 'R'; calc_buf[3] = 0;
}
/* 写 x100 定点值 (带小数点) */
void calc_put_fixed(int v100)
{
    int neg = (v100 < 0);
    if (neg) v100 = -v100;
    int ip = v100 / 100, fp = v100 % 100;
    char ib[8]; int ii = 0;
    if (ip == 0) ib[ii++] = '0';
    while (ip > 0) { ib[ii++] = '0' + (ip % 10); ip /= 10; }
    calc_len = 0;
    char tmp[16]; int ti = 0;
    if (neg) tmp[ti++] = '-';
    for (int j = ii - 1; j >= 0; j--) tmp[ti++] = ib[j];
    tmp[ti++] = '.';
    tmp[ti++] = '0' + (fp / 10); tmp[ti++] = '0' + (fp % 10);
    for (int j = 0; j < ti && calc_len < CALC_BUF_MAX - 1; j++)
        calc_buf[calc_len++] = tmp[j];
    calc_buf[calc_len] = 0;
}
/* 对当前 calc_buf 数值应用单目函数并写回 (ERR 哨兵) */
void calc_apply_func(int (*fn)(int))
{
    int v = 0;
    for (int i = 0; calc_buf[i] >= '0' && calc_buf[i] <= '9'; i++)
        v = v * 10 + (calc_buf[i] - '0');
    int r = fn(v);
    if (r == CALC_ERR) { calc_put_err(); return; }
    calc_len = 0;
    char tmp[12]; int ti = 0;
    int neg = (r < 0);
    int rr = neg ? -r : r;
    if (rr == 0) tmp[ti++] = '0';
    while (rr > 0) { tmp[ti++] = '0' + (rr % 10); rr /= 10; }
    if (neg) tmp[ti++] = '-';
    for (int j = ti - 1; j >= 0 && calc_len < CALC_BUF_MAX - 1; j--)
        calc_buf[calc_len++] = tmp[j];
    calc_buf[calc_len] = 0;
    ide_ext2_sync();
}

char calls_buf[512];
int calls_count = 0;
char calls_nums[CALLS_MAX][16];
char calls_times[CALLS_MAX][8];
int calls_flags[CALLS_MAX];
int call_sel = 0;
char phone_prefill_to[16];   /* V3.60: 通讯录->电话预填号码 */
int phone_prefill_active = 0;   /* V3.60 */

/* ============================================================================
 * V2.71: 隐私看板 (设置 -> Privacy)
 * 聚合显示各应用权限使用次数; Enter 清空日志; ESC 返回
 * ========================================================================== */

void mobile_launch_app(int idx)
{
    if (idx < 0 || idx >= MOBILE_APPS) return;   /* V2.53: 边界保护 */
    /* V2.65: 应用沙箱——进入应用时切换到其安全域 */
    extern int sec_set_active_app(const char *);
    if (idx < MOBILE_APPS && mob_app_ids[idx]) {
        sec_set_active_app(mob_app_ids[idx]);
    }
    /* V3.07: 按应用配色——进入应用时用应用专属强调色 (设置应用保持用户色) */
    int saved_accent = mob_accent;
    /* V3.52: TRON 主题下应用进入不再覆盖青色强调 */
    if (idx != 4 && idx < MOBILE_APPS && !ui_tron) mob_accent = mob_app_colors[idx];
    /* V2.44: 电话/短信走专用界面 */
    if (idx == 0) { mobile_run_phone(); mob_accent = saved_accent; return; }
    if (idx == 1) { mobile_run_sms(); mob_accent = saved_accent; return; }
    /* V2.46: 相机/相册走专用界面 */
    if (idx == 2) { mobile_run_camera(); mob_accent = saved_accent; return; }
    if (idx == 3) { mobile_run_album(); mob_accent = saved_accent; return; }
    /* V2.45: 设置/音乐走专用界面 */
    if (idx == 4) { mobile_run_settings(); mob_accent = saved_accent; return; }
    if (idx == 5) { mobile_run_music(); mob_accent = saved_accent; return; }
    /* V2.48: 浏览器 */
    if (idx == 6) { mobile_run_browser(); mob_accent = saved_accent; return; }
    /* V2.43: 计算器走专用虚拟键盘 */
    if (idx == 7) { mobile_run_calc(); mob_accent = saved_accent; return; }
    /* V2.51: 双模式桌面应用 (终端/文件/关于) — 手机全屏页 */
    if (idx == 8) { mobile_run_terminal(); mob_accent = saved_accent; return; }
    if (idx == 9) { mobile_run_files(); mob_accent = saved_accent; return; }
    if (idx == 10) { mobile_run_about(); mob_accent = saved_accent; return; }
    if (idx == 11) { mobile_run_clock(); mob_accent = saved_accent; return; }
    /* V3.35: 通讯录 */
    if (idx == 12) { mobile_run_contacts(); mob_accent = saved_accent; return; }
    /* V3.79: AI 助手 */
    if (idx == 13) { mobile_run_ai(); mob_accent = saved_accent; return; }
    /* 其他: 全屏页停留——直到 ESC 返回桌面 */
    int app_running = 1;
    while (app_running) {
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "Back: ESC", 0x0F);
        int tw = zh_str_width(mob_app_names[idx]);
        gfx_puts_zh((gfx_width - tw) / 2, gfx_height / 2 - 8,
                    mob_app_names[idx], 0x0F);
        gfx_puts((gfx_width - 110) / 2, gfx_height / 2 + 12,
                 "App fullscreen page", 0x0A);
        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) { app_running = 0; break; }
        }
        for (volatile int d = 0; d < 200000; d++) ;
    }
    mob_accent = ui_tron ? 0x0B : saved_accent;   /* V3.07/V3.49: 恢复强调色 (TRON 保持青) */
    /* V2.72: 应用退出 → 清除权限使用指示 (相机/麦克风/定位) */
    prm_release_all_in_use();
}

/* ---- V2.51: 双模式桌面应用的手机全屏页 ---- */

/* 文件: 内置文件列表 */
/* V2.58: 文件大小格式化 (B/KB/MB) */
void fm_fmt_size(uint32_t sz, char *out, int cap)
{
    int n = 0;
    char tmp[16];
    int tn = 0;
    uint32_t v = sz;
    const char *unit = "B";
    if (sz >= 1024 * 1024) { v = sz / (1024 * 1024); unit = "MB"; }
    else if (sz >= 1024) { v = sz / 1024; unit = "KB"; }
    if (v == 0) { tmp[tn++] = '0'; }
    while (v > 0 && tn < 14) { tmp[tn++] = '0' + (v % 10); v /= 10; }
    while (tn > 0 && n < cap - 2) out[n++] = tmp[--tn];
    for (int i = 0; unit[i] && n < cap - 1; i++) out[n++] = unit[i];
    out[n] = 0;
}

/* V2.58: 拼接子路径  base(/docs) + name -> /docs/name */
void fm_join(const char *base, const char *name, char *out, int cap)
{
    int n = 0;
    while (base[n] && n < cap - 1) { out[n] = base[n]; n++; }
    if (n > 1 && out[n-1] == '/') n--;
    if (n < cap - 1) out[n++] = '/';
    for (int i = 0; name[i] && n < cap - 1; i++) out[n++] = name[i];
    out[n] = 0;
}

/* V2.58: 返回上级路径  /docs/sub -> /docs ; /docs -> / */
void fm_parent(const char *path, char *out, int cap)
{
    int n = 0;
    while (path[n] && n < cap - 1) { out[n] = path[n]; n++; }
    out[n] = 0;
    if (n <= 1) return;
    int last = n - 1;
    while (last > 0 && out[last] != '/') last--;
    if (last <= 1) { out[0] = '/'; out[1] = 0; }
    else { out[last] = 0; }
}

/* V3.76: 取路径末段文件名 /docs/a.txt -> a.txt */
const char *fm_basename(const char *path)
{
    const char *p = path;
    const char *last = path;
    while (*p) {
        if (*p == '/') last = p + 1;
        p++;
    }
    return last;
}

/* V2.58: 查看文件内容页 */
void fm_view_file(const char *dirpath, const char *name)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    static char content[1600];
    int rd = ide_ext2_read_file(dirpath, name, content, 1600);
    int running = 1;
    int scroll = 0;
    while (running) {
        ui_draw_app_bg();   /* V3.50: TRON 网格背景 */
        gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
        gfx_puts(4, 3, "ESC 返回  W/S 滚动", 0x0F);
        gfx_puts(60, 3, name, 0x0B);
        if (rd < 0) {
            if (rd == -2) if (sys_lang_en) gfx_puts(20, 60, "It's a folder", 0x07); else gfx_puts_zh(20, 60, "这是文件夹", 0x07);
            else if (sys_lang_en) gfx_puts(20, 60, "Cannot read file", 0x07); else gfx_puts_zh(20, 60, "无法读取文件", 0x07);
        } else {
            int lines = (gfx_height - 30) / 14;
            int ln = 0;
            int line_start = 0;
            for (int i = 0; i <= rd; i++) {
                if (i == rd || content[i] == '\n') {
                    if (ln >= scroll && ln < scroll + lines) {
                        int y = 30 + (ln - scroll) * 14;
                        if (i - line_start > 0 && y < gfx_height - 4) {
                            char tmp[80];
                            int tl = i - line_start;
                            if (tl > 79) tl = 79;
                            memcpy(tmp, content + line_start, tl);
                            tmp[tl] = 0;
                            /* 去 
 */
                            for (int t = 0; t < tl; t++) if (tmp[t] == '\r') tmp[t] = ' ';
                            gfx_puts(6, y, tmp, 0x07);
                        }
                    }
                    ln++;
                    line_start = i + 1;
                }
            }
            gfx_puts(4, gfx_height - 14, "Line " , 0x08);
        }
        gfx_flip();
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc == 0x1B) { running = 0; break; }
            if (kc == 'w' || kc == 'W') { if (scroll > 0) scroll--; }
            if (kc == 's' || kc == 'S') { scroll++; }
        }
        for (volatile int d = 0; d < 150000; d++) ;
    }
}

/* V2.58: 手机文件管理器 —— 真实 ext2 磁盘浏览 */
/* V2.79: 数据域权限门——/data 敏感目录需对应应用权限 (无权限拒绝浏览) */
int files_domain_granted(const char *path)
{
    int perm = -1;
    if (strstr(path, "/data/contacts")) perm = PM_PERM_CONTACTS;
    else if (strstr(path, "/data/sms")) perm = PM_PERM_SMS;
    else if (strstr(path, "/data/phone")) perm = PM_PERM_PHONE;
    if (perm < 0) return 1;   /* 非敏感目录放行 */
    int id = prm_app_register("files", "com.hepu.files");
    if (id < 0) return 0;
    int g = 0;
    prm_check_permission(id, perm, &g);
    return g;
}

/* V2.51: 供 appreg 注册表调用的手机模式 launcher (slot 0..10) */
int hepu_mobile_launch_slot(int slot)
{
    if (slot < 0 || slot >= MOBILE_APPS) return -1;
    mobile_launch_app(slot);
    return 0;
}

/* ---- 触摸模拟 (鼠标) ---- */
void mobile_handle_touch(void)
{
    int mx = mouse_get_x();
    int my = mouse_get_y();
    uint8_t btn = mouse_get_buttons();

    if (btn & WM_MOUSE_LEFT) {
        if (!touch_down) {
            touch_down = 1;
            touch_start_x = touch_cur_x = mx;
            touch_start_y = touch_cur_y = my;
            touch_moved = 0;
            touch_dragging = 0;
            touch_hold_count = 0;
            touch_hold_done = 0;
        } else {
            /* V2.49: 真机长按检测——按住约 0.5s 未移动 → 打开上下文菜单 */
            touch_hold_count++;
            if (!touch_hold_done && touch_hold_count > 8 && !touch_moved) {
                touch_hold_done = 1;
                int tx = (mx - app_area_x) / cell_w;
                int ty = (my - app_area_y) / cell_h;
                int hit = ty * MOBILE_COLS + tx + mob_page * MOBILE_APPS_PER_PAGE;
                if (hit >= 0 && hit < MOBILE_APPS && mob_menu < 0) {
                    mob_menu = hit;
                    mob_menu_sel = 0;
                }
            }
            int dx = mx - touch_start_x;
            touch_cur_x = mx; touch_cur_y = my;
            /* 滑动翻页: 横向拖动超过阈值 */
            if (dx > 60) {
                if (mob_page > 0) { mob_page--; touch_start_x = mx; }
                touch_moved = 1; touch_dragging = 1;
            } else if (dx < -60) {
                if (mob_page < MOBILE_PAGES - 1) { mob_page++; touch_start_x = mx; }
                touch_moved = 1; touch_dragging = 1;
            }
        }
    } else {
        if (touch_down) {
            /* 释放: 若未滑动则为点击, 命中应用 */
            if (!touch_moved && !touch_dragging) {
                for (int i = 0; i < MOBILE_APPS_PER_PAGE; i++) {
                    int idx = mob_page * MOBILE_APPS_PER_PAGE + i;
                    if (idx >= MOBILE_APPS) break;
                    int col = i % MOBILE_COLS;
                    int row = i / MOBILE_COLS;
                    int cx = app_area_x + col * cell_w + (cell_w - icon_w) / 2;
                    int cy = app_area_y + row * cell_h + (cell_h - icon_h - 18) / 2;
                    if (mx >= cx && mx < cx + icon_w &&
                        my >= cy && my < cy + icon_h + 20) {
                        mobile_launch_app(idx);
                    }
                }
            }
            touch_down = 0;
            touch_moved = 0;
            touch_dragging = 0;
        }
    }
}

/* ---- V2.53: 双模式切换全局标记 ----
 * hepu_mode_switch_request: 1=请求从手机模式切到桌面模式 (mobile_run 退出后由 shell 处理)
 * hepu_ui_mode:            UI 模式 (0=shell/文本, 1=桌面, 2=手机), 供持久化记录
 */
int hepu_mode_switch_request = 0;
int hepu_ui_mode = 0;

/* ---- 主循环 ---- */
void mobile_run(void)
{
    /* V2.89: 强调色主题恢复 (CMOS) */
    accent_load();
    /* V2.98: 时间制恢复 (CMOS 0x39) */
    time12_load();
    /* V2.99: 浅色主题恢复 (CMOS 0x3A + gfx 反色) */
    theme_load();
    tron_load();   /* V3.49: TRON 主题恢复 */
    if (ui_tron) mob_accent = 0x0B;   /* V3.49: TRON 下全局青色强调 */
    wallpaper_load();   /* V3.07 */
    datefmt_load();   /* V3.09 */
    /* V2.51: 应用双模式注册——手机 launcher 注入统一注册表 */
    appreg_init();
    appreg_set_mobile_launcher(hepu_mobile_launch_slot);
    hepu_ui_mode = 2;   /* V2.53: 记录当前 UI 模式为手机 */

    mobile_layout();
    mob_page = 0;
    lang_load();   /* V3.63: 恢复系统语言 */
    sms_load_from_disk();   /* V3.75: 启动即加载短信未读 (桌面角标) */
    mob_running = 1;
    mobile_draw_all();
    gfx_flip();
    vga_puts("[MOBILE] phone mode: ESC to exit\n");

    while (mob_running) {
        /* V2.61: 系统休眠(屏幕灭) -> 灭屏 + HLT 等待任意键唤醒, 不重绘省电 */
        extern int pm_get_state(void);
        if (pm_get_state() == 3) {  /* PM_STATE_SUSPEND = 3 */
            extern void gfx_black_screen(void);
            gfx_black_screen();
            vga_puts("[MOBILE] screen off, press any key to wake\n");
            /* 阻塞等待: keyboard_getchar 消费字符后内部自动 pm_wakeup */
            (void)keyboard_getchar();
            /* 唤醒后恢复画面 */
            mobile_draw_all();
            gfx_flip();
            continue;
        }
        /* 键盘: ESC 退出, 方向键移动焦点, Enter 启动, P/N 翻页 */
        char kc;
        while (keyboard_try_getchar(&kc)) {
            if (kc != 0x1B) mob_esc_arm = 0;  /* V3.67: 其他任何键取消 ESC 确认 */
            if (kc == 0x1B) {  /* ESC: 先关通知, 再二次确认退出 */
                if (mob_notify_open) { mob_notify_open = 0; mob_esc_arm = 0; }
                else if (mob_esc_arm) { mob_running = 0; break; }  /* V3.67: 窗口内第二次 ESC 才退出 */
                else { mob_esc_arm = 1; mob_esc_left = MOB_ESC_CONFIRM_TICKS; }  /* V3.67: 第一次 ESC 仅武装 */
            } else if (mob_notify_open && (kc == '\n' || kc == '\r')) {
                /* V3.62: 通知面板 Enter 直达短信 */
                mob_notify_open = 0;
                mobile_run_sms();
            } else if (kc == 'g' || kc == 'G') {  /* V2.53: 切到桌面模式 */
                hepu_mode_switch_request = 1;
                mob_running = 0;
                break;
            } else if (kc == 'l' || kc == 'L') {  /* V2.49: 长按当前焦点应用 */
                if (mob_menu < 0) {
                    mob_menu = mob_focus;
                    mob_menu_sel = 0;
                }
            } else if (kc == '\n' || kc == '\r') { /* Enter */
                if (mob_menu >= 0) {
                    /* 执行菜单 */
                    if (mob_menu_sel == 0) {           /* Open */
                        mobile_launch_app(mob_menu);
                        mob_menu = -1;
                    } else if (mob_menu_sel == 1) {    /* Info: 全屏页 */
                        mobile_app_info(mob_menu);
                        mob_menu = -1;
                    } else {                            /* Uninstall */
                        mob_menu = -1;
                        vga_puts("[MOBILE] app uninstalled (demo)\n");
                    }
                } else {
                    if (mob_focus < MOBILE_APPS) mobile_launch_app(mob_focus);
                }
            } else if (kc == 'w' || kc == 'W') {
                if (mob_menu >= 0) { if (mob_menu_sel > 0) mob_menu_sel--; }
                else if (mob_focus >= MOBILE_COLS) mob_focus -= MOBILE_COLS;
            } else if (kc == 's' || kc == 'S') {
                if (mob_menu >= 0) { if (mob_menu_sel < MOB_MENU_ITEMS - 1) mob_menu_sel++; }
                else if (mob_focus + MOBILE_COLS < MOBILE_APPS) mob_focus += MOBILE_COLS;
            } else if (kc == 'a' || kc == 'A') {   /* 左 */
                if (mob_menu >= 0) { mob_menu = -1; }
                else if (mob_focus > 0) mob_focus--;
            } else if (kc == 'd' || kc == 'D') {   /* 右 */
                if (mob_menu >= 0) { mob_menu = -1; }
                else if (mob_focus < MOBILE_APPS - 1) mob_focus++;
            } else if (kc == 'p' || kc == 'P') {   /* 下一页 */
                if (mob_menu >= 0) { mob_menu = -1; }
                if (mob_page < MOBILE_PAGES - 1) {
                    mob_page++;
                    mob_focus = mob_page * MOBILE_APPS_PER_PAGE;
                }
            } else if (kc == 'n' || kc == 'N') {   /* 上一页 / 通知面板 */
                if (mob_menu >= 0) { mob_menu = -1; }
                else if (mob_notify_open) { mob_notify_open = 0; }
                else {
                    /* V2.80: 打开通知面板前刷新短信未读计数 */
                    sms_load_from_disk();
                    mob_notify_open = 1;
                }
            }
        }
        if (!mob_running) break;
        /* 触摸 (鼠标模拟) */
        mobile_handle_touch();

        /* 重绘 */
        mobile_draw_all();
        /* V3.67: ESC 确认提示横幅 (第一次 ESC 后显示, 超时自动消失) */
        if (mob_esc_arm) {
            gfx_fillrect(0, 0, gfx_width, 14, mob_accent);
            gfx_puts((gfx_width - (int)strlen("ESC again to exit") * 8) / 2, 3,
                     "ESC again to exit", GFX_COLOR_WHITE);
        }
        gfx_flip();
        /* 小延迟 */
        for (volatile int d = 0; d < 200000; d++) ;
        if (mob_esc_arm && --mob_esc_left <= 0) mob_esc_arm = 0;
    }
}
/* V3.01: 播放模式持久化 (/data/music/mode.txt = 0/1/2) */
void music_mode_save(void)
{
    extern int ide_ext2_writefile(const char *, const char *);
    char buf[2];
    buf[0] = '0' + music_mode; buf[1] = 0;
    ide_ext2_writefile("/data/music/mode.txt", buf);
    ide_ext2_sync();
}
void music_mode_load(void)
{
    extern int ide_ext2_read_file(const char *, const char *, char *, int);
    char buf[2];
    int n = ide_ext2_read_file("/data/music", "mode.txt", buf, 1);
    if (n >= 1 && buf[0] >= '0' && buf[0] <= '2') music_mode = buf[0] - '0';
}

