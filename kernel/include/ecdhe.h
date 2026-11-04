#ifndef ECDHE_H
#define ECDHE_H

#include <stdint.h>

#define ECDHE_MAX_KEY_SIZE 32
#define ECDHE_SHARED_SECRET_SIZE 32

/* 椭圆曲线类型 */
typedef enum {
    ECDHE_CURVE_SECP256R1 = 0,  /* NIST P-256 */
    ECDHE_CURVE_SECP384R1 = 1,  /* NIST P-384 */
    ECDHE_CURVE_SECP521R1 = 2,  /* NIST P-521 */
    ECDHE_CURVE_X25519 = 3      /* Curve25519 */
} ecdhe_curve_t;

/* 椭圆曲线点 */
typedef struct {
    uint8_t x[ECDHE_MAX_KEY_SIZE];
    uint8_t y[ECDHE_MAX_KEY_SIZE];
    int     infinity;
} ecdhe_point_t;

/* ECDHE密钥对 */
typedef struct {
    ecdhe_curve_t curve;
    uint8_t  private_key[ECDHE_MAX_KEY_SIZE];
    uint8_t  public_key[ECDHE_MAX_KEY_SIZE * 2 + 1];  /* 未压缩格式 */
    uint32_t key_size;
    int      valid;
} ecdhe_keypair_t;

/* ECDHE会话 */
typedef struct {
    ecdhe_curve_t curve;
    ecdhe_keypair_t local_keypair;
    uint8_t  peer_public_key[ECDHE_MAX_KEY_SIZE * 2 + 1];
    uint32_t peer_key_size;
    uint8_t  shared_secret[ECDHE_SHARED_SECRET_SIZE];
    int      shared_secret_computed;
    int      initialized;
} ecdhe_session_t;

void ecdhe_init(void);
int ecdhe_generate_keypair(ecdhe_keypair_t *keypair, ecdhe_curve_t curve);
int ecdhe_compute_shared_secret(const ecdhe_keypair_t *local,
                                 const uint8_t *peer_public,
                                 uint32_t peer_len,
                                 uint8_t *shared_secret,
                                 uint32_t *secret_len);
int ecdhe_session_init(ecdhe_session_t *session, ecdhe_curve_t curve);
int ecdhe_session_set_peer_key(ecdhe_session_t *session,
                                const uint8_t *peer_key, uint32_t len);
int ecdhe_session_get_shared_secret(ecdhe_session_t *session,
                                     uint8_t *secret, uint32_t *len);
int ecdhe_get_public_key(const ecdhe_keypair_t *keypair,
                          uint8_t *pub_key, uint32_t *pub_len);
uint32_t ecdhe_get_key_size(ecdhe_curve_t curve);
const char *ecdhe_curve_name(ecdhe_curve_t curve);

#endif /* ECDHE_H */
