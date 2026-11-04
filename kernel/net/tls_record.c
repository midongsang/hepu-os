/* ============================================================================
 * 赫普OS - TLS 1.3 记录层加密/解密
 * 基于AES-GCM AEAD，支持TLS 1.3记录格式
 * V7.85: 初始实现，漏洞S（TLS握手真实化）完善
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "tls_record.h"
#include "aes.h"
#include "vga.h"

/* TLS 1.3记录类型 */
#define TLS_RECORD_APPLICATION_DATA 0x17
#define TLS_RECORD_ALERT            0x15
#define TLS_RECORD_HANDSHAKE        0x16

/* TLS 1.3记录头大小：type(1) + version(2) + length(2) = 5 */
#define TLS_RECORD_HEADER_SIZE 5

/* TLS 1.3 AEAD开销：IV(12)不传输，tag(16) + 内容类型(1) = 17 */
#define TLS_RECORD_AEAD_OVERHEAD 17

/* 最大记录大小：16KB + 256字节加密开销 */
#define TLS_MAX_RECORD_SIZE 16640

/* 初始化TLS记录层上下文 */
void tls_record_init(tls_record_ctx_t *ctx)
{
    if (!ctx) return;
    memset(ctx, 0, sizeof(tls_record_ctx_t));
    ctx->initialized = 1;
    ctx->seq_num_client = 0;
    ctx->seq_num_server = 0;
    vga_puts("[TLS-REC] TLS 1.3 record layer initialized\n");
}

/* 设置加密密钥 */
int tls_record_set_keys(tls_record_ctx_t *ctx,
                        const uint8_t *client_key, uint32_t client_key_len,
                        const uint8_t *server_key, uint32_t server_key_len,
                        const uint8_t *client_iv, uint32_t client_iv_len,
                        const uint8_t *server_iv, uint32_t server_iv_len)
{
    if (!ctx || !client_key || !server_key || !client_iv || !server_iv) return -1;
    if (client_key_len > 32 || server_key_len > 32) return -1;
    if (client_iv_len > 12 || server_iv_len > 12) return -1;

    memcpy(ctx->client_write_key, client_key, client_key_len);
    ctx->client_key_len = client_key_len;
    memcpy(ctx->server_write_key, server_key, server_key_len);
    ctx->server_key_len = server_key_len;
    memcpy(ctx->client_write_iv, client_iv, client_iv_len);
    ctx->client_iv_len = client_iv_len;
    memcpy(ctx->server_write_iv, server_iv, server_iv_len);
    ctx->server_iv_len = server_iv_len;

    ctx->seq_num_client = 0;
    ctx->seq_num_server = 0;
    ctx->keys_set = 1;

    vga_printf("[TLS-REC] keys set: client_key=%d bytes, server_key=%d bytes\n",
               client_key_len, server_key_len);
    return 0;
}

/* 构造TLS 1.3记录的nonce
 * nonce = IV XOR sequence_number (sequence_number左填充到IV长度) */
static void tls_record_build_nonce(const uint8_t *iv, uint32_t iv_len,
                                   uint64_t seq_num, uint8_t *nonce)
{
    memcpy(nonce, iv, iv_len);
    /* XOR sequence number到nonce的最后8字节 */
    for (int i = 0; i < 8 && i < iv_len; i++) {
        nonce[iv_len - 1 - i] ^= (seq_num >> (i * 8)) & 0xFF;
    }
}

/* 构造AAD（Additional Authenticated Data）
 * TLS 1.3 AAD = record_header (type + version + length) */
static void tls_record_build_aad(uint8_t type, uint16_t ciphertext_len,
                                 uint8_t *aad, uint32_t *aad_len)
{
    aad[0] = type;
    aad[1] = 0x03;  /* TLS 1.2 legacy version */
    aad[2] = 0x03;
    aad[3] = (ciphertext_len >> 8) & 0xFF;
    aad[4] = ciphertext_len & 0xFF;
    *aad_len = 5;
}

/* 加密TLS记录（客户端发送）
 * 输入：plaintext, plaintext_len
 * 输出：record（包含记录头+密文+tag）, record_len
 * 注意：TLS 1.3中，明文末尾追加内容类型字节（0x17 for ApplicationData） */
