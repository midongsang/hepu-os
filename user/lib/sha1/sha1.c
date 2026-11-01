/* ============================================================================
 * 赫普OS - SHA-1 哈希实现
 * V0.9.0: P5-4 第三方库移植
 * 参考: RFC 3174
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "sha1.h"

/* 简单的memcpy实现（避免依赖系统库） */
static void local_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

/* 循环左移 */
#define ROTL(x, n) (((x) << (n)) | ((x) >> (32-(n))))

/* SHA-1函数 */
#define F1(b,c,d) (((b) & (c)) | (~(b) & (d)))
#define F2(b,c,d) ((b) ^ (c) ^ (d))
#define F3(b,c,d) (((b) & (c)) | ((b) & (d)) | ((c) & (d)))
#define F4(b,c,d) ((b) ^ (c) ^ (d))

/* 常量 */
#define K1 0x5A827999
#define K2 0x6ED9EBA1
#define K3 0x8F1BBCDC
#define K4 0xCA62C1D6

/* ============================================================================
 * SHA-1压缩函数
 * ========================================================================== */

static void sha1_transform(sha1_ctx_t *ctx, const uint8_t block[SHA1_BLOCK_SIZE])
{
    uint32_t w[80];
    uint32_t a, b, c, d, e, temp;
    int i;

    /* 大端序转换：前16个字 */
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) |
               ((uint32_t)block[i*4+3]);
    }

    /* 扩展到80个字 */
    for (i = 16; i < 80; i++) {
        w[i] = ROTL(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    }

    /* 初始化工作变量 */
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];

    /* 第1轮：0-19 */
    for (i = 0; i < 20; i++) {
        temp = ROTL(a, 5) + F1(b,c,d) + e + w[i] + K1;
        e = d; d = c; c = ROTL(b, 30); b = a; a = temp;
    }

    /* 第2轮：20-39 */
    for (i = 20; i < 40; i++) {
        temp = ROTL(a, 5) + F2(b,c,d) + e + w[i] + K2;
        e = d; d = c; c = ROTL(b, 30); b = a; a = temp;
    }

    /* 第3轮：40-59 */
    for (i = 40; i < 60; i++) {
        temp = ROTL(a, 5) + F3(b,c,d) + e + w[i] + K3;
        e = d; d = c; c = ROTL(b, 30); b = a; a = temp;
    }

    /* 第4轮：60-79 */
    for (i = 60; i < 80; i++) {
        temp = ROTL(a, 5) + F4(b,c,d) + e + w[i] + K4;
        e = d; d = c; c = ROTL(b, 30); b = a; a = temp;
    }

    /* 更新状态 */
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
}

/* ============================================================================
 * 公共API
 * ========================================================================== */

void sha1_init(sha1_ctx_t *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count = 0;
}

void sha1_update(sha1_ctx_t *ctx, const uint8_t *data, size_t len)
{
    size_t i;
    size_t buffer_idx = (size_t)(ctx->count % SHA1_BLOCK_SIZE);
    ctx->count += len;

    if (buffer_idx > 0) {
        size_t space = SHA1_BLOCK_SIZE - buffer_idx;
        if (len >= space) {
            local_memcpy(ctx->buffer + buffer_idx, data, space);
            sha1_transform(ctx, ctx->buffer);
            data += space;
            len -= space;
            buffer_idx = 0;
        } else {
            local_memcpy(ctx->buffer + buffer_idx, data, len);
            return;
        }
    }

    while (len >= SHA1_BLOCK_SIZE) {
        sha1_transform(ctx, data);
        data += SHA1_BLOCK_SIZE;
        len -= SHA1_BLOCK_SIZE;
    }

    if (len > 0) {
        local_memcpy(ctx->buffer, data, len);
    }
}

void sha1_final(sha1_ctx_t *ctx, uint8_t digest[SHA1_DIGEST_SIZE])
{
    uint8_t padding[SHA1_BLOCK_SIZE];
    padding[0] = 0x80;
    for (int i = 1; i < SHA1_BLOCK_SIZE; i++) padding[i] = 0;

    size_t buffer_idx = (size_t)(ctx->count % SHA1_BLOCK_SIZE);
    size_t pad_len = (buffer_idx < 56) ? (56 - buffer_idx) : (120 - buffer_idx);

    /* 64位大端长度（比特数） */
    uint64_t bits = ctx->count * 8;
    uint8_t bits_be[8];
    for (int i = 0; i < 8; i++) {
        bits_be[i] = (uint8_t)((bits >> (56 - i * 8)) & 0xFF);
    }

    sha1_update(ctx, padding, pad_len);
    sha1_update(ctx, bits_be, 8);

    /* 输出大端序摘要 */
    for (int i = 0; i < 5; i++) {
        digest[i*4]   = (uint8_t)((ctx->state[i] >> 24) & 0xFF);
        digest[i*4+1] = (uint8_t)((ctx->state[i] >> 16) & 0xFF);
        digest[i*4+2] = (uint8_t)((ctx->state[i] >> 8) & 0xFF);
        digest[i*4+3] = (uint8_t)(ctx->state[i] & 0xFF);
    }
}

void sha1(const uint8_t *data, size_t len, uint8_t digest[SHA1_DIGEST_SIZE])
{
    sha1_ctx_t ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(&ctx, digest);
}

void sha1_to_hex(const uint8_t digest[SHA1_DIGEST_SIZE], char *hex)
{
    static const char hex_chars[] = "0123456789abcdef";
    for (int i = 0; i < SHA1_DIGEST_SIZE; i++) {
        hex[i*2]   = hex_chars[(digest[i] >> 4) & 0xF];
        hex[i*2+1] = hex_chars[digest[i] & 0xF];
    }
    hex[SHA1_DIGEST_SIZE * 2] = '\0';
}
