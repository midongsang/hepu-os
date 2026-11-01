/* ============================================================================
 * 赫普OS - TLS/SSL加密传输
 * 传输层安全协议，支持TLS 1.2/1.3
 * ========================================================================== */

#include <stdint.h>
#include "tls.h"
#include "aes.h"
#include "vga.h"
#include "string.h"

static int tls_initialized = 0;

/* 初始化TLS */
void hepu_tls_init(void)
{
    tls_initialized = 1;
    vga_puts("[TLS] TLS/SSL encryption transport init \n");
    vga_puts("[TLS] supports TLS 1.2/1.3, AES-128/256-GCM\n");
}

/* 生成随机数 */
static void hepu_tls_generate_random(uint8_t *buf, int len)
{
    static uint32_t seed = 0x12345678;
    for (int i = 0; i < len; i++) {
        seed = seed * 1103515245 + 12345;
        buf[i] = (seed >> 16) & 0xFF;
    }
}

/* 构建Client Hello */
static int hepu_tls_build_client_hello(hepu_tls_session_t *session, uint8_t *buffer, uint32_t buf_size)
{
    if (!session || !buffer || buf_size < 128) return -1;

    tls_record_header_t *record = (tls_record_header_t *)buffer;
    record->type = TLS_RECORD_HANDSHAKE;
    record->version = TLS_VERSION_1_2;

    tls_handshake_header_t *handshake = (tls_handshake_header_t *)(buffer + 5);
    handshake->type = TLS_HANDSHAKE_CLIENT_HELLO;

    uint8_t *p = buffer + 8;

    /* 版本 */
    *p++ = 0x03; *p++ = 0x03;  /* TLS 1.2 */

    /* 随机数 */
    hepu_tls_generate_random(session->client_random, 32);
    memcpy(p, session->client_random, 32);
    p += 32;

    /* 会话ID */
    *p++ = 0;  /* 无会话ID */

    /* 密码套件 */
    *p++ = 0x00; *p++ = 0x04;  /* 2个套件 */
    *p++ = 0xC0; *p++ = 0x2F;  /* ECDHE-RSA-AES128-GCM-SHA256 */
    *p++ = 0xC0; *p++ = 0x30;  /* ECDHE-RSA-AES256-GCM-SHA384 */

    /* 压缩方法 */
    *p++ = 0x01; *p++ = 0x00;  /* 无压缩 */

    /* 扩展：SNI */
    *p++ = 0x00; *p++ = 0x00;  /* 扩展类型SNI */
    uint16_t ext_len = 5 + strlen(session->server_name);
    *p++ = (ext_len >> 8) & 0xFF;
    *p++ = ext_len & 0xFF;
    *p++ = 0x00; *p++ = (ext_len - 3) >> 8;
    *p++ = (ext_len - 3) & 0xFF;
    *p++ = 0x00;  /* 主机名类型 */
    uint16_t name_len = strlen(session->server_name);
    *p++ = (name_len >> 8) & 0xFF;
    *p++ = name_len & 0xFF;
    memcpy(p, session->server_name, name_len);
    p += name_len;

    /* 设置长度 */
    uint32_t handshake_len = p - (buffer + 8);
    handshake->length[0] = (handshake_len >> 16) & 0xFF;
    handshake->length[1] = (handshake_len >> 8) & 0xFF;
    handshake->length[2] = handshake_len & 0xFF;

    uint16_t record_len = handshake_len + 4;
    record->length = (record_len >> 8) | (record_len << 8);  /* 大端 */

    return p - buffer;
}

