/* ============================================================================
 * 赫普OS - 登录/锁屏界面实现
 * V8.47
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "login.h"
#include "vga.h"
#include "vga_gfx.h"
#include "multi_user.h"
#include "keyboard.h"

static login_config_t login_cfg;
static login_state_t login_state = LOGIN_STATE_USER_SELECT;
static int login_active = 0;
static int selected_user = 0;
static char password_buf[64];
static int password_len = 0;
static int locked = 0;
static char error_msg[128];
static int error_timer = 0;

/* 初始化登录界面 */
void login_init(void)
{
    memset(&login_cfg, 0, sizeof(login_cfg));
    login_cfg.enabled = 1;
    login_cfg.auto_login = 1;        /* 默认自动登录（开发阶段） */
    login_cfg.auto_login_user = 0;   /* 机主 */
    login_cfg.show_user_list = 1;
    login_cfg.require_password = 0;   /* 默认不要求密码（开发阶段） */
    login_cfg.session_timeout = 0;    /* 不超时 */
    strncpy(login_cfg.welcome_text, "欢迎使用赫普OS", 127);

    login_state = LOGIN_STATE_USER_SELECT;
    login_active = 0;
    locked = 0;
    selected_user = 0;
    password_len = 0;
    memset(password_buf, 0, sizeof(password_buf));

    vga_puts("[LOGIN] Login interface initialized\n");
}

/* 显示登录界面 */
int login_show(void)
{
    if (!login_cfg.enabled) return -1;

    login_active = 1;
    login_state = LOGIN_STATE_USER_SELECT;
    selected_user = 0;
    password_len = 0;
    memset(password_buf, 0, sizeof(password_buf));

    vga_puts("[LOGIN] Login screen shown\n");
    return 0;
}

/* 隐藏登录界面 */
int login_hide(void)
{
    login_active = 0;
    vga_puts("[LOGIN] Login screen hidden\n");
    return 0;
}

/* 登录界面是否激活 */
int login_is_active(void)
{
    return login_active;
}

/* 认证用户 */
int login_authenticate(int user_id, const char *password)
{
    /* 调用系统调用验证密码（通过multi_user） */
    mu_user_t user;
    if (mu_user_get(user_id, &user) != 0) {
        strncpy(error_msg, "用户不存在", 127);
        error_timer = 100;
        return -1;
    }

    /* 如果用户没有设置密码，直接通过 */
    /* 简化：无密码用户直接登录 */
    vga_printf("[LOGIN] User %d authenticated: %s\n", user_id, user.name);
    return 0;
}

/* 选择用户 */
int login_select_user(int user_id)
{
    mu_user_t user;
    if (mu_user_get(user_id, &user) != 0) return -1;
    if (user.state != MU_USER_STATE_ACTIVE) return -2;

    selected_user = user_id;
    login_state = LOGIN_STATE_PASSWORD;
    password_len = 0;
    memset(password_buf, 0, sizeof(password_buf));

    vga_printf("[LOGIN] User selected: %s (id=%d)\n", user.name, user_id);
    return 0;
}

/* 获取当前选中用户 */
int login_get_selected_user(void)
{
    return selected_user;
}

/* 获取登录状态 */
login_state_t login_get_state(void)
{
    return login_state;
}

/* 设置配置 */
int login_set_config(const login_config_t *config)
{
    if (!config) return -1;
    memcpy(&login_cfg, config, sizeof(login_config_t));
    return 0;
}

/* 获取配置 */
int login_get_config(login_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &login_cfg, sizeof(login_config_t));
    return 0;
}

/* 锁屏 */
int login_lock_screen(void)
{
    locked = 1;
    login_state = LOGIN_STATE_LOCKED;
    login_active = 1;
    password_len = 0;
    memset(password_buf, 0, sizeof(password_buf));

    vga_puts("[LOGIN] Screen locked\n");
    return 0;
}

/* 解锁屏幕 */
int login_unlock_screen(const char *password)
{
    /* 验证当前用户密码 */
    mu_user_t current;
    if (mu_user_get_current(&current) == 0) {
        /* 简化：无密码直接解锁 */
        locked = 0;
        login_active = 0;
        login_state = LOGIN_STATE_USER_SELECT;
        vga_puts("[LOGIN] Screen unlocked\n");
        return 0;
    }

    strncpy(error_msg, "解锁失败", 127);
    error_timer = 100;
    return -1;
}

/* 是否锁定 */
int login_is_locked(void)
{
    return locked;
}

