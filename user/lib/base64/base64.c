/* ============================================================================
 * 赫普OS - Base64 编码/解码实现
 * V9.00: 第三方库移植
 * P5-4 第三方库移植
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "base64.h"

static void *local_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

/* Base64字符表 */
static const char base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* 反查表 */
static const int8_t base64_decode_table[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

/* ============================================================================
 * 编码
 * ========================================================================== */

size_t base64_encoded_length(size_t input_len)
{
    return ((input_len + 2) / 3) * 4;
}

int base64_encode(const uint8_t *input, size_t input_len,
                  char *output, size_t output_size)
{
    if (!input || !output) return -1;

    size_t needed = base64_encoded_length(input_len) + 1;  /* +1 for NUL */
    if (output_size < needed) return -1;

    size_t i, j;
    for (i = 0, j = 0; i + 2 < input_len; i += 3, j += 4) {
        uint32_t triple = ((uint32_t)input[i] << 16) |
                          ((uint32_t)input[i+1] << 8) |
                          ((uint32_t)input[i+2]);
        output[j]   = base64_table[(triple >> 18) & 0x3F];
        output[j+1] = base64_table[(triple >> 12) & 0x3F];
        output[j+2] = base64_table[(triple >> 6) & 0x3F];
        output[j+3] = base64_table[triple & 0x3F];
    }

    /* 处理剩余字节 */
    if (i < input_len) {
        uint32_t triple = (uint32_t)input[i] << 16;
        if (i + 1 < input_len) {
            triple |= (uint32_t)input[i+1] << 8;
        }

        output[j++] = base64_table[(triple >> 18) & 0x3F];
        output[j++] = base64_table[(triple >> 12) & 0x3F];
        output[j++] = (i + 1 < input_len) ? base64_table[(triple >> 6) & 0x3F] : '=';
        output[j++] = '=';
    }

    output[j] = '\0';
    return (int)j;
}

/* ============================================================================
 * 解码
 * ========================================================================== */

size_t base64_decoded_length(const char *input, size_t input_len)
{
    if (!input || input_len == 0) return 0;

    size_t padding = 0;
    if (input_len >= 1 && input[input_len-1] == '=') padding++;
    if (input_len >= 2 && input[input_len-2] == '=') padding++;

    return (input_len / 4) * 3 - padding;
}

int base64_decode(const char *input, size_t input_len,
                  uint8_t *output, size_t output_size)
{
    if (!input || !output) return -1;

    /* 跳过空白字符，计算有效长度 */
    size_t valid_len = 0;
    for (size_t i = 0; i < input_len; i++) {
        if (input[i] != ' ' && input[i] != '\n' &&
            input[i] != '\r' && input[i] != '\t') {
            valid_len++;
        }
    }

    if (valid_len % 4 != 0) return -1;

    size_t max_out = base64_decoded_length(input, input_len);
    if (output_size < max_out) return -1;

    size_t out_idx = 0;
    uint32_t buffer = 0;
    int bits = 0;

    for (size_t i = 0; i < input_len; i++) {
        char c = input[i];

        /* 跳过空白 */
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;

        /* 遇到填充结束 */
        if (c == '=') break;

        int8_t val = base64_decode_table[(uint8_t)c];
        if (val < 0) return -1;  /* 非法字符 */

        buffer = (buffer << 6) | (uint32_t)val;
        bits += 6;

        if (bits >= 8) {
            bits -= 8;
            output[out_idx++] = (uint8_t)((buffer >> bits) & 0xFF);
        }
    }

    return (int)out_idx;
}
