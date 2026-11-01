/* ============================================================================
 * 赫普OS - CRC32 校验库
 * V0.9.0: P5-4 第三方库移植
 * 参考: ISO 3309 / ITU-T V.42
 * ========================================================================== */

#ifndef _CRC32_H
#define _CRC32_H

#include <stdint.h>
#include <stddef.h>

#define CRC32_INIT_VALUE  0xFFFFFFFF
#define CRC32_FINAL_XOR   0xFFFFFFFF

/* 初始化CRC32上下文 */
uint32_t crc32_init(void);

/* 更新CRC32校验和 */
uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len);

/* 完成CRC32计算 */
uint32_t crc32_final(uint32_t crc);

/* 一次性计算CRC32 */
uint32_t crc32(const uint8_t *data, size_t len);

/* 转换为十六进制字符串 */
void crc32_to_hex(uint32_t crc, char *hex);

#endif /* _CRC32_H */
