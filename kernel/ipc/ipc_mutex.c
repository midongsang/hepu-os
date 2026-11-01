/* ============================================================================
 * 赫普OS - IPC互斥锁与条件变量 (V0.96新增)
 * 实现：可重入互斥锁 + 条件变量，支持进程间同步
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "ipc_mutex.h"
#include "vga.h"
#include "string.h"
#include "pit.h"  /* V3.87-E9: 睡眠等待 */

#define MUTEX_MAX 32
#define COND_MAX 16
#define MUTEX_MAGIC 0x4D555458  /* "MUTX" */
#define COND_MAGIC 0x434F4E44   /* "COND" */

/* === 互斥锁 === */
typedef struct {
    uint32_t magic;
    uint32_t key;
    uint32_t owner_pid;
    uint32_t lock_count;   /* 可重入计数 */
    uint8_t  used;
    uint8_t  locked;
    uint32_t wait_count;
    uint32_t op_count;
} mutex_t;

static mutex_t mutexes[MUTEX_MAX];
static uint32_t mutex_count = 0;

/* V3.87-E9: 锁等待统计 */
static uint32_t mutex_waits = 0;
static uint32_t mutex_timeouts = 0;

void ipc_mutex_wait_stats(uint32_t *waits, uint32_t *timeouts)
{
    if (waits)    *waits = mutex_waits;
    if (timeouts) *timeouts = mutex_timeouts;
}

/* V3.87-E9: 睡眠锁——有界阻塞获取.
 * 被其他进程持有时, PIT 睡眠 10ms 后重试(省电让出 CPU, 不忙等);
 * 直到获取或超时(返回-3). */
int ipc_mutex_lock_timed(int mutex_id, uint32_t pid, uint32_t timeout_ms)
{
    if (mutex_id < 0 || mutex_id >= MUTEX_MAX || !mutexes[mutex_id].used) return -1;
    if (mutexes[mutex_id].magic != MUTEX_MAGIC) return -1;

    uint32_t waited_ms = 0;
    for (;;) {
        if (!mutexes[mutex_id].locked) {
            mutexes[mutex_id].locked = 1;
            mutexes[mutex_id].owner_pid = pid;
            mutexes[mutex_id].lock_count = 1;
            mutexes[mutex_id].op_count++;
            return 0;
        }
        if (mutexes[mutex_id].owner_pid == pid) {
            mutexes[mutex_id].lock_count++;
            mutexes[mutex_id].op_count++;
            return 0;
        }
        /* 被他人持有: 睡眠 10ms 重试(省电让出 CPU, 不忙等) */
        mutexes[mutex_id].wait_count++;
        mutex_waits++;
        extern void pit_sleep(uint32_t);
        pit_sleep(10);
        waited_ms += 10;
        mutexes[mutex_id].wait_count--;
        if (waited_ms >= timeout_ms) {
            mutex_timeouts++;
            return -3;  /* 超时 */
        }
    }
}

void ipc_mutex_init(void)
{
    memset(mutexes, 0, sizeof(mutexes));
    mutex_count = 0;
    vga_puts("[IPC-MUTEX] mutex subsystem init done \n");
}

int ipc_mutex_create(uint32_t key)
{
    for (int i = 0; i < MUTEX_MAX; i++) {
        if (mutexes[i].used && mutexes[i].key == key) return i;
    }
    for (int i = 0; i < MUTEX_MAX; i++) {
        if (!mutexes[i].used) {
            mutexes[i].magic = MUTEX_MAGIC;
            mutexes[i].key = key;
            mutexes[i].owner_pid = 0;
            mutexes[i].lock_count = 0;
            mutexes[i].used = 1;
            mutexes[i].locked = 0;
            mutexes[i].wait_count = 0;
            mutexes[i].op_count = 0;
            mutex_count++;
            return i;
        }
    }
    return -1;
}

int ipc_mutex_lock(int mutex_id, uint32_t pid)
{
    if (mutex_id < 0 || mutex_id >= MUTEX_MAX || !mutexes[mutex_id].used) return -1;
    if (mutexes[mutex_id].magic != MUTEX_MAGIC) return -1;

    if (!mutexes[mutex_id].locked) {
        /* 未锁定，直接获取 */
        mutexes[mutex_id].locked = 1;
        mutexes[mutex_id].owner_pid = pid;
        mutexes[mutex_id].lock_count = 1;
        mutexes[mutex_id].op_count++;
        return 0;
    }
    if (mutexes[mutex_id].owner_pid == pid) {
        /* 可重入：同一进程再次加锁 */
        mutexes[mutex_id].lock_count++;
        mutexes[mutex_id].op_count++;
        return 0;
    }
    /* 被其他进程持有，需要等待 */
    mutexes[mutex_id].wait_count++;
    return -2;
}

