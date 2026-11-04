/* ============================================================================
 * 赫普OS - IPC信号量 (V0.96新增)
 * 实现：System V风格信号量，支持PV操作、二元/计数信号量
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "ipc_sem.h"
#include "vga.h"
#include "pit.h"  /* V3.87-E9: 睡眠等待 */
#include "string.h"

#define SEM_MAX 32
#define SEM_MAGIC 0x53454D41  /* "SEMA" */

typedef struct {
    uint32_t magic;
    uint32_t key;
    int32_t  value;
    uint32_t owner_pid;
    uint8_t  used;
    uint8_t  binary;       /* 1=二元信号量, 0=计数信号量 */
    uint32_t wait_count;   /* 等待进程数 */
    uint32_t op_count;     /* 操作计数（统计用） */
} semaphore_t;

static semaphore_t semaphores[SEM_MAX];
static uint32_t sem_count = 0;

void ipc_sem_init(void)
{
    memset(semaphores, 0, sizeof(semaphores));
    sem_count = 0;
    vga_puts("[IPC-SEM] semaphore subsystem init done \n");
}

/* 创建或获取信号量 */
int ipc_semget(uint32_t key, int32_t init_value)
{
    /* 查找已存在的 */
    for (int i = 0; i < SEM_MAX; i++) {
        if (semaphores[i].used && semaphores[i].key == key) {
            return i;
        }
    }
    /* 创建新的 */
    for (int i = 0; i < SEM_MAX; i++) {
        if (!semaphores[i].used) {
            semaphores[i].magic = SEM_MAGIC;
            semaphores[i].key = key;
            semaphores[i].value = init_value;
            /* V7.90: 记录创建者PID（漏洞X权限接线） */
            extern uint32_t sched_current_pid(void);
            semaphores[i].owner_pid = sched_current_pid();
            semaphores[i].used = 1;
            semaphores[i].binary = (init_value <= 1) ? 1 : 0;
            semaphores[i].wait_count = 0;
            semaphores[i].op_count = 0;
            sem_count++;

            /* V7.90: 注册IPC权限对象（漏洞X完全接线）
             * 信号量对象ID = key + 0x30000偏移 */
            extern int ipc_perm_register(uint32_t, uint32_t, uint8_t, uint8_t, uint8_t);
            ipc_perm_register(semaphores[i].owner_pid,
                              key + 0x30000,
                              3,  /* IPC_OBJ_SEM */
                              0x01, 0x01);  /* 读写权限：所有用户（简化） */

            vga_printf("[IPC-SEM] sem %d created: key=%d, value=%d, owner=%d\n",
                       i, key, init_value, semaphores[i].owner_pid);
            return i;
        }
    }
    return -1;
}

/* P操作（等待/减1） */
int ipc_sem_wait(int sem_id)
{
    if (sem_id < 0 || sem_id >= SEM_MAX || !semaphores[sem_id].used) return -1;
    if (semaphores[sem_id].magic != SEM_MAGIC) return -1;

    /* V7.90: IPC权限检查（漏洞X完全接线） */
    extern uint32_t sched_current_pid(void);
    extern int ipc_perm_check(uint32_t, uint32_t, uint8_t, uint8_t, uint8_t);
    uint32_t caller_pid = sched_current_pid();
    if (!ipc_perm_check(caller_pid, semaphores[sem_id].key + 0x30000,
                        3,  /* IPC_OBJ_SEM */
                        0,  /* IPC_OP_RECV */
                        0)) {
        vga_printf("[IPC-SEM] sem %d: pid %d denied wait\n", sem_id, caller_pid);
        return -1;
    }

    /* 非阻塞尝试：如果value>0，直接减1 */
    if (semaphores[sem_id].value > 0) {
        semaphores[sem_id].value--;
        semaphores[sem_id].op_count++;
        return 0;
    }
    /* value==0，需要等待 */
    semaphores[sem_id].wait_count++;
    /* 在实际系统中这里会阻塞进程，简化实现返回-2表示需要等待 */
    return -2;
}

