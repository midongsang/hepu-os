#ifndef HEPU_MAC_H
#define HEPU_MAC_H

#include <stdint.h>

/* ============================================================================
 * V4.18-E63: SELinux 式强制访问控制 (MAC)
 *
 * 模型: subject(进程标签) × object(文件/路径前缀标签) × perm(R/W/X)
 * - 规则命中且含所需权限 → 允许
 * - 无规则命中 → enforcing 模式拒绝, permissive 模式放行并计数
 * 挂钩: ext2 VFS read/write (file name 为 object)
 * ========================================================================== */

#define MAC_MAX_RULES  32
#define MAC_LABEL_MAX  24
#define MAC_PERM_R     1
#define MAC_PERM_W     2
#define MAC_PERM_X     4

typedef struct {
    char     subject[MAC_LABEL_MAX];  /* 主体标签(进程名) */
    char     object[MAC_LABEL_MAX];   /* 客体标签(文件名前缀) */
    uint32_t perms;                   /* R|W|X 位掩码 */
    int      enabled;
} mac_rule_t;

void mac_init(void);
int  mac_add_rule(const char *subject, const char *object, uint32_t perms);
int  mac_check(const char *subject, const char *object, uint32_t perm);
void mac_show_all(void);
void mac_setenforce(int on);
int  mac_enforcing_get(void);
void mac_stats(uint32_t *allow, uint32_t *deny);

/* V4.42-E87: 规则持久化 */
int  mac_rule_get(int idx, mac_rule_t *out);   /* 枚举规则 (save 用) */
void mac_rule_clear(void);                     /* 清空规则表 (load 用) */
int  mac_save(const char *path);               /* 规则落盘 */
int  mac_load(const char *path);               /* 从盘读取 */
void mac_autoload(void);                       /* 启动自动加载 */
int  mac_rule_del(const char *subject, const char *object);  /* V4.43-E88: 删除规则 */

#endif /* HEPU_MAC_H */
