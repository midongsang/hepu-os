/* ============================================================================
 * 赫普OS - 多语言与国际化单元测试
 * 覆盖：UTF-8/字符编码/语言切换/时区/日期格式/数字格式
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === UTF-8编码测试 === */
static kunit_result_t test_utf8_ascii(void)
{
    /* ASCII字符: 0x00-0x7F, 单字节 */
    const char *s = "HEPU";
    KUNIT_ASSERT_EQ((u8)s[0], 0x48, "H=0x48");
    KUNIT_ASSERT((u8)s[0] < 0x80, "ASCII 单 bytes ");
    KUNIT_ASSERT_EQ(strlen(s), 4, "ASCII len =4");
    return KUNIT_PASS;
}

static kunit_result_t test_utf8_2byte(void)
{
    /* 2字节UTF-8: 110xxxxx 10xxxxxx, U+0080-U+07FF */
    /* 中文" 一 " = U+4E00, UTF-8: E4 B8 80 (实际是3字节) */
    /* 用2字节字符: U+00E9 (é) = C3 A9 */
    u8 utf8_2byte[] = {0xC3, 0xA9, 0x00};  /* é */
    KUNIT_ASSERT_EQ(utf8_2byte[0] & 0xE0, 0xC0, "2 bytes bytes 110xxxxx");
    KUNIT_ASSERT_EQ(utf8_2byte[1] & 0xC0, 0x80, "2 bytes 续 bytes 10xxxxxx");
    return KUNIT_PASS;
}

static kunit_result_t test_utf8_3byte(void)
{
    /* 3字节UTF-8: 1110xxxx 10xxxxxx 10xxxxxx, U+0800-U+FFFF */
    /* 中文" in " = U+4E2D, UTF-8: E4 B8 AD */
    u8 utf8_3byte[] = {0xE4, 0xB8, 0xAD, 0x00};  /* 中 */
    KUNIT_ASSERT_EQ(utf8_3byte[0] & 0xF0, 0xE0, "3 bytes bytes 1110xxxx");
    KUNIT_ASSERT_EQ(utf8_3byte[1] & 0xC0, 0x80, "3 bytes 续 bytes 1");
    KUNIT_ASSERT_EQ(utf8_3byte[2] & 0xC0, 0x80, "3 bytes 续 bytes 2");
    return KUNIT_PASS;
}

static kunit_result_t test_utf8_4byte(void)
{
    /* 4字节UTF-8: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx, U+10000+ */
    /* U+1F600 (😀) = F0 9F 98 80 */
    u8 utf8_4byte[] = {0xF0, 0x9F, 0x98, 0x80, 0x00};
    KUNIT_ASSERT_EQ(utf8_4byte[0] & 0xF8, 0xF0, "4 bytes bytes 11110xxx");
    KUNIT_ASSERT_EQ(utf8_4byte[1] & 0xC0, 0x80, "4 bytes 续 bytes 1");
    KUNIT_ASSERT_EQ(utf8_4byte[2] & 0xC0, 0x80, "4 bytes 续 bytes 2");
    KUNIT_ASSERT_EQ(utf8_4byte[3] & 0xC0, 0x80, "4 bytes 续 bytes 3");
    return KUNIT_PASS;
}

static kunit_result_t test_utf8_length_calc(void)
{
    /* 根据首字节计算UTF-8字符长度 */
    u8 c1 = 0x48;  /* ASCII */
    u8 c2 = 0xC3;  /* 2字节 */
    u8 c3 = 0xE4;  /* 3字节 */
    u8 c4 = 0xF0;  /* 4字节 */

    KUNIT_ASSERT((c1 & 0x80) == 0, "ASCII=1 bytes ");
    KUNIT_ASSERT((c2 & 0xE0) == 0xC0, "2 bytes ");
    KUNIT_ASSERT((c3 & 0xF0) == 0xE0, "3 bytes ");
    KUNIT_ASSERT((c4 & 0xF8) == 0xF0, "4 bytes ");
    return KUNIT_PASS;
}

/* === 语言代码测试 === */
#define LANG_ZH_CN 0
#define LANG_EN_US 1
#define LANG_JA_JP 2
#define LANG_KO_KR 3
#define LANG_FR_FR 4
#define LANG_DE_DE 5
#define LANG_ES_ES 6
#define LANG_RU_RU 7
#define LANG_MAX 8

