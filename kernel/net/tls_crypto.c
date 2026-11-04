/* ============================================================================
 * 赫普OS - TLS加密传输核心模块
 * 实现TLS 1.2的PRF、密钥派生、AES-CBC加密、HMAC-SHA1、记录层封装
 * V8.17: 初始实现
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "vga.h"

/* TLS常量 */
#define TLS_MASTER_SECRET_LEN  48
#define TLS_KEY_BLOCK_LEN      104  /* 2*(mac_key + enc_key + iv) for AES-128-CBC + SHA1 */
#define TLS_RANDOM_LEN         32
#define TLS_FINISHED_LEN       12

/* AES-128-CBC常量 */
#define AES_KEY_LEN            16
#define AES_BLOCK_SIZE         16
#define HMAC_SHA1_LEN          20

/* TLS记录类型 */
#define TLS_RECORD_CHANGE_CIPHER_SPEC  20
#define TLS_RECORD_ALERT               21
#define TLS_RECORD_HANDSHAKE           22
#define TLS_RECORD_APPLICATION_DATA    23

/* TLS版本 */
#define TLS_VERSION_1_2  0x0303

/* TLS加密上下文 */
typedef struct {
    uint8_t master_secret[TLS_MASTER_SECRET_LEN];
    uint8_t client_write_key[AES_KEY_LEN];
    uint8_t server_write_key[AES_KEY_LEN];
    uint8_t client_mac_key[HMAC_SHA1_LEN];
    uint8_t server_mac_key[HMAC_SHA1_LEN];
    uint8_t client_iv[AES_BLOCK_SIZE];
    uint8_t server_iv[AES_BLOCK_SIZE];
    uint64_t client_seq;
    uint64_t server_seq;
    int keys_ready;
} tls_crypto_ctx_t;

static tls_crypto_ctx_t tls_ctx;

/* 外部加密函数声明 */
extern void sha256(const uint8_t *data, uint32_t len, uint8_t *hash);
extern int hmac_compute(int alg, const uint8_t *key, uint32_t key_len,
                         const uint8_t *data, uint32_t data_len,
                         uint8_t *mac, uint32_t *mac_len);
extern int aes_set_key(void *ctx, const uint8_t *key, int key_size);
extern int aes_encrypt_cbc(void *ctx, const uint8_t *iv, const uint8_t *input,
                            uint32_t length, uint8_t *output);
extern int aes_decrypt_cbc(void *ctx, const uint8_t *iv, const uint8_t *input,
                            uint32_t length, uint8_t *output);

/* HMAC算法常量 */
#define HMAC_ALG_SHA1   1
#define HMAC_ALG_SHA256 2

/* TLS PRF (Pseudo-Random Function) for TLS 1.2
 * 使用SHA-256作为基础哈希
 * PRF(secret, label, seed) = P_hash(secret, label + seed)
 */
static void tls_prf_sha256(const uint8_t *secret, uint32_t secret_len,
                            const char *label, const uint8_t *seed, uint32_t seed_len,
                            uint8_t *output, uint32_t output_len)
{
    /* 简化实现：使用HMAC-SHA256的P_hash */
    /* P_hash(secret, seed) = HMAC_hash(secret, A(1) + seed) +
     *                        HMAC_hash(secret, A(2) + seed) + ...
     * A(0) = seed
     * A(i) = HMAC_hash(secret, A(i-1))
     */

    uint8_t a[32];  /* A(i) */
    uint8_t hmac_result[32];
    uint32_t hmac_result_len;
    uint32_t offset = 0;

    /* A(0) = label + seed */
    uint8_t a0[256];
    uint32_t a0_len = 0;
    uint32_t label_len = 0;
    while (label[label_len]) label_len++;
    memcpy(a0, label, label_len);
    a0_len = label_len;
    memcpy(a0 + a0_len, seed, seed_len);
    a0_len += seed_len;

    /* A(1) = HMAC(secret, A(0)) */
    hmac_compute(HMAC_ALG_SHA256, secret, secret_len, a0, a0_len, a, &hmac_result_len);

    while (offset < output_len) {
        /* HMAC(secret, A(i) + seed) */
        uint8_t input[256];
        uint32_t input_len = 0;
        memcpy(input, a, 32);
        input_len = 32;
        memcpy(input + input_len, a0, a0_len);
        input_len += a0_len;

        hmac_compute(HMAC_ALG_SHA256, secret, secret_len, input, input_len,
                     hmac_result, &hmac_result_len);

        uint32_t copy_len = output_len - offset;
        if (copy_len > hmac_result_len) copy_len = hmac_result_len;
        memcpy(output + offset, hmac_result, copy_len);
        offset += copy_len;

        /* A(i+1) = HMAC(secret, A(i)) */
        hmac_compute(HMAC_ALG_SHA256, secret, secret_len, a, 32, a, &hmac_result_len);
    }
}

