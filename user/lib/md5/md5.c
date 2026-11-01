/* ============================================================================
 * 赫普OS - MD5 哈希实现
 * V9.00: 第三方库移植
 * P5-4 第三方库移植
 * 参考: RFC 1321
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include "md5.h"

static void *local_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

#define memcpy local_memcpy

/* MD5辅助函数 */
#define F(x,y,z) (((x) & (y)) | (~(x) & (z)))
#define G(x,y,z) (((x) & (z)) | ((y) & ~(z)))
#define H(x,y,z) ((x) ^ (y) ^ (z))
#define I(x,y,z) ((y) ^ ((x) | ~(z)))

#define ROTL(x,n) (((x) << (n)) | ((x) >> (32-(n))))

#define FF(a,b,c,d,x,s,ac) { (a) += F((b),(c),(d)) + (x) + (uint32_t)(ac); (a) = ROTL((a),(s)); (a) += (b); }
#define GG(a,b,c,d,x,s,ac) { (a) += G((b),(c),(d)) + (x) + (uint32_t)(ac); (a) = ROTL((a),(s)); (a) += (b); }
#define HH(a,b,c,d,x,s,ac) { (a) += H((b),(c),(d)) + (x) + (uint32_t)(ac); (a) = ROTL((a),(s)); (a) += (b); }
#define II(a,b,c,d,x,s,ac) { (a) += I((b),(c),(d)) + (x) + (uint32_t)(ac); (a) = ROTL((a),(s)); (a) += (b); }

/* ============================================================================
 * MD5压缩函数
 * ========================================================================== */

static void md5_transform(md5_ctx_t *ctx, const uint8_t block[MD5_BLOCK_SIZE])
{
    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t x[16];

    /* 小端序转换 */
    for (int i = 0; i < 16; i++) {
        x[i] = ((uint32_t)block[i*4]) |
               ((uint32_t)block[i*4+1] << 8) |
               ((uint32_t)block[i*4+2] << 16) |
               ((uint32_t)block[i*4+3] << 24);
    }

    /* 第1轮 */
    FF(a,b,c,d,x[0],  7,0xd76aa478);
    FF(d,a,b,c,x[1], 12,0xe8c7b756);
    FF(c,d,a,b,x[2], 17,0x242070db);
    FF(b,c,d,a,x[3], 22,0xc1bdceee);
    FF(a,b,c,d,x[4],  7,0xf57c0faf);
    FF(d,a,b,c,x[5], 12,0x4787c62a);
    FF(c,d,a,b,x[6], 17,0xa8304613);
    FF(b,c,d,a,x[7], 22,0xfd469501);
    FF(a,b,c,d,x[8],  7,0x698098d8);
    FF(d,a,b,c,x[9], 12,0x8b44f7af);
    FF(c,d,a,b,x[10],17,0xffff5bb1);
    FF(b,c,d,a,x[11],22,0x895cd7be);
    FF(a,b,c,d,x[12], 7,0x6b901122);
    FF(d,a,b,c,x[13],12,0xfd987193);
    FF(c,d,a,b,x[14],17,0xa679438e);
    FF(b,c,d,a,x[15],22,0x49b40821);

    /* 第2轮 */
    GG(a,b,c,d,x[1],  5,0xf61e2562);
    GG(d,a,b,c,x[6],  9,0xc040b340);
    GG(c,d,a,b,x[11],14,0x265e5a51);
    GG(b,c,d,a,x[0], 20,0xe9b6c7aa);
    GG(a,b,c,d,x[5],  5,0xd62f105d);
    GG(d,a,b,c,x[10], 9,0x02441453);
    GG(c,d,a,b,x[15],14,0xd8a1e681);
    GG(b,c,d,a,x[4], 20,0xe7d3fbc8);
    GG(a,b,c,d,x[9],  5,0x21e1cde6);
    GG(d,a,b,c,x[14], 9,0xc33707d6);
    GG(c,d,a,b,x[3], 14,0xf4d50d87);
    GG(b,c,d,a,x[8], 20,0x455a14ed);
    GG(a,b,c,d,x[13], 5,0xa9e3e905);
    GG(d,a,b,c,x[2],  9,0xfcefa3f8);
    GG(c,d,a,b,x[7], 14,0x676f02d9);
    GG(b,c,d,a,x[12],20,0x8d2a4c8a);

    /* 第3轮 */
    HH(a,b,c,d,x[5],  4,0xfffa3942);
    HH(d,a,b,c,x[8], 11,0x8771f681);
    HH(c,d,a,b,x[11],16,0x6d9d6122);
    HH(b,c,d,a,x[14],23,0xfde5380c);
    HH(a,b,c,d,x[1],  4,0xa4beea44);
    HH(d,a,b,c,x[4], 11,0x4bdecfa9);
    HH(c,d,a,b,x[7], 16,0xf6bb4b60);
    HH(b,c,d,a,x[10],23,0xbebfbc70);
    HH(a,b,c,d,x[13], 4,0x289b7ec6);
    HH(d,a,b,c,x[0], 11,0xeaa127fa);
    HH(c,d,a,b,x[3], 16,0xd4ef3085);
    HH(b,c,d,a,x[6], 23,0x04881d05);
    HH(a,b,c,d,x[9],  4,0xd9d4d039);
    HH(d,a,b,c,x[12],11,0xe6db99e5);
    HH(c,d,a,b,x[15],16,0x1fa27cf8);
    HH(b,c,d,a,x[2], 23,0xc4ac5665);

    /* 第4轮 */
    II(a,b,c,d,x[0],  6,0xf4292244);
    II(d,a,b,c,x[7], 10,0x432aff97);
    II(c,d,a,b,x[14],15,0xab9423a7);
    II(b,c,d,a,x[5], 21,0xfc93a039);
    II(a,b,c,d,x[12], 6,0x655b59c3);
    II(d,a,b,c,x[3], 10,0x8f0ccc92);
    II(c,d,a,b,x[10],15,0xffeff47d);
    II(b,c,d,a,x[1], 21,0x85845dd1);
    II(a,b,c,d,x[8],  6,0x6fa87e4f);
    II(d,a,b,c,x[15],10,0xfe2ce6e0);
    II(c,d,a,b,x[6], 15,0xa3014314);
    II(b,c,d,a,x[13],21,0x4e0811a1);
    II(a,b,c,d,x[4],  6,0xf7537e82);
    II(d,a,b,c,x[11],10,0xbd3af235);
    II(c,d,a,b,x[2], 15,0x2ad7d2bb);
    II(b,c,d,a,x[9], 21,0xeb86d391);

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
}

