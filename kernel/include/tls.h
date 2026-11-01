#ifndef TLS_H
#define TLS_H

#include <stdint.h>

#define TLS_VERSION_1_0 0x0301
#define TLS_VERSION_1_1 0x0302
#define TLS_VERSION_1_2 0x0303
#define TLS_VERSION_1_3 0x0304

#define TLS_RECORD_CHANGE_CIPHER_SPEC 20
#define TLS_RECORD_ALERT             21
#define TLS_RECORD_HANDSHAKE         22
#define TLS_RECORD_APPLICATION_DATA  23

#define TLS_HANDSHAKE_HELLO_REQUEST       0
#define TLS_HANDSHAKE_CLIENT_HELLO        1
#define TLS_HANDSHAKE_SERVER_HELLO        2
#define TLS_HANDSHAKE_CERTIFICATE         11
#define TLS_HANDSHAKE_SERVER_KEY_EXCHANGE 12
#define TLS_HANDSHAKE_CERTIFICATE_REQUEST 13
#define TLS_HANDSHAKE_SERVER_HELLO_DONE   14
#define TLS_HANDSHAKE_CERTIFICATE_VERIFY  15
#define TLS_HANDSHAKE_CLIENT_KEY_EXCHANGE 16
#define TLS_HANDSHAKE_FINISHED            20

#define TLS_ALERT_WARNING 1
#define TLS_ALERT_FATAL   2

#define TLS_ALERT_CLOSE_NOTIFY        0
#define TLS_ALERT_UNEXPECTED_MESSAGE  10
#define TLS_ALERT_BAD_RECORD_MAC      20
#define TLS_ALERT_HANDSHAKE_FAILURE   40
#define TLS_ALERT_BAD_CERTIFICATE     42
#define TLS_ALERT_UNSUPPORTED_CERT    43
#define TLS_ALERT_CERTIFICATE_REVOKED 44
#define TLS_ALERT_CERTIFICATE_EXPIRED 45
#define TLS_ALERT_UNKNOWN_CA          48
#define TLS_ALERT_INTERNAL_ERROR      80

/* TLS记录头 */
typedef struct {
    uint8_t  type;
    uint16_t version;
    uint16_t length;
} __attribute__((packed)) tls_record_header_t;

/* TLS握手头 */
typedef struct {
    uint8_t  type;
    uint8_t  length[3];
} __attribute__((packed)) tls_handshake_header_t;

/* TLS会话状态 */
typedef enum {
    TLS_STATE_INIT = 0,
    TLS_STATE_CLIENT_HELLO_SENT = 1,
    TLS_STATE_SERVER_HELLO_RECEIVED = 2,
    TLS_STATE_CERTIFICATE_RECEIVED = 3,
    TLS_STATE_KEY_EXCHANGE_DONE = 4,
    TLS_STATE_HANDSHAKE_DONE = 5,
    TLS_STATE_APPLICATION_DATA = 6,
    TLS_STATE_CLOSED = 7
} hepu_tls_state_t;

/* TLS密码套件 */
typedef enum {
    TLS_CIPHER_RSA_WITH_AES_128_CBC_SHA = 0x002F,
    TLS_CIPHER_RSA_WITH_AES_256_CBC_SHA = 0x0035,
    TLS_CIPHER_RSA_WITH_AES_128_GCM_SHA256 = 0x009C,
    TLS_CIPHER_RSA_WITH_AES_256_GCM_SHA384 = 0x009D,
    TLS_CIPHER_ECDHE_RSA_WITH_AES_128_GCM_SHA256 = 0xC02F,
    TLS_CIPHER_ECDHE_RSA_WITH_AES_256_GCM_SHA384 = 0xC030
} tls_cipher_suite_t;

/* TLS会话 */
typedef struct {
    hepu_tls_state_t state;
    uint16_t version;
    tls_cipher_suite_t cipher_suite;
    uint8_t  client_random[32];
    uint8_t  server_random[32];
    uint8_t  master_secret[48];
    uint8_t  client_write_key[32];
    uint8_t  server_write_key[32];
    uint8_t  client_write_iv[16];
    uint8_t  server_write_iv[16];
    uint64_t client_seq;
    uint64_t server_seq;
    int      encrypted;
    char     server_name[128];
    uint16_t server_port;
} hepu_tls_session_t;

void hepu_tls_init(void);
int hepu_tls_connect(hepu_tls_session_t *session, const char *hostname, uint16_t port);
int hepu_tls_close(hepu_tls_session_t *session);
int hepu_tls_send(hepu_tls_session_t *session, const uint8_t *data, uint32_t length);
int hepu_tls_receive(hepu_tls_session_t *session, uint8_t *buffer, uint32_t max_length);
int hepu_tls_handshake(hepu_tls_session_t *session);
int hepu_tls_get_state(const hepu_tls_session_t *session, hepu_tls_state_t *state);
const char *hepu_tls_alert_text(uint8_t alert);

#endif /* TLS_H */
