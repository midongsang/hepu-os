#ifndef I18N_SYSTEM_H
#define I18N_SYSTEM_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 系统国际化(i18n)框架
 * 多语言支持 / 语言包 / 本地化 / 区域设置
 * ========================================================================== */

#define I18N_MAX_LANGUAGES 64
#define I18NS_MAX_STRINGS 4096
#define I18NS_MAX_LOCALES 32
#define I18N_LANG_CODE_LEN 8
#define I18N_COUNTRY_CODE_LEN 4
#define I18N_NAME_LEN 128

/* 语言ID */
typedef enum {
    I18N_LANG_ZH_CN = 0,   /* 简体中文 */
    I18N_LANG_ZH_TW,       /* 繁体中文 */
    I18N_LANG_ZH_HK,       /* 粤语 */
    I18N_LANG_EN_US,       /* 英语(美国) */
    I18N_LANG_EN_GB,       /* 英语(英国) */
    I18N_LANG_JA_JP,       /* 日语 */
    I18N_LANG_KO_KR,       /* 韩语 */
    I18N_LANG_FR_FR,       /* 法语 */
    I18N_LANG_DE_DE,       /* 德语 */
    I18N_LANG_ES_ES,       /* 西班牙语 */
    I18N_LANG_IT_IT,       /* 意大利语 */
    I18N_LANG_PT_BR,       /* 葡萄牙语(巴西) */
    I18N_LANG_RU_RU,       /* 俄语 */
    I18N_LANG_AR_SA,       /* 阿拉伯语 */
    I18N_LANG_TH_TH,       /* 泰语 */
    I18N_LANG_VI_VN,       /* 越南语 */
    I18N_LANG_ID_ID,       /* 印尼语 */
    I18N_LANG_MS_MY,       /* 马来语 */
    I18N_LANG_HI_IN,       /* 印地语 */
    I18N_LANG_TR_TR,       /* 土耳其语 */
    I18N_LANG_NL_NL,       /* 荷兰语 */
    I18N_LANG_PL_PL,       /* 波兰语 */
    I18N_LANG_SV_SE,       /* 瑞典语 */
    I18N_LANG_FI_FI,       /* 芬兰语 */
    I18N_LANG_NO_NO,       /* 挪威语 */
    I18N_LANG_DA_DK,       /* 丹麦语 */
    I18N_LANG_CS_CZ,       /* 捷克语 */
    I18N_LANG_HU_HU,       /* 匈牙利语 */
    I18N_LANG_EL_GR,       /* 希腊语 */
    I18N_LANG_HE_IL,       /* 希伯来语 */
    I18N_LANG_UK_UA,       /* 乌克兰语 */
    I18N_LANG_RO_RO,       /* 罗马尼亚语 */
} i18ns_language_t;

/* 文字方向 */
typedef enum {
    I18N_DIR_LTR = 0,  /* 从左到右 */
    I18N_DIR_RTL,      /* 从右到左 */
} i18ns_direction_t;

/* 语言信息 */
typedef struct {
    int used;
    i18ns_language_t id;
    char code[I18N_LANG_CODE_LEN];    /* zh-CN */
    char lang_code[I18N_LANG_CODE_LEN]; /* zh */
    char country[I18N_COUNTRY_CODE_LEN]; /* CN */
    char name[I18N_NAME_LEN];         /* 中文(简体) */
    char native_name[I18N_NAME_LEN];  /* 简体中文 */
    i18ns_direction_t direction;
    int is_default;
    int supported;
    /* 区域设置 */
    char date_format[32];
    char time_format[32];
    char decimal_separator[4];
    char thousands_separator[4];
    char currency_symbol[8];
    char currency_code[8];
    int first_day_of_week;  /* 0=周日,1=周一 */
    int metric;  /* 1=公制,0=英制 */
} i18ns_language_info_t;

/* 字符串条目 */
typedef struct {
    int used;
    int id;
    char key[64];
    char default_text[256];  /* 英文默认 */
} i18ns_string_entry_t;

/* 语言包 */
typedef struct {
    int used;
    i18ns_language_t lang;
    char version[16];
    int string_count;
    int loaded;
    uint64_t file_size;
} i18ns_pack_t;

/* 区域设置 */
typedef struct {
    i18ns_language_t language;
    i18ns_language_t secondary_language;
    char timezone[32];
    int auto_timezone;
    int auto_time;
    int date_format_custom;
    int time_format_24h;
    int temperature_unit;  /* 0=C,1=F */
    int measurement_system;  /* 0=metric,1=imperial */
} i18ns_locale_t;

/* i18n状态 */
typedef struct {
    int initialized;
    int enabled;
    i18ns_language_t current_language;
    i18ns_locale_t locale;
    /* 语言 */
    i18ns_language_info_t languages[I18N_MAX_LANGUAGES];
    int language_count;
    /* 字符串 */
    i18ns_string_entry_t strings[I18NS_MAX_STRINGS];
    int string_count;
    /* 语言包 */
    i18ns_pack_t packs[I18N_MAX_LANGUAGES];
    int pack_count;
    /* 统计 */
    uint64_t total_translations;
    int fallback_count;
} i18ns_system_t;

/* 初始化 */
void i18ns_system_init(void);

/* 语言管理 */
int i18ns_set_language(i18ns_language_t lang);
int i18ns_get_language(i18ns_language_t *lang);
int i18ns_get_language_info(i18ns_language_t lang, i18ns_language_info_t *info);
int i18ns_list_languages(i18ns_language_info_t *langs, int max);
int i18ns_get_language_by_code(const char *code, i18ns_language_t *lang);
int i18ns_is_rtl(i18ns_language_t lang);

/* 字符串翻译 */
int i18ns_register_string(const char *key, const char *default_text);
int i18ns_translate(const char *key, char *buf, int size);
int i18ns_translate_lang(const char *key, i18ns_language_t lang, char *buf, int size);
int i18ns_get_string_id(const char *key);
int i18ns_get_default_text(int id, char *buf, int size);

/* 语言包 */
int i18ns_load_pack(i18ns_language_t lang, const char *path);
int i18ns_unload_pack(i18ns_language_t lang);
int i18ns_get_pack(i18ns_language_t lang, i18ns_pack_t *pack);
int i18ns_list_packs(i18ns_pack_t *packs, int max);

/* 区域设置 */
int i18ns_set_locale(const i18ns_locale_t *locale);
int i18ns_get_locale(i18ns_locale_t *locale);
int i18ns_set_timezone(const char *tz);
int i18ns_get_timezone(char *tz, int size);
int i18ns_format_date(int year, int month, int day, char *buf, int size);
int i18ns_format_time(int hour, int minute, int second, char *buf, int size);
int i18ns_format_number(int64_t number, char *buf, int size);
int i18ns_format_currency(int64_t amount, char *buf, int size);

/* 配置 */
int i18ns_set_secondary_language(i18ns_language_t lang);
int i18ns_get_secondary_language(i18ns_language_t *lang);

/* 统计 */
int i18ns_get_stats(i18ns_system_t *stats);

#endif /* I18N_SYSTEM_H */