/* ============================================================================
 * 公共API
 * ========================================================================== */

void md5_init(md5_ctx_t *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->count = 0;
}

void md5_update(md5_ctx_t *ctx, const uint8_t *data, size_t len)
{
    size_t i;
    size_t buffer_idx = (size_t)(ctx->count % MD5_BLOCK_SIZE);
    ctx->count += len;

    if (buffer_idx > 0) {
        size_t space = MD5_BLOCK_SIZE - buffer_idx;
        if (len >= space) {
            memcpy(ctx->buffer + buffer_idx, data, space);
            md5_transform(ctx, ctx->buffer);
            data += space;
            len -= space;
            buffer_idx = 0;
        } else {
            memcpy(ctx->buffer + buffer_idx, data, len);
            return;
        }
    }

    while (len >= MD5_BLOCK_SIZE) {
        md5_transform(ctx, data);
        data += MD5_BLOCK_SIZE;
        len -= MD5_BLOCK_SIZE;
    }

    if (len > 0) {
        memcpy(ctx->buffer, data, len);
    }
}

void md5_final(md5_ctx_t *ctx, uint8_t digest[MD5_DIGEST_SIZE])
{
    uint8_t padding[MD5_BLOCK_SIZE] = {0x80};
    size_t buffer_idx = (size_t)(ctx->count % MD5_BLOCK_SIZE);
    size_t pad_len = (buffer_idx < 56) ? (56 - buffer_idx) : (120 - buffer_idx);

    uint64_t bits = ctx->count * 8;
    uint8_t bits_le[8];
    for (int i = 0; i < 8; i++) {
        bits_le[i] = (uint8_t)((bits >> (i * 8)) & 0xFF);
    }

    md5_update(ctx, padding, pad_len);
    md5_update(ctx, bits_le, 8);

    for (int i = 0; i < 4; i++) {
        digest[i*4]   = (uint8_t)(ctx->state[i] & 0xFF);
        digest[i*4+1] = (uint8_t)((ctx->state[i] >> 8) & 0xFF);
        digest[i*4+2] = (uint8_t)((ctx->state[i] >> 16) & 0xFF);
        digest[i*4+3] = (uint8_t)((ctx->state[i] >> 24) & 0xFF);
    }
}

void md5(const uint8_t *data, size_t len, uint8_t digest[MD5_DIGEST_SIZE])
{
    md5_ctx_t ctx;
    md5_init(&ctx);
    md5_update(&ctx, data, len);
    md5_final(&ctx, digest);
}

void md5_to_hex(const uint8_t digest[MD5_DIGEST_SIZE], char *hex)
{
    static const char hex_chars[] = "0123456789abcdef";
    for (int i = 0; i < MD5_DIGEST_SIZE; i++) {
        hex[i*2]   = hex_chars[(digest[i] >> 4) & 0xF];
        hex[i*2+1] = hex_chars[digest[i] & 0xF];
    }
    hex[MD5_DIGEST_SIZE * 2] = '\0';
}
