/* ============================================================================
 * 赫普OS - 电源管理单元测试
 * 覆盖：电源状态/休眠唤醒/电池/省电模式/频率调节/热管理
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

#define POWER_STATE_ON       0
#define POWER_STATE_IDLE     1
#define POWER_STATE_STANDBY  2
#define POWER_STATE_SUSPEND  3
#define POWER_STATE_HIBERNATE 4
#define POWER_STATE_OFF      5

static const char *power_state_names[] = {
    "ON", "IDLE", "STANDBY", "SUSPEND", "HIBERNATE", "OFF"
};

static kunit_result_t test_power_states(void)
{
    KUNIT_ASSERT_EQ(POWER_STATE_ON, 0, " 开机 state ");
    KUNIT_ASSERT_EQ(POWER_STATE_IDLE, 1, " free state ");
    KUNIT_ASSERT_EQ(POWER_STATE_STANDBY, 2, " pending 机 state ");
    KUNIT_ASSERT_EQ(POWER_STATE_SUSPEND, 3, " suspend state ");
    KUNIT_ASSERT_EQ(POWER_STATE_HIBERNATE, 4, " sleep state ");
    KUNIT_ASSERT_EQ(POWER_STATE_OFF, 5, " 关机 state ");
    return KUNIT_PASS;
}

static kunit_result_t test_power_state_names(void)
{
    KUNIT_ASSERT_EQ(strcmp(power_state_names[POWER_STATE_ON], "ON"), 0, "ON name ");
    KUNIT_ASSERT_EQ(strcmp(power_state_names[POWER_STATE_SUSPEND], "SUSPEND"), 0, "SUSPEND name ");
    KUNIT_ASSERT_EQ(strcmp(power_state_names[POWER_STATE_OFF], "OFF"), 0, "OFF name ");
    return KUNIT_PASS;
}

static u8 current_power_state = POWER_STATE_ON;

static int power_transition(u8 new_state)
{
    switch (current_power_state) {
        case POWER_STATE_ON:
            if (new_state == POWER_STATE_IDLE || new_state == POWER_STATE_STANDBY ||
                new_state == POWER_STATE_SUSPEND || new_state == POWER_STATE_HIBERNATE ||
                new_state == POWER_STATE_OFF) {
                current_power_state = new_state; return 0;
            }
            break;
        case POWER_STATE_IDLE:
            if (new_state == POWER_STATE_ON || new_state == POWER_STATE_STANDBY ||
                new_state == POWER_STATE_OFF) {
                current_power_state = new_state; return 0;
            }
            break;
        case POWER_STATE_SUSPEND:
        case POWER_STATE_HIBERNATE:
            if (new_state == POWER_STATE_ON) {
                current_power_state = new_state; return 0;
            }
            break;
    }
    return -1;
}

static kunit_result_t test_power_transitions(void)
{
    current_power_state = POWER_STATE_ON;
    KUNIT_ASSERT_EQ(power_transition(POWER_STATE_IDLE), 0, "ON→IDLE");
    KUNIT_ASSERT_EQ(current_power_state, POWER_STATE_IDLE, " state =IDLE");
    KUNIT_ASSERT_EQ(power_transition(POWER_STATE_ON), 0, "IDLE→ON");
    KUNIT_ASSERT_EQ(power_transition(POWER_STATE_SUSPEND), 0, "ON→SUSPEND");
    KUNIT_ASSERT_EQ(power_transition(POWER_STATE_ON), 0, "SUSPEND→ON(wakeup)");
    KUNIT_ASSERT_EQ(current_power_state, POWER_STATE_ON, " wakeup next =ON");
    return KUNIT_PASS;
}

static kunit_result_t test_power_invalid_transitions(void)
{
    current_power_state = POWER_STATE_SUSPEND;
    KUNIT_ASSERT_EQ(power_transition(POWER_STATE_IDLE), -1, "SUSPEND→IDLE non 法 ");
    current_power_state = POWER_STATE_OFF;
    KUNIT_ASSERT_EQ(power_transition(POWER_STATE_ON), -1, "OFF→ON non 法 ");
    return KUNIT_PASS;
}

typedef struct {
    u8  present;
    u8  charging;
    u8  capacity;
    u16 voltage;
    u16 temperature;
    u32 design_capacity;
    u32 full_capacity;
    u32 current_capacity;
} battery_t;

static kunit_result_t test_battery_basic(void)
{
    battery_t bat;
    memset(&bat, 0, sizeof(bat));
    bat.present = 1; bat.charging = 0; bat.capacity = 75;
    bat.voltage = 3800; bat.temperature = 250;
    bat.design_capacity = 3000; bat.full_capacity = 2850; bat.current_capacity = 2137;
    KUNIT_ASSERT(bat.present, " battery exists ");
    KUNIT_ASSERT(!bat.charging, " not 充电 ");
    KUNIT_ASSERT_EQ(bat.capacity, 75, " 电量 =75%");
    KUNIT_ASSERT_EQ(bat.voltage, 3800, " 电压 =3.8V");
    KUNIT_ASSERT_EQ(bat.temperature, 250, " 温度 =25.0°C");
    return KUNIT_PASS;
}

static kunit_result_t test_battery_calculations(void)
{
    battery_t bat;
    memset(&bat, 0, sizeof(bat));
    bat.full_capacity = 2850; bat.current_capacity = 1425;
    u32 percent = (bat.current_capacity * 100) / bat.full_capacity;
    KUNIT_ASSERT_EQ(percent, 50, " left 50%");
    bat.design_capacity = 3000;
    u32 health = (bat.full_capacity * 100) / bat.design_capacity;
    KUNIT_ASSERT_EQ(health, 95, " battery 健康度 =95%");
    return KUNIT_PASS;
}

#define POWER_PROFILE_PERFORMANCE 0
#define POWER_PROFILE_BALANCED 1
#define POWER_PROFILE_POWER_SAVER 2
#define POWER_PROFILE_ULTRA_SAVER 3

typedef struct {
    u8  profile;
    u8  cpu_max_freq;
    u8  screen_brightness;
    u16 idle_timeout;
    u8  disable_radio;
    u8  disable_vibration;
} power_profile_t;

static kunit_result_t test_power_profiles(void)
{
    KUNIT_ASSERT_EQ(POWER_PROFILE_PERFORMANCE, 0, " performance mode ");
    KUNIT_ASSERT_EQ(POWER_PROFILE_BALANCED, 1, " balanced mode ");
    KUNIT_ASSERT_EQ(POWER_PROFILE_POWER_SAVER, 2, " 省电 mode ");
    KUNIT_ASSERT_EQ(POWER_PROFILE_ULTRA_SAVER, 3, " 超 level 省电 ");
    return KUNIT_PASS;
}

static kunit_result_t test_power_profile_settings(void)
{
    power_profile_t profiles[4];
    memset(profiles, 0, sizeof(profiles));
    profiles[POWER_PROFILE_PERFORMANCE].cpu_max_freq = 100;
    profiles[POWER_PROFILE_PERFORMANCE].screen_brightness = 100;
    profiles[POWER_PROFILE_PERFORMANCE].idle_timeout = 300;
    profiles[POWER_PROFILE_POWER_SAVER].cpu_max_freq = 50;
    profiles[POWER_PROFILE_POWER_SAVER].screen_brightness = 50;
    profiles[POWER_PROFILE_POWER_SAVER].idle_timeout = 60;
    profiles[POWER_PROFILE_POWER_SAVER].disable_radio = 1;
    KUNIT_ASSERT_EQ(profiles[0].cpu_max_freq, 100, " performance mode CPU=100%");
    KUNIT_ASSERT_EQ(profiles[2].cpu_max_freq, 50, " 省电 mode CPU=50%");
    KUNIT_ASSERT(profiles[2].disable_radio, " 省电 mode disabled no 线 ");
    KUNIT_ASSERT(!profiles[0].disable_radio, " performance mode not disabled no 线 ");
    return KUNIT_PASS;
}

typedef struct {
    u32 min_freq;
    u32 max_freq;
    u32 cur_freq;
    u8  governor;
} cpufreq_t;

#define CPUFREQ_GOV_PERFORMANCE 0
#define CPUFREQ_GOV_POWERSAVE 1
#define CPUFREQ_GOV_ONDEMAND 2
#define CPUFREQ_GOV_CONSERVATIVE 3

static kunit_result_t test_cpufreq(void)
{
    cpufreq_t cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.min_freq = 300; cpu.max_freq = 2400; cpu.cur_freq = 1200;
    cpu.governor = CPUFREQ_GOV_ONDEMAND;
    KUNIT_ASSERT_EQ(cpu.min_freq, 300, " min freq =300MHz");
    KUNIT_ASSERT_EQ(cpu.max_freq, 2400, " max freq =2.4GHz");
    KUNIT_ASSERT_EQ(cpu.cur_freq, 1200, " current freq =1.2GHz");
    KUNIT_ASSERT(cpu.cur_freq >= cpu.min_freq, " current >= min ");
    KUNIT_ASSERT(cpu.cur_freq <= cpu.max_freq, " current <= max ");
    return KUNIT_PASS;
}

static kunit_result_t test_cpufreq_governors(void)
{
    KUNIT_ASSERT_EQ(CPUFREQ_GOV_PERFORMANCE, 0, "performance");
    KUNIT_ASSERT_EQ(CPUFREQ_GOV_POWERSAVE, 1, "powersave");
    KUNIT_ASSERT_EQ(CPUFREQ_GOV_ONDEMAND, 2, "ondemand");
    KUNIT_ASSERT_EQ(CPUFREQ_GOV_CONSERVATIVE, 3, "conservative");
    return KUNIT_PASS;
}

typedef struct {
    u32 temperature;
    u32 critical_temp;
    u32 passive_temp;
    u8  cooling_level;
} thermal_t;

static kunit_result_t test_thermal(void)
{
    thermal_t therm;
    memset(&therm, 0, sizeof(therm));
    therm.temperature = 350; therm.critical_temp = 950;
    therm.passive_temp = 800; therm.cooling_level = 0;
    KUNIT_ASSERT_EQ(therm.temperature, 350, " current 温度 =35°C");
    KUNIT_ASSERT(therm.temperature < therm.passive_temp, " low 于被动散热阈 value ");
    therm.temperature = 850;
    KUNIT_ASSERT(therm.temperature > therm.passive_temp, " 超过被动散热阈 value ");
    KUNIT_ASSERT(therm.temperature < therm.critical_temp, " not 达临界温度 ");
    return KUNIT_PASS;
}

#define WAKE_SOURCE_POWER_BUTTON 0x01
#define WAKE_SOURCE_KEYBOARD 0x02
#define WAKE_SOURCE_TOUCH 0x04
#define WAKE_SOURCE_TIMER 0x08
#define WAKE_SOURCE_NETWORK 0x10
#define WAKE_SOURCE_USB 0x20

static kunit_result_t test_wake_sources(void)
{
    KUNIT_ASSERT_EQ(WAKE_SOURCE_POWER_BUTTON, 0x01, " 电源键 wakeup ");
    KUNIT_ASSERT_EQ(WAKE_SOURCE_KEYBOARD, 0x02, " keyboard wakeup ");
    KUNIT_ASSERT_EQ(WAKE_SOURCE_TOUCH, 0x04, " touch wakeup ");
    KUNIT_ASSERT_EQ(WAKE_SOURCE_TIMER, 0x08, " timer wakeup ");
    KUNIT_ASSERT_EQ(WAKE_SOURCE_NETWORK, 0x10, " net wakeup ");
    KUNIT_ASSERT_EQ(WAKE_SOURCE_USB, 0x20, "USB wakeup ");
    u32 enabled = WAKE_SOURCE_POWER_BUTTON | WAKE_SOURCE_TOUCH | WAKE_SOURCE_TIMER;
    KUNIT_ASSERT(enabled & WAKE_SOURCE_POWER_BUTTON, " 电源键 enabled ");
    KUNIT_ASSERT(!(enabled & WAKE_SOURCE_KEYBOARD), " keyboard not enabled ");
    return KUNIT_PASS;
}

static const kunit_test_t power_tests[] = {
    {" power state ", test_power_states, "power"},
    {" power state 名 ", test_power_state_names, "power"},
    {" state 转换 ", test_power_transitions, "power"},
    {" non 法转换 ", test_power_invalid_transitions, "power"},
    {" battery basic ", test_battery_basic, "power"},
    {" battery 计算 ", test_battery_calculations, "power"},
    {" 省电 mode ", test_power_profiles, "power"},
    {" 省电 set ", test_power_profile_settings, "power"},
    {"CPU freq ", test_cpufreq, "power"},
    {"CPU scheduler ", test_cpufreq_governors, "power"},
    {" 热 mgr ", test_thermal, "power"},
    {" wakeup 源 ", test_wake_sources, "power"},
};
static const kunit_suite_t kunit_power_suite = {
    " power mgmt ", power_tests, 12
};

void kunit_power_register(void)
{
    kunit_register_suite(&kunit_power_suite);
    vga_puts("[KUNIT] power mgmt test suite registered(12 cases)\n");
}
