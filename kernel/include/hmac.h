#ifndef HMAC_H
#define HMAC_H

#include <stdint.h>
#include "sha.h"

#define HMAC_MAX_KEY_SIZE 128
#define HMAC_MAX_DIGEST_SIZE 64

/* HMAC上下文 */
typedef struct {
    sha_alg_t alg;
    uint8_t  key[HMAC_MAX_KEY_SIZE];
    uint32_t key_len;
    sha_ctx_t inner_ctx;
    sha_ctx_t outer_ctx;
    int      initialized;
} hmac_ctx_t;

void hmac_init(void);
int hmac_init_ctx(hmac_ctx_t *ctx, sha_alg_t alg, const uint8_t *key, uint32_t key_len);
int hmac_update(hmac_ctx_t *ctx, const uint8_t *data, uint32_t len);
int hmac_final(hmac_ctx_t *ctx, uint8_t *mac, uint32_t *mac_len);
int hmac_compute(sha_alg_t alg, const uint8_t *key, uint32_t key_len,
                 const uint8_t *data, uint32_t data_len,
                 uint8_t *mac, uint32_t *mac_len);
int hmac_verify(sha_alg_t alg, const uint8_t *key, uint32_t key_len,
                const uint8_t *data, uint32_t data_len,
                const uint8_t *expected_mac, uint32_t mac_len);

#endif /* HMAC_H */
