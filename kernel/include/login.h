/* ============================================================================
 * 赫普OS - 登录/锁屏界面头文件
 * V8.47
 * ========================================================================== */

#ifndef _LOGIN_H
#define _LOGIN_H

#include <stdint.h>

/* 登录界面状态 */
typedef enum {
    LOGIN_STATE_USER_SELECT = 0,  /* 选择用户 */
    LOGIN_STATE_PASSWORD,          /* 输入密码 */
    LOGIN_STATE_LOGGING_IN,        /* 登录中 */
    LOGIN_STATE_ERROR,             /* 登录错误 */
    LOGIN_STATE_LOCKED,            /* 已锁定 */
} login_state_t;

/* 登录界面配置 */
typedef struct {
    int enabled;               /* 是否启用登录界面 */
    int auto_login;            /* 自动登录 */
    int auto_login_user;       /* 自动登录用户ID */
    int show_user_list;        /* 显示用户列表 */
    int require_password;      /* 要求密码 */
    int session_timeout;       /* 会话超时（秒，0=不超时） */
    char wallpaper[256];       /* 登录壁纸路径 */
    char welcome_text[128];    /* 欢迎文字 */
} login_config_t;

/* 函数声明 */
void login_init(void);
int login_show(void);
int login_hide(void);
int login_is_active(void);
int login_authenticate(int user_id, const char *password);
int login_select_user(int user_id);
int login_get_selected_user(void);
login_state_t login_get_state(void);
int login_set_config(const login_config_t *config);
int login_get_config(login_config_t *config);
int login_lock_screen(void);
int login_unlock_screen(const char *password);
int login_is_locked(void);
void login_draw(void);
int login_handle_key(char key);
int login_handle_enter(void);
int login_handle_backspace(void);
int login_handle_arrow(int direction);

#endif /* _LOGIN_H */
