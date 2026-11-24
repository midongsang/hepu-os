/* ============================================================================
 * 32 位账户适配层 —— auth64_* 接口
 *
 * P3 起不再返回“未配置/恒失败”的桩，而是接到 32 位已有的 multi_user 账户表
 * 与 sys_user 密码表：
 *   - root：multi_user id 0，机主/开发者角色；
 *   - 普通用户：multi_user id 1，name=" user "，无装应用/改设置权限；
 *   - 密码：sys_user.c 的 SHA-256 哈希表，按 uid 索引。
 *
 * 持久化（/account.conf）与完整的 OOBE/登录流程不在本最小闭环内；当前目标是
 * 让 VFS 有一个真实的 uid 来源、S3（普通用户写系统目录被拒）可以复现。
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "multi_user.h"
#include "string.h"

#define ROLE_NONE      0
#define ROLE_NORMAL    1
#define ROLE_DEVELOPER 2

extern int hepu_current_uid(void);

const char *auth64_user_name(void)
{
    static char name[MU_NAME_LEN];
    mu_user_t user;
    name[0] = '\0';
    if (mu_user_get_current(&user) == 0) {
        strncpy(name, user.name, MU_NAME_LEN - 1);
        name[MU_NAME_LEN - 1] = '\0';
    }
    return name;
}

int auth64_current_role(void)
{
    mu_user_t user;
    if (mu_user_get_current(&user) != 0) return ROLE_NONE;
    return user.type == MU_USER_OWNER ? ROLE_DEVELOPER : ROLE_NORMAL;
}

int auth64_is_configured(void)
{
    mu_user_t user;
    return mu_user_get_current(&user) == 0 ? 1 : 0;
}

int auth64_verify_password(const char *pass)
{
    extern int auth32_verify_password_for_uid(int, const char *);
    if (!pass) return 0;
    return auth32_verify_password_for_uid(hepu_current_uid(), pass);
}

int auth64_create_account(const char *name, const char *pass, int role,
                          const char *admin_pass)
{
    extern int auth32_set_password_for_uid(int, const char *);
    (void)admin_pass;
    if (!name || !name[0] || !pass || !pass[0]) return -1;
    if (role != ROLE_NORMAL) return -1;  /* root 已在启动时固定，不通过此接口创建 */

    int uid = -1;
    for (int i = 0; i < MU_MAX_USERS; i++) {
        mu_user_t u;
        if (mu_user_get(i, &u) == 0 && strcmp(u.name, name) == 0) {
            uid = u.id;
            break;
        }
    }
    if (uid < 0) uid = mu_user_create(name, MU_USER_SECONDARY);
    if (uid < 0) return -1;
    if (auth32_set_password_for_uid(uid, pass) != 0) return -1;
    return 0;
}

/* 32 位最小账户不提供开发者提权；保持与 64 位签名一致，明确拒绝。 */
int auth64_elevate(const char *pass) { (void)pass; return -1; }
int auth64_deelevate(void) { return -1; }
