#ifndef RSA_H
#define RSA_H

#include <stdint.h>

#define RSA_MAX_KEY_BITS 2048
#define RSA_MAX_KEY_WORDS (RSA_MAX_KEY_BITS / 32)  /* 64个32位字 */
#define RSA_MAX_KEY_BYTES (RSA_MAX_KEY_BITS / 8)   /* 256字节 */

/* RSA公钥 */
typedef struct {
    uint32_t n[RSA_MAX_KEY_WORDS];  /* 模数 */
    uint32_t e[RSA_MAX_KEY_WORDS];  /* 公钥指数 */
    uint32_t n_len;  /* n的字数 */
    uint32_t e_len;  /* e的字数 */
    uint32_t bits;   /* 密钥位数 */
    int      valid;
} rsa_public_key_t;

/* RSA签名算法 */
typedef enum {
    RSA_PKCS1_SHA1 = 0,
    RSA_PKCS1_SHA256 = 1,
    RSA_PKCS1_SHA384 = 2,
    RSA_PKCS1_SHA512 = 3
} rsa_sig_alg_t;

void rsa_init(void);
int rsa_parse_public_key(rsa_public_key_t *key, const uint8_t *der, uint32_t len);
int rsa_verify_signature(const rsa_public_key_t *key,
                         const uint8_t *signature, uint32_t sig_len,
                         const uint8_t *digest, uint32_t digest_len,
                         rsa_sig_alg_t alg);
int rsa_encrypt(const rsa_public_key_t *key,
                const uint8_t *plaintext, uint32_t plaintext_len,
                uint8_t *ciphertext, uint32_t *ciphertext_len);
uint32_t rsa_get_key_size(const rsa_public_key_t *key);
const char *rsa_algorithm_name(rsa_sig_alg_t alg);

#endif /* RSA_H */
