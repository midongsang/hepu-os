#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>

void     sched_init(void);
uint32_t sched_create_process(const char *name, uint32_t entry, uint32_t priority);
void     sched_tick(void);
uint32_t sched_current_pid(void);
void     sched_stats(uint32_t *total, uint32_t *running, uint32_t *ready);
void     sched_perf_stats(uint32_t *switches, uint32_t *idle_ticks);  /* V4.51-E96 */

#endif
