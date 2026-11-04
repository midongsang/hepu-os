#ifndef HEPU_I18N_H
#define HEPU_I18N_H

/* ============================================================================
 * V4.19-E64: 多语言 i18n 框架
 *
 * 语言表: 当前 4 语言槽(EN/ZH/JA/DE), 每条消息一个 ID.
 * 显示端: 当前 VGA 文本模式仅 ASCII 字库——非拉丁字符(中文/日文)
 *         显示为 '?' 占位, 等位图字库接入后原生渲染 (字符串本身
 *         以 UTF-8 存储, 直接可用).
 * ========================================================================== */

#define I18N_LANG_EN  0
#define I18N_LANG_ZH  1
#define I18N_LANG_JA  2
#define I18N_LANG_DE  3
#define I18N_MAX_LANGS 4
#define I18N_MSG_MAX  40

/* 消息 ID 枚举 */
enum {
    I18N_WELCOME = 0,       /* 欢迎语 */
    I18N_PROMPT,            /* shell 提示符前缀 */
    I18N_LANG_NAME,         /* 语言名称 */
    I18N_LANG_CURRENT,      /* 当前语言 */
    I18N_LANG_SET_OK,       /* 语言切换成功 */
    I18N_LANG_UNKNOWN,      /* 未知语言 */
    I18N_CMD_NOT_FOUND,     /* 命令未找到 */
    I18N_UPTIME,            /* 运行时间 */
    I18N_MEM_USED,          /* 内存使用 */
    I18N_STORAGE_FREE,      /* 存储剩余 */
    I18N_PROC_TOTAL,        /* 进程总数 */
    I18N_BOOT_COMPLETE,     /* 启动完成 */
    I18N_KERNEL_VER,        /* 内核版本 */
    I18N_SYSTEM_VER,        /* 系统版本 */
    I18N_HELP_TITLE,        /* 帮助标题 */
    I18N_QUOTA_SHOW,        /* 配额标题 */
    I18N_MAC_SHOW,          /* MAC 标题 */
    I18N_SAFE_MODE,         /* 安全模式 */
    I18N_DIAG_MODE,         /* 诊断模式 */
    I18N_ALARM_SET,         /* 闹钟已设置 */
    I18N_GROUP_SYS,         /* 分组: 系统/电源 */
    I18N_GROUP_INFO,        /* 分组: 信息/日志 */
    I18N_GROUP_PROC,        /* 分组: 进程/资源 */
    I18N_GROUP_FILES,       /* 分组: 文件/应用 */
    I18N_GROUP_DEV,         /* 分组: 设备/界面 */
    I18N_GROUP_SEC,         /* 分组: 安全/机器 */
    I18N_LAST               /* 哨兵 */
};

void i18n_init(void);
int  i18n_set_lang(int lang);
int  i18n_get_lang(void);
const char *i18n_get(int id);
void i18n_list(void);

#endif /* HEPU_I18N_H */
