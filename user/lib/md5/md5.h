/* ============================================================================
 * 赫普OS - MD5 哈希库
 * V9.00: 第三方库移植
 * P5-4 第三方库移植
 * ========================================================================== */

#ifndef _MD5_H
#define _MD5_H

#include <stdint.h>
#include <stddef.h>

#define MD5_DIGEST_SIZE  16
#define MD5_BLOCK_SIZE   64

typedef struct {
    uint32_t state[4];
    uint64_t count;
    uint8_t  buffer[MD5_BLOCK_SIZE];
} md5_ctx_t;

void md5_init(md5_ctx_t *ctx);
void md5_update(md5_ctx_t *ctx, const uint8_t *data, size_t len);
void md5_final(md5_ctx_t *ctx, uint8_t digest[MD5_DIGEST_SIZE]);
void md5(const uint8_t *data, size_t len, uint8_t digest[MD5_DIGEST_SIZE]);
void md5_to_hex(const uint8_t digest[MD5_DIGEST_SIZE], char *hex);

#endif /* _MD5_H */
