/* ============================================================================
 * 赫普OS - AES-GCM (Galois/Counter Mode) AEAD实现
 * NIST SP 800-38D标准
 * 提供机密性 + 完整性保护
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "aes.h"
#include "vga.h"

/* GF(2^128)域乘法：r = x * y mod P(x) = x^128 + x^7 + x^2 + x + 1 */
static void gf_mult(const uint8_t *x, const uint8_t *y, uint8_t *r)
{
    uint8_t z[16] = {0};
    uint8_t v[16];
    memcpy(v, y, 16);

    for (int i = 0; i < 128; i++) {
        /* 如果x的第i位为1，z ^= v */
        if (x[i / 8] & (0x80 >> (i % 8))) {
            for (int j = 0; j < 16; j++) {
                z[j] ^= v[j];
            }
        }
        /* v右移1位，如果最低位为1则v ^= R (0xE1在最高字节) */
        int lsb = v[15] & 1;
        for (int j = 15; j > 0; j--) {
            v[j] = (v[j] >> 1) | ((v[j - 1] & 1) << 7);
        }
        v[0] >>= 1;
        if (lsb) {
            v[0] ^= 0xE1;
        }
    }
    memcpy(r, z, 16);
}

/* GHASH：将数据块追加到GHASH累加器 */
static void ghash_update(gcm_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint8_t block[16];
    uint32_t offset = 0;

    while (offset < len) {
        uint32_t block_len = len - offset;
        if (block_len > 16) block_len = 16;

        memset(block, 0, 16);
        memcpy(block, data + offset, block_len);

        /* XOR到累加器 */
        for (int i = 0; i < 16; i++) {
            ctx->ghash[i] ^= block[i];
        }

        /* 乘以H */
        gf_mult(ctx->ghash, ctx->H, ctx->ghash);

        offset += block_len;
    }
}

/* 计数器递增（大端序32位计数器在最后4字节） */
static void increment_counter(uint8_t *counter)
{
    for (int i = 15; i >= 12; i--) {
        counter[i]++;
        if (counter[i] != 0) break;
    }
}

/* GCM初始化 */
int gcm_init(gcm_ctx_t *ctx, const uint8_t *key, int key_size,
             const uint8_t *iv, int iv_len)
{
    if (!ctx || !key || !iv) return -1;
    if (key_size != 16 && key_size != 24 && key_size != 32) return -1;

    memset(ctx, 0, sizeof(gcm_ctx_t));

    /* 设置AES密钥 */
    if (aes_set_key(&ctx->aes_ctx, key, key_size) != 0) return -1;

    /* 计算H = AES(K, 0^128) */
    uint8_t zero[16] = {0};
    aes_encrypt_block(&ctx->aes_ctx, zero, ctx->H);

    /* 计算J0 */
    if (iv_len == 12) {
        /* 96位IV：J0 = IV || 0^31 || 1 */
        memcpy(ctx->J0, iv, 12);
        ctx->J0[12] = 0;
        ctx->J0[13] = 0;
        ctx->J0[14] = 0;
        ctx->J0[15] = 1;
    } else {
        /* 其他长度IV：J0 = GHASH(IV || 0^s || [len(IV)]_64) */
        uint8_t len_block[16] = {0};
        uint64_t iv_bits = (uint64_t)iv_len * 8;
        for (int i = 0; i < 8; i++) {
            len_block[15 - i] = (iv_bits >> (i * 8)) & 0xFF;
        }
        ghash_update(ctx, iv, iv_len);
        /* 填充到16字节边界 */
        uint32_t pad = (16 - (iv_len % 16)) % 16;
        uint8_t zeros[16] = {0};
        if (pad > 0) ghash_update(ctx, zeros, pad);
        ghash_update(ctx, len_block, 16);
        memcpy(ctx->J0, ctx->ghash, 16);
        memset(ctx->ghash, 0, 16);
    }

    /* 初始计数器 = J0 + 1 */
    memcpy(ctx->counter, ctx->J0, 16);
    increment_counter(ctx->counter);

    ctx->aad_len = 0;
    ctx->data_len = 0;
    ctx->initialized = 1;

    return 0;
}

/* 更新AAD（附加认证数据） */
int gcm_update_aad(gcm_ctx_t *ctx, const uint8_t *aad, uint32_t len)
{
    if (!ctx || !ctx->initialized) return -1;
    if (len == 0) return 0;

    ghash_update(ctx, aad, len);
    ctx->aad_len += (uint64_t)len * 8;

    return 0;
}

/* GCM加密 */
int gcm_encrypt(gcm_ctx_t *ctx, const uint8_t *input, uint8_t *output, uint32_t len)
{
    if (!ctx || !ctx->initialized) return -1;
    if (len == 0) return 0;

    uint8_t keystream[16];
    uint32_t offset = 0;

    while (offset < len) {
        /* 生成密钥流：AES(K, counter) */
        aes_encrypt_block(&ctx->aes_ctx, ctx->counter, keystream);
        increment_counter(ctx->counter);

        uint32_t block_len = len - offset;
        if (block_len > 16) block_len = 16;

        /* XOR加密 */
        for (uint32_t i = 0; i < block_len; i++) {
            output[offset + i] = input[offset + i] ^ keystream[i];
        }

        /* GHASH更新（用密文） */
        ghash_update(ctx, output + offset, block_len);
        ctx->data_len += (uint64_t)block_len * 8;

        offset += block_len;
    }

    return 0;
}

