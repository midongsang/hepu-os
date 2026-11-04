/* ============================================================================
 * 赫普OS - 系统设置应用
 * ========================================================================== */

#include <stdint.h>
#include "settings_app.h"
#include "vga.h"
#include "string.h"
#include "power.h"



static settings_app_t sa;

/* 分类名称表 */
static const char *category_names[] = {
    " net & connect ", " device ", " app ", " display ", " 声音 ", " battery ",
    " storage ", " security & 隐私 ", " accounts ", " system ", " 关于手机 ",
    " no 障碍 ", "AI assistant ", " 开发者选 "
};

void settings_init(void)
{
    memset(&sa, 0, sizeof(settings_app_t));
    sa.initialized = 1;
    sa.enabled = 1;
    sa.current_category = SETTING_CAT_SYSTEM;

    /* 注册分类 */
    for (int i = 0; i < 14; i++) {
        sa.categories[i].id = (setting_category_t)i;
        strncpy(sa.categories[i].name, category_names[i], 63);
        sa.categories[i].visible = 1;
    }
    sa.category_count = 14;

    /* 注册常用设置项 */
    /* 网络 */
    settings_register("WiFi", SETTING_CAT_NETWORK, SETTING_TYPE_BOOL, " on / close WiFi", 0);
    settings_register(" BT ", SETTING_CAT_NETWORK, SETTING_TYPE_BOOL, " on / close BT ", 0);
    settings_register(" airplane mode ", SETTING_CAT_NETWORK, SETTING_TYPE_BOOL, " close all no 线 connect ", 0);
    settings_register(" mobile data ", SETTING_CAT_NETWORK, SETTING_TYPE_BOOL, " used 蜂窝 data ", 0);
    settings_register(" hotspot ", SETTING_CAT_NETWORK, SETTING_TYPE_BOOL, " 便携式 WiFi hotspot ", 0);
    settings_register("VPN", SETTING_CAT_NETWORK, SETTING_TYPE_BOOL, " virtual 专用 net ", 0);
    settings_register("NFC", SETTING_CAT_NETWORK, SETTING_TYPE_BOOL, " 近场通信 ", 0);

    /* 显示 */
    settings_register(" 亮度 ", SETTING_CAT_DISPLAY, SETTING_TYPE_SLIDER, " screen 亮度 0-100", 0);
    settings_set_int(sa.item_count - 1, 80);
    settings_register(" auto 亮度 ", SETTING_CAT_DISPLAY, SETTING_TYPE_BOOL, " 根据环境光 auto 调节 ", 0);
    settings_register(" dark mode ", SETTING_CAT_DISPLAY, SETTING_TYPE_BOOL, " 全局深色 master 题 ", 0);
    settings_register(" 字体 size ", SETTING_CAT_DISPLAY, SETTING_TYPE_SLIDER, " 字体 size 0-100", 0);
    settings_set_int(sa.item_count - 1, 50);
    settings_register(" display size ", SETTING_CAT_DISPLAY, SETTING_TYPE_SLIDER, " 界面元素 size 0-100", 0);
    settings_set_int(sa.item_count - 1, 50);
    settings_register(" screen timeout ", SETTING_CAT_DISPLAY, SETTING_TYPE_ENUM, " no ops auto 息屏 time ", 0);
    settings_register(" 护眼 mode ", SETTING_CAT_DISPLAY, SETTING_TYPE_BOOL, " 减少蓝光 ", 0);
    settings_register(" refresh rate ", SETTING_CAT_DISPLAY, SETTING_TYPE_ENUM, " screen refresh rate ", 0);

    /* 声音 */
    settings_register(" 铃声 ", SETTING_CAT_SOUND, SETTING_TYPE_SLIDER, " 铃声音量 0-100", 0);
    settings_set_int(sa.item_count - 1, 70);
    settings_register(" media ", SETTING_CAT_SOUND, SETTING_TYPE_SLIDER, " media 音量 0-100", 0);
    settings_set_int(sa.item_count - 1, 60);
    settings_register(" 闹钟 ", SETTING_CAT_SOUND, SETTING_TYPE_SLIDER, " 闹钟音量 0-100", 0);
    settings_set_int(sa.item_count - 1, 50);
    settings_register(" 通话 ", SETTING_CAT_SOUND, SETTING_TYPE_SLIDER, " 通话音量 0-100", 0);
    settings_set_int(sa.item_count - 1, 80);
    settings_register(" 静音 ", SETTING_CAT_SOUND, SETTING_TYPE_BOOL, " 全局静音 ", 0);
    settings_register(" 震动 ", SETTING_CAT_SOUND, SETTING_TYPE_BOOL, " 触感反馈 ", 0);
    settings_set_bool(sa.item_count - 1, 1);
    settings_register(" DND ", SETTING_CAT_SOUND, SETTING_TYPE_BOOL, " 屏蔽 notify ", 0);

    /* 电池 */
    settings_register(" 省电 mode ", SETTING_CAT_BATTERY, SETTING_TYPE_BOOL, " 限制 next 台活动省电 ", 0);
    settings_register(" 超 level 省电 ", SETTING_CAT_BATTERY, SETTING_TYPE_BOOL, " 仅保留 base 功能 ", 0);
    settings_register(" battery 百分比 ", SETTING_CAT_BATTERY, SETTING_TYPE_BOOL, " status bar display 百分比 ", 0);
    settings_set_bool(sa.item_count - 1, 1);

    /* 存储 */
    settings_register(" storage cleanup ", SETTING_CAT_STORAGE, SETTING_TYPE_ACTION, " cleanup cache & 垃圾 file ", 0);
    settings_register(" default storage ", SETTING_CAT_STORAGE, SETTING_TYPE_ENUM, " default storage bit 置 ", 0);

    /* 安全与隐私 */
    settings_register(" screen lock ", SETTING_CAT_SECURITY, SETTING_TYPE_ENUM, " 锁屏方式 ", 1);
    settings_register(" fingerprint unlock ", SETTING_CAT_SECURITY, SETTING_TYPE_BOOL, " used fingerprint unlock ", 1);
    settings_register(" 面部 unlock ", SETTING_CAT_SECURITY, SETTING_TYPE_BOOL, " used 面部 unlock ", 1);
    settings_register(" 隐私沙盒 ", SETTING_CAT_SECURITY, SETTING_TYPE_BOOL, " app isolation run ", 0);
    settings_set_bool(sa.item_count - 1, 1);
    settings_register(" data 金库 ", SETTING_CAT_SECURITY, SETTING_TYPE_BOOL, " local data encryption storage ", 0);
    settings_set_bool(sa.item_count - 1, 1);
    settings_register(" location ", SETTING_CAT_SECURITY, SETTING_TYPE_BOOL, " 允许 app get bit 置 ", 0);
    settings_register(" camera perm ", SETTING_CAT_SECURITY, SETTING_TYPE_BOOL, " 全局 camera perm ", 0);
    settings_set_bool(sa.item_count - 1, 1);
    settings_register(" 麦克风 perm ", SETTING_CAT_SECURITY, SETTING_TYPE_BOOL, " 全局麦克风 perm ", 0);
    settings_set_bool(sa.item_count - 1, 1);
    settings_register(" secure boot ", SETTING_CAT_SECURITY, SETTING_TYPE_BOOL, " verify system 完整性 ", 1);
    settings_set_bool(sa.item_count - 1, 1);

    /* 系统 */
    settings_register(" system lang ", SETTING_CAT_SYSTEM, SETTING_TYPE_ENUM, " display lang ", 0);
    settings_register(" runtime region ", SETTING_CAT_SYSTEM, SETTING_TYPE_STRING, " system runtime region ", 0);
    settings_register(" auto time ", SETTING_CAT_SYSTEM, SETTING_TYPE_BOOL, " net auto sync time ", 0);
    settings_set_bool(sa.item_count - 1, 1);
    settings_register("24 小 runtime 制 ", SETTING_CAT_SYSTEM, SETTING_TYPE_BOOL, " used 24 小 runtime 制 ", 0);
    settings_register(" system update ", SETTING_CAT_SYSTEM, SETTING_TYPE_ACTION, " check system update ", 0);
    settings_register(" backup ", SETTING_CAT_SYSTEM, SETTING_TYPE_ACTION, " backup settings ", 0);
    settings_register(" reset ", SETTING_CAT_SYSTEM, SETTING_TYPE_ACTION, " resume 出厂 set ", 1);

    /* 关于 */
    settings_register(" system version ", SETTING_CAT_ABOUT, SETTING_TYPE_STRING, "HEPU OS V0.27", 0);
    settings_register(" kernel version ", SETTING_CAT_ABOUT, SETTING_TYPE_STRING, "HEPU Microkernel 0.27", 0);
    settings_register(" model ", SETTING_CAT_ABOUT, SETTING_TYPE_STRING, "HEPU Generic", 0);
    settings_register(" 序列号 ", SETTING_CAT_ABOUT, SETTING_TYPE_STRING, "HEPU-000001", 0);
    settings_register(" 开源声明 ", SETTING_CAT_ABOUT, SETTING_TYPE_ACTION, " 查看开源许可证 ", 0);

    /* AI */
    settings_register("AI assistant ", SETTING_CAT_AI, SETTING_TYPE_BOOL, " enabled system AI", 0);
    settings_set_bool(sa.item_count - 1, 1);
    settings_register(" local 推理 ", SETTING_CAT_AI, SETTING_TYPE_BOOL, " 优先 local AI 推理 ", 0);
    settings_set_bool(sa.item_count - 1, 1);
    settings_register("AI notif classify ", SETTING_CAT_AI, SETTING_TYPE_BOOL, "AI 智能过滤 notify ", 0);

    /* 开发者选项 */
    settings_register("USB 调试 ", SETTING_CAT_DEVELOPER, SETTING_TYPE_BOOL, " 允许 USB 调试 ", 1);
    settings_register("ADB", SETTING_CAT_DEVELOPER, SETTING_TYPE_BOOL, " 允许 ADB connect ", 1);
    settings_register("CPU stats ", SETTING_CAT_DEVELOPER, SETTING_TYPE_BOOL, " display CPU used 率 ", 1);
    settings_register("GPU 渲染 ", SETTING_CAT_DEVELOPER, SETTING_TYPE_BOOL, " mandatory GPU 渲染 ", 1);
    settings_register(" 动画缩放 ", SETTING_CAT_DEVELOPER, SETTING_TYPE_SLIDER, " window 动画缩放 0-100", 1);
    settings_set_int(sa.item_count - 1, 100);

    vga_puts("[SETTINGS] system settings app init done \n");
    vga_printf(" set: %d, categories: %d \n", sa.item_count, sa.category_count);
}

