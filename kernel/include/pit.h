#ifndef PIT_H
#define PIT_H

#include <stdint.h>

/* 8254 PIT 端口 */
#define PIT_CHANNEL0  0x40   /* 通道0 (系统时钟) */
#define PIT_CHANNEL1  0x41   /* 通道1 (DRAM刷新) */
#define PIT_CHANNEL2  0x42   /* 通道2 (扬声器) */
#define PIT_CMD       0x43   /* 命令/模式寄存器 */

/* PIT 命令字节 */
#define PIT_BCD       0x01   /* BCD模式 */
#define PIT_MODE0     0x00   /* 模式0: 中断结束计数 */
#define PIT_MODE2     0x04   /* 模式2: 速率发生器 */
#define PIT_MODE3     0x06   /* 模式3: 方波发生器 */
#define PIT_RL_LO     0x10   /* 只读写低字节 */
#define PIT_RL_HI     0x20   /* 只读写高字节 */
#define PIT_RL_BOTH   0x30   /* 先低后高 */
#define PIT_SC0       0x00   /* 选择通道0 */

#define PIT_FREQUENCY 1193182  /* PIT 输入频率 Hz */
#define PIT_HZ        100      /* 目标时钟频率 100Hz (10ms一次) */

/* 初始化PIT，设置为100Hz */
void pit_init(void);

/* 获取系统启动以来的tick数 */
uint32_t pit_get_ticks(void);

/* 获取秒数 */
uint32_t pit_get_seconds(void);

/* 简单延时 (毫秒) */
void pit_sleep(uint32_t ms);

/* 时钟中断处理函数 */
void pit_handler(void);

#endif /* PIT_H */
