/* ============================================================================
 * 赫普OS - HMAC消息认证码
 * 基于哈希的消息认证码（RFC 2104）
 * ========================================================================== */

#include <stdint.h>
#include "hmac.h"
#include "vga.h"
#include "string.h"

#define HMAC_IPAD 0x36
#define HMAC_OPAD 0x5C

static int hmac_initialized = 0;

/* 初始化HMAC */
void hmac_init(void)
{
    hmac_initialized = 1;
    vga_puts("[HMAC] HMAC init \n");
    vga_puts("[HMAC] supports: SHA1/SHA256/SHA512\n");
}

/* 初始化HMAC上下文 */
int hmac_init_ctx(hmac_ctx_t *ctx, sha_alg_t alg, const uint8_t *key, uint32_t key_len)
{
    if (!ctx || !key || key_len == 0) return -1;
    if (!hmac_initialized) return -1;

    uint32_t block_size;
    switch (alg) {
        case SHA_ALG_SHA1:
        case SHA_ALG_SHA256:
        case SHA_ALG_SHA224:
            block_size = SHA256_BLOCK_SIZE;
            break;
        case SHA_ALG_SHA512:
        case SHA_ALG_SHA384:
            block_size = SHA512_BLOCK_SIZE;
            break;
        default:
            return -1;
    }

    memset(ctx, 0, sizeof(hmac_ctx_t));
    ctx->alg = alg;

    /* 处理密钥：如果密钥长于块大小，先哈希 */
    if (key_len > block_size) {
        sha_compute(alg, key, key_len, ctx->key);
        ctx->key_len = sha_digest_size(alg);
    } else {
        memcpy(ctx->key, key, key_len);
        ctx->key_len = key_len;
    }

    /* 准备内部和外部填充 */
    uint8_t ipad[HMAC_MAX_KEY_SIZE];
    uint8_t opad[HMAC_MAX_KEY_SIZE];

    for (uint32_t i = 0; i < block_size; i++) {
        uint8_t k = i < ctx->key_len ? ctx->key[i] : 0;
        ipad[i] = k ^ HMAC_IPAD;
        opad[i] = k ^ HMAC_OPAD;
    }

    /* 内部哈希：H((K xor ipad) || message) */
    sha_init_ctx(&ctx->inner_ctx, alg);
    sha_update_ctx(&ctx->inner_ctx, ipad, block_size);

    /* 外部哈希上下文准备 */
    sha_init_ctx(&ctx->outer_ctx, alg);
    sha_update_ctx(&ctx->outer_ctx, opad, block_size);

    ctx->initialized = 1;

    /* 清零临时缓冲区 */
    memset(ipad, 0, sizeof(ipad));
    memset(opad, 0, sizeof(opad));

    return 0;
}

/* 更新HMAC */
int hmac_update(hmac_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    if (!ctx || !ctx->initialized) return -1;
    return sha_update_ctx(&ctx->inner_ctx, data, len);
}

/* 完成HMAC */
int hmac_final(hmac_ctx_t *ctx, uint8_t *mac, uint32_t *mac_len)
{
    if (!ctx || !ctx->initialized || !mac) return -1;

    uint8_t inner_digest[HMAC_MAX_DIGEST_SIZE];
    uint32_t digest_size = sha_digest_size(ctx->alg);

    /* 完成内部哈希 */
    sha_final_ctx(&ctx->inner_ctx, inner_digest);

    /* 外部哈希：H((K xor opad) || inner_digest) */
    sha_update_ctx(&ctx->outer_ctx, inner_digest, digest_size);
    sha_final_ctx(&ctx->outer_ctx, mac);

    if (mac_len) *mac_len = digest_size;

    /* 清零临时缓冲区 */
    memset(inner_digest, 0, sizeof(inner_digest));
    ctx->initialized = 0;

    return 0;
}

/* 一次性计算HMAC */
int hmac_compute(sha_alg_t alg, const uint8_t *key, uint32_t key_len,
                 const uint8_t *data, uint32_t data_len,
                 uint8_t *mac, uint32_t *mac_len)
{
    hmac_ctx_t ctx;
    int ret;

    ret = hmac_init_ctx(&ctx, alg, key, key_len);
    if (ret != 0) return ret;

    ret = hmac_update(&ctx, data, data_len);
    if (ret != 0) return ret;

    return hmac_final(&ctx, mac, mac_len);
}

/* 验证HMAC（恒定时间比较） */
int hmac_verify(sha_alg_t alg, const uint8_t *key, uint32_t key_len,
                const uint8_t *data, uint32_t data_len,
                const uint8_t *expected_mac, uint32_t mac_len)
{
    uint8_t computed_mac[HMAC_MAX_DIGEST_SIZE];
    uint32_t computed_len;

    if (hmac_compute(alg, key, key_len, data, data_len,
                     computed_mac, &computed_len) != 0) {
        return -1;
    }

    if (computed_len != mac_len) return -1;

    /* 恒定时间比较，防止时序攻击 */
    uint8_t diff = 0;
    for (uint32_t i = 0; i < mac_len; i++) {
        diff |= computed_mac[i] ^ expected_mac[i];
    }

    memset(computed_mac, 0, sizeof(computed_mac));
    return diff == 0 ? 0 : -1;
}
