/* ============================================================================
 * 赫普OS - 系统国际化(i18n)框架
 * ========================================================================== */

#include <stdint.h>
#include "i18n_system.h"
#include "vga.h"
#include "string.h"

static i18ns_system_t i18n;

/* 内置语言列表 */
static const struct {
    i18ns_language_t id;
    const char *code;
    const char *name;
    const char *native;
    i18ns_direction_t dir;
    const char *currency;
    int metric;
} builtin_langs[] = {
    {I18N_LANG_ZH_CN, "zh-CN", "Chinese(Simplified)", " Simplified Chinese ", I18N_DIR_LTR, "CNY", 1},
    {I18N_LANG_ZH_TW, "zh-TW", "Chinese(Traditional)", " 繁體 in 文 ", I18N_DIR_LTR, "TWD", 1},
    {I18N_LANG_EN_US, "en-US", "English(US)", "English", I18N_DIR_LTR, "USD", 0},
    {I18N_LANG_EN_GB, "en-GB", "English(UK)", "English", I18N_DIR_LTR, "GBP", 0},
    {I18N_LANG_JA_JP, "ja-JP", "Japanese", " 日本語 ", I18N_DIR_LTR, "JPY", 1},
    {I18N_LANG_KO_KR, "ko-KR", "Korean", "한국어", I18N_DIR_LTR, "KRW", 1},
    {I18N_LANG_FR_FR, "fr-FR", "French", "Français", I18N_DIR_LTR, "EUR", 1},
    {I18N_LANG_DE_DE, "de-DE", "German", "Deutsch", I18N_DIR_LTR, "EUR", 1},
    {I18N_LANG_ES_ES, "es-ES", "Spanish", "Español", I18N_DIR_LTR, "EUR", 1},
    {I18N_LANG_IT_IT, "it-IT", "Italian", "Italiano", I18N_DIR_LTR, "EUR", 1},
    {I18N_LANG_PT_BR, "pt-BR", "Portuguese(BR)", "Português", I18N_DIR_LTR, "BRL", 1},
    {I18N_LANG_RU_RU, "ru-RU", "Russian", "Русский", I18N_DIR_LTR, "RUB", 1},
    {I18N_LANG_AR_SA, "ar-SA", "Arabic", "العربية", I18N_DIR_RTL, "SAR", 1},
    {I18N_LANG_TH_TH, "th-TH", "Thai", "ไทย", I18N_DIR_LTR, "THB", 1},
    {I18N_LANG_VI_VN, "vi-VN", "Vietnamese", "Tiếng Việt", I18N_DIR_LTR, "VND", 1},
    {I18N_LANG_ID_ID, "id-ID", "Indonesian", "Bahasa Indonesia", I18N_DIR_LTR, "IDR", 1},
    {I18N_LANG_HI_IN, "hi-IN", "Hindi", "हिन्दी", I18N_DIR_LTR, "INR", 1},
    {I18N_LANG_TR_TR, "tr-TR", "Turkish", "Türkçe", I18N_DIR_LTR, "TRY", 1},
    {I18N_LANG_NL_NL, "nl-NL", "Dutch", "Nederlands", I18N_DIR_LTR, "EUR", 1},
    {I18N_LANG_PL_PL, "pl-PL", "Polish", "Polski", I18N_DIR_LTR, "PLN", 1},
    {I18N_LANG_SV_SE, "sv-SE", "Swedish", "Svenska", I18N_DIR_LTR, "SEK", 1},
    {I18N_LANG_FI_FI, "fi-FI", "Finnish", "Suomi", I18N_DIR_LTR, "EUR", 1},
    {I18N_LANG_NO_NO, "no-NO", "Norwegian", "Norsk", I18N_DIR_LTR, "NOK", 1},
    {I18N_LANG_DA_DK, "da-DK", "Danish", "Dansk", I18N_DIR_LTR, "DKK", 1},
    {I18N_LANG_CS_CZ, "cs-CZ", "Czech", "Čeština", I18N_DIR_LTR, "CZK", 1},
    {I18N_LANG_HU_HU, "hu-HU", "Hungarian", "Magyar", I18N_DIR_LTR, "HUF", 1},
    {I18N_LANG_EL_GR, "el-GR", "Greek", "Ελληνικά", I18N_DIR_LTR, "EUR", 1},
    {I18N_LANG_HE_IL, "he-IL", "Hebrew", "עברית", I18N_DIR_RTL, "ILS", 1},
    {I18N_LANG_UK_UA, "uk-UA", "Ukrainian", "Українська", I18N_DIR_LTR, "UAH", 1},
    {I18N_LANG_RO_RO, "ro-RO", "Romanian", "Română", I18N_DIR_LTR, "RON", 1},
};