/* 非阻塞P操作 */
int ipc_sem_trywait(int sem_id)
{
    if (sem_id < 0 || sem_id >= SEM_MAX || !semaphores[sem_id].used) return -1;
    if (semaphores[sem_id].value > 0) {
        semaphores[sem_id].value--;
        semaphores[sem_id].op_count++;
        return 0;
    }
    return -1;  /* 会阻塞 */
}

/* V3.87-E9: 信号量等待统计 */
static uint32_t sem_waits = 0;
static uint32_t sem_timeouts = 0;

void ipc_sem_wait_stats(uint32_t *waits, uint32_t *timeouts)
{
    if (waits)    *waits = sem_waits;
    if (timeouts) *timeouts = sem_timeouts;
}

/* V3.87-E9: 信号量有界阻塞等待——value=0 时 PIT 睡眠 10ms 重试 */
int ipc_sem_wait_timed(int sem_id, uint32_t timeout_ms)
{
    if (sem_id < 0 || sem_id >= SEM_MAX || !semaphores[sem_id].used) return -1;
    if (semaphores[sem_id].magic != SEM_MAGIC) return -1;

    uint32_t waited_ms = 0;
    for (;;) {
        if (semaphores[sem_id].value > 0) {
            semaphores[sem_id].value--;
            semaphores[sem_id].op_count++;
            return 0;
        }
        semaphores[sem_id].wait_count++;
        sem_waits++;
        extern void pit_sleep(uint32_t);
        pit_sleep(10);
        waited_ms += 10;
        semaphores[sem_id].wait_count--;
        if (waited_ms >= timeout_ms) {
            sem_timeouts++;
            return -3;
        }
    }
}

/* V操作（释放/加1） */
int ipc_sem_post(int sem_id)
{
    if (sem_id < 0 || sem_id >= SEM_MAX || !semaphores[sem_id].used) return -1;
    if (semaphores[sem_id].magic != SEM_MAGIC) return -1;

    /* V7.90: IPC权限检查（漏洞X完全接线） */
    extern uint32_t sched_current_pid(void);
    extern int ipc_perm_check(uint32_t, uint32_t, uint8_t, uint8_t, uint8_t);
    uint32_t caller_pid = sched_current_pid();
    if (!ipc_perm_check(caller_pid, semaphores[sem_id].key + 0x30000,
                        3,  /* IPC_OBJ_SEM */
                        1,  /* IPC_OP_SEND */
                        1)) {
        vga_printf("[IPC-SEM] sem %d: pid %d denied post\n", sem_id, caller_pid);
        return -1;
    }

    if (semaphores[sem_id].binary) {
        /* 二元信号量：value最大为1 */
        if (semaphores[sem_id].value >= 1) return -1;
    }
    semaphores[sem_id].value++;
    semaphores[sem_id].op_count++;
    if (semaphores[sem_id].wait_count > 0) {
        semaphores[sem_id].wait_count--;
    }
    return 0;
}

/* 获取信号量当前值 */
int ipc_sem_getvalue(int sem_id, int32_t *value)
{
    if (sem_id < 0 || sem_id >= SEM_MAX || !semaphores[sem_id].used) return -1;
    if (value) *value = semaphores[sem_id].value;
    return 0;
}

/* 删除信号量 */
int ipc_sem_remove(int sem_id)
{
    if (sem_id < 0 || sem_id >= SEM_MAX || !semaphores[sem_id].used) return -1;
    semaphores[sem_id].used = 0;
    semaphores[sem_id].magic = 0;
    sem_count--;
    return 0;
}

/* 信号量统计 */
void ipc_sem_stats(uint32_t *total, uint32_t *used)
{
    if (total) *total = SEM_MAX;
    if (used) *used = sem_count;
}