/* 设置项管理 */
int settings_register(const char *name, setting_category_t category, setting_type_t type,
                      const char *desc, int protected)
{
    for (int i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if (!sa.items[i].used) {
            memset(&sa.items[i], 0, sizeof(setting_item_t));
            sa.items[i].used = 1;
            sa.items[i].id = i;
            if (name) strncpy(sa.items[i].name, name, 63);
            sa.items[i].category = category;
            sa.items[i].type = type;
            if (desc) strncpy(sa.items[i].description, desc, 127);
            sa.items[i].protected = protected;
            sa.items[i].visible = 1;
            sa.items[i].enabled = 1;
            sa.items[i].min_val = 0;
            sa.items[i].max_val = 100;
            sa.items[i].step = 1;
            sa.item_count++;
            if (category < SETTINGS_MAX_CATEGORIES)
                sa.categories[category].item_count++;
            return i;
        }
    }
    return -1;
}

int settings_set_value(int id, const char *value)
{
    if (id < 0 || id >= SETTINGS_MAX_ITEMS || !sa.items[id].used) return -1;
    if (sa.items[id].protected) return -1;
    if (value) strncpy(sa.items[id].value_str, value, 255);
    sa.items[id].modified = 1;
    sa.total_changes++;
    return 0;
}

int settings_get_value(int id, char *buf, int size)
{
    if (id < 0 || id >= SETTINGS_MAX_ITEMS || !sa.items[id].used || !buf || size <= 0) return -1;
    strncpy(buf, sa.items[id].value_str, size - 1);
    return 0;
}

