/* ============================================================================
 * 赫普OS - RTC 实时时钟驱动实现 (V2.23新增)
 * CMOS 0x70/0x71 读取, 支持 BCD/二进制、12/24小时自动转换.
 * 参考: MC146818A datasheet / OSDev wiki "CMOS" (真实标准)
 * ========================================================================== */
#include <stdint.h>
#include "rtc.h"
#include "vga.h"

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* 读 CMOS 寄存器: bit7=1 禁 NMI 防止时钟更新期间读到撕裂值 */
uint8_t rtc_read_cmos(uint8_t reg)
{
    outb(RTC_INDEX_PORT, (uint8_t)(reg | 0x80));  /* NMI disable */
    return inb(RTC_DATA_PORT);
}

/* BCD → 二进制 */
static uint8_t bcd_to_bin(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F));
}

void rtc_read_time(rtc_time_t *t)
{
    uint8_t status_b = rtc_read_cmos(RTC_STATUS_B);
    int binary = (status_b & RTC_B_BINARY) != 0;
    int hour24 = (status_b & RTC_B_24H) != 0;

    uint8_t sec, min, hour, day, month, year, weekday, century = 0;

    /* 等待 UIP 清除, 避免读到更新时间中的撕裂值 */
    int spin = 0;
    while ((rtc_read_cmos(RTC_STATUS_A) & RTC_UIP) && spin < 100000) {
        spin++;
    }

    sec    = rtc_read_cmos(RTC_SECONDS);
    min    = rtc_read_cmos(RTC_MINUTES);
    hour   = rtc_read_cmos(RTC_HOURS);
    day    = rtc_read_cmos(RTC_DAY);
    month  = rtc_read_cmos(RTC_MONTH);
    year   = rtc_read_cmos(RTC_YEAR);
    weekday = rtc_read_cmos(RTC_WEEKDAY);
    century = rtc_read_cmos(RTC_CENTURY);

    if (!binary) {
        sec    = bcd_to_bin(sec);
        min    = bcd_to_bin(min);
        hour   = bcd_to_bin(hour);
        day    = bcd_to_bin(day);
        month  = bcd_to_bin(month);
        year   = bcd_to_bin(year);
        weekday = bcd_to_bin(weekday);
        if (century != 0) century = bcd_to_bin(century);
    }

    /* 12小时制 → 24小时制 */
    if (!hour24 && (hour & 0x80)) {      /* bit7 = PM 标志 */
        hour = (uint8_t)((hour & 0x7F) + 12);
    }

    /* 年份: 若主板提供世纪寄存器用世纪, 否则按 2000 基准 */
    uint16_t y;
    if (century != 0) {
        y = (uint16_t)(century * 100 + year);
    } else if (year < 80) {
        y = (uint16_t)(2000 + year);
    } else {
        y = (uint16_t)(1900 + year);
    }

    if (t) {
        t->year = y;
        t->month = month;
        t->day = day;
        t->hour = hour;
        t->minute = min;
        t->second = sec;
        t->weekday = weekday;
    }
}

void rtc_init(void)
{
    rtc_time_t t;
    rtc_read_time(&t);
    vga_printf("[RTC] CMOS clock: %d-%d-%d %d:%d:%d weekday=%d\n",
               t.year, t.month, t.day, t.hour, t.minute, t.second, t.weekday);
    /* V4.06-E51: 注册 IRQ8 (RTC 周期中断) 顶半部 */
    extern void irq_register_handler(uint8_t, void (*)(void));
    irq_register_handler(8, rtc_irq_handler);
    serial_puts(0x3F8, "[RTC] irq8 handler registered\n");
}

/* V2.87: 简化接口 (mobile 时钟应用) */
int rtc_get_time(int *h, int *m, int *s, int *d, int *mo, int *y)
{
    rtc_time_t t;
    rtc_read_time(&t);
    if (t.year == 0 && t.month == 0) return 1;   /* 读取异常判据 */
    *h = t.hour; *m = t.minute; *s = t.second;
    *d = t.day; *mo = t.month; *y = t.year;
    return 0;
}

