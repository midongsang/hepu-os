/* ============================================================================
 * 赫普OS - RTC 实时钟驱动接口
 * V2.23: 基础读取 (rtc_read_time) + CMOS 宏.
 * V2.87: 追加 rtc_get_time 简化接口 (mobile 时钟应用用).
 * 标准 MC146818: 端口 0x70/0x71, 时间寄存器 BCD 默认.
 * ========================================================================== */
#ifndef HEPU_RTC_H
#define HEPU_RTC_H

#include <stdint.h>

/* --- CMOS 端口 --- */
#define RTC_INDEX_PORT  0x70
#define RTC_DATA_PORT   0x71

/* --- 时间寄存器 --- */
#define RTC_SECONDS     0x00
#define RTC_MINUTES     0x02
#define RTC_HOURS       0x04
#define RTC_WEEKDAY     0x06
#define RTC_DAY         0x07
#define RTC_MONTH       0x08
#define RTC_YEAR        0x09
#define RTC_CENTURY     0x32

/* --- 状态寄存器 --- */
#define RTC_STATUS_A    0x0A
#define RTC_STATUS_B    0x0B
#define RTC_UIP         0x80   /* 状态A bit7: 更新时间标志 */
#define RTC_B_BINARY    0x04   /* 状态B bit2: 二进制模式 */
#define RTC_B_24H       0x02   /* 状态B bit1: 24小时制 */

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int weekday;
} rtc_time_t;

/* 读 CMOS 寄存器 (bit7 禁 NMI) */
uint8_t rtc_read_cmos(uint8_t reg);
/* 读取完整时间 (处理 BCD/12h/UIP) */
void rtc_read_time(rtc_time_t *t);
/* V2.87: 简化读取; 成功返回 0, 读取异常返回 1 */
int rtc_get_time(int *h, int *m, int *s, int *d, int *mo, int *y);
/* V2.91: 写入 RTC (时/分/秒/日/月/年), 处理 UIP + SET 暂停更新 */
void rtc_set_time(int h, int m, int s, int d, int mo, int y);

/* ============ V4.06-E51: RTC 闹钟唤醒 ============ */
/* 状态B: PIE bit6(周期中断使能) AIE bit5(闹钟中断使能) */
#define RTC_B_PIE   0x40
#define RTC_B_AIE   0x20
/* 状态C: IRQF bit7 PF bit6 AF bit5 (读即清) */
#define RTC_C_IRQF  0x80
#define RTC_C_PF    0x40
#define RTC_C_AF    0x20

/* V4.13-E58: 多组闹钟槽位 (最多 4 个, 三类可并存)
 *   add: type 1=oneshot(p1=剩余秒) 2=daily(p1=hour,p2=minute)
 *        3=weekly(p1=dow,p2=minute,p3=hour); 返回槽位号或 -1 */
int  rtc_alarm_add(int type, int p1, int p2, int p3);
void rtc_alarm_del(int idx);
void rtc_alarm_clear_all(void);
int  rtc_alarm_get(int idx, int *type, int *p1, int *p2, int *p3);

/* N 秒后触发闹钟唤醒 (0=关闭). 用 RTC 1Hz 周期中断 + 软件计数. */
void rtc_alarm_sec(uint32_t sec);
/* 关闭闹钟 */
void rtc_alarm_off(void);
/* V4.14-E59: 稍后提醒——minutes 分钟后一次性闹钟; 返回 1=成功 0=无触发 -1=参数错 -2=槽满 */
int  rtc_alarm_snooze(int minutes);
/* 闹钟是否已触发 (shell 查询) */
int  rtc_alarm_fired(void);
/* IRQ8 顶半部: 清中断+计数; 到点置 rtc_wake_pending (由 softirq 处理唤醒) */
void rtc_irq_handler(void);
/* softirq 轮询: 返回并消费 pending 标志 (1=有 RTC 唤醒待处理) */
int  rtc_consume_wake_pending(void);

/* V4.09-E54: 每日闹钟——按 hh:mm 时间匹配触发 (0-23/0-59, -1=关闭).
 * 触发后自动关闭(当次); 与一次性闹钟互斥. */
void rtc_alarm_daily(int hour, int minute);

/* V4.11-E56: 周几重复闹钟——按 星期(dow: 0=周日..6=周六) + hh:mm 匹配.
 * 触发后保留, 每周同时间重复; 与一次性/每日闹钟互斥; dow<0 关闭. */
void rtc_alarm_weekly(int dow, int hour, int minute);

#endif /* HEPU_RTC_H */
