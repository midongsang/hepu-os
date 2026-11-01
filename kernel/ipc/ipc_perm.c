/* ============================================================================
 * 赫普OS - IPC权限检查 (V1.02新增)
 * 实现：IPC对象访问控制，进程权限级别，操作审计
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "ipc_perm.h"
#include "vga.h"
#include "string.h"

#define IPC_PERM_MAGIC 0x49504350  /* "IPCP" */
#define IPC_PERM_MAX_ENTRIES 32

/* 权限级别 */
#define IPC_PRIV_USER  0   /* 普通用户进程 */
#define IPC_PRIV_SYSTEM 1  /* 系统服务 */
#define IPC_PRIV_KERNEL 2  /* 内核级 */

/* 操作类型 */
#define IPC_OP_SEND   0
#define IPC_OP_RECV   1
#define IPC_OP_CREATE 2
#define IPC_OP_DESTROY 3

/* IPC对象访问控制条目 */
typedef struct {
    uint8_t  used;
    uint32_t owner_pid;      /* 所有者 */
    uint32_t object_id;      /* IPC对象ID（队列/管道/shm/sem） */
    uint8_t  object_type;    /* 0=队列 1=管道 2=shm 3=信号量 */
    uint8_t  owner_priv;     /* 所有者权限级别 */
    uint8_t  read_perm;      /* 读权限位图 */
    uint8_t  write_perm;     /* 写权限位图 */
    uint8_t  trusted;        /* 是否受信对象 */
    uint32_t access_count;
} ipc_perm_entry_t;

static ipc_perm_entry_t perm_table[IPC_PERM_MAX_ENTRIES];
static uint32_t perm_count = 0;
static uint8_t perm_initialized = 0;
static uint32_t denied_count = 0;

void ipc_perm_init(void)
{
    memset(perm_table, 0, sizeof(perm_table));
    perm_count = 0;
    denied_count = 0;
    perm_initialized = 1;
    vga_puts("[IPC-PERM] IPC perm check subsystem init done \n");
}

/* 获取进程权限级别（简化：从PID归属判断） */
static uint8_t get_process_priv(uint32_t pid)
{
    /* 低PID通常为系统服务 */
    if (pid < 4) return IPC_PRIV_KERNEL;
    if (pid < 16) return IPC_PRIV_SYSTEM;
    return IPC_PRIV_USER;
}

/* 注册IPC对象的所有权 */
int ipc_perm_register(uint32_t owner_pid, uint32_t object_id,
                      uint8_t object_type, uint8_t read_perm, uint8_t write_perm)
{
    if (!perm_initialized) return -1;
    if (perm_count >= IPC_PERM_MAX_ENTRIES) return -2;

    for (int i = 0; i < IPC_PERM_MAX_ENTRIES; i++) {
        if (perm_table[i].used &&
            perm_table[i].object_id == object_id &&
            perm_table[i].object_type == object_type) {
            return -3;  /* 已注册 */
        }
    }

    for (int i = 0; i < IPC_PERM_MAX_ENTRIES; i++) {
        if (!perm_table[i].used) {
            perm_table[i].used = 1;
            perm_table[i].owner_pid = owner_pid;
            perm_table[i].object_id = object_id;
            perm_table[i].object_type = object_type;
            perm_table[i].owner_priv = get_process_priv(owner_pid);
            perm_table[i].read_perm = read_perm;
            perm_table[i].write_perm = write_perm;
            perm_table[i].trusted = 0;
            perm_table[i].access_count = 0;
            perm_count++;
            return i;
        }
    }
    return -1;
}

/* 注销IPC对象 */
int ipc_perm_unregister(uint32_t object_id, uint8_t object_type)
{
    for (int i = 0; i < IPC_PERM_MAX_ENTRIES; i++) {
        if (perm_table[i].used &&
            perm_table[i].object_id == object_id &&
            perm_table[i].object_type == object_type) {
            perm_table[i].used = 0;
            perm_count--;
            return 0;
        }
    }
    return -1;
}