int ipc_mutex_trylock(int mutex_id, uint32_t pid)
{
    if (mutex_id < 0 || mutex_id >= MUTEX_MAX || !mutexes[mutex_id].used) return -1;
    if (!mutexes[mutex_id].locked) {
        mutexes[mutex_id].locked = 1;
        mutexes[mutex_id].owner_pid = pid;
        mutexes[mutex_id].lock_count = 1;
        return 0;
    }
    if (mutexes[mutex_id].owner_pid == pid) {
        mutexes[mutex_id].lock_count++;
        return 0;
    }
    return -1;  /* 会阻塞 */
}

int ipc_mutex_unlock(int mutex_id, uint32_t pid)
{
    if (mutex_id < 0 || mutex_id >= MUTEX_MAX || !mutexes[mutex_id].used) return -1;
    if (mutexes[mutex_id].owner_pid != pid) return -1;  /* 非所有者 */
    if (mutexes[mutex_id].lock_count == 0) return -1;

    mutexes[mutex_id].lock_count--;
    mutexes[mutex_id].op_count++;
    if (mutexes[mutex_id].lock_count == 0) {
        mutexes[mutex_id].locked = 0;
        mutexes[mutex_id].owner_pid = 0;
        if (mutexes[mutex_id].wait_count > 0) {
            mutexes[mutex_id].wait_count--;
        }
    }
    return 0;
}

int ipc_mutex_destroy(int mutex_id)
{
    if (mutex_id < 0 || mutex_id >= MUTEX_MAX || !mutexes[mutex_id].used) return -1;
    mutexes[mutex_id].used = 0;
    mutexes[mutex_id].magic = 0;
    mutex_count--;
    return 0;
}

/* === 条件变量 === */
typedef struct {
    uint32_t magic;
    uint32_t key;
    uint8_t  used;
    uint32_t wait_count;
    uint32_t signal_count;
} cond_t;

static cond_t conds[COND_MAX];
static uint32_t cond_count = 0;

void ipc_cond_init(void)
{
    memset(conds, 0, sizeof(conds));
    cond_count = 0;
    vga_puts("[IPC-COND] condvar subsystem init done \n");
}

int ipc_cond_create(uint32_t key)
{
    for (int i = 0; i < COND_MAX; i++) {
        if (conds[i].used && conds[i].key == key) return i;
    }
    for (int i = 0; i < COND_MAX; i++) {
        if (!conds[i].used) {
            conds[i].magic = COND_MAGIC;
            conds[i].key = key;
            conds[i].used = 1;
            conds[i].wait_count = 0;
            conds[i].signal_count = 0;
            cond_count++;
            return i;
        }
    }
    return -1;
}

int ipc_cond_wait(int cond_id, int mutex_id, uint32_t pid)
{
    if (cond_id < 0 || cond_id >= COND_MAX || !conds[cond_id].used) return -1;
    if (conds[cond_id].magic != COND_MAGIC) return -1;

    /* 释放互斥锁 */
    ipc_mutex_unlock(mutex_id, pid);
    /* 等待条件（简化：直接增加等待计数） */
    conds[cond_id].wait_count++;
    /* 在实际系统中这里会阻塞，直到被signal唤醒 */
    /* 重新获取互斥锁 */
    return ipc_mutex_lock(mutex_id, pid);
}

int ipc_cond_signal(int cond_id)
{
    if (cond_id < 0 || cond_id >= COND_MAX || !conds[cond_id].used) return -1;
    if (conds[cond_id].wait_count > 0) {
        conds[cond_id].wait_count--;
        conds[cond_id].signal_count++;
        return 0;
    }
    return 0;  /* 没有等待者，signal无效果 */
}

int ipc_cond_broadcast(int cond_id)
{
    if (cond_id < 0 || cond_id >= COND_MAX || !conds[cond_id].used) return -1;
    uint32_t awakened = conds[cond_id].wait_count;
    conds[cond_id].wait_count = 0;
    conds[cond_id].signal_count += awakened;
    return (int)awakened;
}

int ipc_cond_destroy(int cond_id)
{
    if (cond_id < 0 || cond_id >= COND_MAX || !conds[cond_id].used) return -1;
    conds[cond_id].used = 0;
    conds[cond_id].magic = 0;
    cond_count--;
    return 0;
}

/* 统计 */
void ipc_mutex_stats(uint32_t *total, uint32_t *used)
{
    if (total) *total = MUTEX_MAX;
    if (used) *used = mutex_count;
}

void ipc_cond_stats(uint32_t *total, uint32_t *used)
{
    if (total) *total = COND_MAX;
    if (used) *used = cond_count;
}