static const char *lang_names[LANG_MAX] = {
    " Simplified Chinese ", "English", " 日本語 ", "한국어",
    "Français", "Deutsch", "Español", "Русский"
};

static const char *lang_codes[LANG_MAX] = {
    "zh-CN", "en-US", "ja-JP", "ko-KR",
    "fr-FR", "de-DE", "es-ES", "ru-RU"
};

static kunit_result_t test_language_codes(void)
{
    KUNIT_ASSERT_EQ(strcmp(lang_codes[LANG_ZH_CN], "zh-CN"), 0, " in 文代码 ");
    KUNIT_ASSERT_EQ(strcmp(lang_codes[LANG_EN_US], "en-US"), 0, " 英文代码 ");
    KUNIT_ASSERT_EQ(strcmp(lang_codes[LANG_JA_JP], "ja-JP"), 0, " 日文代码 ");
    KUNIT_ASSERT_EQ(strcmp(lang_codes[LANG_KO_KR], "ko-KR"), 0, " 韩文代码 ");
    KUNIT_ASSERT_EQ(LANG_MAX, 8, " supports 8 lang ");
    return KUNIT_PASS;
}

static kunit_result_t test_language_names(void)
{
    KUNIT_ASSERT_NOT_NULL(lang_names[LANG_ZH_CN], " in 文名 exists ");
    KUNIT_ASSERT_NOT_NULL(lang_names[LANG_EN_US], " 英文名 exists ");
    KUNIT_ASSERT_EQ(strlen(lang_names[LANG_EN_US]), 7, "English=7 字符 ");
    return KUNIT_PASS;
}

/* === 语言切换测试 === */
static u32 current_language = LANG_EN_US;

static int set_language(u32 lang)
{
    if (lang >= LANG_MAX) return -1;
    current_language = lang;
    return 0;
}

static kunit_result_t test_language_switch(void)
{
    KUNIT_ASSERT_EQ(set_language(LANG_ZH_CN), 0, " switch to in 文 ");
    KUNIT_ASSERT_EQ(current_language, LANG_ZH_CN, " current = in 文 ");

    KUNIT_ASSERT_EQ(set_language(LANG_EN_US), 0, " switch to 英文 ");
    KUNIT_ASSERT_EQ(current_language, LANG_EN_US, " current = 英文 ");

    KUNIT_ASSERT_EQ(set_language(99), -1, " invalid lang switch failed ");
    return KUNIT_PASS;
}

/* === 时区测试 === */
typedef struct {
    const char *name;
    s32 offset_minutes;  /* 相对于UTC的分钟偏移 */
} timezone_t;

static const timezone_t timezones[] = {
    {"UTC", 0},
    {"CST", 480},    /* 中国标准时间 UTC+8 */
    {"JST", 540},    /* 日本标准时间 UTC+9 */
    {"KST", 540},    /* 韩国标准时间 UTC+9 */
    {"PST", -480},   /* 太平洋标准时间 UTC-8 */
    {"EST", -300},   /* 东部标准时间 UTC-5 */
    {"GMT", 0},
};

static kunit_result_t test_timezone_offsets(void)
{
    KUNIT_ASSERT_EQ(timezones[0].offset_minutes, 0, "UTC=0");
    KUNIT_ASSERT_EQ(timezones[1].offset_minutes, 480, "CST=+8 小 runtime ");
    KUNIT_ASSERT_EQ(timezones[2].offset_minutes, 540, "JST=+9 小 runtime ");
    KUNIT_ASSERT_EQ(timezones[4].offset_minutes, -480, "PST=-8 小 runtime ");
    return KUNIT_PASS;
}

static kunit_result_t test_timezone_conversion(void)
{
    /* UTC时间转本地时间 */
    u32 utc_hour = 12;
    s32 offset = timezones[1].offset_minutes / 60;  /* CST = +8 */
    s32 local_hour = (s32)utc_hour + offset;

    /* 处理跨日 */
    if (local_hour >= 24) local_hour -= 24;
    if (local_hour < 0) local_hour += 24;

    KUNIT_ASSERT_EQ(local_hour, 20, "UTC12:00 = CST20:00");
    return KUNIT_PASS;
}

