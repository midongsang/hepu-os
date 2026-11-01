#ifndef USERLAND_H
#define USERLAND_H

#include <stdint.h>

#define UL_MAX_COMMANDS 64
#define UL_MAX_APPS 32
#define UL_MAX_ENV_VARS 64
#define UL_MAX_ALIASES 32
#define UL_MAX_HISTORY 256

/* Shell命令 */
typedef struct {
    char name[32];
    char description[128];
    char usage[128];
    int arg_count;
    int (*handler)(int argc, char **argv);
    int builtin;  /* 1=内置,0=外部 */
    int category;  /* 0=文件,1=进程,2=网络,3=系统,4=开发,5=其他 */
} shell_command_t;

/* 用户应用 */
typedef struct {
    int used;
    int id;
    char name[64];
    char path[256];
    char description[128];
    int type;  /* 0=ELF,1=HUG,2=脚本 */
    int size;
    int permissions;
    int owner;
    int group;
    uint64_t last_modified;
    int run_count;
} user_app_t;

/* 环境变量 */
typedef struct {
    char name[64];
    char value[256];
    int exported;
} env_var_t;

/* 别名 */
typedef struct {
    char name[32];
    char command[256];
} alias_t;

/* Shell状态 */
typedef struct {
    char cwd[256];
    char home[256];
    char path[512];
    char prompt[64];
    int exit_code;
    int interactive;
    int verbose;
    int echo;
    int job_count;
    env_var_t env[UL_MAX_ENV_VARS];
    int env_count;
    alias_t aliases[UL_MAX_ALIASES];
    int alias_count;
    char history[UL_MAX_HISTORY][256];
    int history_count;
    int history_index;
} shell_state_t;

/* libc函数统计 */
typedef struct {
    int stdio_functions;
    int stdlib_functions;
    int string_functions;
    int unistd_functions;
    int math_functions;
    int time_functions;
    int signal_functions;
    int pthread_functions;
    int socket_functions;
    int total_functions;
} libc_stats_t;

/* 用户态状态 */
typedef struct {
    int initialized;
    /* Shell */
    shell_state_t shell;
    shell_command_t commands[UL_MAX_COMMANDS];
    int command_count;
    /* 应用 */
    user_app_t apps[UL_MAX_APPS];
    int app_count;
    /* libc */
    libc_stats_t libc;
    /* 统计 */
    uint64_t commands_executed;
    uint64_t apps_launched;
    uint64_t syscalls_made;
} userland_t;

/* 初始化用户态 */
void userland_init(void);

/* Shell命令 */
int ul_command_register(const char *name, const char *desc, const char *usage, int (*handler)(int, char**), int builtin, int category);
int ul_command_execute(const char *command_line);
int ul_command_list(shell_command_t *commands, int max);
int ul_command_get(const char *name, shell_command_t *command);
int ul_command_parse(char *line, char **argv, int max_args);

/* 内置命令 */
int ul_cmd_cd(int argc, char **argv);
int ul_cmd_pwd(int argc, char **argv);
int ul_cmd_ls(int argc, char **argv);
int ul_cmd_cat(int argc, char **argv);
int ul_cmd_echo(int argc, char **argv);
int ul_cmd_ps(int argc, char **argv);
int ul_cmd_kill(int argc, char **argv);
int ul_cmd_mkdir(int argc, char **argv);
int ul_cmd_rm(int argc, char **argv);
int ul_cmd_cp(int argc, char **argv);
int ul_cmd_mv(int argc, char **argv);
int ul_cmd_touch(int argc, char **argv);
int ul_cmd_chmod(int argc, char **argv);
int ul_cmd_chown(int argc, char **argv);
int ul_cmd_env(int argc, char **argv);
int ul_cmd_export(int argc, char **argv);
int ul_cmd_alias(int argc, char **argv);
int ul_cmd_history(int argc, char **argv);
int ul_cmd_clear(int argc, char **argv);
int ul_cmd_help(int argc, char **argv);
int ul_cmd_exit(int argc, char **argv);
int ul_cmd_uname(int argc, char **argv);
int ul_cmd_date(int argc, char **argv);
int ul_cmd_whoami(int argc, char **argv);
int ul_cmd_id(int argc, char **argv);
int ul_cmd_df(int argc, char **argv);
int ul_cmd_free(int argc, char **argv);
int ul_cmd_top(int argc, char **argv);
int ul_cmd_ifconfig(int argc, char **argv);
int ul_cmd_ping(int argc, char **argv);
int ul_cmd_netstat(int argc, char **argv);

/* 环境变量 */
int ul_env_set(const char *name, const char *value, int exported);
int ul_env_get(const char *name, char *value, int max);
int ul_env_unset(const char *name);
int ul_env_list(env_var_t *env, int max);

/* 别名 */
int ul_alias_set(const char *name, const char *command);
int ul_alias_get(const char *name, char *command, int max);
int ul_alias_unset(const char *name);
int ul_alias_list(alias_t *aliases, int max);

/* 历史 */
int ul_history_add(const char *command);
int ul_history_get(int index, char *command, int max);
int ul_history_list(char (*history)[256], int max);

/* 用户应用 */
int ul_app_register(const char *name, const char *path, const char *desc, int type);
int ul_app_unregister(int app_id);
int ul_app_list(user_app_t *apps, int max);
int ul_app_get(int app_id, user_app_t *app);
int ul_app_run(int app_id, int argc, char **argv);

/* libc */
int ul_libc_init(void);
int ul_libc_get_stats(libc_stats_t *stats);

/* Shell状态 */
int ul_shell_get_state(shell_state_t *state);
int ul_shell_set_cwd(const char *path);
int ul_shell_get_cwd(char *path, int max);

/* 统计 */
int ul_get_stats(userland_t *stats);

#endif /* USERLAND_H */
