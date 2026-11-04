/* ============================================================================
 * 赫普OS - Base64 编码/解码库
 * V9.00: 第三方库移植
 * P5-4 第三方库移植
 * ========================================================================== */

#ifndef _BASE64_H
#define _BASE64_H

#include <stdint.h>
#include <stddef.h>

/* Base64编码 */
int base64_encode(const uint8_t *input, size_t input_len,
                  char *output, size_t output_size);

/* Base64解码 */
int base64_decode(const char *input, size_t input_len,
                  uint8_t *output, size_t output_size);

/* 获取编码后长度 */
size_t base64_encoded_length(size_t input_len);

/* 获取解码后最大长度 */
size_t base64_decoded_length(const char *input, size_t input_len);

#endif /* _BASE64_H */
