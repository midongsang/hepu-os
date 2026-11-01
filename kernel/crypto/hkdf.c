/* ============================================================================
 * 赫普OS - HKDF密钥派生函数
 * RFC 5869: HMAC-based Extract-and-Expand Key Derivation Function
 * ========================================================================== */

#include <stdint.h>
#include "hkdf.h"
#include "hmac.h"
#include "vga.h"
#include "string.h"

static int hkdf_initialized = 0;

/* 初始化HKDF */
void hkdf_init(void)
{
    hkdf_initialized = 1;
    vga_puts("[HKDF] KDF init \n");
    vga_puts("[HKDF] RFC 5869, based on HMAC-SHA1/SHA256/SHA512\n");
}

/* HKDF-Extract: PRK = HMAC-Hash(salt, IKM) */
int hkdf_extract(sha_alg_t alg, const uint8_t *salt, uint32_t salt_len,
                 const uint8_t *ikm, uint32_t ikm_len,
                 uint8_t *prk, uint32_t *prk_len)
{
    if (!hkdf_initialized || !ikm || ikm_len == 0 || !prk) return -1;

    uint32_t hash_len = sha_digest_size(alg);

    /* 如果salt未提供，使用全零字符串 */
    uint8_t default_salt[64] = {0};
    const uint8_t *actual_salt;
    uint32_t actual_salt_len;

    if (!salt || salt_len == 0) {
        actual_salt = default_salt;
        actual_salt_len = hash_len;
    } else {
        actual_salt = salt;
        actual_salt_len = salt_len;
    }

    /* PRK = HMAC-Hash(salt, IKM) */
    int ret = hmac_compute(alg, actual_salt, actual_salt_len,
                           ikm, ikm_len, prk, prk_len);
    if (ret != 0) return -1;

    vga_printf("[HKDF] Extract done: PRK len =%d\n", *prk_len);
    return 0;
}

/* HKDF-Expand: OKM = T(1) || T(2) || ... || T(n) */
int hkdf_expand(sha_alg_t alg, const uint8_t *prk, uint32_t prk_len,
                const uint8_t *info, uint32_t info_len,
                uint8_t *okm, uint32_t okm_len)
{
    if (!hkdf_initialized || !prk || prk_len == 0 || !okm || okm_len == 0) return -1;

    uint32_t hash_len = sha_digest_size(alg);
    uint32_t n = (okm_len + hash_len - 1) / hash_len;  /* 向上取整 */

    if (n > 255) return -1;  /* RFC 5869限制 */

    uint8_t t[64];  /* 当前T(i)，最大SHA512 */
    uint32_t t_len = 0;
    uint32_t offset = 0;

    for (uint32_t i = 1; i <= n; i++) {
        /* T(i) = HMAC-Hash(PRK, T(i-1) || info || i) */
        hmac_ctx_t ctx;
        hmac_init_ctx(&ctx, alg, prk, prk_len);

        if (i > 1) {
            hmac_update(&ctx, t, t_len);
        }
        if (info && info_len > 0) {
            hmac_update(&ctx, info, info_len);
        }
        uint8_t counter = (uint8_t)i;
        hmac_update(&ctx, &counter, 1);
        hmac_final(&ctx, t, &t_len);

        /* 复制到输出 */
        uint32_t copy_len = okm_len - offset;
        if (copy_len > hash_len) copy_len = hash_len;
        memcpy(okm + offset, t, copy_len);
        offset += copy_len;
    }

    /* 清零临时缓冲区 */
    memset(t, 0, sizeof(t));

    vga_printf("[HKDF] Expand done: out len =%d\n", okm_len);
    return 0;
}

/* 完整HKDF: Extract + Expand */
int hkdf(sha_alg_t alg, const uint8_t *salt, uint32_t salt_len,
         const uint8_t *ikm, uint32_t ikm_len,
         const uint8_t *info, uint32_t info_len,
         uint8_t *okm, uint32_t okm_len)
{
    uint8_t prk[64];
    uint32_t prk_len;

    /* Extract */
    int ret = hkdf_extract(alg, salt, salt_len, ikm, ikm_len, prk, &prk_len);
    if (ret != 0) return ret;

    /* Expand */
    ret = hkdf_expand(alg, prk, prk_len, info, info_len, okm, okm_len);

    /* 清零PRK */
    memset(prk, 0, sizeof(prk));
    return ret;
}

/* HKDF-Expand-Label (TLS 1.3风格) */
int hkdf_expand_label(sha_alg_t alg, const uint8_t *secret, uint32_t secret_len,
                      const char *label, const uint8_t *context, uint32_t context_len,
                      uint8_t *output, uint32_t output_len)
{
    if (!secret || !label || !output) return -1;

    /* 构造HkdfLabel结构:
     * struct {
     *   uint16 length = output_len;
     *   opaque label<7..255> = "tls13 " + label;
     *   opaque context<0..255> = context;
     * } HkdfLabel;
     */
    uint8_t hkdf_label[256];
    uint32_t label_len = strlen(label);
    uint32_t full_label_len = 6 + label_len;  /* "tls13 " = 6字节 */

    if (full_label_len > 255 || context_len > 255) return -1;

    uint32_t offset = 0;

    /* length (2字节大端) */
    hkdf_label[offset++] = (output_len >> 8) & 0xFF;
    hkdf_label[offset++] = output_len & 0xFF;

    /* label: 长度前缀 + "tls13 " + label */
    hkdf_label[offset++] = (uint8_t)full_label_len;
    memcpy(hkdf_label + offset, "tls13 ", 6);
    offset += 6;
    memcpy(hkdf_label + offset, label, label_len);
    offset += label_len;

    /* context: 长度前缀 + context */
    hkdf_label[offset++] = (uint8_t)context_len;
    if (context && context_len > 0) {
        memcpy(hkdf_label + offset, context, context_len);
        offset += context_len;
    }

    return hkdf_expand(alg, secret, secret_len, hkdf_label, offset,
                       output, output_len);
}
