/* V3.87-E6: 软看门狗实现 */
#include "watchdog.h"
#include "pit.h"
#include "serial.h"

static volatile uint32_t wd_last_feed = 0;
static volatile uint32_t wd_triggers = 0;
static volatile int wd_on = 0;
static volatile int wd_feed_paused = 0;  /* wd test: 暂停喂狗以触发超时 */

void wd_init(void)
{
    wd_last_feed = pit_get_ticks();
    wd_triggers = 0;
    wd_on = 1;
}

void wd_feed(void)
{
    if (wd_on && !wd_feed_paused) wd_last_feed = pit_get_ticks();
}

int wd_check(void)
{
    if (!wd_on) return 0;
    return (pit_get_ticks() - wd_last_feed) > WD_TIMEOUT_TICKS;
}

int wd_enabled(void) { return wd_on; }

uint32_t wd_stats(void) { return wd_triggers; }

/* V3.89-E30: 心跳详情——距上次喂狗 tick(诊断"谁没喂狗") */
uint32_t wd_heartbeat_age(void)
{
    return pit_get_ticks() - wd_last_feed;
}

void wd_arm_test(void)
{
    wd_feed_paused = 1;  /* 停止喂狗 → 心跳过期 → wd_check 命中超时 */
}

void wd_trigger(void)
{
    wd_triggers++;
    wd_on = 0;  /* 防止重复触发 */
    wd_feed_paused = 0;
    serial_puts(0x3F8, "\r\n[WATCHDOG] kernel heartbeat timeout, resetting...\r\n");
    serial_puts(0x3F8, "[WATCHDOG] triggers=");
    /* 简单十进制输出 */
    {
        char buf[12]; int i = 0; uint32_t n = wd_triggers;
        if (n == 0) buf[i++] = '0';
        while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
        while (i > 0) serial_putc(0x3F8, buf[--i]);
    }
    serial_puts(0x3F8, "\r\n");

    /* 硬复位: 优先 0xCF9 (PIIX 全复位, QEMU 下最可靠), 回退 8042 软复位 */
    __asm__ volatile ("cli");
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x02), "Nd"((uint16_t)0xCF9));
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0x06), "Nd"((uint16_t)0xCF9));
    for (volatile int i = 0; i < 2000000; i++);
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"((uint16_t)0x64));
    for (;;) { __asm__ volatile ("hlt"); }
}
