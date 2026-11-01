/* ============================================================================
 * 赫普OS - SHA哈希算法
 * 支持SHA1/SHA256/SHA512
 * ========================================================================== */

#include <stdint.h>
#include "sha.h"
#include "vga.h"
#include "string.h"

static int sha_initialized = 0;

/* 循环左移 */
static inline uint32_t sha_rotl32(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

static inline uint64_t sha_rotl64(uint64_t x, int n)
{
    return (x << n) | (x >> (64 - n));
}

/* 大端序读取32位 */
static inline uint32_t sha_get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* 大端序写入32位 */
static inline void sha_put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

/* 大端序读取64位 */
static inline uint64_t sha_get_be64(const uint8_t *p)
{
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8) | (uint64_t)p[7];
}

/* 大端序写入64位 */
static inline void sha_put_be64(uint8_t *p, uint64_t v)
{
    p[0] = (v >> 56) & 0xFF;
    p[1] = (v >> 48) & 0xFF;
    p[2] = (v >> 40) & 0xFF;
    p[3] = (v >> 32) & 0xFF;
    p[4] = (v >> 24) & 0xFF;
    p[5] = (v >> 16) & 0xFF;
    p[6] = (v >> 8) & 0xFF;
    p[7] = v & 0xFF;
}

/* ============================================================================
 * SHA1
 * ========================================================================== */

static const uint32_t sha1_k[4] = {
    0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6
};

static void sha1_process_block(sha1_ctx_t *ctx, const uint8_t *block)
{
    uint32_t w[80];
    uint32_t a, b, c, d, e, temp;

    for (int i = 0; i < 16; i++) {
        w[i] = sha_get_be32(block + i * 4);
    }
    for (int i = 16; i < 80; i++) {
        w[i] = sha_rotl32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = sha1_k[0];
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = sha1_k[1];
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = sha1_k[2];
        } else {
            f = b ^ c ^ d;
            k = sha1_k[3];
        }

        temp = sha_rotl32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = sha_rotl32(b, 30);
        b = a;
        a = temp;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
}

void sha1_init(sha1_ctx_t *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count = 0;
}

void sha1_update(sha1_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t buffer_idx = (uint32_t)(ctx->count % SHA1_BLOCK_SIZE);
    ctx->count += len;

    if (buffer_idx + len >= SHA1_BLOCK_SIZE) {
        uint32_t copy = SHA1_BLOCK_SIZE - buffer_idx;
        memcpy(ctx->buffer + buffer_idx, data, copy);
        sha1_process_block(ctx, ctx->buffer);
        data += copy;
        len -= copy;

        while (len >= SHA1_BLOCK_SIZE) {
            sha1_process_block(ctx, data);
            data += SHA1_BLOCK_SIZE;
            len -= SHA1_BLOCK_SIZE;
        }
        buffer_idx = 0;
    }

    if (len > 0) {
        memcpy(ctx->buffer + buffer_idx, data, len);
    }
}

void sha1_final(sha1_ctx_t *ctx, uint8_t *digest)
{
    uint32_t buffer_idx = (uint32_t)(ctx->count % SHA1_BLOCK_SIZE);
    uint64_t bit_count = ctx->count * 8;

    ctx->buffer[buffer_idx++] = 0x80;

    if (buffer_idx > 56) {
        while (buffer_idx < SHA1_BLOCK_SIZE) {
            ctx->buffer[buffer_idx++] = 0;
        }
        sha1_process_block(ctx, ctx->buffer);
        buffer_idx = 0;
    }

    while (buffer_idx < 56) {
        ctx->buffer[buffer_idx++] = 0;
    }

    sha_put_be64(ctx->buffer + 56, bit_count);
    sha1_process_block(ctx, ctx->buffer);

    for (int i = 0; i < 5; i++) {
        sha_put_be32(digest + i * 4, ctx->state[i]);
    }
}

int sha1(const uint8_t *data, uint32_t len, uint8_t *digest)
{
    sha1_ctx_t ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(&ctx, digest);
    return 0;
}

/* ============================================================================
 * SHA256
 * ========================================================================== */

static const uint32_t sha256_k[64] = {
    0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5,
    0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
    0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
    0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
    0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC,
    0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
    0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7,
    0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
    0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
    0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
    0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3,
    0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
    0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5,
    0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
    0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
    0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2
};

static inline uint32_t sha256_ch(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (~x & z);
}

