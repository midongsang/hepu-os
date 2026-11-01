/* ============================================================================
 * 赫普OS - SHA-1 哈希库
 * V0.9.0: P5-4 第三方库移植
 * 参考: RFC 3174
 * ========================================================================== */

#ifndef _SHA1_H
#define _SHA1_H

#include <stdint.h>
#include <stddef.h>

#define SHA1_DIGEST_SIZE  20
#define SHA1_BLOCK_SIZE   64

typedef struct {
    uint32_t state[5];
    uint64_t count;
    uint8_t  buffer[SHA1_BLOCK_SIZE];
} sha1_ctx_t;

void sha1_init(sha1_ctx_t *ctx);
void sha1_update(sha1_ctx_t *ctx, const uint8_t *data, size_t len);
void sha1_final(sha1_ctx_t *ctx, uint8_t digest[SHA1_DIGEST_SIZE]);
void sha1(const uint8_t *data, size_t len, uint8_t digest[SHA1_DIGEST_SIZE]);
void sha1_to_hex(const uint8_t digest[SHA1_DIGEST_SIZE], char *hex);

#endif /* _SHA1_H */
