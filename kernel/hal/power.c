/* ============================================================================
 * 赫普OS - 电源管理抽象 (V1.00新增)
 * 实现：电源状态管理、休眠/唤醒、CPU调频、电池管理、热管理
 * ========================================================================== */
#include <stdint.h>
#include <stddef.h>
#include "power.h"
#include "vga.h"
#include "string.h"
#include "acpi.h"

#define POWER_WAKE_SOURCES 8
#define POWER_MAGIC 0x50575231  /* "PWR1" */

/* 端口I/O */
static inline void pwr_outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t pwr_inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void pwr_outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t pwr_inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void pwr_io_wait(void) {
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
}

/* 唤醒源 */
typedef struct {
    uint8_t  used;
    uint8_t  enabled;
    uint8_t  type;           /* 0=按键, 1=定时器, 2=网络, 3=USB, 4=其他 */
    char     name[16];
    uint32_t trigger_count;
} wake_source_t;

/* 电源管理状态 */
typedef struct {
    uint32_t       magic;
    power_state_t  state;
    power_state_t  last_state;
    uint32_t       cpu_freq_mhz;
    uint32_t       cpu_freq_min;
    uint32_t       cpu_freq_max;
    uint8_t        thermal_throttle;
    uint32_t       temperature_c;
    uint32_t       suspend_count;
    uint32_t       wake_count;
    wake_source_t  wake_sources[POWER_WAKE_SOURCES];
    battery_info_t battery;
} power_manager_t;

static power_manager_t pm;

void power_init(void)
{
    memset(&pm, 0, sizeof(pm));
    pm.magic = POWER_MAGIC;
    pm.state = POWER_STATE_ON;
    pm.last_state = POWER_STATE_ON;
    pm.cpu_freq_mhz = 1000;    /* 默认1GHz */
    pm.cpu_freq_min = 300;
    pm.cpu_freq_max = 2000;
    pm.thermal_throttle = 0;
    pm.temperature_c = 35;     /* 默认35度 */
    pm.battery.present = 1;
    pm.battery.charging = 0;
    pm.battery.percentage = 100;
    pm.battery.voltage_mv = 3700;
    pm.battery.capacity_mah = 3000;

    vga_puts("[POWER] power subsystem init done \n");
    vga_printf("[POWER] CPU freq: %d MHz(%d-%d)\n",
               pm.cpu_freq_mhz, pm.cpu_freq_min, pm.cpu_freq_max);
}

/* 设置电源状态 */
int power_set_state(power_state_t state)
{
    if (pm.magic != POWER_MAGIC) return -1;
    if (state > POWER_STATE_OFF) return -1;

    pm.last_state = pm.state;
    pm.state = state;

    switch (state) {
        case POWER_STATE_ON:
            pm.cpu_freq_mhz = pm.cpu_freq_max;
            break;
        case POWER_STATE_IDLE:
            pm.cpu_freq_mhz = pm.cpu_freq_min;
            break;
        case POWER_STATE_STANDBY:
            pm.cpu_freq_mhz = pm.cpu_freq_min / 2;
            break;
        case POWER_STATE_SUSPEND:
            pm.suspend_count++;
            break;
        case POWER_STATE_HIBERNATE:
            pm.suspend_count++;
            break;
        case POWER_STATE_OFF:
            /* V2.20: 真正关机 */
            machine_poweroff();
            break;
    }

    vga_printf("[POWER] state switch: %d -> %d\n", pm.last_state, state);
    return 0;
}

/* 获取当前电源状态 */
power_state_t power_get_state(void)
{
    return pm.state;
}

/* 注册唤醒源 */
int power_register_wake_source(const char *name, uint8_t type)
{
    for (int i = 0; i < POWER_WAKE_SOURCES; i++) {
        if (!pm.wake_sources[i].used) {
            pm.wake_sources[i].used = 1;
            pm.wake_sources[i].enabled = 1;
            pm.wake_sources[i].type = type;
            if (name) strncpy(pm.wake_sources[i].name, name, 15);
            return i;
        }
    }
    return -1;
}

/* 使能/禁用唤醒源 */
int power_enable_wake_source(int id, uint8_t enable)
{
    if (id < 0 || id >= POWER_WAKE_SOURCES) return -1;
    if (!pm.wake_sources[id].used) return -1;
    pm.wake_sources[id].enabled = enable ? 1 : 0;
    return 0;
}

/* 触发唤醒 */
int power_wake(int wake_source_id)
{
    if (wake_source_id < 0 || wake_source_id >= POWER_WAKE_SOURCES) return -1;
    if (!pm.wake_sources[wake_source_id].used) return -1;
    if (!pm.wake_sources[wake_source_id].enabled) return -1;

    pm.wake_sources[wake_source_id].trigger_count++;
    pm.wake_count++;
    pm.state = POWER_STATE_ON;
    pm.cpu_freq_mhz = pm.cpu_freq_max;

    vga_printf("[POWER] woken by: %s(total wakeups:%d)\n",
               pm.wake_sources[wake_source_id].name, pm.wake_count);
    return 0;
}

/* CPU调频 */
int power_set_cpu_freq(uint32_t freq_mhz)
{
    if (freq_mhz < pm.cpu_freq_min || freq_mhz > pm.cpu_freq_max) return -1;
    pm.cpu_freq_mhz = freq_mhz;
    return 0;
}