/* GCM解密 */
int gcm_decrypt(gcm_ctx_t *ctx, const uint8_t *input, uint8_t *output, uint32_t len)
{
    if (!ctx || !ctx->initialized) return -1;
    if (len == 0) return 0;

    uint8_t keystream[16];
    uint32_t offset = 0;

    while (offset < len) {
        /* 先GHASH更新（用密文） */
        uint32_t block_len = len - offset;
        if (block_len > 16) block_len = 16;
        ghash_update(ctx, input + offset, block_len);
        ctx->data_len += (uint64_t)block_len * 8;

        /* 生成密钥流：AES(K, counter) */
        aes_encrypt_block(&ctx->aes_ctx, ctx->counter, keystream);
        increment_counter(ctx->counter);

        /* XOR解密 */
        for (uint32_t i = 0; i < block_len; i++) {
            output[offset + i] = input[offset + i] ^ keystream[i];
        }

        offset += block_len;
    }

    return 0;
}

/* 完成GCM操作，生成标签 */
int gcm_final(gcm_ctx_t *ctx, uint8_t *tag, int tag_len)
{
    if (!ctx || !ctx->initialized || !tag) return -1;
    if (tag_len < 4 || tag_len > 16) return -1;

    /* 填充AAD到16字节边界 */
    uint32_t aad_pad = (16 - (ctx->aad_len / 8 % 16)) % 16;
    if (aad_pad > 0) {
        uint8_t zeros[16] = {0};
        ghash_update(ctx, zeros, aad_pad);
    }

    /* 填充密文到16字节边界 */
    uint32_t ct_pad = (16 - (ctx->data_len / 8 % 16)) % 16;
    if (ct_pad > 0) {
        uint8_t zeros[16] = {0};
        ghash_update(ctx, zeros, ct_pad);
    }

    /* 追加长度块：[len(AAD)]_64 || [len(CT)]_64 */
    uint8_t len_block[16];
    for (int i = 0; i < 8; i++) {
        len_block[7 - i] = (ctx->aad_len >> (i * 8)) & 0xFF;
        len_block[15 - i] = (ctx->data_len >> (i * 8)) & 0xFF;
    }
    ghash_update(ctx, len_block, 16);

    /* 标签 = GHASH XOR AES(K, J0) */
    uint8_t j0_enc[16];
    aes_encrypt_block(&ctx->aes_ctx, ctx->J0, j0_enc);
    for (int i = 0; i < 16; i++) {
        ctx->ghash[i] ^= j0_enc[i];
    }

    memcpy(tag, ctx->ghash, tag_len);

    return 0;
}

/* 恒定时间标签比较 */
int gcm_verify_tag(const uint8_t *tag1, const uint8_t *tag2, int len)
{
    if (!tag1 || !tag2 || len <= 0) return -1;

    uint8_t diff = 0;
    for (int i = 0; i < len; i++) {
        diff |= tag1[i] ^ tag2[i];
    }
    return diff == 0 ? 0 : -1;
}

/* 便捷接口：一次性GCM加密 */
int aes_gcm_encrypt(const uint8_t *key, int key_size,
                    const uint8_t *iv, int iv_len,
                    const uint8_t *aad, uint32_t aad_len,
                    const uint8_t *plaintext, uint8_t *ciphertext, uint32_t pt_len,
                    uint8_t *tag, int tag_len)
{
    gcm_ctx_t ctx;
    if (gcm_init(&ctx, key, key_size, iv, iv_len) != 0) return -1;
    if (aad_len > 0) gcm_update_aad(&ctx, aad, aad_len);
    if (pt_len > 0) gcm_encrypt(&ctx, plaintext, ciphertext, pt_len);
    return gcm_final(&ctx, tag, tag_len);
}

/* 便捷接口：一次性GCM解密 */
int aes_gcm_decrypt(const uint8_t *key, int key_size,
                    const uint8_t *iv, int iv_len,
                    const uint8_t *aad, uint32_t aad_len,
                    const uint8_t *ciphertext, uint8_t *plaintext, uint32_t ct_len,
                    const uint8_t *tag, int tag_len)
{
    gcm_ctx_t ctx;
    uint8_t computed_tag[16];

    if (gcm_init(&ctx, key, key_size, iv, iv_len) != 0) return -1;
    if (aad_len > 0) gcm_update_aad(&ctx, aad, aad_len);
    if (ct_len > 0) gcm_decrypt(&ctx, ciphertext, plaintext, ct_len);
    if (gcm_final(&ctx, computed_tag, tag_len) != 0) return -1;

    /* 恒定时间标签比较 */
    return gcm_verify_tag(computed_tag, tag, tag_len);
}