/* 初始化TLS加密上下文 */
void tls_crypto_init(void)
{
    memset(&tls_ctx, 0, sizeof(tls_ctx));
    tls_ctx.client_seq = 0;
    tls_ctx.server_seq = 0;
    tls_ctx.keys_ready = 0;
    vga_puts("[TLS-CRYPTO] initialized\n");
}

/* 派生主密钥
 * master_secret = PRF(pre_master_secret, "master secret",
 *                      ClientHello.random + ServerHello.random)
 */
int tls_crypto_derive_master_secret(const uint8_t *pre_master_secret,
                                     uint32_t pre_master_len,
                                     const uint8_t *client_random,
                                     const uint8_t *server_random)
{
    uint8_t seed[64];  /* client_random + server_random */
    memcpy(seed, client_random, 32);
    memcpy(seed + 32, server_random, 32);

    tls_prf_sha256(pre_master_secret, pre_master_len,
                    "master secret", seed, 64,
                    tls_ctx.master_secret, TLS_MASTER_SECRET_LEN);

    vga_puts("[TLS-CRYPTO] master_secret derived\n");
    return 0;
}

/* 派生密钥材料
 * key_block = PRF(SecurityParameters.master_secret, "key expansion",
 *                 SecurityParameters.server_random +
 *                 SecurityParameters.client_random)
 *
 * key_block布局（AES-128-CBC + SHA1）：
 * client_write_MAC_key (20 bytes)
 * server_write_MAC_key (20 bytes)
 * client_write_key (16 bytes)
 * server_write_key (16 bytes)
 * client_write_IV (16 bytes)
 * server_write_IV (16 bytes)
 */
int tls_crypto_derive_keys(const uint8_t *client_random,
                            const uint8_t *server_random)
{
    uint8_t seed[64];  /* server_random + client_random */
    memcpy(seed, server_random, 32);
    memcpy(seed + 32, client_random, 32);

    uint8_t key_block[TLS_KEY_BLOCK_LEN];
    tls_prf_sha256(tls_ctx.master_secret, TLS_MASTER_SECRET_LEN,
                    "key expansion", seed, 64,
                    key_block, TLS_KEY_BLOCK_LEN);

    /* 提取密钥材料 */
    uint32_t offset = 0;

    /* client_write_MAC_key (20 bytes) */
    memcpy(tls_ctx.client_mac_key, key_block + offset, HMAC_SHA1_LEN);
    offset += HMAC_SHA1_LEN;

    /* server_write_MAC_key (20 bytes) */
    memcpy(tls_ctx.server_mac_key, key_block + offset, HMAC_SHA1_LEN);
    offset += HMAC_SHA1_LEN;

    /* client_write_key (16 bytes) */
    memcpy(tls_ctx.client_write_key, key_block + offset, AES_KEY_LEN);
    offset += AES_KEY_LEN;

    /* server_write_key (16 bytes) */
    memcpy(tls_ctx.server_write_key, key_block + offset, AES_KEY_LEN);
    offset += AES_KEY_LEN;

    /* client_write_IV (16 bytes) */
    memcpy(tls_ctx.client_iv, key_block + offset, AES_BLOCK_SIZE);
    offset += AES_BLOCK_SIZE;

    /* server_write_IV (16 bytes) */
    memcpy(tls_ctx.server_iv, key_block + offset, AES_BLOCK_SIZE);
    offset += AES_BLOCK_SIZE;

    tls_ctx.keys_ready = 1;
    tls_ctx.client_seq = 0;
    tls_ctx.server_seq = 0;

    vga_puts("[TLS-CRYPTO] keys derived (client/server MAC+key+IV)\n");
    return 0;
}

/* 计算TLS记录的HMAC
 * HMAC(MAC_write_key, seq_num + TLSCompressed.type +
 *                      TLSCompressed.version + TLSCompressed.length +
 *                      TLSCompressed.fragment)
 */