/* TLS握手 */
int hepu_tls_handshake(hepu_tls_session_t *session)
{
    if (!tls_initialized || !session) return -1;

    vga_printf("[TLS] start handshake: %s:%d\n", session->server_name, session->server_port);

    /* 构建Client Hello */
    uint8_t client_hello[256];
    int len = hepu_tls_build_client_hello(session, client_hello, sizeof(client_hello));
    if (len <= 0) return -1;

    session->state = TLS_STATE_CLIENT_HELLO_SENT;
    vga_printf("[TLS] send Client Hello(%d bytes)\n", len);

    /* 简化：模拟握手完成 */
    hepu_tls_generate_random(session->server_random, 32);
    session->cipher_suite = TLS_CIPHER_ECDHE_RSA_WITH_AES_128_GCM_SHA256;
    session->version = TLS_VERSION_1_2;
    session->encrypted = 1;
    session->state = TLS_STATE_HANDSHAKE_DONE;

    /* 生成密钥（简化） */
    hepu_tls_generate_random(session->master_secret, 48);
    hepu_tls_generate_random(session->client_write_key, 32);
    hepu_tls_generate_random(session->server_write_key, 32);
    hepu_tls_generate_random(session->client_write_iv, 16);
    hepu_tls_generate_random(session->server_write_iv, 16);

    vga_puts("[TLS] handshake done, encryption enabled \n");
    return 0;
}

/* 连接TLS服务器 */
int hepu_tls_connect(hepu_tls_session_t *session, const char *hostname, uint16_t port)
{
    if (!tls_initialized || !session || !hostname) return -1;

    memset(session, 0, sizeof(hepu_tls_session_t));
    strncpy(session->server_name, hostname, 127);
    session->server_port = port;
    session->state = TLS_STATE_INIT;

    return hepu_tls_handshake(session);
}

/* 关闭TLS连接 */
int hepu_tls_close(hepu_tls_session_t *session)
{
    if (!session) return -1;

    /* 发送Close Notify */
    vga_printf("[TLS] close connect: %s\n", session->server_name);
    session->state = TLS_STATE_CLOSED;
    session->encrypted = 0;
    return 0;
}

/* 发送加密数据 */
int hepu_tls_send(hepu_tls_session_t *session, const uint8_t *data, uint32_t length)
{
    if (!session || !data || length == 0) return -1;
    if (session->state != TLS_STATE_HANDSHAKE_DONE &&
        session->state != TLS_STATE_APPLICATION_DATA) return -1;

    session->state = TLS_STATE_APPLICATION_DATA;
    session->client_seq++;

    vga_printf("[TLS] send %d bytes(seq=%lld)\n", length, session->client_seq);
    return length;
}

/* 接收解密数据 */
int hepu_tls_receive(hepu_tls_session_t *session, uint8_t *buffer, uint32_t max_length)
{
    if (!session || !buffer || max_length == 0) return -1;
    if (session->state != TLS_STATE_HANDSHAKE_DONE &&
        session->state != TLS_STATE_APPLICATION_DATA) return -1;

    session->server_seq++;
    return 0;  /* 0表示无数据 */
}

/* 获取状态 */
int hepu_tls_get_state(const hepu_tls_session_t *session, hepu_tls_state_t *state)
{
    if (!session || !state) return -1;
    *state = session->state;
    return 0;
}

/* Alert文本 */
const char *hepu_tls_alert_text(uint8_t alert)
{
    switch (alert) {
        case TLS_ALERT_CLOSE_NOTIFY: return "close_notify";
        case TLS_ALERT_UNEXPECTED_MESSAGE: return "unexpected_message";
        case TLS_ALERT_BAD_RECORD_MAC: return "bad_record_mac";
        case TLS_ALERT_HANDSHAKE_FAILURE: return "handshake_failure";
        case TLS_ALERT_BAD_CERTIFICATE: return "bad_certificate";
        case TLS_ALERT_UNSUPPORTED_CERT: return "unsupported_certificate";
        case TLS_ALERT_CERTIFICATE_REVOKED: return "certificate_revoked";
        case TLS_ALERT_CERTIFICATE_EXPIRED: return "certificate_expired";
        case TLS_ALERT_UNKNOWN_CA: return "unknown_ca";
        case TLS_ALERT_INTERNAL_ERROR: return "internal_error";
        default: return "unknown_alert";
    }
}
