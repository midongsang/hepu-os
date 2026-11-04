/* ============================================================================
 * 赫普OS - TLS 1.3 记录层加密/解密 头文件
 * V7.85: 初始实现
 * ========================================================================== */

#ifndef TLS_RECORD_H
#define TLS_RECORD_H

#include <stdint.h>

/* TLS记录层上下文 */
typedef struct {
    int initialized;
    int keys_set;

    /* 客户端写密钥和IV */
    uint8_t client_write_key[32];
    uint32_t client_key_len;
    uint8_t client_write_iv[12];
    uint32_t client_iv_len;

    /* 服务器写密钥和IV */
    uint8_t server_write_key[32];
    uint32_t server_key_len;
    uint8_t server_write_iv[12];
    uint32_t server_iv_len;

    /* 序列号 */
    uint64_t seq_num_client;
    uint64_t seq_num_server;
} tls_record_ctx_t;

/* 初始化TLS记录层上下文 */
void tls_record_init(tls_record_ctx_t *ctx);

/* 设置加密密钥（从握手派生的密钥） */
int tls_record_set_keys(tls_record_ctx_t *ctx,
                        const uint8_t *client_key, uint32_t client_key_len,
                        const uint8_t *server_key, uint32_t server_key_len,
                        const uint8_t *client_iv, uint32_t client_iv_len,
                        const uint8_t *server_iv, uint32_t server_iv_len);

/* 加密TLS记录（客户端发送）
 * 输入：plaintext, plaintext_len
 * 输出：record（包含记录头+密文+tag）, record_len */
int tls_record_encrypt(tls_record_ctx_t *ctx,
                       const uint8_t *plaintext, uint32_t plaintext_len,
                       uint8_t *record, uint32_t *record_len,
                       uint32_t record_buf_size);

/* 解密TLS记录（服务器接收）
 * 输入：record（包含记录头+密文+tag）, record_len
 * 输出：plaintext, plaintext_len */
int tls_record_decrypt(tls_record_ctx_t *ctx,
                       const uint8_t *record, uint32_t record_len,
                       uint8_t *plaintext, uint32_t *plaintext_len,
                       uint32_t plaintext_buf_size);

/* 获取当前序列号（调试用） */
uint64_t tls_record_get_seq_client(const tls_record_ctx_t *ctx);
uint64_t tls_record_get_seq_server(const tls_record_ctx_t *ctx);

/* 重置序列号（密钥更新时使用） */
void tls_record_reset_seq(tls_record_ctx_t *ctx);

#endif /* TLS_RECORD_H */
