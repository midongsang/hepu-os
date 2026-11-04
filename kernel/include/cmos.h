/* ============================================================================
 * 赫普OS - CMOS RAM 读写 (V2.53)
 * 标准 x86 端口 0x70/0x71, 用于配置持久化 (电池供电).
 * ========================================================================== */
#ifndef HEPU_CMOS_H
#define HEPU_CMOS_H

#include <stdint.h>

/* CMOS 扩展 RAM 通用存储区: 0x30~0x7F */
#define HEPU_CMOS_UI_MODE_ADDR  0x37   /* UI 模式标记 (0=shell 1=desktop 2=phone) */

uint8_t cmos_read_byte(uint8_t addr);
void    cmos_write_byte(uint8_t addr, uint8_t val);

#endif /* HEPU_CMOS_H */
