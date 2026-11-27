/* ============================================================================
 * 赫普OS - ACPI 电池读数（M2）
 * 详见 acpi_battery.c 顶部说明。
 * ========================================================================== */

#ifndef ACPI_BATTERY_H
#define ACPI_BATTERY_H

#include <stdint.h>

typedef struct {
    int present;
    int percent;
    int charging;
    int state_bits;
    int power_unit;     /* 0=mWh/0x80000000 表示 mAh 的 _BIF 约定；统一按原值回报 */
    int64_t remaining;
    int64_t full;
    int64_t design;
    int64_t voltage_mv;
    int used_bix;
} acpi_batt_sample_t;

/* 返回值：0 = 可信读数；1 = _STA 明确说没有电池；2 = 已声明但未读到/不可信。 */
int acpi_battery_poll(acpi_batt_sample_t *out);

/* 返回 0 可信，负值是拒绝原因；*pct 只在 0 时写入。 */
int acpi_battery_make_percent(int64_t rem, int64_t full, int state, int *pct);

int acpi_battery_init(void);
int acpi_battery_ready(void);
int acpi_battery_last_rc(void);
const char *acpi_battery_path(void);

#endif
