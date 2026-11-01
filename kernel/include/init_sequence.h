#ifndef INIT_SEQUENCE_H
#include "types.h"
#define INIT_SEQUENCE_H

#include <stdint.h>

/* 初始化阶段 */
typedef enum {
    INIT_PHASE_CORE = 0,       /* 核心：内存/进程/调度 */
    INIT_PHASE_HARDWARE = 1,   /* 硬件：中断/驱动 */
    INIT_PHASE_FS = 2,         /* 文件系统 */
    INIT_PHASE_NETWORK = 3,    /* 网络协议栈 */
    INIT_PHASE_SECURITY = 4,   /* 安全/加密 */
    INIT_PHASE_SERVICES = 5,   /* 系统服务 */
    INIT_PHASE_APPS = 6,       /* 应用框架 */
    INIT_PHASE_COUNT = 7
} init_phase_t;

/* 初始化统计 */
typedef struct {
    u32 modules_initialized;
    u32 modules_failed;
    u64 total_time_ms;
    u64 phase_times[INIT_PHASE_COUNT];
} init_stats_t;

void hepu_init_sequence(void);
int  hepu_init_phase(init_phase_t phase);
void hepu_init_get_stats(init_stats_t *stats);
const char *hepu_init_phase_name(init_phase_t phase);

#endif /* INIT_SEQUENCE_H */
