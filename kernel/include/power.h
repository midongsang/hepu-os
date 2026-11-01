#ifndef POWER_H
#define POWER_H

#include <stdint.h>

/* 电源状态 */
typedef enum {
    POWER_STATE_ON = 0,
    POWER_STATE_IDLE,
    POWER_STATE_STANDBY,
    POWER_STATE_SUSPEND,
    POWER_STATE_HIBERNATE,
    POWER_STATE_OFF
} power_state_t;

/* 唤醒源类型 */
#define WAKE_TYPE_KEY     0
#define WAKE_TYPE_TIMER   1
#define WAKE_TYPE_NETWORK 2
#define WAKE_TYPE_USB     3
#define WAKE_TYPE_OTHER   4

/* 电池信息 */
typedef struct {
    uint8_t  present;
    uint8_t  charging;
    uint8_t  percentage;
    uint16_t voltage_mv;
    uint16_t current_ma;
    uint32_t capacity_mah;
    uint32_t remaining_s;
} battery_info_t;

/* 初始化 */
void power_init(void);
/* V2.20: 真实关机/重启 */
void machine_reboot(void);
void machine_poweroff(void);

/* 电源状态 */
int           power_set_state(power_state_t state);
power_state_t power_get_state(void);

/* 唤醒源 */
int power_register_wake_source(const char *name, uint8_t type);
int power_enable_wake_source(int id, uint8_t enable);
int power_wake(int wake_source_id);

/* CPU调频 */
int      power_set_cpu_freq(uint32_t freq_mhz);
uint32_t power_get_cpu_freq(void);

/* 温度管理 */
int      power_set_temperature(uint32_t temp_c);
uint32_t power_get_temperature(void);

/* 电池 */
void power_set_battery(uint8_t percentage, uint8_t charging,
                       uint16_t voltage_mv, uint16_t current_ma);
void power_get_battery(battery_info_t *info);

/* 统计 */
void power_stats(uint32_t *suspend_count, uint32_t *wake_count,
                 uint32_t *temperature, uint8_t *throttling);

#endif
