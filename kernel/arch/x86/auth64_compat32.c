/* ============================================================================
 * 32 位兼容层 —— auth64_* 最小桩（非真实账户）
 *
 * 背景：共用桌面层 hpdesk.c 引用 64 位账户子系统 arch/x86_64/auth64.c，
 * 该文件依赖 ext2_64_read_file / ext2_64_write_file 持久化 /account.conf，
 * 32 位内核没有对应 FS 后端，整体纳入 32 位构建会连带拖入 64 位 ext2 层，
 * 因此在本文件提供同签名的最小兼容实现，让 32 位内核链接通过。
 *
 * 语义（与 64 位 auth64.c 同签名，安全兜底）：
 *   - 未配置任何账户：用户名为空串、角色 ROLE_NONE(0)、is_configured=0；
 *   - 密码校验恒失败（返回 0）；
 *   - 创建账户返回 -1：32 位兼容层不持久化账户配置。
 *
 * 这是桩，不是真实账户/凭据接入。真实账户子系统（uid/凭据主体绑定、
 * 持久化、密码校验）属于后续 32 位安全工作，接入点：把本桩替换为对
 * 32 位账户服务的真实调用。本文件与 gfx64_compat_ext.c 同属 32 位兼容层。
 * ========================================================================== */
#include <stdint.h>

#define ROLE_NONE 0

const char *auth64_user_name(void) { return ""; }
int auth64_current_role(void)      { return ROLE_NONE; }
int auth64_is_configured(void)     { return 0; }

int auth64_verify_password(const char *pass)
{
    (void)pass;
    return 0;  /* 恒失败 */
}

int auth64_create_account(const char *name, const char *pass, int role,
                           const char *admin_pass)
{
    (void)name; (void)pass; (void)role; (void)admin_pass;
    return -1;  /* 32 位兼容层不持久化账户 */
}
