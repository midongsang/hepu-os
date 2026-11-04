/* ============================================================================
 * 赫普OS 64位 - 多语言框架 (l10n)
 * V0.13.23: P1 底层 — 系统多语言与语言切换
 *
 * 字符串表: 双语言 (zh_CN 中文 / en_US 英文), ID 查询
 * 语言设置: 持久化到系统注册表 sys.lang (zh|en)
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "l10n64.h"

extern const char *reg64_get(const char *key, const char *def);
extern int reg64_set(const char *key, const char *value);
extern int reg64_save(void);
extern void vga_puts(const char *s);

/* 语言 ID */
enum { HEPU_LANG_ZH = 0, HEPU_LANG_EN = 1 };

static int g_lang = HEPU_LANG_EN;

/* 字符串表: [语言][ID] */
static const char *g_str[2][L10N_STR_COUNT] = {
    /* zh_CN */
    {
        "\xe6\xac\xa2\xe8\xbf\x8e\xe4\xbd\xbf\xe7\x94\xa8 Hepu OS",              /* 欢迎使用 Hepu OS */
        "\xe8\xaf\xb7\xe8\xbe\x93\xe5\x85\xa5\xe5\xaf\x86\xe7\xa0\x81",          /* 请输入密码 */
        "\xe7\x82\xb9\xe5\x87\xbb\xe4\xbb\xbb\xe6\x84\x8f\xe5\x8a\xa8\xe4\xbd\x9c\xe8\xa7\xa3\xe9\x94\x81", /* 点击任意动作解锁 */
        "\xe5\x85\xb3\xe6\x9c\xba",                                              /* 关机 */
        "\xe7\xbd\x91\xe7\xbb\x9c",                                              /* 网络 */
        "\xe9\x9f\xb3\xe9\x87\x8f",                                              /* 音量 */
        "\xe7\x94\xb5\xe6\xb1\xa0",                                              /* 电池 */
        "\xe7\xb3\xbb\xe7\xbb\x9f\xe8\xae\xbe\xe7\xbd\xae",                      /* 系统设置 */
        "\xe8\xaf\xad\xe8\xa8\x80",                                              /* 语言 */
        "\xe5\xbd\x93\xe5\x89\x8d\xe8\xaf\xad\xe8\xa8\x80",                      /* 当前语言 */
        "\xe5\x88\x87\xe6\x8d\xa2\xe8\xaf\xad\xe8\xa8\x80",                      /* 切换语言 */
        "\xe8\xa7\xa3\xe9\x94\x81",                                              /* 解锁 */
        "ESC \xe9\x94\x81\xe5\xb1\x8f",                                          /* ESC 锁屏 */
        "%02d:%02d %04d-%02d-%02d",                                              /* 时钟格式 */
    },
    /* en_US */
    {
        "Welcome to Hepu OS",
        "Enter password",
        "Click any action to unlock",
        "Shut down",
        "Network",
        "Volume",
        "Battery",
        "System Settings",
        "Language",
        "Current language",
        "Switch language",
        "Unlock",
        "ESC to lock",
        "%02d:%02d %04d-%02d-%02d",
    }
};

/* 初始化: 从注册表读语言 */
int l10n64_init(void)
{
    const char *v = reg64_get("sys.lang", 0);
    if (v && v[0] == 'z' && v[1] == 'h' && !v[2]) {
        g_lang = HEPU_LANG_ZH;
    } else {
        g_lang = HEPU_LANG_EN;
    }
    vga_puts("[L10N] language: ");
    vga_puts(g_lang == HEPU_LANG_EN ? "en_US" : "zh_CN");
    vga_puts("\n");
    return 0;
}

/* 当前语言: 0=zh, 1=en */
int l10n64_get_lang(void)
{
    return g_lang;
}

/* 切换语言并持久化 */
int l10n64_set_lang(int lang)
{
    if (lang != HEPU_LANG_ZH && lang != HEPU_LANG_EN) return -1;
    g_lang = lang;
    reg64_set("sys.lang", lang == HEPU_LANG_EN ? "en" : "zh");
    reg64_save();
    return 0;
}

/* 语言名(展示用): "zh_CN" / "en_US" */
const char *l10n64_lang_name(void)
{
    return g_lang == HEPU_LANG_EN ? "en_US" : "zh_CN";
}

/* 查询翻译字符串 */
const char *l10n64_tr(int id)
{
    if (id < 0 || id >= L10N_STR_COUNT) return "";
    return g_str[g_lang][id];
}
