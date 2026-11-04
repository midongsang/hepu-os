/* ============================================================================
 * 赫普OS - 统一应用注册表实现 (V2.51)
 *
 * 应用双模式注册：手机模式与桌面模式共享同一份应用清单。
 *  - 8 个原生手机应用（电话/短信/相机/相册/设置/音乐/浏览器/计算器）
 *  - 3 个桌面应用以"双模式"注册（终端/文件/关于），手机桌面也可进入
 *
 * 注册表只存元数据；实际启动由 mobile.c / desktop.c 注入的 launcher 执行，
 * 从而实现"一份清单、两种形态入口"。
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "appreg.h"
#include "vga.h"

/* ---- 注册表数据 ---- */
static const hepu_app_t app_table[] = {
    /* 原生手机应用 (V2.42~V2.48) */
    { 0x01, HEPU_APP_MOBILE, 0, 0xFF, "电话",   "Phone"    },
    { 0x02, HEPU_APP_MOBILE, 1, 0xFF, "短信",   "SMS"      },
    { 0x03, HEPU_APP_MOBILE, 2, 0xFF, "相机",   "Camera"   },
    { 0x04, HEPU_APP_MOBILE, 3, 0xFF, "相册",   "Album"    },
    { 0x05, HEPU_APP_MOBILE, 4, 0xFF, "设置",   "Settings" },
    { 0x06, HEPU_APP_MOBILE, 5, 0xFF, "音乐",   "Music"    },
    { 0x07, HEPU_APP_MOBILE, 6, 0xFF, "浏览器", "Browser"  },
    { 0x08, HEPU_APP_MOBILE, 7, 0xFF, "计算器", "Calc"     },
    /* 双模式桌面应用：手机桌面也可进入 */
    { 0x20, HEPU_APP_BOTH,   8, 0,   "终端",   "Terminal" },
    { 0x21, HEPU_APP_BOTH,   9, 1,   "文件",   "Files"    },
    { 0x22, HEPU_APP_BOTH,  10, 2,   "关于",   "About"    },
};

#define APP_TABLE_N (sizeof(app_table) / sizeof(app_table[0]))

static hepu_app_launch_fn g_mob_launcher  = NULL;
static hepu_app_launch_fn g_desk_launcher = NULL;

/* ---- 基础查询 ---- */
int appreg_init(void)
{
    g_mob_launcher  = NULL;
    g_desk_launcher = NULL;
    return (int)APP_TABLE_N;
}

int appreg_count(void)
{
    return (int)APP_TABLE_N;
}

const hepu_app_t *appreg_get(int idx)
{
    if (idx < 0 || idx >= (int)APP_TABLE_N) return NULL;
    return &app_table[idx];
}

/* ---- launcher 注入 ---- */
void appreg_set_mobile_launcher(hepu_app_launch_fn fn)
{
    g_mob_launcher = fn;
}
void appreg_set_desktop_launcher(hepu_app_launch_fn fn)
{
    g_desk_launcher = fn;
}

/* ---- 手机模式侧 ---- */
int appreg_mobile_count(void)
{
    int n = 0;
    for (unsigned i = 0; i < APP_TABLE_N; i++)
        if (app_table[i].mode & HEPU_APP_MOBILE) n++;
    return n;
}

int appreg_mobile_slot_to_idx(int slot)
{
    int n = 0;
    for (unsigned i = 0; i < APP_TABLE_N; i++) {
        if (app_table[i].mode & HEPU_APP_MOBILE) {
            if (n == slot) return (int)i;
            n++;
        }
    }
    return -1;
}

const char *appreg_mobile_name(int slot)
{
    int idx = appreg_mobile_slot_to_idx(slot);
    if (idx < 0) return NULL;
    return app_table[idx].name_zh;
}

int appreg_mobile_launch(int slot)
{
    int idx = appreg_mobile_slot_to_idx(slot);
    if (idx < 0) return -1;
    if (!g_mob_launcher) return -2;
    return g_mob_launcher(slot);
}

/* ---- 桌面模式侧 ---- */
int appreg_desktop_count(void)
{
    int n = 0;
    for (unsigned i = 0; i < APP_TABLE_N; i++)
        if (app_table[i].mode & HEPU_APP_DESKTOP) n++;
    return n;
}

int appreg_desktop_slot_to_idx(int slot)
{
    int n = 0;
    for (unsigned i = 0; i < APP_TABLE_N; i++) {
        if (app_table[i].mode & HEPU_APP_DESKTOP) {
            if (n == slot) return (int)i;
            n++;
        }
    }
    return -1;
}

const char *appreg_desktop_name(int slot)
{
    int idx = appreg_desktop_slot_to_idx(slot);
    if (idx < 0) return NULL;
    return app_table[idx].name_zh;
}

int appreg_desktop_launch(int slot)
{
    int idx = appreg_desktop_slot_to_idx(slot);
    if (idx < 0) return -1;
    if (!g_desk_launcher) return -2;
    return g_desk_launcher(slot);
}