void i18ns_system_init(void)
{
    memset(&i18n, 0, sizeof(i18ns_system_t));
    i18n.initialized = 1;
    i18n.enabled = 1;
    i18n.current_language = I18N_LANG_ZH_CN;
    i18n.locale.language = I18N_LANG_ZH_CN;
    i18n.locale.secondary_language = I18N_LANG_EN_US;
    i18n.locale.time_format_24h = 1;
    i18n.locale.measurement_system = 0;  /* 公制 */
    strncpy(i18n.locale.timezone, "Asia/Shanghai", 31);

    /* 注册内置语言 */
    int count = sizeof(builtin_langs) / sizeof(builtin_langs[0]);
    for (int i = 0; i < count && i < I18N_MAX_LANGUAGES; i++) {
        i18n.languages[i].used = 1;
        i18n.languages[i].id = builtin_langs[i].id;
        strncpy(i18n.languages[i].code, builtin_langs[i].code, 7);
        strncpy(i18n.languages[i].name, builtin_langs[i].name, 127);
        strncpy(i18n.languages[i].native_name, builtin_langs[i].native, 127);
        i18n.languages[i].direction = builtin_langs[i].dir;
        strncpy(i18n.languages[i].currency_symbol, builtin_langs[i].currency, 7);
        i18n.languages[i].metric = builtin_langs[i].metric;
        i18n.languages[i].supported = 1;
        if (builtin_langs[i].id == I18N_LANG_ZH_CN) i18n.languages[i].is_default = 1;
        i18n.language_count++;
    }

    vga_puts("[I18N] system i18n framework init done \n");
    vga_printf(" lang: %d, current: Simplified Chinese, RTL: %d \n",
               i18n.language_count, 2);
}

int i18ns_set_language(i18ns_language_t lang)
{
    if (lang < 0 || lang >= I18N_MAX_LANGUAGES) return -1;
    i18n.current_language = lang;
    i18n.locale.language = lang;
    return 0;
}

int i18ns_get_language(i18ns_language_t *lang)
{
    if (!lang) return -1;
    *lang = i18n.current_language;
    return 0;
}

int i18ns_get_language_info(i18ns_language_t lang, i18ns_language_info_t *info)
{
    if (!info) return -1;
    for (int i = 0; i < I18N_MAX_LANGUAGES; i++) {
        if (i18n.languages[i].used && i18n.languages[i].id == lang) {
            memcpy(info, &i18n.languages[i], sizeof(i18ns_language_info_t));
            return 0;
        }
    }
    return -1;
}

int i18ns_list_languages(i18ns_language_info_t *langs, int max)
{
    if (!langs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < I18N_MAX_LANGUAGES && count < max; i++) {
        if (i18n.languages[i].used) memcpy(&langs[count++], &i18n.languages[i], sizeof(i18ns_language_info_t));
    }
    return count;
}

int i18ns_get_language_by_code(const char *code, i18ns_language_t *lang)
{
    if (!code || !lang) return -1;
    for (int i = 0; i < I18N_MAX_LANGUAGES; i++) {
        if (i18n.languages[i].used && strcmp(i18n.languages[i].code, code) == 0) {
            *lang = i18n.languages[i].id;
            return 0;
        }
    }
    return -1;
}

int i18ns_is_rtl(i18ns_language_t lang)
{
    for (int i = 0; i < I18N_MAX_LANGUAGES; i++) {
        if (i18n.languages[i].used && i18n.languages[i].id == lang)
            return i18n.languages[i].direction == I18N_DIR_RTL ? 1 : 0;
    }
    return 0;
}

