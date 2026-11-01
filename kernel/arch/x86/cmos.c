/* ============================================================================
 * 赫普OS - CMOS RAM 读写 (V2.53)
 *
 * 通过标准 x86 端口 0x70/0x71 读写 CMOS RAM（RTC 实时钟 + 扩展 RAM）。
 * 用途：UI 模式等少量配置的持久化存储（CMOS 电池供电，重启不丢）。
 *
 * 注意：CMOS 地址 0x00~0x0F 为 RTC 时间寄存器、0x10~0x2F 为状态/设备寄存器，
 * 扩展 RAM 区 0x30~0x7F 可用于通用配置存储。本项目用 0x37 存 UI 模式标记。
 * ========================================================================== */

#include <stdint.h>
#include "cmos.h"

static inline void cmos_outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t cmos_inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* 读 CMOS RAM 指定地址 */
uint8_t cmos_read_byte(uint8_t addr)
{
    /* NMI 使能位保持 (0x80 写地址端口时), 标准做法: addr & 0x7F */
    cmos_outb(0x70, (uint8_t)(addr & 0x7F));
    return cmos_inb(0x71);
}

/* 写 CMOS RAM 指定地址 */
void cmos_write_byte(uint8_t addr, uint8_t val)
{
    /* 先关 NMI (写 0x80|addr) 再写, 避免期间 RTC 更新中断 */
    cmos_outb(0x70, (uint8_t)(0x80 | (addr & 0x7F)));
    cmos_outb(0x71, val);
    /* 恢复 NMI 使能 */
    cmos_outb(0x70, (uint8_t)(addr & 0x7F));
}
