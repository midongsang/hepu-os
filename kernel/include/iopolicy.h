#ifndef IOPOLICY_H
#define IOPOLICY_H

/* V4.02-E47: IO 策略框架——设备注册表 + 忙等让步 + 统计 */

typedef enum {
    IO_POLICY_IRQ = 0,   /* 中断驱动(低速设备: 键盘/鼠标) */
    IO_POLICY_POLL = 1   /* 轮询(高速设备: IDE) */
} io_policy_t;

/* 设备固定索引(与 iopolicy_init 注册顺序一致) */
#define IO_DEV_KBD    0
#define IO_DEV_IDE0   1
#define IO_DEV_MOUSE  2
#define IO_DEV_SERIAL 3

void iopolicy_init(void);
int  iopolicy_register(const char *name, io_policy_t policy);
void iopolicy_relax(int idx);       /* 忙等循环内: 统计+每64次pause让步 */
void iopolicy_wait_start(int idx);  /* 一次等待事务开始 */
void iopolicy_irq(int idx);         /* ISR 顶半部: 中断计数 */
void iopolicy_dump(void);           /* iostat 输出 */

#endif /* IOPOLICY_H */
