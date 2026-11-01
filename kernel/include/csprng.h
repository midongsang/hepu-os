#ifndef CSPRNG_H
#define CSPRNG_H

#include <stdint.h>

#define CSPRNG_SEED_SIZE 32
#define CSPRNG_STATE_SIZE 64

/* CSPRNG算法 */
typedef enum {
    CSPRNG_ALG_CHACHA20 = 0,
    CSPRNG_ALG_AES_CTR = 1,
    CSPRNG_ALG_HASH_DRBG = 2
} csprng_alg_t;

/* CSPRNG状态 */
typedef struct {
    uint8_t  state[CSPRNG_STATE_SIZE];
    uint8_t  seed[CSPRNG_SEED_SIZE];
    uint64_t counter;
    uint64_t reseed_counter;
    uint64_t reseed_interval;
    csprng_alg_t algorithm;
    int      initialized;
    int      health_check_passed;
} csprng_state_t;

void csprng_init(void);
int csprng_seed(const uint8_t *seed, uint32_t len);
int csprng_reseed(const uint8_t *additional_data, uint32_t len);
int csprng_get_bytes(uint8_t *buffer, uint32_t len);
uint32_t csprng_get_uint32(void);
uint64_t csprng_get_uint64(void);
int csprng_get_random_range(uint32_t min, uint32_t max, uint32_t *result);
int csprng_health_check(void);
int csprng_get_entropy(uint8_t *buffer, uint32_t len);
void csprng_add_entropy(const uint8_t *data, uint32_t len);
const char *csprng_algorithm_name(csprng_alg_t alg);

#endif /* CSPRNG_H */
