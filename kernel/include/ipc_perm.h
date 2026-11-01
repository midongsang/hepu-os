#ifndef IPC_PERM_H
#define IPC_PERM_H

#include <stdint.h>

/* 权限级别 */
#define IPC_PRIV_USER   0
#define IPC_PRIV_SYSTEM 1
#define IPC_PRIV_KERNEL 2

/* IPC对象类型 */
#define IPC_OBJ_QUEUE 0
#define IPC_OBJ_PIPE  1
#define IPC_OBJ_SHM   2
#define IPC_OBJ_SEM   3

/* 操作类型 */
#define IPC_OP_SEND    0
#define IPC_OP_RECV    1
#define IPC_OP_CREATE  2
#define IPC_OP_DESTROY 3

/* 初始化 */
void ipc_perm_init(void);

/* 注册/注销IPC对象所有权 */
int ipc_perm_register(uint32_t owner_pid, uint32_t object_id,
                      uint8_t object_type, uint8_t read_perm, uint8_t write_perm);
int ipc_perm_unregister(uint32_t object_id, uint8_t object_type);

/* 受信对象 */
int ipc_perm_set_trusted(uint32_t object_id, uint8_t object_type, uint8_t trusted);

/* 权限检查：返回1允许，0拒绝 */
int ipc_perm_check(uint32_t pid, uint32_t object_id, uint8_t object_type,
                   uint8_t operation, uint8_t is_write);

/* 获取所有者 */
int ipc_perm_get_owner(uint32_t object_id, uint8_t object_type, uint32_t *owner);

/* 统计/调试 */
void ipc_perm_stats(uint32_t *entries, uint32_t *denied);
void ipc_perm_dump(void);

#endif