int tls_record_encrypt(tls_record_ctx_t *ctx,
                       const uint8_t *plaintext, uint32_t plaintext_len,
                       uint8_t *record, uint32_t *record_len,
                       uint32_t record_buf_size)
{
    if (!ctx || !ctx->initialized || !ctx->keys_set) return -1;
    if (!plaintext || plaintext_len == 0 || !record || !record_len) return -1;

    /* 计算需要的缓冲区大小：
     * 记录头(5) + 明文 + 内容类型(1) + tag(16) */
    uint32_t total_len = TLS_RECORD_HEADER_SIZE + plaintext_len + 1 + 16;
    if (record_buf_size < total_len) return -1;
    if (plaintext_len + 1 + 16 > TLS_MAX_RECORD_SIZE) return -1;

    /* 构造明文：原始明文 + 内容类型字节(0x17) */
    uint8_t *plaintext_with_type = record + TLS_RECORD_HEADER_SIZE;
    memcpy(plaintext_with_type, plaintext, plaintext_len);
    plaintext_with_type[plaintext_len] = TLS_RECORD_APPLICATION_DATA;
    uint32_t pt_len = plaintext_len + 1;

    /* 构造nonce */
    uint8_t nonce[12];
    tls_record_build_nonce(ctx->client_write_iv, ctx->client_iv_len,
                           ctx->seq_num_client, nonce);

    /* 构造AAD（使用加密后的长度） */
    uint16_t ciphertext_len = pt_len + 16;  /* 明文+tag */
    uint8_t aad[5];
    uint32_t aad_len;
    tls_record_build_aad(TLS_RECORD_APPLICATION_DATA, ciphertext_len, aad, &aad_len);

    /* AES-GCM加密 */
    uint8_t ciphertext[TLS_MAX_RECORD_SIZE];
    uint8_t tag[16];
    int ret = aes_gcm_encrypt(ctx->client_write_key, ctx->client_key_len,
                              nonce, 12,
                              aad, aad_len,
                              plaintext_with_type, ciphertext, pt_len,
                              tag, 16);
    if (ret != 0) {
        vga_puts("[TLS-REC] AES-GCM encrypt failed\n");
        return -1;
    }

    /* 构造记录头 */
    record[0] = TLS_RECORD_APPLICATION_DATA;
    record[1] = 0x03;
    record[2] = 0x03;
    record[3] = (ciphertext_len >> 8) & 0xFF;
    record[4] = ciphertext_len & 0xFF;

    /* 复制密文+tag */
    memcpy(record + TLS_RECORD_HEADER_SIZE, ciphertext, pt_len);
    memcpy(record + TLS_RECORD_HEADER_SIZE + pt_len, tag, 16);

    *record_len = TLS_RECORD_HEADER_SIZE + pt_len + 16;

    /* 递增序列号 */
    ctx->seq_num_client++;

    return 0;
}

/* 解密TLS记录（服务器接收）
 * 输入：record（包含记录头+密文+tag）, record_len
 * 输出：plaintext, plaintext_len */
int tls_record_decrypt(tls_record_ctx_t *ctx,
                       const uint8_t *record, uint32_t record_len,
                       uint8_t *plaintext, uint32_t *plaintext_len,
                       uint32_t plaintext_buf_size)
{
    if (!ctx || !ctx->initialized || !ctx->keys_set) return -1;
    if (!record || record_len < TLS_RECORD_HEADER_SIZE + 16) return -1;
    if (!plaintext || !plaintext_len) return -1;

    /* 解析记录头 */
    uint8_t type = record[0];
    uint16_t ciphertext_len = (record[3] << 8) | record[4];

    if (record_len != TLS_RECORD_HEADER_SIZE + ciphertext_len) {
        vga_printf("[TLS-REC] record length mismatch: expected %d, got %d\n",
                   TLS_RECORD_HEADER_SIZE + ciphertext_len, record_len);
        return -1;
    }

    if (ciphertext_len < 16) return -1;

    /* 构造nonce */
    uint8_t nonce[12];
    tls_record_build_nonce(ctx->server_write_iv, ctx->server_iv_len,
                           ctx->seq_num_server, nonce);

    /* 构造AAD */
    uint8_t aad[5];
    uint32_t aad_len;
    tls_record_build_aad(type, ciphertext_len, aad, &aad_len);

    /* 分离密文和tag */
    const uint8_t *ciphertext = record + TLS_RECORD_HEADER_SIZE;
    uint32_t ct_len = ciphertext_len - 16;
    const uint8_t *tag = record + TLS_RECORD_HEADER_SIZE + ct_len;

    /* AES-GCM解密 */
    uint8_t decrypted[TLS_MAX_RECORD_SIZE];
    int ret = aes_gcm_decrypt(ctx->server_write_key, ctx->server_key_len,
                              nonce, 12,
                              aad, aad_len,
                              ciphertext, decrypted, ct_len,
                              tag, 16);
    if (ret != 0) {
        vga_puts("[TLS-REC] AES-GCM decrypt failed (bad tag?)\n");
        return -1;
    }

    /* 移除末尾的内容类型字节 */
    if (ct_len == 0) return -1;
    uint8_t content_type = decrypted[ct_len - 1];
    uint32_t pt_len = ct_len - 1;

    if (plaintext_buf_size < pt_len) return -1;
    memcpy(plaintext, decrypted, pt_len);
    *plaintext_len = pt_len;

    /* 递增序列号 */
    ctx->seq_num_server++;

    vga_printf("[TLS-REC] decrypt OK: type=0x%02x, len=%d\n", content_type, pt_len);
    return 0;
}

/* 获取当前序列号（调试用） */
uint64_t tls_record_get_seq_client(const tls_record_ctx_t *ctx)
{
    return ctx ? ctx->seq_num_client : 0;
}

uint64_t tls_record_get_seq_server(const tls_record_ctx_t *ctx)
{
    return ctx ? ctx->seq_num_server : 0;
}

/* 重置序列号（密钥更新时使用） */
void tls_record_reset_seq(tls_record_ctx_t *ctx)
{
    if (!ctx) return;
    ctx->seq_num_client = 0;
    ctx->seq_num_server = 0;
    vga_puts("[TLS-REC] sequence numbers reset\n");
}
