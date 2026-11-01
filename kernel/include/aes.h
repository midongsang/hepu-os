#ifndef AES_H
#define AES_H

#include <stdint.h>

#define AES_BLOCK_SIZE 16
#define AES_KEY_SIZE_128 16
#define AES_KEY_SIZE_192 24
#define AES_KEY_SIZE_256 32
#define AES_MAX_ROUNDS 14
#define AES_MAX_EXP_KEY_SIZE 240

/* AES上下文 */
typedef struct {
    uint32_t round_key[AES_MAX_EXP_KEY_SIZE / 4];
    int rounds;
    int key_size;
} aes_ctx_t;

void aes_init(void);
int aes_set_key(aes_ctx_t *ctx, const uint8_t *key, int key_size);
int aes_encrypt_block(const aes_ctx_t *ctx, const uint8_t input[16], uint8_t output[16]);
int aes_decrypt_block(const aes_ctx_t *ctx, const uint8_t input[16], uint8_t output[16]);
int aes_encrypt_cbc(const aes_ctx_t *ctx, const uint8_t *iv,
                    const uint8_t *input, uint8_t *output, uint32_t length);
int aes_decrypt_cbc(const aes_ctx_t *ctx, const uint8_t *iv,
                    const uint8_t *input, uint8_t *output, uint32_t length);

/* V7.74: AES-GCM (AEAD) - NIST SP 800-38D */
#define GCM_IV_SIZE 12
#define GCM_TAG_SIZE 16

typedef struct {
    aes_ctx_t aes_ctx;
    uint8_t H[16];       /* GHASH哈希密钥 = AES(K, 0) */
    uint8_t J0[16];      /* 初始计数器 */
    uint8_t counter[16]; /* 当前计数器 */
    uint8_t ghash[16];   /* GHASH累加器 */
    uint64_t aad_len;    /* AAD长度（位） */
    uint64_t data_len;   /* 密文长度（位） */
    int initialized;
} gcm_ctx_t;

int gcm_init(gcm_ctx_t *ctx, const uint8_t *key, int key_size,
             const uint8_t *iv, int iv_len);
int gcm_update_aad(gcm_ctx_t *ctx, const uint8_t *aad, uint32_t len);
int gcm_encrypt(gcm_ctx_t *ctx, const uint8_t *input, uint8_t *output, uint32_t len);
int gcm_decrypt(gcm_ctx_t *ctx, const uint8_t *input, uint8_t *output, uint32_t len);
int gcm_final(gcm_ctx_t *ctx, uint8_t *tag, int tag_len);
int gcm_verify_tag(const uint8_t *tag1, const uint8_t *tag2, int len);

/* 便捷接口：一次性GCM加密/解密 */
int aes_gcm_encrypt(const uint8_t *key, int key_size,
                    const uint8_t *iv, int iv_len,
                    const uint8_t *aad, uint32_t aad_len,
                    const uint8_t *plaintext, uint8_t *ciphertext, uint32_t pt_len,
                    uint8_t *tag, int tag_len);
int aes_gcm_decrypt(const uint8_t *key, int key_size,
                    const uint8_t *iv, int iv_len,
                    const uint8_t *aad, uint32_t aad_len,
                    const uint8_t *ciphertext, uint8_t *plaintext, uint32_t ct_len,
                    const uint8_t *tag, int tag_len);

#endif /* AES_H */