/* V2.91: 写 CMOS 寄存器 (禁 NMI) */
static void rtc_write_cmos(uint8_t reg, uint8_t val)
{
    outb(RTC_INDEX_PORT, (uint8_t)(reg | 0x80));   /* NMI disable */
    outb(RTC_DATA_PORT, val);
}

/* V2.91: 二进制 -> BCD */
static uint8_t bin_to_bcd(uint8_t v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

/* V2.91: 写入 RTC (SET 标志暂停更新 -> 写寄存器 -> 恢复) */
void rtc_set_time(int h, int m, int s, int d, int mo, int y)
{
    int spin = 0;
    while ((rtc_read_cmos(RTC_STATUS_A) & RTC_UIP) && spin < 100000) spin++;
    uint8_t sb = rtc_read_cmos(RTC_STATUS_B);
    rtc_write_cmos(RTC_STATUS_B, (uint8_t)(sb | 0x80));   /* SET 暂停 */
    rtc_write_cmos(RTC_SECONDS, bin_to_bcd((uint8_t)(s & 0x7F)));
    rtc_write_cmos(RTC_MINUTES, bin_to_bcd((uint8_t)(m & 0x7F)));
    rtc_write_cmos(RTC_HOURS,   bin_to_bcd((uint8_t)(h & 0x3F)));
    rtc_write_cmos(RTC_DAY,     bin_to_bcd((uint8_t)(d & 0x3F)));
    rtc_write_cmos(RTC_MONTH,   bin_to_bcd((uint8_t)(mo & 0x1F)));
    rtc_write_cmos(RTC_YEAR,    bin_to_bcd((uint8_t)((y % 100) & 0x7F)));
    rtc_write_cmos(RTC_CENTURY, bin_to_bcd((uint8_t)((y / 100) & 0x3F)));
    rtc_write_cmos(RTC_STATUS_B, (uint8_t)(sb & 0x7F));   /* 恢复更新 */
}

/* ============================================================================
 * V4.06-E51: RTC 闹钟唤醒 (1Hz 周期中断 + 软件倒计时)
 * 标准 MC146818: 状态A rate select (bit3-0=0x0F -> 1Hz),
 * 状态B PIE(bit6) 使能周期中断, 状态C 读即清中断.
 * 顶半部只清中断+倒计时; 到点置 pending, 由 softirq 底半部调 pm_wakeup.
 * ========================================================================== */

/* ==========================================================================
 * V4.13-E58: 多组闹钟槽位 (最多 RTC_ALARM_MAX 个, 三种类型可并存)
 *   type: 0=off 1=oneshot(剩余秒) 2=daily(hh:mm) 3=weekly(dow hh:mm)
 *   p1/p2/p3: 见 type 说明; last_mark: weekly 防同分钟重复
 * ========================================================================== */
#define RTC_ALARM_MAX 8

typedef struct {
    int type;
    int p1;
    int p2;
    int p3;
    int last_mark;
} rtc_alarm_slot_t;

static rtc_alarm_slot_t rtc_alarms[RTC_ALARM_MAX];
static volatile int rtc_alarm_fired_flag = 0;
static volatile int rtc_wake_pending = 0;
static volatile int rtc_last_fired_type = 0;   /* V4.14-E59: 最近触发类型 (snooze 用) */

static int rtc_alarm_any_active(void)
{
    for (int i = 0; i < RTC_ALARM_MAX; i++)
        if (rtc_alarms[i].type != 0) return 1;
    return 0;
}

/* 读状态C 清中断标志 (必须读, 否则 IRQ8 持续触发) */
static void rtc_clear_irq(void)
{
    (void)rtc_read_cmos(0x0C);
}

/* 设置周期中断频率: rate 0-15, 0x0F = 1Hz (标准 MC146818) */
static void rtc_set_periodic_rate(uint8_t rate)
{
    uint8_t a = rtc_read_cmos(RTC_STATUS_A);
    a = (uint8_t)((a & 0xF0) | (rate & 0x0F));
    rtc_write_cmos(RTC_STATUS_A, a);
}

/* 使能/关闭周期中断 */
static void rtc_periodic_enable(int on)
{
    uint8_t b = rtc_read_cmos(RTC_STATUS_B);
    if (on) b = (uint8_t)(b | RTC_B_PIE);
    else    b = (uint8_t)(b & ~RTC_B_PIE);
    rtc_write_cmos(RTC_STATUS_B, b);
}

/* 新增闹钟槽位 (type 1/2/3); 槽满返回 -1 */
int rtc_alarm_add(int type, int p1, int p2, int p3)
{
    if (type < 1 || type > 3) return -1;
    for (int i = 0; i < RTC_ALARM_MAX; i++) {
        if (rtc_alarms[i].type == 0) {
            rtc_alarms[i].type = type;
            rtc_alarms[i].p1 = p1;
            rtc_alarms[i].p2 = p2;
            rtc_alarms[i].p3 = p3;
            rtc_alarms[i].last_mark = -1;
            rtc_alarm_fired_flag = 0;
            rtc_wake_pending = 0;
            rtc_clear_irq();
            rtc_set_periodic_rate(0x0F);
            rtc_periodic_enable(1);
            serial_puts(0x3F8, "[RTC] slot ");
            serial_puthex(0x3F8, (uint32_t)i);
            serial_puts(0x3F8, " added (type=");
            serial_puthex(0x3F8, (uint32_t)type);
            serial_puts(0x3F8, ")\n");
            return i;
        }
    }
    serial_puts(0x3F8, "[RTC] alarm slots full!\n");
    return -1;
}

/* 删除槽位; 全部清空则关周期中断 */
void rtc_alarm_del(int idx)
{
    if (idx < 0 || idx >= RTC_ALARM_MAX) return;
    if (rtc_alarms[idx].type == 0) return;
    rtc_alarms[idx].type = 0;
    serial_puts(0x3F8, "[RTC] slot ");
    serial_puthex(0x3F8, (uint32_t)idx);
    serial_puts(0x3F8, " removed\n");
    if (!rtc_alarm_any_active()) rtc_periodic_enable(0);
}

/* 清空全部闹钟 */
void rtc_alarm_clear_all(void)
{
    for (int i = 0; i < RTC_ALARM_MAX; i++) rtc_alarms[i].type = 0;
    rtc_periodic_enable(0);
    rtc_wake_pending = 0;
    serial_puts(0x3F8, "[RTC] all alarms cleared\n");
}

/* 读取槽位信息 (shell list 用); 返回 1=有 0=无 */
int rtc_alarm_get(int idx, int *type, int *p1, int *p2, int *p3)
{
    if (idx < 0 || idx >= RTC_ALARM_MAX) return 0;
    if (rtc_alarms[idx].type == 0) return 0;
    *type = rtc_alarms[idx].type;
    *p1 = rtc_alarms[idx].p1;
    *p2 = rtc_alarms[idx].p2;
    *p3 = rtc_alarms[idx].p3;
    return 1;
}

/* 兼容旧 API: 一次性闹钟 (并入槽位) */
void rtc_alarm_sec(uint32_t sec)
{
    if (sec == 0) { rtc_alarm_clear_all(); return; }
    rtc_alarm_add(1, (int)sec, 0, 0);
}

void rtc_alarm_off(void) { rtc_alarm_clear_all(); }

int rtc_alarm_fired(void) { return rtc_alarm_fired_flag; }

/* IRQ8 顶半部: 遍历全部槽位 (时间匹配类共享一次时间读取) */
void rtc_irq_handler(void)
{
    rtc_clear_irq();
    if (!rtc_alarm_any_active()) return;
    int need_time = 0;
    for (int i = 0; i < RTC_ALARM_MAX; i++) {
        if (rtc_alarms[i].type == 2 || rtc_alarms[i].type == 3) { need_time = 1; break; }
    }
    rtc_time_t t;
    int have_t = 0;
    if (need_time) { rtc_read_time(&t); have_t = 1; }
    int fired_any = 0;
    for (int i = 0; i < RTC_ALARM_MAX; i++) {
        rtc_alarm_slot_t *a = &rtc_alarms[i];
        if (a->type == 1) {
            /* 一次性倒计时 */
            if (a->p1 > 0 && --a->p1 == 0) {
                a->type = 0;
                serial_puts(0x3F8, "[RTC] alarm fired!\n");
                fired_any = 1;
            }
        } else if (a->type == 2 && have_t) {
            /* 每日: hh:mm 匹配, 触发后移除 */
            if (t.hour == a->p1 && t.minute == a->p2) {
                a->type = 0;
                serial_puts(0x3F8, "[RTC] daily alarm fired!\n");
                fired_any = 1;
            }
        } else if (a->type == 3 && have_t) {
            /* 周几: dow+hh:mm 匹配, 保留每周重复 */
            int dow = (t.weekday + 6) % 7;   /* 1=周日..7=周六 -> 0=周日..6=周六 */
            if (dow == a->p1 && t.hour == a->p3 && t.minute == a->p2) {
                int mark = dow * 1440 + t.hour * 60 + t.minute;
                if (mark != a->last_mark) {
                    a->last_mark = mark;
                    serial_puts(0x3F8, "[RTC] weekly alarm fired!\n");
                    fired_any = 1;
                }
            }
        }
    }
    if (fired_any) {
        rtc_alarm_fired_flag = 1;
        rtc_wake_pending = 1;
        if (!rtc_alarm_any_active()) rtc_periodic_enable(0);
        /* V4.14-E59: 记录本轮有闹钟触发 (snooze 用) */
        rtc_last_fired_type = 1;
    }
}

/* softirq 轮询: 返回并消费 pending */
int rtc_consume_wake_pending(void)
{
    if (rtc_wake_pending) {
        rtc_wake_pending = 0;
        return 1;
    }
    return 0;
}

/* 兼容旧 API: 每日闹钟 (并入槽位) */
void rtc_alarm_daily(int hour, int minute)
{
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        for (int i = 0; i < RTC_ALARM_MAX; i++)
            if (rtc_alarms[i].type == 2) rtc_alarm_del(i);
        if (!rtc_alarm_any_active()) rtc_periodic_enable(0);
        serial_puts(0x3F8, "[RTC] daily alarm off\n");
        return;
    }
    rtc_alarm_add(2, hour, minute, 0);
}