int i18ns_register_string(const char *key, const char *default_text)
{
    for (int i = 0; i < I18NS_MAX_STRINGS; i++) {
        if (!i18n.strings[i].used) {
            memset(&i18n.strings[i], 0, sizeof(i18ns_string_entry_t));
            i18n.strings[i].used = 1;
            i18n.strings[i].id = i;
            if (key) strncpy(i18n.strings[i].key, key, 63);
            if (default_text) strncpy(i18n.strings[i].default_text, default_text, 255);
            i18n.string_count++;
            return i;
        }
    }
    return -1;
}

int i18ns_translate(const char *key, char *buf, int size)
{
    return i18ns_translate_lang(key, i18n.current_language, buf, size);
}

int i18ns_translate_lang(const char *key, i18ns_language_t lang, char *buf, int size)
{
    (void)lang;
    if (!key || !buf || size <= 0) return -1;
    for (int i = 0; i < I18NS_MAX_STRINGS; i++) {
        if (i18n.strings[i].used && strcmp(i18n.strings[i].key, key) == 0) {
            /* 简化：返回默认文本（英文），实际应从语言包查找翻译 */
            int len = 0;
            const char *src = i18n.strings[i].default_text;
            while (src[len] && len < size - 1) { buf[len] = src[len]; len++; }
            buf[len] = 0;
            i18n.total_translations++;
            return len;
        }
    }
    /* 未找到，返回key */
    int len = 0;
    while (key[len] && len < size - 1) { buf[len] = key[len]; len++; }
    buf[len] = 0;
    i18n.fallback_count++;
    return len;
}

int i18ns_get_string_id(const char *key)
{
    for (int i = 0; i < I18NS_MAX_STRINGS; i++) {
        if (i18n.strings[i].used && strcmp(i18n.strings[i].key, key) == 0) return i;
    }
    return -1;
}

int i18ns_get_default_text(int id, char *buf, int size)
{
    if (id < 0 || id >= I18NS_MAX_STRINGS || !i18n.strings[id].used || !buf || size <= 0) return -1;
    int len = 0;
    const char *src = i18n.strings[id].default_text;
    while (src[len] && len < size - 1) { buf[len] = src[len]; len++; }
    buf[len] = 0;
    return len;
}

int i18ns_load_pack(i18ns_language_t lang, const char *path)
{
    (void)path;
    for (int i = 0; i < I18N_MAX_LANGUAGES; i++) {
        if (!i18n.packs[i].used) {
            memset(&i18n.packs[i], 0, sizeof(i18ns_pack_t));
            i18n.packs[i].used = 1;
            i18n.packs[i].lang = lang;
            i18n.packs[i].loaded = 1;
            i18n.pack_count++;
            return 0;
        }
    }
    return -1;
}

int i18ns_unload_pack(i18ns_language_t lang)
{
    for (int i = 0; i < I18N_MAX_LANGUAGES; i++) {
        if (i18n.packs[i].used && i18n.packs[i].lang == lang) {
            i18n.packs[i].used = 0;
            i18n.pack_count--;
            return 0;
        }
    }
    return -1;
}

int i18ns_get_pack(i18ns_language_t lang, i18ns_pack_t *pack)
{
    if (!pack) return -1;
    for (int i = 0; i < I18N_MAX_LANGUAGES; i++) {
        if (i18n.packs[i].used && i18n.packs[i].lang == lang) {
            memcpy(pack, &i18n.packs[i], sizeof(i18ns_pack_t));
            return 0;
        }
    }
    return -1;
}

int i18ns_list_packs(i18ns_pack_t *packs, int max)
{
    if (!packs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < I18N_MAX_LANGUAGES && count < max; i++) {
        if (i18n.packs[i].used) memcpy(&packs[count++], &i18n.packs[i], sizeof(i18ns_pack_t));
    }
    return count;
}

int i18ns_set_locale(const i18ns_locale_t *locale)
{
    if (!locale) return -1;
    memcpy(&i18n.locale, locale, sizeof(i18ns_locale_t));
    return 0;
}

int i18ns_get_locale(i18ns_locale_t *locale)
{
    if (!locale) return -1;
    memcpy(locale, &i18n.locale, sizeof(i18ns_locale_t));
    return 0;
}

int i18ns_set_timezone(const char *tz)
{
    if (!tz) return -1;
    strncpy(i18n.locale.timezone, tz, 31);
    return 0;
}