int settings_set_bool(int id, int value)
{
    if (id < 0 || id >= SETTINGS_MAX_ITEMS || !sa.items[id].used) return -1;
    if (sa.items[id].protected) return -1;
    sa.items[id].value_bool = value ? 1 : 0;
    sa.items[id].modified = 1;
    sa.total_changes++;
    return 0;
}

int settings_get_bool(int id)
{
    if (id < 0 || id >= SETTINGS_MAX_ITEMS || !sa.items[id].used) return 0;
    return sa.items[id].value_bool;
}

int settings_set_int(int id, int value)
{
    if (id < 0 || id >= SETTINGS_MAX_ITEMS || !sa.items[id].used) return -1;
    if (sa.items[id].protected) return -1;
    if (value < sa.items[id].min_val) value = sa.items[id].min_val;
    if (value > sa.items[id].max_val) value = sa.items[id].max_val;
    sa.items[id].value_int = value;
    sa.items[id].modified = 1;
    sa.total_changes++;
    return 0;
}

int settings_get_int(int id)
{
    if (id < 0 || id >= SETTINGS_MAX_ITEMS || !sa.items[id].used) return 0;
    return sa.items[id].value_int;
}

int settings_reset(int id)
{
    if (id < 0 || id >= SETTINGS_MAX_ITEMS || !sa.items[id].used) return -1;
    sa.items[id].value_int = 0;
    sa.items[id].value_bool = 0;
    sa.items[id].value_str[0] = 0;
    sa.items[id].modified = 0;
    return 0;
}

