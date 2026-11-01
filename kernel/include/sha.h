#ifndef SHA_H
#define SHA_H

#include <stdint.h>

#define SHA1_DIGEST_SIZE 20
#define SHA1_BLOCK_SIZE 64
#define SHA256_DIGEST_SIZE 32
#define SHA256_BLOCK_SIZE 64
#define SHA512_DIGEST_SIZE 64
#define SHA512_BLOCK_SIZE 128

/* SHA算法类型 */
typedef enum {
    SHA_ALG_SHA1 = 0,
    SHA_ALG_SHA224 = 1,
    SHA_ALG_SHA256 = 2,
    SHA_ALG_SHA384 = 3,
    SHA_ALG_SHA512 = 4
} sha_alg_t;

/* SHA1上下文 */
typedef struct {
    uint32_t state[5];
    uint64_t count;
    uint8_t  buffer[SHA1_BLOCK_SIZE];
} sha1_ctx_t;

/* SHA256上下文 */
typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buffer[SHA256_BLOCK_SIZE];
} sha256_ctx_t;

/* SHA512上下文 */
typedef struct {
    uint64_t state[8];
    uint64_t count;
    uint8_t  buffer[SHA512_BLOCK_SIZE];
} sha512_ctx_t;

/* 统一哈希上下文 */
typedef struct {
    sha_alg_t alg;
    union {
        sha1_ctx_t sha1;
        sha256_ctx_t sha256;
        sha512_ctx_t sha512;
    } ctx;
} sha_ctx_t;

void sha_init(void);

/* SHA1 */
void sha1_init(sha1_ctx_t *ctx);
void sha1_update(sha1_ctx_t *ctx, const uint8_t *data, uint32_t len);
void sha1_final(sha1_ctx_t *ctx, uint8_t *digest);
int sha1(const uint8_t *data, uint32_t len, uint8_t *digest);

/* SHA256 */
void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t *digest);
int sha256(const uint8_t *data, uint32_t len, uint8_t *digest);

/* SHA512 */
void sha512_init(sha512_ctx_t *ctx);
void sha512_update(sha512_ctx_t *ctx, const uint8_t *data, uint32_t len);
void sha512_final(sha512_ctx_t *ctx, uint8_t *digest);
int sha512(const uint8_t *data, uint32_t len, uint8_t *digest);

/* 统一接口 */
int sha_init_ctx(sha_ctx_t *ctx, sha_alg_t alg);
int sha_update_ctx(sha_ctx_t *ctx, const uint8_t *data, uint32_t len);
int sha_final_ctx(sha_ctx_t *ctx, uint8_t *digest);
int sha_compute(sha_alg_t alg, const uint8_t *data, uint32_t len, uint8_t *digest);
uint32_t sha_digest_size(sha_alg_t alg);
const char *sha_algorithm_name(sha_alg_t alg);

#endif /* SHA_H */