int i18ns_get_timezone(char *tz, int size)
{
    if (!tz || size <= 0) return -1;
    int len = 0;
    while (i18n.locale.timezone[len] && len < size - 1) { tz[len] = i18n.locale.timezone[len]; len++; }
    tz[len] = 0;
    return len;
}

int i18ns_format_date(int year, int month, int day, char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    /* 简化：YYYY-MM-DD */
    int len = 0;
    char tmp[16];
    /* year */
    int y = year, ylen = 0;
    if (y == 0) tmp[ylen++] = '0';
    while (y > 0) { tmp[ylen++] = '0' + y % 10; y /= 10; }
    for (int i = ylen - 1; i >= 0 && len < size - 1; i--) buf[len++] = tmp[i];
    if (len < size - 1) buf[len++] = '-';
    /* month */
    if (month < 10 && len < size - 1) buf[len++] = '0';
    int m = month, mlen = 0;
    while (m > 0) { tmp[mlen++] = '0' + m % 10; m /= 10; }
    for (int i = mlen - 1; i >= 0 && len < size - 1; i--) buf[len++] = tmp[i];
    if (len < size - 1) buf[len++] = '-';
    /* day */
    if (day < 10 && len < size - 1) buf[len++] = '0';
    int d = day, dlen = 0;
    while (d > 0) { tmp[dlen++] = '0' + d % 10; d /= 10; }
    for (int i = dlen - 1; i >= 0 && len < size - 1; i--) buf[len++] = tmp[i];
    buf[len] = 0;
    return len;
}

int i18ns_format_time(int hour, int minute, int second, char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    int len = 0;
    char tmp[8];
    if (hour < 10 && len < size - 1) buf[len++] = '0';
    int h = hour, hlen = 0;
    if (h == 0) tmp[hlen++] = '0';
    while (h > 0) { tmp[hlen++] = '0' + h % 10; h /= 10; }
    for (int i = hlen - 1; i >= 0 && len < size - 1; i--) buf[len++] = tmp[i];
    if (len < size - 1) buf[len++] = ':';
    if (minute < 10 && len < size - 1) buf[len++] = '0';
    int mi = minute, milen = 0;
    if (mi == 0) tmp[milen++] = '0';
    while (mi > 0) { tmp[milen++] = '0' + mi % 10; mi /= 10; }
    for (int i = milen - 1; i >= 0 && len < size - 1; i--) buf[len++] = tmp[i];
    if (len < size - 1) buf[len++] = ':';
    if (second < 10 && len < size - 1) buf[len++] = '0';
    int s = second, slen = 0;
    if (s == 0) tmp[slen++] = '0';
    while (s > 0) { tmp[slen++] = '0' + s % 10; s /= 10; }
    for (int i = slen - 1; i >= 0 && len < size - 1; i--) buf[len++] = tmp[i];
    buf[len] = 0;
    return len;
}

int i18ns_format_number(int64_t number, char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    int len = 0;
    char tmp[32];
    int tlen = 0;
    int64_t n = number;
    if (n == 0) tmp[tlen++] = '0';
    if (n < 0) { buf[len++] = '-'; n = -n; }
    while (n > 0) { tmp[tlen++] = '0' + (int)(n % 10); n /= 10; }
    for (int i = tlen - 1; i >= 0 && len < size - 1; i--) buf[len++] = tmp[i];
    buf[len] = 0;
    return len;
}

int i18ns_format_currency(int64_t amount, char *buf, int size)
{
    if (!buf || size <= 0) return -1;
    /* 简化：¥ + 数字 */
    int len = 0;
    if (len < size - 1) buf[len++] = 0xC2;  /* ¥ UTF-8 */
    if (len < size - 1) buf[len++] = 0xA5;
    int numlen = i18ns_format_number(amount, buf + len, size - len);
    if (numlen > 0) len += numlen;
    return len;
}

int i18ns_set_secondary_language(i18ns_language_t lang)
{
    i18n.locale.secondary_language = lang;
    return 0;
}

int i18ns_get_secondary_language(i18ns_language_t *lang)
{
    if (!lang) return -1;
    *lang = i18n.locale.secondary_language;
    return 0;
}

int i18ns_get_stats(i18ns_system_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &i18n, sizeof(i18ns_system_t));
    return 0;
}
