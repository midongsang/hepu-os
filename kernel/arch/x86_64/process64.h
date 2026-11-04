/* ============================================================================
 * 赫普OS 64位 - 进程快照契约
 * 视图层只读本结构，不直接访问内核 process_t。
 * ========================================================================== */

#ifndef HEPU_PROCESS64_H
#define HEPU_PROCESS64_H

#include <stdint.h>

#define PROCESS64_MAX_SLOTS 32

typedef struct {
    int pid;
    int parent_pid;
    int state;
    int priority;
    int cpu_id;
    int env_type;          /* 0=native, 1=linux_compat, 2=win_compat */
    int domain_id;         /* 所属隔离域 ID */
    uint64_t runtime;      /* 累计运行 tick */
    uint64_t mem_pages;    /* 已映射物理页数 */
    uint64_t mem_limit;    /* 内存上限（页），0=无限制 */
    char name[32];
} process64_stat_t;

/* 把活动进程拷进 out，最多 max 项；返回实际项数。 */
int process64_snapshot(process64_stat_t *out, int max);

/* ============================================================================
 * V0.54.6: hepu_process_* 统一接口 → process64_* 私有 API 映射
 * 新代码 include "process/hepu_process.h" 后直接用 hepu_process_* 即可跨架构
 * ========================================================================== */
#include "process/hepu_process.h"

static inline int hepu_process_snapshot(hepu_process_stat_t *out, int max) {
    if (!out || max <= 0) return -1;
    static process64_stat_t tmp[64];
    int n = process64_snapshot(tmp, max);
    for (int i = 0; i < n; i++) {
        out[i].pid        = tmp[i].pid;
        out[i].parent_pid = tmp[i].parent_pid;
        out[i].state      = tmp[i].state;
        out[i].priority   = tmp[i].priority;
        out[i].cpu_id     = tmp[i].cpu_id;
        out[i].env_type   = tmp[i].env_type;
        out[i].domain_id  = tmp[i].domain_id;
        out[i].runtime    = tmp[i].runtime;
        out[i].mem_pages  = tmp[i].mem_pages;
        out[i].mem_limit  = tmp[i].mem_limit;
        for (int j = 0; j < 32 && tmp[i].name[j]; j++) out[i].name[j] = tmp[i].name[j];
        out[i].name[31] = 0;
    }
    return n;
}

static inline int hepu_process_count(void) {
    static process64_stat_t tmp[1];
    return process64_snapshot(tmp, 64);
    /* 注：上面返回的是 snapshot 项数，不是 count。
       真正 count 需要遍历所有槽，64 位暂用 snapshot 数量近似 */
}

static inline int hepu_process_get_current_pid(void) {
    static process64_stat_t tmp[1];
    int n = process64_snapshot(tmp, 64);
    for (int i = 0; i < n; i++)
        if (tmp[i].state == 1 /* PROC_RUNNING */) return tmp[i].pid;
    return 0;
}

#endif /* HEPU_PROCESS64_H */
