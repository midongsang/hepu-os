/* ============================================================================
 * 赫普OS 64位 - 进程凭据与进程级能力位
 *
 * SEC-04: 凭据从全局静态改为每进程一份，fork 子进程经 fork 快照继承；
 * 高危操作（裸盘读写 / mkfs / reboot）由进程能力位放行，不再只靠 PID 白名单。
 *
 * 说明：这是进程级能力位，不是内核 capability 对象句柄；对象模型仍由
 * capability64.c 维护。能力位绑定到授权时的 uid，setuid 变更后自动清空。
 * ========================================================================== */
#ifndef HEPU_USER_CRED_H
#define HEPU_USER_CRED_H

#include <stdint.h>

#define HEPU_PROC_CAP_RAW_DISK  (1u << 0)
#define HEPU_PROC_CAP_MKFS      (1u << 1)
#define HEPU_PROC_CAP_REBOOT    (1u << 2)

/* 当前进程凭据 */
int user_process_cred_get_uid(void);
int user_process_cred_get_gid(void);
int user_process_cred_get_euid(void);
int user_process_cred_get_egid(void);
int user_process_cred_get_umask(void);
int user_process_cred_setuid(int uid);
int user_process_cred_setgid(int gid);
int user_process_cred_setumask(int mask);

/* 进程级能力位：只有授权 uid 仍等于当前 uid/euid 时才算持有 */
int user_process_cred_has_cap(uint32_t cap);
int user_process_grant_cap(int pid, uint32_t cap);

/* fork 子进程的凭据快照（按 fork 快照槽索引）；由 sys_fork/sys_exit 调用 */
int user_process_cred_fork_prepare(int child_idx);
void user_process_cred_bind_proc64(int proc_idx);
void user_process_cred_fork_leave(int child_idx);

/* 启动期逻辑自测（不依赖真实进程切换） */
int user_process_cred_selftest(void);

#endif /* HEPU_USER_CRED_H */