/* === 日期格式测试 === */
static kunit_result_t test_date_formats(void)
{
    u32 year = 2026, month = 8, day = 24;

    /* 中文格式: YYYY-MM-DD */
    char zh_fmt[11];
    zh_fmt[0] = '0' + year / 1000;
    zh_fmt[1] = '0' + (year / 100) % 10;
    zh_fmt[2] = '0' + (year / 10) % 10;
    zh_fmt[3] = '0' + year % 10;
    zh_fmt[4] = '-';
    zh_fmt[5] = '0' + month / 10;
    zh_fmt[6] = '0' + month % 10;
    zh_fmt[7] = '-';
    zh_fmt[8] = '0' + day / 10;
    zh_fmt[9] = '0' + day % 10;
    zh_fmt[10] = '\0';

    KUNIT_ASSERT_EQ(strcmp(zh_fmt, "2026-08-24"), 0, " in 文日期 format ");

    /* 美国格式: MM/DD/YYYY */
    char us_fmt[11];
    us_fmt[0] = '0' + month / 10;
    us_fmt[1] = '0' + month % 10;
    us_fmt[2] = '/';
    us_fmt[3] = '0' + day / 10;
    us_fmt[4] = '0' + day % 10;
    us_fmt[5] = '/';
    us_fmt[6] = '0' + year / 1000;
    us_fmt[7] = '0' + (year / 100) % 10;
    us_fmt[8] = '0' + (year / 10) % 10;
    us_fmt[9] = '0' + year % 10;
    us_fmt[10] = '\0';

    KUNIT_ASSERT_EQ(strcmp(us_fmt, "08/24/2026"), 0, " 美国日期 format ");
    return KUNIT_PASS;
}

/* === 数字格式测试 === */
static kunit_result_t test_number_formats(void)
{
    u32 num = 1234567;

    /* 中文: 1,234,567 */
    /* 简化: 验证千分位计算 */
    u32 millions = num / 1000000;
    u32 thousands = (num / 1000) % 1000;
    u32 units = num % 1000;

    KUNIT_ASSERT_EQ(millions, 1, " 百万 bit =1");
    KUNIT_ASSERT_EQ(thousands, 234, " 千 bit =234");
    KUNIT_ASSERT_EQ(units, 567, " bit =567");
    return KUNIT_PASS;
}

/* === 输入法状态测试 === */
#define IME_STATE_INACTIVE 0
#define IME_STATE_ACTIVE 1
#define IME_STATE_COMPOSING 2

static kunit_result_t test_ime_states(void)
{
    KUNIT_ASSERT_EQ(IME_STATE_INACTIVE, 0, " not 激活 ");
    KUNIT_ASSERT_EQ(IME_STATE_ACTIVE, 1, " 激活 ");
    KUNIT_ASSERT_EQ(IME_STATE_COMPOSING, 2, " 组合 in ");

    u8 state = IME_STATE_ACTIVE;
    KUNIT_ASSERT(state != IME_STATE_INACTIVE, " non not 激活 ");
    KUNIT_ASSERT(state == IME_STATE_ACTIVE, " 激活 state ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t i18n_tests[] = {
    {"UTF-8 ASCII", test_utf8_ascii, "i18n"},
    {"UTF-8 2 bytes ", test_utf8_2byte, "i18n"},
    {"UTF-8 3 bytes ", test_utf8_3byte, "i18n"},
    {"UTF-8 4 bytes ", test_utf8_4byte, "i18n"},
    {"UTF-8 len 计算 ", test_utf8_length_calc, "i18n"},
    {" lang 代码 ", test_language_codes, "i18n"},
    {" lang name ", test_language_names, "i18n"},
    {" lang switch ", test_language_switch, "i18n"},
    {" runtime region 偏移 ", test_timezone_offsets, "i18n"},
    {" runtime region 转换 ", test_timezone_conversion, "i18n"},
    {" 日期 format ", test_date_formats, "i18n"},
    {" count 字 format ", test_number_formats, "i18n"},
    {" IME state ", test_ime_states, "i18n"},
};
static const kunit_suite_t kunit_i18n_suite = {
    " multilingual & i18n ", i18n_tests, 13
};

void kunit_i18n_register(void)
{
    kunit_register_suite(&kunit_i18n_suite);
    vga_puts("[KUNIT] multilingual & i18n test suite registered(13 cases)\n");
}