static inline uint32_t sha256_maj(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

/* E-43 修复: SHA-256 标准用 ROTR(右旋)——原实现误用 ROTL(SHA-1 语义),
 * 导致全部 SHA-256 输出错误(标准向量验证失败).
 * ROTRn(x) == ROTL(32-n)(x), 复用 rotl 实现. */
static inline uint32_t sha256_sigma0(uint32_t x)
{
    return sha_rotl32(x, 30) ^ sha_rotl32(x, 19) ^ sha_rotl32(x, 10);
}

static inline uint32_t sha256_sigma1(uint32_t x)
{
    return sha_rotl32(x, 26) ^ sha_rotl32(x, 21) ^ sha_rotl32(x, 7);
}

static inline uint32_t sha256_gamma0(uint32_t x)
{
    return sha_rotl32(x, 25) ^ sha_rotl32(x, 14) ^ (x >> 3);
}

static inline uint32_t sha256_gamma1(uint32_t x)
{
    return sha_rotl32(x, 15) ^ sha_rotl32(x, 13) ^ (x >> 10);
}

static void sha256_process_block(sha256_ctx_t *ctx, const uint8_t *block)
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h, t1, t2;

    for (int i = 0; i < 16; i++) {
        w[i] = sha_get_be32(block + i * 4);
    }
    for (int i = 16; i < 64; i++) {
        w[i] = sha256_gamma1(w[i-2]) + w[i-7] +
               sha256_gamma0(w[i-15]) + w[i-16];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        t1 = h + sha256_sigma1(e) + sha256_ch(e, f, g) + sha256_k[i] + w[i];
        t2 = sha256_sigma0(a) + sha256_maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void sha256_init(sha256_ctx_t *ctx)
{
    ctx->state[0] = 0x6A09E667;
    ctx->state[1] = 0xBB67AE85;
    ctx->state[2] = 0x3C6EF372;
    ctx->state[3] = 0xA54FF53A;
    ctx->state[4] = 0x510E527F;
    ctx->state[5] = 0x9B05688C;
    ctx->state[6] = 0x1F83D9AB;
    ctx->state[7] = 0x5BE0CD19;
    ctx->count = 0;
}

void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t buffer_idx = (uint32_t)(ctx->count % SHA256_BLOCK_SIZE);
    ctx->count += len;

    if (buffer_idx + len >= SHA256_BLOCK_SIZE) {
        uint32_t copy = SHA256_BLOCK_SIZE - buffer_idx;
        memcpy(ctx->buffer + buffer_idx, data, copy);
        sha256_process_block(ctx, ctx->buffer);
        data += copy;
        len -= copy;

        while (len >= SHA256_BLOCK_SIZE) {
            sha256_process_block(ctx, data);
            data += SHA256_BLOCK_SIZE;
            len -= SHA256_BLOCK_SIZE;
        }
        buffer_idx = 0;
    }

    if (len > 0) {
        memcpy(ctx->buffer + buffer_idx, data, len);
    }
}

void sha256_final(sha256_ctx_t *ctx, uint8_t *digest)
{
    uint32_t buffer_idx = (uint32_t)(ctx->count % SHA256_BLOCK_SIZE);
    uint64_t bit_count = ctx->count * 8;

    ctx->buffer[buffer_idx++] = 0x80;

    if (buffer_idx > 56) {
        while (buffer_idx < SHA256_BLOCK_SIZE) {
            ctx->buffer[buffer_idx++] = 0;
        }
        sha256_process_block(ctx, ctx->buffer);
        buffer_idx = 0;
    }

    while (buffer_idx < 56) {
        ctx->buffer[buffer_idx++] = 0;
    }

    sha_put_be64(ctx->buffer + 56, bit_count);
    sha256_process_block(ctx, ctx->buffer);

    for (int i = 0; i < 8; i++) {
        sha_put_be32(digest + i * 4, ctx->state[i]);
    }
}

int sha256(const uint8_t *data, uint32_t len, uint8_t *digest)
{
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
    return 0;
}

/* ============================================================================
 * 统一接口
 * ========================================================================== */

void sha_init(void)
{
    sha_initialized = 1;
    vga_puts("[SHA] hash init \n");
    vga_puts("[SHA] supports: SHA1, SHA256, SHA512\n");
}

int sha_init_ctx(sha_ctx_t *ctx, sha_alg_t alg)
{
    if (!ctx) return -1;
    ctx->alg = alg;
    switch (alg) {
        case SHA_ALG_SHA1: sha1_init(&ctx->ctx.sha1); break;
        case SHA_ALG_SHA256: case SHA_ALG_SHA224: sha256_init(&ctx->ctx.sha256); break;
        case SHA_ALG_SHA512: case SHA_ALG_SHA384: sha512_init(&ctx->ctx.sha512); break;
        default: return -1;
    }
    return 0;
}

int sha_update_ctx(sha_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    if (!ctx) return -1;
    switch (ctx->alg) {
        case SHA_ALG_SHA1: sha1_update(&ctx->ctx.sha1, data, len); break;
        case SHA_ALG_SHA256: case SHA_ALG_SHA224: sha256_update(&ctx->ctx.sha256, data, len); break;
        case SHA_ALG_SHA512: case SHA_ALG_SHA384: sha512_update(&ctx->ctx.sha512, data, len); break;
        default: return -1;
    }
    return 0;
}

int sha_final_ctx(sha_ctx_t *ctx, uint8_t *digest)
{
    if (!ctx) return -1;
    switch (ctx->alg) {
        case SHA_ALG_SHA1: sha1_final(&ctx->ctx.sha1, digest); break;
        case SHA_ALG_SHA256: case SHA_ALG_SHA224: sha256_final(&ctx->ctx.sha256, digest); break;
        case SHA_ALG_SHA512: case SHA_ALG_SHA384: sha512_final(&ctx->ctx.sha512, digest); break;
        default: return -1;
    }
    return 0;
}

int sha_compute(sha_alg_t alg, const uint8_t *data, uint32_t len, uint8_t *digest)
{
    sha_ctx_t ctx;
    if (sha_init_ctx(&ctx, alg) != 0) return -1;
    sha_update_ctx(&ctx, data, len);
    return sha_final_ctx(&ctx, digest);
}

uint32_t sha_digest_size(sha_alg_t alg)
{
    switch (alg) {
        case SHA_ALG_SHA1: return SHA1_DIGEST_SIZE;
        case SHA_ALG_SHA224: return 28;
        case SHA_ALG_SHA256: return SHA256_DIGEST_SIZE;
        case SHA_ALG_SHA384: return 48;
        case SHA_ALG_SHA512: return SHA512_DIGEST_SIZE;
        default: return 0;
    }
}

const char *sha_algorithm_name(sha_alg_t alg)
{
    switch (alg) {
        case SHA_ALG_SHA1: return "SHA-1";
        case SHA_ALG_SHA224: return "SHA-224";
        case SHA_ALG_SHA256: return "SHA-256";
        case SHA_ALG_SHA384: return "SHA-384";
        case SHA_ALG_SHA512: return "SHA-512";
        default: return "Unknown";
    }
}

