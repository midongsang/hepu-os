#ifndef IPC_MUTEX_H
#define IPC_MUTEX_H

#include <stdint.h>

/* === 互斥锁 === */
void ipc_mutex_init(void);
int  ipc_mutex_create(uint32_t key);
int  ipc_mutex_lock(int mutex_id, uint32_t pid);
int  ipc_mutex_trylock(int mutex_id, uint32_t pid);
int  ipc_mutex_unlock(int mutex_id, uint32_t pid);
int  ipc_mutex_destroy(int mutex_id);
void ipc_mutex_stats(uint32_t *total, uint32_t *used);

/* V3.87-E9: 睡眠锁——有界阻塞获取(基于 PIT 睡眠等待, 省电让出 CPU).
 * 成功返回0; 超时返回-3; 参数错返回-1. */
int ipc_mutex_lock_timed(int mutex_id, uint32_t pid, uint32_t timeout_ms);

/* V3.87-E9: 锁等待统计(供 perf): 等待次数/超时次数 */
void ipc_mutex_wait_stats(uint32_t *waits, uint32_t *timeouts);

/* === 条件变量 === */
void ipc_cond_init(void);
int  ipc_cond_create(uint32_t key);
int  ipc_cond_wait(int cond_id, int mutex_id, uint32_t pid);
int  ipc_cond_signal(int cond_id);
int  ipc_cond_broadcast(int cond_id);
int  ipc_cond_destroy(int cond_id);
void ipc_cond_stats(uint32_t *total, uint32_t *used);

#endif
