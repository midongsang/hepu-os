/* ============================================================================
 * 赫普OS - 多语言 i18n 框架
 * V4.19-E64
 *
 * 语言表: EN/ZH/JA/DE, UTF-8 存储.
 * 显示: 当前 VGA ASCII 字库, 非拉丁字符显示 '?' 占位;
 *       字库接入后 i18n_get 原样输出即可原生渲染.
 * ========================================================================== */

#include <stdint.h>
#include "i18n.h"
#include "vga.h"
#include "string.h"

static int i18n_current = I18N_LANG_EN;

static const char *i18n_table[I18N_MAX_LANGS][I18N_MSG_MAX] = {
    /* EN */
    {
        "Welcome to Hepu OS",            /* WELCOME */
        "hepu@os",                       /* PROMPT */
        "English",                       /* LANG_NAME */
        "current language",              /* LANG_CURRENT */
        "language set to",               /* LANG_SET_OK */
        "unknown language",              /* LANG_UNKNOWN */
        "command not found",             /* CMD_NOT_FOUND */
        "uptime",                        /* UPTIME */
        "mem used",                      /* MEM_USED */
        "storage free",                  /* STORAGE_FREE */
        "total procs",                   /* PROC_TOTAL */
        "boot complete",                 /* BOOT_COMPLETE */
        "kernel version",                /* KERNEL_VER */
        "system version",                /* SYSTEM_VER */
        "Help",                          /* HELP_TITLE */
        "Disk Quota",                    /* QUOTA_SHOW */
        "MAC Rules",                     /* MAC_SHOW */
        "safe mode",                     /* SAFE_MODE */
        "diagnostic mode",               /* DIAG_MODE */
        "alarm set",                     /* ALARM_SET */
        "[System/Power]",                        /* GROUP_0 */
        "[Info/Logs]",                        /* GROUP_1 */
        "[Process/Resources]",                        /* GROUP_2 */
        "[Files/Apps]",                        /* GROUP_3 */
        "[Devices/UI]",                        /* GROUP_4 */
        "[Security/Machine]",                        /* GROUP_5 */
    },
    /* ZH (UTF-8) */
    {
        "\xe6\xac\xa2\xe8\xbf\x8e\xe4\xbd\xbf\xe7\x94\xa8\xe8\xb5\xab\xe6\x99\xae\xe7\xb3\xbb\xe7\xbb\x9f",
        "\xe8\xb5\xab\xe6\x99\xae\x40\x6f\x73",
        "\xe4\xb8\xad\xe6\x96\x87",
        "\xe5\xbd\x93\xe5\x89\x8d\xe8\xaf\xad\xe8\xa8\x80",
        "\xe8\xaf\xad\xe8\xa8\x80\xe5\xb7\xb2\xe5\x88\x87\xe6\x8d\xa2",
        "\xe6\x9c\xaa\xe7\x9f\xa5\xe8\xaf\xad\xe8\xa8\x80",
        "\xe5\x91\xbd\xe4\xbb\xa4\xe6\x9c\xaa\xe6\x89\xbe\xe5\x88\xb0",
        "\xe8\xbf\x90\xe8\xa1\x8c\xe6\x97\xb6\xe9\x97\xb4",
        "\xe5\x86\x85\xe5\xad\x98\xe4\xbd\xbf\xe7\x94\xa8",
        "\xe5\xad\x98\xe5\x82\xa8\xe5\x89\xa9\xe4\xbd\x99",
        "\xe8\xbf\x9b\xe7\xa8\x8b\xe6\x80\xbb\xe6\x95\xb0",
        "\xe5\x90\xaf\xe5\x8a\xa8\xe5\xae\x8c\xe6\x88\x90",
        "\xe5\x86\x85\xe6\xa0\xb8\xe7\x89\x88\xe6\x9c\xac",
        "\xe7\xb3\xbb\xe7\xbb\x9f\xe7\x89\x88\xe6\x9c\xac",
        "\xe5\xb8\xae\xe5\x8a\xa9",
        "\xe7\xa3\x81\xe7\x9b\x98\xe9\x85\x8d\xe9\xa2\x9d",
        "MAC \xe8\xa7\x84\xe5\x88\x99",
        "\xe5\xae\x89\xe5\x85\xa8\xe6\xa8\xa1\xe5\xbc\x8f",
        "\xe8\xaf\x8a\xe6\x96\xad\xe6\xa8\xa1\xe5\xbc\x8f",
        "\xe9\x97\xb9\xe9\x92\x9f\xe5\xb7\xb2\xe8\xae\xbe\xe7\xbd\xae",
        "[\xe7\xb3\xbb\xe7\xbb\x9f/\xe7\x94\xb5\xe6\xba\x90]",                        /* GROUP_0 */
        "[\xe4\xbf\xa1\xe6\x81\xaf/\xe6\x97\xa5\xe5\xbf\x97]",                        /* GROUP_1 */
        "[\xe8\xbf\x9b\xe7\xa8\x8b/\xe8\xb5\x84\xe6\xba\x90]",                        /* GROUP_2 */
        "[\xe6\x96\x87\xe4\xbb\xb6/\xe5\xba\x94\xe7\x94\xa8]",                        /* GROUP_3 */
        "[\xe8\xae\xbe\xe5\xa4\x87/\xe7\x95\x8c\xe9\x9d\xa2]",                        /* GROUP_4 */
        "[\xe5\xae\x89\xe5\x85\xa8/\xe6\x9c\xba\xe5\x99\xa8]",                        /* GROUP_5 */
    },
    /* JA (UTF-8) */
    {
        "\xe3\x83\x98\xe3\x83\x97\xe3\x83\x9d\xe3\x82\xb9\xe3\x82\x92\xe3\x81\x94\xe5\x88\xa9\xe7\x94\xa8\xe3\x81\x84\xe3\x81\x9f\xe3\x81\xa0\xe3\x81\x8d\xe3\x81\x82\xe3\x82\x8a\xe3\x81\x8c\xe3\x81\xa8\xe3\x81\x86",
        "\xe3\x83\x98\xe3\x83\x97\xe3\x83\x9d\x40\x6f\x73",
        "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e",
        "\xe5\x88\x87\xe3\x82\x8a\xe6\x9b\xbf\xe3\x81\x88\xe8\xa8\x80\xe8\xaa\x9e",
        "\xe8\xa8\x80\xe8\xaa\x9e\xe3\x82\x92\xe5\x88\x87\xe3\x82\x8a\xe6\x9b\xbf\xe3\x81\x88\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f",
        "\xe4\xb8\x8d\xe6\x98\x8e\xe3\x81\xaa\xe8\xa8\x80\xe8\xaa\x9e",
        "\xe3\x82\xb3\xe3\x83\x9e\xe3\x83\xb3\xe3\x83\x89\xe3\x81\x8c\xe8\xa6\x8b\xe3\x81\xa4\xe3\x81\x8b\xe3\x82\x8a\xe3\x81\xbe\xe3\x81\x9b\xe3\x82\x93",
        "\xe9\x81\x8b\xe7\x94\xa8\xe6\x99\x82\xe9\x96\x93",
        "\xe3\x83\xa1\xe3\x83\xa2\xe3\x83\xaa\xe4\xbd\xbf\xe7\x94\xa8",
        "\xe5\x84\xb2\xe5\xad\x98\xe6\xae\x8b\xe3\x82\x8a",
        "\xe3\x83\x97\xe3\x83\xad\xe3\x82\xbb\xe3\x82\xb9\xe6\x95\xb0",
        "\xe8\xb5\xb7\xe5\x8b\x95\xe5\xae\x8c\xe4\xba\x86",
        "\xe3\x82\xab\xe3\x83\xbc\xe3\x83\x8d\xe3\x83\xab\xe3\x83\x90\xe3\x83\xbc\xe3\x82\xb8\xe3\x83\xa7\xe3\x83\xb3",
        "\xe3\x82\xb7\xe3\x82\xb9\xe3\x83\x86\xe3\x83\xa0\xe3\x83\x90\xe3\x83\xbc\xe3\x82\xb8\xe3\x83\xa7\xe3\x83\xb3",
        "\xe3\x83\x98\xe3\x83\xab\xe3\x83\x97",
        "\xe3\x83\x87\xe3\x82\xa3\xe3\x82\xb9\xe3\x82\xaf\xe3\x82\xaf\xe3\x82\xaa\xe3\x83\xbc\xe3\x82\xbf",
        "MAC \xe3\x83\xab\xe3\x83\xbc\xe3\x83\xab",
        "\xe3\x82\xbb\xe3\x83\xbc\xe3\x83\x95\xe3\x83\x86\xe3\x82\xa3\xe3\x83\xa2\xe3\x83\xbc\xe3\x83\x89",
        "\xe3\x83\x80\xe3\x82\xa4\xe3\x82\xa2\xe3\x82\xb0\xe3\x83\x8e\xe3\x82\xb9\xe3\x83\x86\xe3\x82\xa3\xe3\x83\x83\xe3\x82\xaf",
        "\xe3\x82\xa2\xe3\x83\xa9\xe3\x83\xbc\xe3\x83\xa0\xe8\xa8\xad\xe5\xae\x9a",
        "[\xe3\x82\xb7\xe3\x82\xb9\xe3\x83\x86\xe3\x83\xa0/\xe9\x9b\xbb\xe6\xba\x90]",                        /* GROUP_0 */
        "[\xe6\x83\x85\xe5\xa0\xb1/\xe3\x83\xad\xe3\x82\xb0]",                        /* GROUP_1 */
        "[\xe3\x83\x97\xe3\x83\xad\xe3\x82\xbb\xe3\x82\xb9/\xe8\xb3\x87\xe6\xba\x90]",                        /* GROUP_2 */
        "[\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\xab/\xe3\x82\xa2\xe3\x83\x97\xe3\x83\xaa]",                        /* GROUP_3 */
        "[\xe3\x83\x87\xe3\x83\x90\xe3\x82\xa4\xe3\x82\xb9/UI]",                        /* GROUP_4 */
        "[\xe3\x82\xbb\xe3\x82\xad\xe3\x83\xa5\xe3\x83\xaa\xe3\x83\x86\xe3\x82\xa3/\xe6\xa9\x9f\xe6\xa2\xb0]",                        /* GROUP_5 */
    },
    /* DE */
    {
        "Willkommen bei Hepu OS",
        "hepu@os",
        "Deutsch",
        "aktuelle Sprache",
        "Sprache geaendert auf",
        "unbekannte Sprache",
        "Befehl nicht gefunden",
        "Betriebszeit",
        "Speicher belegt",
        "Speicher frei",
        "Prozesse gesamt",
        "Boot abgeschlossen",
        "Kernel-Version",
        "System-Version",
        "Hilfe",
        "Festplatten-Quota",
        "MAC-Regeln",
        "Sicherheitsmodus",
        "Diagnosemodus",
        "Wecker gesetzt",
        "[System/Strom]",                        /* GROUP_0 */
        "[Info/Logs]",                        /* GROUP_1 */
        "[Prozess/Ressourcen]",                        /* GROUP_2 */
        "[Dateien/Apps]",                        /* GROUP_3 */
        "[Geraete/UI]",                        /* GROUP_4 */
        "[Sicherheit/Maschine]",                        /* GROUP_5 */
    },
};

void i18n_init(void)
{
    i18n_current = I18N_LANG_EN;
    vga_puts("[I18N] multi-language init (EN/ZH/JA/DE)\n");
}

int i18n_set_lang(int lang)
{
    if (lang < 0 || lang >= I18N_MAX_LANGS) return -1;
    i18n_current = lang;
    return 0;
}

int i18n_get_lang(void) { return i18n_current; }

const char *i18n_get(int id)
{
    if (id < 0 || id >= I18N_MSG_MAX) return "";
    return i18n_table[i18n_current][id];
}

void i18n_list(void)
{
    static const char *names[I18N_MAX_LANGS] = {
        "en (English)", "zh (Chinese)", "ja (Japanese)", "de (German)"
    };
    vga_puts("available languages:\n");
    for (int i = 0; i < I18N_MAX_LANGS; i++) {
        vga_printf("  [%c] %s%s\n", (i == i18n_current) ? '*' : ' ',
                   names[i], (i == i18n_current) ? "  <- current" : "");
    }
}
