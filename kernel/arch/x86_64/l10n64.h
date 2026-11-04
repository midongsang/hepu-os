/* l10n64.h - 多语言框架接口 */
#ifndef L10N64_H
#define L10N64_H

/* 字符串 ID */
enum {
    L10N_STR_LOCK_WELCOME = 0,
    L10N_STR_LOCK_PASSWORD,
    L10N_STR_LOCK_HINT,
    L10N_STR_POWER_SHUTDOWN,
    L10N_STR_TRAY_NETWORK,
    L10N_STR_TRAY_VOLUME,
    L10N_STR_TRAY_BATTERY,
    L10N_STR_SETTINGS_TITLE,
    L10N_STR_LANG_TITLE,
    L10N_STR_LANG_CURRENT,
    L10N_STR_LANG_SET,
    L10N_STR_LOGIN_BTN,
    L10N_STR_ESC_LOCK,
    L10N_STR_CLOCK_FORMAT,
    L10N_STR_COUNT
};

int l10n64_init(void);
int l10n64_get_lang(void);
int l10n64_set_lang(int lang);
const char *l10n64_lang_name(void);
const char *l10n64_tr(int id);

#endif