int settings_reset_category(setting_category_t category)
{
    for (int i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if (sa.items[i].used && sa.items[i].category == category && !sa.items[i].protected)
            settings_reset(i);
    }
    return 0;
}

int settings_reset_all(void)
{
    for (int i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if (sa.items[i].used && !sa.items[i].protected)
            settings_reset(i);
    }
    return 0;
}

/* 查询 */
int settings_get_item(int id, setting_item_t *item)
{
    if (id < 0 || id >= SETTINGS_MAX_ITEMS || !sa.items[id].used || !item) return -1;
    memcpy(item, &sa.items[id], sizeof(setting_item_t));
    return 0;
}

int settings_list_by_category(setting_category_t category, setting_item_t *items, int max)
{
    if (!items || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < SETTINGS_MAX_ITEMS && count < max; i++) {
        if (sa.items[i].used && sa.items[i].category == category && sa.items[i].visible)
            memcpy(&items[count++], &sa.items[i], sizeof(setting_item_t));
    }
    return count;
}

int settings_list_all(setting_item_t *items, int max)
{
    if (!items || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < SETTINGS_MAX_ITEMS && count < max; i++) {
        if (sa.items[i].used) memcpy(&items[count++], &sa.items[i], sizeof(setting_item_t));
    }
    return count;
}

int settings_get_category_info(setting_category_t category, setting_category_info_t *info)
{
    if (category >= SETTINGS_MAX_CATEGORIES || !info) return -1;
    memcpy(info, &sa.categories[category], sizeof(setting_category_info_t));
    return 0;
}

int settings_list_categories(setting_category_info_t *categories, int max)
{
    if (!categories || max <= 0) return 0;
    int count = sa.category_count < max ? sa.category_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&categories[i], &sa.categories[i], sizeof(setting_category_info_t));
    return count;
}

