#ifndef IPC_SEM_H
#define IPC_SEM_H

#include <stdint.h>

/* 初始化信号量子系统 */
void ipc_sem_init(void);

/* 创建或获取信号量（key相同返回同一个） */
int ipc_semget(uint32_t key, int32_t init_value);

/* P操作（等待/减1），返回-2表示需要阻塞等待 */
int ipc_sem_wait(int sem_id);

/* 非阻塞P操作 */
int ipc_sem_trywait(int sem_id);

/* V操作（释放/加1） */
int ipc_sem_post(int sem_id);

/* V3.87-E9: 信号量有界阻塞等待——value=0 时 PIT 睡眠重试.
 * 成功返回0; 超时返回-3; 参数错返回-1. */
int ipc_sem_wait_timed(int sem_id, uint32_t timeout_ms);

/* V3.87-E9: 信号量等待统计(供 perf) */
void ipc_sem_wait_stats(uint32_t *waits, uint32_t *timeouts);

/* 获取当前值 */
int ipc_sem_getvalue(int sem_id, int32_t *value);

/* 删除信号量 */
int ipc_sem_remove(int sem_id);

/* 统计 */
void ipc_sem_stats(uint32_t *total, uint32_t *used);

#endif