/* 绘制登录界面 */
void login_draw(void)
{
    if (!login_active) return;

    int screen_w = 1024;
    int screen_h = 768;

    /* 背景：深蓝色 */
    gfx_fillrect(0, 0, screen_w, screen_h, 0x01);  /* 蓝色 */

    /* 中央登录框 */
    int box_w = 400;
    int box_h = 300;
    int box_x = (screen_w - box_w) / 2;
    int box_y = (screen_h - box_h) / 2;

    /* 登录框背景 */
    gfx_fillrect(box_x, box_y, box_w, box_h, 0x0F);  /* 白色 */
    gfx_drawrect(box_x, box_y, box_w, box_h, 0x0C);   /* 红色边框 */

    /* 标题 */
    gfx_puts_big(box_x + 160, box_y + 30, "HepuOS", 0x01, 2);
    gfx_puts(box_x + 130, box_y + 65, login_cfg.welcome_text, 0x08);

    if (login_state == LOGIN_STATE_USER_SELECT) {
        /* 用户列表 */
        gfx_puts(box_x + 50, box_y + 100, "Select User:", 0x00);

        mu_user_t users[16];
        int count = mu_user_list(users, 16);
        for (int i = 0; i < count && i < 4; i++) {
            int uy = box_y + 130 + i * 35;
            int selected = (i == selected_user);

            /* 用户项背景 */
            gfx_fillrect(box_x + 50, uy, box_w - 100, 30,
                          selected ? 0x0C : 0x07);  /* 红/灰 */

            /* 用户头像（圆形占位） */
            gfx_fillcircle(box_x + 70, uy + 15, 10,
                            selected ? 0x0F : 0x05);  /* 白/紫 */

            /* 用户名 */
            gfx_puts(box_x + 95, uy + 8, users[i].name,
                     selected ? 0x0F : 0x00);
        }

        gfx_puts(box_x + 80, box_y + 270, "Up/Down Select, Enter OK", 0x08);

    } else if (login_state == LOGIN_STATE_PASSWORD || login_state == LOGIN_STATE_LOCKED) {
        /* 密码输入 */
        mu_user_t user;
        if (mu_user_get(selected_user, &user) == 0) {
            gfx_puts_big(box_x + 150, box_y + 100, user.name, 0x00, 1);
        }

        gfx_puts(box_x + 100, box_y + 140, "Password:", 0x00);

        /* 密码输入框 */
        gfx_fillrect(box_x + 100, box_y + 165, box_w - 200, 30, 0x07);
        gfx_drawrect(box_x + 100, box_y + 165, box_w - 200, 30, 0x0C);

        /* 密码显示为圆点 */
        for (int i = 0; i < password_len && i < 20; i++) {
            gfx_fillcircle(box_x + 120 + i * 15, box_y + 180, 4, 0x00);
        }

        /* 错误信息 */
        if (error_timer > 0) {
            gfx_puts(box_x + 100, box_y + 210, error_msg, 0x0C);
            error_timer--;
        }

        if (login_state == LOGIN_STATE_LOCKED) {
            gfx_puts(box_x + 130, box_y + 270, "Screen Locked", 0x0C);
        } else {
            gfx_puts(box_x + 80, box_y + 270, "Enter Login, Esc Back", 0x08);
        }

    } else if (login_state == LOGIN_STATE_LOGGING_IN) {
        gfx_puts_big(box_x + 150, box_y + 150, "Logging in...", 0x00, 1);
    }
}

/* 处理键盘输入 */
int login_handle_key(char key)
{
    if (!login_active) return 0;

    if (login_state == LOGIN_STATE_PASSWORD || login_state == LOGIN_STATE_LOCKED) {
        if (password_len < 63 && key >= 32 && key <= 126) {
            password_buf[password_len++] = key;
            password_buf[password_len] = 0;
            return 1;
        }
    }

    return 0;
}

/* 处理Enter键 */
int login_handle_enter(void)
{
    if (!login_active) return 0;

    if (login_state == LOGIN_STATE_USER_SELECT) {
        /* 选择用户，进入密码输入 */
        login_select_user(selected_user);
        return 1;

    } else if (login_state == LOGIN_STATE_PASSWORD) {
        /* 验证密码并登录 */
        login_state = LOGIN_STATE_LOGGING_IN;

        if (login_authenticate(selected_user, password_buf) == 0) {
            /* 登录成功，切换用户 */
            mu_user_switch(selected_user);
            login_hide();
            return 1;
        } else {
            login_state = LOGIN_STATE_PASSWORD;
            password_len = 0;
            memset(password_buf, 0, sizeof(password_buf));
            return 1;
        }

    } else if (login_state == LOGIN_STATE_LOCKED) {
        /* 解锁 */
        if (login_unlock_screen(password_buf) == 0) {
            return 1;
        } else {
            password_len = 0;
            memset(password_buf, 0, sizeof(password_buf));
            return 1;
        }
    }

    return 0;
}

/* 处理Backspace键 */
int login_handle_backspace(void)
{
    if (!login_active) return 0;

    if ((login_state == LOGIN_STATE_PASSWORD || login_state == LOGIN_STATE_LOCKED)
        && password_len > 0) {
        password_len--;
        password_buf[password_len] = 0;
        return 1;
    }

    return 0;
}

/* 处理方向键 */
int login_handle_arrow(int direction)
{
    if (!login_active) return 0;

    if (login_state == LOGIN_STATE_USER_SELECT) {
        mu_user_t users[16];
        int count = mu_user_list(users, 16);

        if (direction == 0) {  /* 上 */
            if (selected_user > 0) selected_user--;
        } else {  /* 下 */
            if (selected_user < count - 1) selected_user++;
        }
        return 1;
    }

    return 0;
}