/* 搜索 */
int settings_search(const char *query, int *results, int max)
{
    if (!query || !results || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < SETTINGS_MAX_ITEMS && count < max; i++) {
        if (sa.items[i].used && sa.items[i].visible) {
            if (strstr(sa.items[i].name, query) || strstr(sa.items[i].description, query))
                results[count++] = i;
        }
    }
    return count;
}

/* 导入导出 */
int settings_export(char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    int pos = 0;
    for (int i = 0; i < SETTINGS_MAX_ITEMS && pos < size - 64; i++) {
        if (sa.items[i].used && sa.items[i].modified) {
            int len = strlen(sa.items[i].name);
            if (pos + len + 32 < size) {
                memcpy(buf + pos, sa.items[i].name, len);
                pos += len;
                buf[pos++] = '=';
                buf[pos++] = '0' + sa.items[i].value_bool;
                buf[pos++] = '\n';
            }
        }
    }
    buf[pos] = 0;
    return pos;
}

int settings_import(const char *data)
{
    (void)data;
    return 0;
}

/* 导航 */
int settings_navigate(setting_category_t category)
{
    if (category >= SETTINGS_MAX_CATEGORIES) return -1;
    sa.current_category = category;
    return 0;
}

int settings_get_current_category(setting_category_t *category)
{
    if (!category) return -1;
    *category = sa.current_category;
    return 0;
}

/* 统计 */
int settings_get_stats(settings_app_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &sa, sizeof(settings_app_t));
    return 0;
}

/* V7.15: 应用字体大小设置 */
int settings_apply_font_size(void)
{
    /* 查找"字体 size"设置项 */
    for (int i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if (sa.items[i].used &&
            strstr(sa.items[i].name, "字体") &&
            strstr(sa.items[i].name, "size")) {
            int val = sa.items[i].value_int;
            /* 将0-100映射到8-64px */
            int font_size = 8 + (val * 56) / 100;
            if (font_size < 8) font_size = 8;
            if (font_size > 64) font_size = 64;

            extern void font_set_size(uint16_t);
            font_set_size((uint16_t)font_size);

            extern void serial_puts(uint16_t, const char*);
            extern void serial_puthex(uint16_t, uint32_t);
            serial_puts(0x3F8, "[SETTINGS] font size applied: ");
            serial_puthex(0x3F8, font_size);
            serial_puts(0x3F8, "px (setting=");
            serial_puthex(0x3F8, val);
            serial_puts(0x3F8, ")\n");

            return font_size;
        }
    }
    return -1;
}

/* V7.15: 获取当前字体大小（像素） */
int settings_get_font_size_px(void)
{
    for (int i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if (sa.items[i].used &&
            strstr(sa.items[i].name, "字体") &&
            strstr(sa.items[i].name, "size")) {
            int val = sa.items[i].value_int;
            return 8 + (val * 56) / 100;
        }
    }
    return 16;  /* 默认16px */
}

/* V8.43: 应用深色模式设置 */
int settings_apply_dark_mode(void)
{
    /* 查找"dark mode"设置项 */
    for (int i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if (sa.items[i].used &&
            (strstr(sa.items[i].name, "dark") ||
             strstr(sa.items[i].name, "深色"))) {
            int enable = sa.items[i].value_bool;

            extern const char *theme_apply(int);
            extern int theme_current(void);

            if (enable) {
                theme_apply(1);  /* THEME_DARK */
                extern void serial_puts(uint16_t, const char*);
                serial_puts(0x3F8, "[SETTINGS] Dark mode enabled\n");
            } else {
                theme_apply(0);  /* THEME_HEPU_BLUE */
                extern void serial_puts(uint16_t, const char*);
                serial_puts(0x3F8, "[SETTINGS] Light mode enabled\n");
            }

            return enable ? 1 : 0;
        }
    }
    return -1;
}