uint32_t power_get_cpu_freq(void)
{
    return pm.cpu_freq_mhz;
}

/* 温度管理 */
int power_set_temperature(uint32_t temp_c)
{
    pm.temperature_c = temp_c;
    /* 超过85度触发降频 */
    if (temp_c > 85 && !pm.thermal_throttle) {
        pm.thermal_throttle = 1;
        pm.cpu_freq_mhz = pm.cpu_freq_min;
        vga_printf("[POWER] thermal throttle: %dC -> %d MHz\n", temp_c, pm.cpu_freq_mhz);
    } else if (temp_c < 70 && pm.thermal_throttle) {
        pm.thermal_throttle = 0;
        pm.cpu_freq_mhz = pm.cpu_freq_max;
        vga_printf("[POWER] temp recover: %dC -> %d MHz\n", temp_c, pm.cpu_freq_mhz);
    }
    return 0;
}

uint32_t power_get_temperature(void)
{
    return pm.temperature_c;
}

/* 电池管理 */
void power_set_battery(uint8_t percentage, uint8_t charging,
                       uint16_t voltage_mv, uint16_t current_ma)
{
    pm.battery.percentage = percentage > 100 ? 100 : percentage;
    pm.battery.charging = charging ? 1 : 0;
    pm.battery.voltage_mv = voltage_mv;
    pm.battery.current_ma = current_ma;
    /* 估算剩余时间：容量/电流 * 百分比 */
    if (current_ma > 0 && !charging) {
        pm.battery.remaining_s = (pm.battery.capacity_mah * percentage / 100) * 3600 / current_ma;
    } else {
        pm.battery.remaining_s = 0;
    }
}

void power_get_battery(battery_info_t *info)
{
    if (info) memcpy(info, &pm.battery, sizeof(battery_info_t));
}

/* 电源统计 */
void power_stats(uint32_t *suspend_count, uint32_t *wake_count,
                 uint32_t *temperature, uint8_t *throttling)
{
    if (suspend_count) *suspend_count = pm.suspend_count;
    if (wake_count) *wake_count = pm.wake_count;
    if (temperature) *temperature = pm.temperature_c;
    if (throttling) *throttling = pm.thermal_throttle;
}

/* ============================================================================
 * V2.20: 真实关机/重启底层
 * - 重启: 8042键盘控制器复位(Intel标准) + ICH/LPC 0xCF9 兜底
 * - 关机: QEMU 0x604 端口 + ACPI PM1a(S5) 多层尝试, 最后停机提示
 * ========================================================================== */
static void reboot_8042(void)
{
    /* 等待键盘控制器输入缓冲空 */
    for (int i = 0; i < 1000; i++) {
        if ((pwr_inb(0x64) & 0x02) == 0) break;
        pwr_io_wait();
    }
    /* CPU Reset 命令 */
    pwr_outb(0x64, 0xFE);
    pwr_io_wait();
}

static void reboot_cf9(void)
{
    /* ICH/LPC RST_CNT 复位控制寄存器 */
    pwr_outb(0xCF9, 0x06);
    pwr_io_wait();
}

void machine_reboot(void)
{
    vga_puts("[POWER] rebooting system ...\n");
    __asm__ volatile ("cli");
    reboot_8042();
    reboot_cf9();
    /* 兜底: 若以上都无效, 无限停机 */
    for (;;) __asm__ volatile ("hlt");
}

void machine_poweroff(void)
{
    vga_puts("[POWER] powering off ...\n");
    __asm__ volatile ("cli");
    /* 1. QEMU 关机按钮端口 */
    pwr_outw(0x604, 0x2000);
    pwr_io_wait();
    /* 2. ACPI PM1a/PM1b 控制寄存器写 S5 (SLP_EN | SLP_TYP)
     * V2.21: 优先解析 DSDT _S5 包获取精确 SLP_TYP, 不再硬编码 5 */
    uint32_t pm1a = 0;
    if (acpi_fadt_get(ACPI_FADT_PM1A_CNT, &pm1a) == 0 && pm1a) {
        uint8_t s5a = 0x05, s5b = 0x05;   /* 默认 SLP_TYP */
        if (acpi_s5_slp_typ(&s5a, &s5b) == 0) {
            vga_printf("[POWER] ACPI _S5 SLP_TYP: PM1a=%d, PM1b=%d\n", s5a, s5b);
        }
        uint16_t v = pwr_inw((uint16_t)pm1a);
        uint16_t slp = (uint16_t)((v & ~0x1C00u) | ((s5a & 0x07u) << 10) | 0x2000u);
        pwr_outw((uint16_t)pm1a, slp);
        pwr_io_wait();
        /* PM1b (若存在) */
        uint32_t pm1b = 0;
        if (acpi_fadt_get(ACPI_FADT_PM1B_CNT, &pm1b) == 0 && pm1b) {
            uint16_t v2 = pwr_inw((uint16_t)pm1b);
            uint16_t slp2 = (uint16_t)((v2 & ~0x1C00u) | ((s5b & 0x07u) << 10) | 0x2000u);
            pwr_outw((uint16_t)pm1b, slp2);
            pwr_io_wait();
        }
    }
    /* 3. 兜底: 若未能关机(真机SLP_TYP可能不同), 停机并提示 */
    vga_puts("[POWER] if system still on, please power off manually\n");
    for (;;) __asm__ volatile ("hlt");
}