/* 标记为受信对象 */
int ipc_perm_set_trusted(uint32_t object_id, uint8_t object_type, uint8_t trusted)
{
    for (int i = 0; i < IPC_PERM_MAX_ENTRIES; i++) {
        if (perm_table[i].used &&
            perm_table[i].object_id == object_id &&
            perm_table[i].object_type == object_type) {
            perm_table[i].trusted = trusted ? 1 : 0;
            return 0;
        }
    }
    return -1;
}

/* 权限检查：进程对对象执行操作是否被允许 */
int ipc_perm_check(uint32_t pid, uint32_t object_id, uint8_t object_type,
                   uint8_t operation, uint8_t is_write)
{
    if (!perm_initialized) return 0;  /* 未初始化则放行 */

    uint8_t priv = get_process_priv(pid);

    /* 内核级进程有完全权限 */
    if (priv >= IPC_PRIV_KERNEL) return 1;

    /* 找对象条目 */
    ipc_perm_entry_t *entry = NULL;
    for (int i = 0; i < IPC_PERM_MAX_ENTRIES; i++) {
        if (perm_table[i].used &&
            perm_table[i].object_id == object_id &&
            perm_table[i].object_type == object_type) {
            entry = &perm_table[i];
            break;
        }
    }

    /* V7.77安全修复: 未注册对象不再直接放行（漏洞X）
     * 原代码"未注册对象放行（兼容旧接口）"是安全旁路，
     * 注册是可选动作，忘注册=全放行。
     * 修复：只有CREATE操作放行，其他操作拒绝（默认拒绝原则）。 */
    if (!entry) {
        if (operation == IPC_OP_CREATE) return 1;
        denied_count++;
        return 0;  /* 未注册对象：非创建操作拒绝 */
    }

    /* 受信对象放行 */
    if (entry->trusted) {
        entry->access_count++;
        return 1;
    }

    /* 所有者：权限级别高于等于所有者级别则放行 */
    if (entry->owner_pid == pid) {
        if (priv >= entry->owner_priv) {
            entry->access_count++;
            return 1;
        }
        denied_count++;
        return 0;
    }

    /* 系统服务：比所有者权限级别低的放行 */
    if (priv == IPC_PRIV_SYSTEM && entry->owner_priv <= IPC_PRIV_SYSTEM) {
        entry->access_count++;
        return 1;
    }

    /* 普通进程访问他人对象：按权限位图检查 */
    if (priv == IPC_PRIV_USER) {
        uint8_t access_perm = is_write ? entry->write_perm : entry->read_perm;
        /* 简化：权限位图bit0表示允许所有用户，bit1表示仅系统 */
        if (access_perm & 0x01) {
            entry->access_count++;
            return 1;
        }
    }

    denied_count++;
    return 0;
}

/* 获取对象所有者 */
int ipc_perm_get_owner(uint32_t object_id, uint8_t object_type, uint32_t *owner)
{
    for (int i = 0; i < IPC_PERM_MAX_ENTRIES; i++) {
        if (perm_table[i].used &&
            perm_table[i].object_id == object_id &&
            perm_table[i].object_type == object_type) {
            if (owner) *owner = perm_table[i].owner_pid;
            return 0;
        }
    }
    return -1;
}

/* 权限统计 */
void ipc_perm_stats(uint32_t *entries, uint32_t *denied)
{
    if (entries) *entries = perm_count;
    if (denied) *denied = denied_count;
}

/* 打印权限表 */
void ipc_perm_dump(void)
{
    vga_printf("[IPC-PERM] perm table(%d entries, denied %d):\n", perm_count, denied_count);
    for (int i = 0; i < IPC_PERM_MAX_ENTRIES; i++) {
        if (perm_table[i].used) {
            vga_printf("  [%d] obj=%d type=%d owner=%d priv=%d r=%d w=%d %s access=%d\n",
                       i, perm_table[i].object_id, perm_table[i].object_type,
                       perm_table[i].owner_pid, perm_table[i].owner_priv,
                       perm_table[i].read_perm, perm_table[i].write_perm,
                       perm_table[i].trusted ? "trusted" : "",
                       perm_table[i].access_count);
        }
    }
}