static int tls_crypto_compute_mac(const uint8_t *mac_key,
                                   uint64_t seq_num,
                                   uint8_t record_type,
                                   uint16_t version,
                                   const uint8_t *data, uint32_t data_len,
                                   uint8_t *mac)
{
    uint8_t input[1024];
    uint32_t input_len = 0;
    uint32_t mac_len;

    /* seq_num (8 bytes, big-endian) */
    for (int i = 7; i >= 0; i--) {
        input[input_len++] = (seq_num >> (i * 8)) & 0xFF;
    }

    /* type (1 byte) */
    input[input_len++] = record_type;

    /* version (2 bytes, big-endian) */
    input[input_len++] = (version >> 8) & 0xFF;
    input[input_len++] = version & 0xFF;

    /* length (2 bytes, big-endian) */
    input[input_len++] = (data_len >> 8) & 0xFF;
    input[input_len++] = data_len & 0xFF;

    /* fragment */
    if (data_len > 0 && data) {
        memcpy(input + input_len, data, data_len);
        input_len += data_len;
    }

    return hmac_compute(HMAC_ALG_SHA1, mac_key, HMAC_SHA1_LEN,
                         input, input_len, mac, &mac_len);
}

/* 加密TLS记录
 * 输入：明文数据
 * 输出：TLS记录（记录头 + 加密数据 + HMAC + 填充）
 */
int tls_crypto_encrypt_record(uint8_t record_type,
                               const uint8_t *plaintext, uint32_t plaintext_len,
                               uint8_t *output, uint32_t *output_len)
{
    if (!tls_ctx.keys_ready) return -1;

    /* 计算HMAC */
    uint8_t mac[HMAC_SHA1_LEN];
    tls_crypto_compute_mac(tls_ctx.client_mac_key, tls_ctx.client_seq,
                            record_type, TLS_VERSION_1_2,
                            plaintext, plaintext_len, mac);

    /* 构造TLSCompressed：plaintext + MAC */
    uint32_t compressed_len = plaintext_len + HMAC_SHA1_LEN;
    uint8_t compressed[2048];
    if (plaintext_len > 0 && plaintext) {
        memcpy(compressed, plaintext, plaintext_len);
    }
    memcpy(compressed + plaintext_len, mac, HMAC_SHA1_LEN);

    /* 添加填充（CBC模式需要块对齐） */
    uint8_t padding_len = AES_BLOCK_SIZE - (compressed_len % AES_BLOCK_SIZE);
    if (padding_len == 0) padding_len = AES_BLOCK_SIZE;
    for (uint32_t i = 0; i < padding_len; i++) {
        compressed[compressed_len + i] = padding_len - 1;
    }
    uint32_t total_len = compressed_len + padding_len;

    /* AES-CBC加密 */
    uint8_t ciphertext[2048];
    static uint8_t aes_ctx[256];
    aes_set_key(aes_ctx, tls_ctx.client_write_key, AES_KEY_LEN);
    aes_encrypt_cbc(aes_ctx, tls_ctx.client_iv, compressed, total_len, ciphertext);

    /* 构造TLS记录 */
    output[0] = record_type;
    output[1] = (TLS_VERSION_1_2 >> 8) & 0xFF;
    output[2] = TLS_VERSION_1_2 & 0xFF;
    output[3] = (total_len >> 8) & 0xFF;
    output[4] = total_len & 0xFF;
    memcpy(output + 5, ciphertext, total_len);

    *output_len = total_len + 5;

    /* 更新序列号 */
    tls_ctx.client_seq++;

    return 0;
}

/* 解密TLS记录
 * 输入：TLS记录（记录头 + 加密数据）
 * 输出：明文数据
 */
