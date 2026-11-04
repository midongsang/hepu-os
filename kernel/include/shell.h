/* ============================================================================
 * 赫普OS - Shell 接口
 * 从 shell.c 拆分时创建（V6.46 代码重构）
 * 包含：help_entry_t/man_entry_t 类型定义，帮助表，命令函数声明
 * ========================================================================== */
#ifndef HEPU_SHELL_H
#define HEPU_SHELL_H

#include <stdint.h>

/* 分层命令帮助条目 */
typedef struct {
    const char *name;
    const char *group;
    const char *syntax;
    const char *desc;
    const char *ex;
} help_entry_t;

/* 命令详细手册条目 */
typedef struct {
    const char *name;
    const char *body;
} man_entry_t;

/* 帮助表（全局，定义在 shell_help.c） */
extern const help_entry_t help_table[];
extern const man_entry_t man_table[];

/* 帮助函数 */
const char *help_group_title(char g);
void cmd_help(char *args);
void cmd_man(char *args);
void cmd_version(void);
void cmd_about(void);

/* Shell 主入口 */
void shell_main(void);
void firstboot_welcome(void);


/* 系统/进程命令 (shell_cmd_system.c) */
void cmd_ps(void);
void cmd_affinity(char *arg);
void cmd_ktest(void);
void cmd_prio(char *arg);
void cmd_meminfo(void);
void cmd_rtc(char *args);
void cmd_sysinfo(void);

/* 测试/性能命令 (shell_cmd_test.c) */
void cmd_dmatest(void);
void cmd_cacheinfo(void);
void cmd_perf(void);
uint32_t heap_used_snapshot(void);
void cmd_locktest(void);
void cmd_hdpk(void);
void firstboot_welcome(void);
void cmd_setappcpu(char *arg);
void cmd_ipcstress(void);
void cmd_memtest(void);
void cmd_limits(char *arg);
void cmd_rlimit(char *arg);
void cmd_softirqtest(char *arg);
void cmd_softstat(void);

/* UI/多媒体命令 (shell_cmd_ui.c) */
void cmd_audio(char *args);
void cmd_banner(char *args);
void cmd_canary(char *arg);
void cmd_cjk(char *args);
void cmd_dtb(char *args);
void cmd_echo(char *args);
void cmd_fwrite(char *args);
void cmd_ime(char *args);
void cmd_lang(char *args);
void cmd_lockhold(char *arg);
void cmd_lockstat(void);
void cmd_ls(void);
void cmd_mac(char *args);
void cmd_quota(char *args);
void cmd_softtick(void);
void cmd_time(void);
void cmd_wd(char *arg);

void sys_reboot_after_gfx(void);
void shell_execute(char *cmd);
void cmd_gesture(char *args);
void cmd_pm(char *args);
void cmd_tcp(char *args);
void cmd_touchtest(char *args);
#endif /* HEPU_SHELL_H */