/* V8.43: 获取当前深色模式状态 */
int settings_get_dark_mode(void)
{
    for (int i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if (sa.items[i].used &&
            (strstr(sa.items[i].name, "dark") ||
             strstr(sa.items[i].name, "深色"))) {
            return sa.items[i].value_bool;
        }
    }
    return 0;  /* 默认浅色 */
}

/* V8.43: 切换主题（0=浅色, 1=深色, 2=OLED, 3=TRON） */
int settings_set_theme(int theme_id)
{
    extern const char *theme_apply(int);
    if (theme_id < 0 || theme_id > 3) return -1;

    const char *name = theme_apply(theme_id);

    /* 同步更新设置项 */
    for (int i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if (sa.items[i].used &&
            (strstr(sa.items[i].name, "dark") ||
             strstr(sa.items[i].name, "深色"))) {
            sa.items[i].value_bool = (theme_id == 1 || theme_id == 2) ? 1 : 0;
            break;
        }
    }

    extern void serial_puts(uint16_t, const char*);
    serial_puts(0x3F8, "[SETTINGS] Theme changed: ");
    serial_puts(0x3F8, name);
    serial_puts(0x3F8, "\n");

    return 0;
}

/* V8.43: 应用亮度设置（通过帧缓冲调节） */
int settings_apply_brightness(void)
{
    /* 查找"亮度"设置项 */
    for (int i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if (sa.items[i].used &&
            (strstr(sa.items[i].name, "亮度") ||
             strstr(sa.items[i].name, "brightness"))) {
            int val = sa.items[i].value_int;
            if (val < 0) val = 0;
            if (val > 100) val = 100;

            extern void serial_puts(uint16_t, const char*);
            extern void serial_puthex(uint16_t, uint32_t);
            serial_puts(0x3F8, "[SETTINGS] Brightness: ");
            serial_puthex(0x3F8, val);
            serial_puts(0x3F8, "%\n");

            /* 实际亮度调节需要硬件支持（背光控制）
             * 这里记录设置值，由显示驱动应用 */
            return val;
        }
    }
    return -1;
}

/* V8.43: 获取当前亮度 */
int settings_get_brightness(void)
{
    for (int i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if (sa.items[i].used &&
            (strstr(sa.items[i].name, "亮度") ||
             strstr(sa.items[i].name, "brightness"))) {
            return sa.items[i].value_int;
        }
    }
    return 80;  /* 默认80% */
}

/* V8.43: 应用所有显示设置 */
int settings_apply_display(void)
{
    int applied = 0;

    /* 字体大小 */
    if (settings_apply_font_size() > 0) applied++;

    /* 深色模式 */
    if (settings_apply_dark_mode() >= 0) applied++;

    /* 亮度 */
    if (settings_apply_brightness() >= 0) applied++;

    extern void serial_puts(uint16_t, const char*);
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puts(0x3F8, "[SETTINGS] Display settings applied: ");
    serial_puthex(0x3F8, applied);
    serial_puts(0x3F8, " items\n");

    return applied;
}

/* V8.44: 应用媒体音量（关联AC97） */
int settings_apply_media_volume(void)
{
    /* 查找"media"设置项 */
    for (int i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if (sa.items[i].used &&
            (strstr(sa.items[i].name, "media") ||
             strstr(sa.items[i].name, "媒体"))) {
            int val = sa.items[i].value_int;
            if (val < 0) val = 0;
            if (val > 100) val = 100;

            /* 将0-100映射到AC97的0-63（AC97: 0=最大, 63=静音） */
            int ac97_vol = 63 - (val * 63) / 100;

            extern int ac97_set_pcm_volume(int, int);
            ac97_set_pcm_volume(ac97_vol, ac97_vol);

            extern void serial_puts(uint16_t, const char*);
            extern void serial_puthex(uint16_t, uint32_t);
            serial_puts(0x3F8, "[SETTINGS] Media volume: ");
            serial_puthex(0x3F8, val);
            serial_puts(0x3F8, "% (AC97=");
            serial_puthex(0x3F8, ac97_vol);
            serial_puts(0x3F8, ")\n");

            return val;
        }
    }
    return -1;
}

