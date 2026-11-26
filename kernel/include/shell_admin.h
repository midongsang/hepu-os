/* ============================================================================
 * 赫普OS - 系统管理命令（chmod/chown/chgrp, mount/umount, fsck, mkfs,
 *           useradd/userdel）统一分派接口。
 *
 * 32 位实现: kernel/sched/shell_cmd_admin.c
 * 64 位实现: kernel/arch/x86_64/shell_cmd_admin64.c
 *
 * 口径（见 docs/status/SYSTEM_TOOLS.md §3/§4）:
 *  - chmod 仅支持数字八进制模式（不支持 ugo+rwx 符号模式）。
 *  - inode 具备 i_uid/i_gid 字段，chown/chgrp 可落盘；无属主字段的虚拟
 *    节点只改内存态并如实标注。
 *  - useradd/userdel：当前是单属主账号模型（/account.conf），无持久化本地
 *    多用户数据库；命令显式拒绝并说明原因，不伪造多用户能力。
 *  - mkfs：内核内无 ext2 格式化能力；命令显式拒绝并记录成本。
 * ========================================================================== */
#ifndef SHELL_ADMIN_H
#define SHELL_ADMIN_H

#include <stdint.h>

/* Shell 分派入口：在 shell_execute 的集中分派块末尾调用。
 * 返回 1 表示命令已被本模块认领，0 表示不认识（交回上层 unknown 分支）。 */
int shell_admin_dispatch(const char *cmd, char *args);

/* ---- 可测试的纯/表操作辅助（KUnit 直接调用） ---- */

/* 解析八进制模式串："755" / "0755" / "0"；非法字符/越界返回 0。 */
int admin_parse_mode(const char *s, uint32_t *out_mode);

/* 解析 owner[:group] 规格；has_uid/has_gid 标记两侧是否给出。
 * 两侧 "-1" 表示该侧不变。返回 0 合法，-1 格式非法。 */
int admin_parse_owner(const char *spec,
                      uint32_t *uid, int *has_uid,
                      uint32_t *gid, int *has_gid);

/* 挂载表运行时操作（rc 带回 FS_E* 错误码；0 成功）。 */
int admin_mount_add(const char *dev, const char *mp, int *rc);
int admin_mount_remove(const char *mp, int *rc);
void admin_mount_list(void);

/* 解析 fsck 参数：--check-only/-c（默认）或 --fix/-f。返回 0 合法。 */
int admin_fsck_parse(const char *args, int *fix_mode);

#endif /* SHELL_ADMIN_H */
