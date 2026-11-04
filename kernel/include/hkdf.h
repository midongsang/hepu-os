#ifndef HKDF_H
#define HKDF_H

#include <stdint.h>
#include "sha.h"

#define HKDF_MAX_OUTPUT_SIZE 255 * 64  /* 最大输出：255 * HashLen */
#define HKDF_MAX_SALT_SIZE 128
#define HKDF_MAX_INFO_SIZE 128

/* HKDF上下文 */
typedef struct {
    sha_alg_t alg;
    uint8_t  prk[64];  /* 伪随机密钥，最大SHA512输出 */
    uint32_t prk_len;
    int      extracted;
} hkdf_ctx_t;

void hkdf_init(void);
int hkdf_extract(sha_alg_t alg, const uint8_t *salt, uint32_t salt_len,
                 const uint8_t *ikm, uint32_t ikm_len,
                 uint8_t *prk, uint32_t *prk_len);
int hkdf_expand(sha_alg_t alg, const uint8_t *prk, uint32_t prk_len,
                const uint8_t *info, uint32_t info_len,
                uint8_t *okm, uint32_t okm_len);
int hkdf(sha_alg_t alg, const uint8_t *salt, uint32_t salt_len,
         const uint8_t *ikm, uint32_t ikm_len,
         const uint8_t *info, uint32_t info_len,
         uint8_t *okm, uint32_t okm_len);
int hkdf_expand_label(sha_alg_t alg, const uint8_t *secret, uint32_t secret_len,
                      const char *label, const uint8_t *context, uint32_t context_len,
                      uint8_t *output, uint32_t output_len);

#endif /* HKDF_H */
