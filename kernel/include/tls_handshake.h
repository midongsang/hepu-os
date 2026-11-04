#ifndef TLS_HANDSHAKE_H
#define TLS_HANDSHAKE_H

#include <stdint.h>
#include "ecdhe.h"
#include "x509.h"
#include "rsa.h"
#include "sha.h"

#define TLS_HANDSHAKE_MAX_MSG 16384
#define TLS_MAX_CIPHER_SUITES 32
#define TLS_MAX_EXTENSIONS 16

/* TLS版本 */
#define TLS_VERSION_1_2 0x0303
#define TLS_VERSION_1_3 0x0304

/* 握手状态 */
typedef enum {
    TLS_HS_INIT = 0,
    TLS_HS_CLIENT_HELLO_SENT = 1,
    TLS_HS_SERVER_HELLO_RECEIVED = 2,
    TLS_HS_CERTIFICATE_RECEIVED = 3,
    TLS_HS_KEY_EXCHANGE_RECEIVED = 4,
    TLS_HS_SERVER_HELLO_DONE_RECEIVED = 5,
    TLS_HS_CLIENT_KEY_EXCHANGE_SENT = 6,
    TLS_HS_CHANGE_CIPHER_SPEC_SENT = 7,
    TLS_HS_FINISHED_SENT = 8,
    TLS_HS_FINISHED_RECEIVED = 9,
    TLS_HS_COMPLETE = 10,
    TLS_HS_FAILED = 11
} hepu_tls_hs_state_t;

/* 密码套件 */
typedef enum {
    TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 = 0xC02F,
    TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384 = 0xC030,
    TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 = 0xC02B,
    TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384 = 0xC02C,
    TLS_RSA_WITH_AES_128_GCM_SHA256 = 0x009C,
    TLS_RSA_WITH_AES_256_GCM_SHA384 = 0x009D,
    TLS_AES_128_GCM_SHA256 = 0x1301,
    TLS_AES_256_GCM_SHA384 = 0x1302,
    TLS_CHACHA20_POLY1305_SHA256 = 0x1303
} hepu_tls_cipher_t;

/* TLS扩展类型 */
typedef enum {
    TLS_EXT_SERVER_NAME = 0,
    TLS_EXT_STATUS_REQUEST = 5,
    TLS_EXT_SUPPORTED_GROUPS = 10,
    TLS_EXT_SIGNATURE_ALGORITHMS = 13,
    TLS_EXT_ALPN = 16,
    TLS_EXT_KEY_SHARE = 51,
    TLS_EXT_SUPPORTED_VERSIONS = 43,
    TLS_EXT_PSK_KEY_EXCHANGE_MODES = 45
} hepu_tls_ext_t;

/* TLS握手上下文 */
typedef struct {
    hepu_tls_hs_state_t state;
    uint16_t version;
    uint16_t cipher_suite;
    uint8_t  client_random[32];
    uint8_t  server_random[32];
    ecdhe_keypair_t client_keypair;
    uint8_t  server_public_key[65];
    uint32_t server_key_len;
    uint8_t  master_secret[48];
    uint8_t  client_write_key[32];
    uint8_t  server_write_key[32];
    uint8_t  client_iv[16];
    uint8_t  server_iv[16];
    x509_cert_t server_cert;
    x509_cert_t ca_cert;
    int      cert_validated;
    uint8_t  transcript_hash[64];
    uint32_t transcript_len;
    int      is_tls13;
    int      handshake_complete;
    /* V7.81: Finished消息验证数据 */
    uint8_t  server_finished_verify[32];
    uint32_t server_finished_len;
    /* V7.83: 增量transcript SHA256上下文
     * TLS 1.3要求从ClientHello开始累积所有握手消息的哈希 */
    sha256_ctx_t transcript_ctx;
    int      transcript_ctx_initialized;
    /* V7.84: Finished验证密钥
     * finished_key = HKDF-Expand-Label(handshake_secret, "finished", "", 32) */
    uint8_t  finished_key[32];
    int      finished_verified;
} hepu_tls_hs_ctx_t;

void tls_handshake_init(void);
int tls_handshake_start(hepu_tls_hs_ctx_t *ctx, const char *hostname);
int tls_handshake_process(hepu_tls_hs_ctx_t *ctx, const uint8_t *data,
                          uint32_t len, uint8_t *response, uint32_t *response_len);
int tls_handshake_is_complete(const hepu_tls_hs_ctx_t *ctx);
int tls_handshake_get_keys(const hepu_tls_hs_ctx_t *ctx,
                           uint8_t *client_key, uint8_t *server_key,
                           uint8_t *client_iv, uint8_t *server_iv);
int tls_handshake_verify_certificate(hepu_tls_hs_ctx_t *ctx,
                                     const uint8_t *cert_der, uint32_t cert_len);
int tls_handshake_derive_keys(hepu_tls_hs_ctx_t *ctx);
const char *tls_handshake_state_name(hepu_tls_hs_state_t state);
const char *tls_cipher_suite_name(uint16_t suite);

#endif /* TLS_HANDSHAKE_H */
