#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <stdint.h>

/* V3.87-E6: 软看门狗——PIT 中断检测主循环心跳, 超时自动硬复位.
 * 内核主循环( idle / shell )周期性 wd_feed(); PIT 中断检查超时.
 * 防"死循环/死锁导致整机卡死"——中断仍运行, 超时即复位. */
#define WD_TIMEOUT_TICKS 3000   /* V3.89-E27: 30s——用户态进程合法运行(演示/应用<30s)不误触发 */

void wd_init(void);
void wd_feed(void);            /* 主循环喂狗 */
int  wd_check(void);           /* PIT 中断调用: 超时返回1 */
void wd_trigger(void);         /* 复位前日志 + 硬复位(0xCF9→0x64/0xFE) */
uint32_t wd_stats(void);       /* 触发次数(供 wd 命令) */
uint32_t wd_heartbeat_age(void); /* V3.89-E30: 距上次喂狗 tick */
int  wd_enabled(void);

#endif /* WATCHDOG_H */

/* 测试用: 使心跳过期(0), 5 秒后触发复位——验证看门狗链路 */
void wd_arm_test(void);
