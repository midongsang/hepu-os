/* ============================================================================
 * 赫普OS - 64位RTC驱动（简化版）
 * V0.10.6: 初始实现（实时时钟读取）
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>

/* RTC端口 */
#define RTC_ADDRESS_PORT 0x70
#define RTC_DATA_PORT    0x71

/* RTC寄存器 */
#define RTC_REG_SECONDS  0x00
#define RTC_REG_MINUTES  0x02
#define RTC_REG_HOURS    0x04
#define RTC_REG_WEEKDAY  0x06
#define RTC_REG_DAY      0x07
#define RTC_REG_MONTH    0x08
#define RTC_REG_YEAR     0x09
#define RTC_REG_CENTURY  0x32
#define RTC_REG_STATUS_A 0x0A
#define RTC_REG_STATUS_B 0x0B
#define RTC_REG_STATUS_C 0x0C

/* 时间结构 */
typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t weekday;
} rtc_time_t;

/* 外部函数 */
extern void vga_puts(const char *str);
extern void vga_put_hex(uint64_t val);

/* 端口读写 */
static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* BCD转二进制 */
static uint8_t bcd_to_bin(uint8_t bcd)
{
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

/* 读取RTC寄存器 */
static uint8_t rtc_read_register(uint8_t reg)
{
    outb(RTC_ADDRESS_PORT, reg);
    return inb(RTC_DATA_PORT);
}

/* 等待RTC更新完成 */
static void rtc_wait_update(void)
{
    /* 等待更新标志清除 */
    while (rtc_read_register(RTC_REG_STATUS_A) & 0x80) {}
}

/* 读取当前时间 */
void rtc64_get_time(rtc_time_t *time)
{
    rtc_wait_update();

    time->second = bcd_to_bin(rtc_read_register(RTC_REG_SECONDS));
    time->minute = bcd_to_bin(rtc_read_register(RTC_REG_MINUTES));
    time->hour = bcd_to_bin(rtc_read_register(RTC_REG_HOURS));
    time->weekday = bcd_to_bin(rtc_read_register(RTC_REG_WEEKDAY));
    time->day = bcd_to_bin(rtc_read_register(RTC_REG_DAY));
    time->month = bcd_to_bin(rtc_read_register(RTC_REG_MONTH));
    time->year = bcd_to_bin(rtc_read_register(RTC_REG_YEAR));

    /* 读取世纪（如果支持） */
    uint8_t century = bcd_to_bin(rtc_read_register(RTC_REG_CENTURY));
    if (century != 0) {
        time->year += century * 100;
    } else {
        time->year += 2000;  /* 默认21世纪 */
    }
}

/* 便捷函数：获取小时 */
uint8_t rtc64_get_hours(void)
{
    rtc_time_t t;
    rtc64_get_time(&t);
    return t.hour;
}

/* 便捷函数：获取分钟 */
uint8_t rtc64_get_minutes(void)
{
    rtc_time_t t;
    rtc64_get_time(&t);
    return t.minute;
}

/* 便捷函数：获取秒 */
uint8_t rtc64_get_seconds(void)
{
    rtc_time_t t;
    rtc64_get_time(&t);
    return t.second;
}

/* 初始化RTC */
void rtc64_init(void)
{
    vga_puts("[RTC64] Initializing Real Time Clock...\n");

    /* 读取状态寄存器B */
    uint8_t status_b = rtc_read_register(RTC_REG_STATUS_B);

    /* 启用周期性中断（如果需要） */
    /* status_b |= 0x40; */
    /* outb(RTC_ADDRESS_PORT, RTC_REG_STATUS_B); */
    /* outb(RTC_DATA_PORT, status_b); */

    vga_puts("[RTC64] RTC initialized\n");

    /* 显示当前时间 */
    rtc_time_t time;
    rtc64_get_time(&time);

    vga_puts("[RTC64] Current time: ");
    vga_put_hex(time.year);
    vga_puts("-");
    vga_put_hex(time.month);
    vga_puts("-");
    vga_put_hex(time.day);
    vga_puts(" ");
    vga_put_hex(time.hour);
    vga_puts(":");
    vga_put_hex(time.minute);
    vga_puts(":");
    vga_put_hex(time.second);
    vga_puts("\n");
}

/* 获取Unix时间戳（简化版） */
uint64_t rtc64_get_timestamp(void)
{
    rtc_time_t time;
    rtc64_get_time(&time);

    /* 简化计算：从2000年1月1日开始的秒数 */
    uint64_t days = 0;

    /* 年份天数 */
    for (uint16_t y = 2000; y < time.year; y++) {
        if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) {
            days += 366;
        } else {
            days += 365;
        }
    }

    /* 月份天数 */
    uint8_t month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((time.year % 4 == 0 && time.year % 100 != 0) || (time.year % 400 == 0)) {
        month_days[1] = 29;
    }

    for (uint8_t m = 0; m < time.month - 1; m++) {
        days += month_days[m];
    }

    /* 日期 */
    days += time.day - 1;

    /* 转换为秒 */
    uint64_t seconds = days * 86400 + time.hour * 3600 + time.minute * 60 + time.second;

    /* 加上2000年1月1日的Unix时间戳（946684800） */
    return seconds + 946684800ULL;
}