/* V8.44: 获取当前媒体音量 */
int settings_get_media_volume(void)
{
    for (int i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if (sa.items[i].used &&
            (strstr(sa.items[i].name, "media") ||
             strstr(sa.items[i].name, "媒体"))) {
            return sa.items[i].value_int;
        }
    }
    return 80;  /* 默认80% */
}

/* V8.44: 应用铃声音量 */
int settings_apply_ring_volume(void)
{
    /* 查找"铃声"设置项 */
    for (int i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if (sa.items[i].used &&
            (strstr(sa.items[i].name, "铃声") ||
             strstr(sa.items[i].name, "ring"))) {
            int val = sa.items[i].value_int;
            if (val < 0) val = 0;
            if (val > 100) val = 100;

            /* 铃声音量也通过主音量控制 */
            int ac97_vol = 63 - (val * 63) / 100;

            extern int ac97_set_master_volume(int, int);
            ac97_set_master_volume(ac97_vol, ac97_vol);

            extern void serial_puts(uint16_t, const char*);
            extern void serial_puthex(uint16_t, uint32_t);
            serial_puts(0x3F8, "[SETTINGS] Ring volume: ");
            serial_puthex(0x3F8, val);
            serial_puts(0x3F8, "%\n");

            return val;
        }
    }
    return -1;
}

/* V8.44: 应用所有声音设置 */
int settings_apply_sound(void)
{
    int applied = 0;

    /* 媒体音量 */
    if (settings_apply_media_volume() >= 0) applied++;

    /* 铃声音量 */
    if (settings_apply_ring_volume() >= 0) applied++;

    extern void serial_puts(uint16_t, const char*);
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puts(0x3F8, "[SETTINGS] Sound settings applied: ");
    serial_puthex(0x3F8, applied);
    serial_puts(0x3F8, " items\n");

    return applied;
}

/* V8.44: 静音/取消静音 */
int settings_set_mute(int mute)
{
    extern int ac97_set_master_volume(int, int);
    extern int ac97_set_pcm_volume(int, int);

    if (mute) {
        ac97_set_master_volume(63, 63);  /* 静音 */
        ac97_set_pcm_volume(63, 63);
    } else {
        /* 恢复当前设置的音量 */
        int media = settings_get_media_volume();
        int ac97_vol = 63 - (media * 63) / 100;
        ac97_set_pcm_volume(ac97_vol, ac97_vol);
        ac97_set_master_volume(ac97_vol, ac97_vol);
    }

    extern void serial_puts(uint16_t, const char*);
    serial_puts(0x3F8, mute ? "[SETTINGS] Muted\n" : "[SETTINGS] Unmuted\n");

    return mute ? 1 : 0;
}

/* V8.45: 获取电池状态 */
int settings_get_battery_info(int *percentage, int *charging, int *remaining_s)
{
    battery_info_t battery;
    power_get_battery(&battery);

    if (percentage) *percentage = battery.percentage;
    if (charging) *charging = battery.charging;
    if (remaining_s) *remaining_s = battery.remaining_s;

    return 0;
}

/* V8.45: 设置低电量模式 */
int settings_set_low_power_mode(int enable)
{
    extern int power_set_cpu_freq(uint32_t);
    extern uint32_t power_get_cpu_freq(void);

    if (enable) {
        power_set_cpu_freq(300);  /* 省电模式：最低频率 */
        extern void serial_puts(uint16_t, const char*);
        serial_puts(0x3F8, "[SETTINGS] Low power mode enabled (300MHz)\n");
    } else {
        power_set_cpu_freq(2000);  /* 高性能模式：最高频率 */
        extern void serial_puts(uint16_t, const char*);
        serial_puts(0x3F8, "[SETTINGS] Low power mode disabled (2000MHz)\n");
    }

    /* 同步更新设置项 */
    for (int i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if (sa.items[i].used &&
            (strstr(sa.items[i].name, "省电") ||
             strstr(sa.items[i].name, "low.power") ||
             strstr(sa.items[i].name, "battery.saver"))) {
            sa.items[i].value_bool = enable ? 1 : 0;
            break;
        }
    }

    return enable ? 1 : 0;
}