/* 兼容旧 API: 周几闹钟 (并入槽位) */
void rtc_alarm_weekly(int dow, int hour, int minute)
{
    if (dow < 0 || dow > 6 || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        for (int i = 0; i < RTC_ALARM_MAX; i++)
            if (rtc_alarms[i].type == 3) rtc_alarm_del(i);
        if (!rtc_alarm_any_active()) rtc_periodic_enable(0);
        serial_puts(0x3F8, "[RTC] weekly alarm off\n");
        return;
    }
    rtc_alarm_add(3, dow, minute, hour);
}


/* V4.14-E59: 稍后提醒——追加一个 minutes 分钟后的一次性闹钟.
 * 若之前有闹钟触发过 (rtc_last_fired_type != 0), 则 snooze 生效. */
int rtc_alarm_snooze(int minutes)
{
    serial_puts(0x3F8, "[RTC] snooze enter min=");
    serial_puthex(0x3F8, (uint32_t)minutes);
    serial_puts(0x3F8, " last=");
    serial_puthex(0x3F8, (uint32_t)rtc_last_fired_type);
    serial_puts(0x3F8, "\n");
    if (minutes < 1 || minutes > 60) return -1;
    if (rtc_last_fired_type == 0) return 0;   /* 无已触发闹钟 */
    int sec = minutes * 60;
    int slot = rtc_alarm_add(1, sec, 0, 0);
    if (slot >= 0) {
        serial_puts(0x3F8, "[RTC] snooze +");
        serial_puthex(0x3F8, (uint32_t)minutes);
        serial_puts(0x3F8, " min (oneshot ");
        serial_puthex(0x3F8, (uint32_t)sec);
        serial_puts(0x3F8, "s)\n");
    }
    return (slot >= 0) ? 1 : -2;
}