int tls_crypto_decrypt_record(const uint8_t *record, uint32_t record_len,
                               uint8_t *plaintext, uint32_t *plaintext_len)
{
    if (!tls_ctx.keys_ready || record_len < 5) return -1;

    uint8_t record_type = record[0];
    uint16_t version = (record[1] << 8) | record[2];
    uint16_t length = (record[3] << 8) | record[4];

    if (record_len < length + 5) return -1;

    /* AES-CBC解密 */
    uint8_t decrypted[2048];
    static uint8_t aes_ctx[256];
    aes_set_key(aes_ctx, tls_ctx.server_write_key, AES_KEY_LEN);
    aes_decrypt_cbc(aes_ctx, tls_ctx.server_iv, record + 5, length, decrypted);

    /* 验证填充 */
    uint8_t padding_len = decrypted[length - 1] + 1;
    if (padding_len > AES_BLOCK_SIZE || padding_len > length) {
        vga_puts("[TLS-CRYPTO] ERROR: invalid padding\n");
        return -1;
    }

    /* 提取明文和MAC */
    uint32_t compressed_len = length - padding_len;
    if (compressed_len < HMAC_SHA1_LEN) return -1;
    uint32_t data_len = compressed_len - HMAC_SHA1_LEN;

    /* 验证HMAC */
    uint8_t expected_mac[HMAC_SHA1_LEN];
    tls_crypto_compute_mac(tls_ctx.server_mac_key, tls_ctx.server_seq,
                            record_type, version,
                            decrypted, data_len, expected_mac);

    if (memcmp(decrypted + data_len, expected_mac, HMAC_SHA1_LEN) != 0) {
        vga_puts("[TLS-CRYPTO] ERROR: MAC verification failed\n");
        return -1;
    }

    /* 复制明文 */
    if (data_len > 0 && plaintext) {
        memcpy(plaintext, decrypted, data_len);
    }
    *plaintext_len = data_len;

    /* 更新序列号 */
    tls_ctx.server_seq++;

    return 0;
}

/* 构造Finished消息
 * verify_data = PRF(master_secret, "client finished",
 *                    Hash(handshake_messages))[0.11]
 */
int tls_crypto_build_finished(const uint8_t *handshake_hash,
                               uint8_t *finished, uint32_t *finished_len)
{
    uint8_t verify_data[TLS_FINISHED_LEN];

    tls_prf_sha256(tls_ctx.master_secret, TLS_MASTER_SECRET_LEN,
                    "client finished", handshake_hash, 32,
                    verify_data, TLS_FINISHED_LEN);

    /* 构造握手消息：type(1) + length(3) + verify_data(12) */
    finished[0] = 0x14;  /* Finished */
    finished[1] = 0;
    finished[2] = 0;
    finished[3] = TLS_FINISHED_LEN;
    memcpy(finished + 4, verify_data, TLS_FINISHED_LEN);

    *finished_len = TLS_FINISHED_LEN + 4;

    return 0;
}

/* V8.23: 验证服务器Finished消息
 * 服务器Finished的verify_data = PRF(master_secret, "server finished",
 *                                      Hash(handshake_messages))[0.11]
 * 注意：服务器Finished的握手哈希包含客户端Finished消息
 * 返回0验证通过，-1验证失败
 */
int tls_crypto_verify_server_finished(const uint8_t *handshake_hash,
                                       const uint8_t *finished_msg,
                                       uint32_t finished_len)
{
    if (!tls_ctx.keys_ready || !handshake_hash || !finished_msg) return -1;

    /* Finished消息格式：type(1) + length(3) + verify_data(12) */
    if (finished_len < 16) return -1;
    if (finished_msg[0] != 0x14) return -1;  /* Finished类型 */

    /* 计算期望的verify_data */
    uint8_t expected_verify_data[TLS_FINISHED_LEN];
    tls_prf_sha256(tls_ctx.master_secret, TLS_MASTER_SECRET_LEN,
                    "server finished", handshake_hash, 32,
                    expected_verify_data, TLS_FINISHED_LEN);

    /* 比较实际verify_data和期望verify_data（恒定时间比较） */
    const uint8_t *actual_verify_data = finished_msg + 4;
    uint8_t diff = 0;
    for (int i = 0; i < TLS_FINISHED_LEN; i++) {
        diff |= actual_verify_data[i] ^ expected_verify_data[i];
    }

    if (diff != 0) {
        vga_puts("[TLS-CRYPTO] server Finished verification FAILED\n");
        vga_puts("[TLS-CRYPTO] expected: ");
        for (int i = 0; i < TLS_FINISHED_LEN; i++) {
            vga_printf("%02x", expected_verify_data[i]);
        }
        vga_puts("\n[TLS-CRYPTO] actual:   ");
        for (int i = 0; i < TLS_FINISHED_LEN; i++) {
            vga_printf("%02x", actual_verify_data[i]);
        }
        vga_puts("\n");
        return -1;
    }

    vga_puts("[TLS-CRYPTO] server Finished verification PASSED\n");
    return 0;
}

/* 获取密钥就绪状态 */
int tls_crypto_keys_ready(void)
{
    return tls_ctx.keys_ready;
}

/* 重置序列号 */
void tls_crypto_reset_seq(void)
{
    tls_ctx.client_seq = 0;
    tls_ctx.server_seq = 0;
}
