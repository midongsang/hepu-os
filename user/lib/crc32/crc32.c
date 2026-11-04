/* ============================================================================
 * 赫普OS - CRC32 校验实现
 * V0.9.0: P5-4 第三方库移植
 * 参考: ISO 3309 / ITU-T V.42
 * 多项式: 0xEDB88320 (反转的0x04C11DB7)
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "crc32.h"

/* CRC32查找表（运行时生成，避免静态初始化） */
static uint32_t crc32_table[256];
static int crc32_table_initialized = 0;

/* 生成CRC32查找表 */
static void crc32_init_table(void)
{
    if (crc32_table_initialized) return;

    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = 1;
}

/* ============================================================================
 * 公共API
 * ========================================================================== */

uint32_t crc32_init(void)
{
    crc32_init_table();
    return CRC32_INIT_VALUE;
}

uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    crc32_init_table();

    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return crc;
}

uint32_t crc32_final(uint32_t crc)
{
    return crc ^ CRC32_FINAL_XOR;
}

uint32_t crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = crc32_init();
    crc = crc32_update(crc, data, len);
    return crc32_final(crc);
}

void crc32_to_hex(uint32_t crc, char *hex)
{
    static const char hex_chars[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        hex[7 - i] = hex_chars[crc & 0xF];
        crc >>= 4;
    }
    hex[8] = '\0';
}
