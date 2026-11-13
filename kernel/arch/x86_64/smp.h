/* ============================================================================
 * 赫普OS - 64位多处理器支持（SMP）
 * V0.10.136: CPU核心数量检测 + SMP启动框架
 * ========================================================================== */

#ifndef SMP_H
#define SMP_H

#include <stdint.h>

/* CPU信息结构 */
typedef struct {
    uint32_t cpu_id;           /* APIC ID */
    uint8_t  online;           /* 是否在线 */
    uint8_t  is_bsp;           /* 是否是BSP（引导处理器） */
    uint16_t reserved;
    uint64_t stack_ptr;        /* 内核栈指针 */
} cpu_info_t;

/* 最大CPU数量 */
#define MAX_CPUS 16

/* SB-21: IPI 消息类型（与 smp.c 内部定义保持一致） */
#define IPI_MSG_RESCHEDULE  1
#define IPI_MSG_TLB_FLUSH   2
#define IPI_MSG_STOP        3

/* SB-21: IPI 通道 */
int  smp_ipi_send(int target, int type, uint64_t arg);
int  smp_ipi_send_others(int type, uint64_t arg);
void smp_ipi_snapshot(uint64_t *out);
int  smp_ipi_wait_ack(const uint64_t *snapshot, int expect, uint64_t timeout);
uint64_t smp_ipi_ack_count(int cpu);
uint64_t smp_ipi_dropped_count(void);

/* SMP初始化 */
int smp_init(void);

/* 获取CPU数量 */
uint32_t smp_get_cpu_count(void);

/* 获取当前CPU ID */
uint32_t smp_get_current_cpu(void);

/* 获取CPU信息 */
cpu_info_t *smp_get_cpu_info(uint32_t cpu_id);

/* 启动应用处理器（AP） */
int smp_start_ap(uint32_t cpu_id);

/* 检查是否是BSP */
int smp_is_bsp(void);

/* AP C入口（由trampoline调用） */
void ap_c_entry(void);

/* SMP定时器tick（每个CPU的Local APIC定时器中断调用） */
void smp_timer_tick(void);

/* IPI核间中断处理 */
void smp_ipi_handler(void);

/* 获取per-CPU定时器tick计数 */
uint64_t smp_get_cpu_ticks(uint32_t cpu_id);

#endif /* SMP_H */