/* V8.45: 获取低电量模式状态 */
int settings_get_low_power_mode(void)
{
    for (int i = 0; i < SETTINGS_MAX_ITEMS; i++) {
        if (sa.items[i].used &&
            (strstr(sa.items[i].name, "省电") ||
             strstr(sa.items[i].name, "low.power") ||
             strstr(sa.items[i].name, "battery.saver"))) {
            return sa.items[i].value_bool;
        }
    }
    return 0;
}

/* V8.45: 应用所有电池设置 */
int settings_apply_battery(void)
{
    int applied = 0;

    /* 低电量模式 */
    if (settings_set_low_power_mode(settings_get_low_power_mode()) >= 0) applied++;

    extern void serial_puts(uint16_t, const char*);
    extern void serial_puthex(uint16_t, uint32_t);
    serial_puts(0x3F8, "[SETTINGS] Battery settings applied: ");
    serial_puthex(0x3F8, applied);
    serial_puts(0x3F8, " items\n");

    return applied;
}

/* V8.45: 存储信息获取 */
int settings_get_storage_info(uint64_t *total, uint64_t *used, uint64_t *free)
{
    /* 从ext2获取磁盘大小 */
    extern uint32_t ext2_disk_size;
    uint64_t t = (uint64_t)ext2_disk_size;
    uint64_t f = t / 2;  /* 简化：假设50%可用 */

    if (total) *total = t;
    if (used) *used = t - f;
    if (free) *free = f;

    return 0;
}

/* V8.45: 系统信息获取 */
int settings_get_system_info(char *buf, int size)
{
    if (!buf || size <= 0) return -1;

    extern int ss_get_uptime(uint64_t*);
    uint64_t uptime = 0;
    ss_get_uptime(&uptime);

    int hours = (int)(uptime / 3600);
    int minutes = (int)((uptime % 3600) / 60);
    int seconds = (int)(uptime % 60);

    int len = 0;
    len += tools_strcat(buf + len, "=== System Info ===\n", size - len);
    len += tools_strcat(buf + len, "OS: HepuOS v1.0.2\n", size - len);
    len += tools_strcat(buf + len, "Kernel: 3.9MB\n", size - len);

    char tmp[64];
    int tmp_len = 0;
    tmp_len += tools_strcat(tmp + tmp_len, "Uptime: ", 64 - tmp_len);
    char num[16];
    int n = hours, i = 0;
    if (n == 0) num[i++] = '0';
    while (n > 0) { num[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) tmp[tmp_len++] = num[--i];
    tmp_len += tools_strcat(tmp + tmp_len, "h ", 64 - tmp_len);

    n = minutes; i = 0;
    if (n == 0) num[i++] = '0';
    while (n > 0) { num[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) tmp[tmp_len++] = num[--i];
    tmp_len += tools_strcat(tmp + tmp_len, "m ", 64 - tmp_len);

    n = seconds; i = 0;
    if (n == 0) num[i++] = '0';
    while (n > 0) { num[i++] = '0' + (n % 10); n /= 10; }
    while (i > 0) tmp[tmp_len++] = num[--i];
    tmp_len += tools_strcat(tmp + tmp_len, "s\n", 64 - tmp_len);

    len += tools_strcat(buf + len, tmp, size - len);
    len += tools_strcat(buf + len, "Memory: 128MB total\n", size - len);
    len += tools_strcat(buf + len, "Storage: 100GB root partition\n", size - len);

    return len;
}
