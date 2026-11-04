/* ============================================================================
 * 赫普OS - 进程抽象层统一接口（类型层）
 * V0.54.6: hepu_process_stat_t 跨架构共享
 *          hepu_process_* 函数由 process.h（32 位）/ process64.h（64 位）
 *          末尾 static inline 包装提供——同一个名字两边都有，调用点零改动。
 * ========================================================================== */

#ifndef HEPU_PROCESS_H
#define HEPU_PROCESS_H

#include <stdint.h>

/* 统一快照视图层结构（上层 shell ps/taskmgr 等只读此结构） */
typedef struct {
    int      pid;
    int      parent_pid;
    int      state;         /* 与 proc_state_t / state 枚举值对齐 */
    int      priority;
    int      cpu_id;        /* 32 位单 CPU 固定 0；64 位记录当前 CPU */
    int      env_type;      /* 0=native, 1=linux_compat */
    int      domain_id;     /* 所属隔离域 ID */
    uint64_t runtime;       /* 累计运行 tick */
    uint64_t mem_pages;     /* 已映射物理页数 */
    uint64_t mem_limit;     /* 内存上限（页），0=无限制 */
    char     name[32];
} hepu_process_stat_t;

/* 注：hepu_process_* 函数由架构头文件提供 static inline 实现：
 *   32 位 → kernel/include/process.h 末尾（#include "process/hepu_process.h" 之后）
 *   64 位 → kernel/arch/x86_64/process64.h 末尾（同上） */

#endif /* HEPU_PROCESS_H */
