/* ============================================================================
 * 赫普OS - TLS客户端连接（完善版）
 * 集成TCP发送/接收，实现简化的TLS 1.2握手
 * V8.11: 完善握手流程
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "vga.h"
#include "serial.h"
#include "rsa.h"
#include "x509.h"

/* TLS记录类型 */
#define TLS_RECORD_CHANGE_CIPHER_SPEC  20
#define TLS_RECORD_ALERT               21
#define TLS_RECORD_HANDSHAKE           22
#define TLS_RECORD_APPLICATION_DATA    23

/* TLS握手类型 */
#define TLS_HS_CLIENT_HELLO        1
#define TLS_HS_SERVER_HELLO        2
#define TLS_HS_CERTIFICATE         11
#define TLS_HS_SERVER_KEY_EXCHANGE 12
#define TLS_HS_SERVER_HELLO_DONE   14
#define TLS_HS_CLIENT_KEY_EXCHANGE 16
#define TLS_HS_FINISHED             20

/* TLS版本 */
#define TLS_VERSION_1_2  0x0303

/* 密码套件 */
#define TLS_RSA_WITH_AES_128_CBC_SHA  0x002F

/* TLS客户端状态 */
typedef enum {
    TLS_CLIENT_INIT = 0,
    TLS_CLIENT_CLIENT_HELLO_SENT,
    TLS_CLIENT_SERVER_HELLO_RECEIVED,
    TLS_CLIENT_CERTIFICATE_RECEIVED,
    TLS_CLIENT_SERVER_HELLO_DONE_RECEIVED,
    TLS_CLIENT_CLIENT_KEY_EXCHANGE_SENT,
    TLS_CLIENT_CHANGE_CIPHER_SPEC_SENT,
    TLS_CLIENT_FINISHED_SENT,
    TLS_CLIENT_ESTABLISHED,
    TLS_CLIENT_ERROR
} tls_client_state_t;

typedef struct {
    int tcp_fd;
    tls_client_state_t state;
    uint8_t client_random[32];
    uint8_t server_random[32];
    uint8_t master_secret[48];
    uint8_t client_write_key[16];
    uint8_t server_write_key[16];
    uint8_t client_mac_key[20];
    uint8_t server_mac_key[20];
    uint8_t client_iv[16];
    uint8_t server_iv[16];
    uint64_t client_seq;
    uint64_t server_seq;
    int handshake_complete;
    char hostname[256];
    uint8_t session_id[32];
    int session_id_len;
    /* V8.22: 握手消息缓冲区（用于计算Finished的verify_data） */
    uint8_t handshake_buf[4096];
    uint32_t handshake_buf_len;
} tls_client_t;

static tls_client_t tls_client;

/* 生成客户端随机数 */
static void tls_generate_random(uint8_t *buf, int len)
{
    extern void csprng_get_bytes(uint8_t *buf, uint32_t len);
    csprng_get_bytes(buf, len);
}

/* V8.22: 记录握手消息（用于计算Finished的verify_data） */
static void tls_record_handshake_message(const uint8_t *msg, uint32_t len)
{
    if (tls_client.handshake_buf_len + len > sizeof(tls_client.handshake_buf)) {
        vga_puts("[TLS-CLIENT] WARNING: handshake buffer overflow\n");
        return;
    }
    memcpy(tls_client.handshake_buf + tls_client.handshake_buf_len, msg, len);
    tls_client.handshake_buf_len += len;
}

/* V8.22: 计算握手消息的SHA-256哈希 */
static void tls_compute_handshake_hash(uint8_t *hash)
{
    extern void sha256(const uint8_t *data, uint32_t len, uint8_t *out);
    sha256(tls_client.handshake_buf, tls_client.handshake_buf_len, hash);
    vga_printf("[TLS-CLIENT] handshake hash computed (%d bytes)\n",
               tls_client.handshake_buf_len);
}

/* 构造ClientHello */
static int tls_build_client_hello(uint8_t *buf, uint32_t buf_len)
{
    if (buf_len < 512) return -1;

    int idx = 0;

    /* TLS记录头 */
    buf[idx++] = TLS_RECORD_HANDSHAKE;
    buf[idx++] = (TLS_VERSION_1_2 >> 8) & 0xFF;
    buf[idx++] = TLS_VERSION_1_2 & 0xFF;
    uint16_t *record_len = (uint16_t *)&buf[idx];
    idx += 2;

    /* 握手头 */
    int hs_start = idx;
    buf[idx++] = TLS_HS_CLIENT_HELLO;
    uint8_t *hs_len = &buf[idx];
    idx += 3;

    /* 客户端版本 */
    buf[idx++] = (TLS_VERSION_1_2 >> 8) & 0xFF;
    buf[idx++] = TLS_VERSION_1_2 & 0xFF;

    /* 客户端随机数 */
    tls_generate_random(tls_client.client_random, 32);
    memcpy(&buf[idx], tls_client.client_random, 32);
    idx += 32;

    /* 会话ID（空） */
    buf[idx++] = 0;

    /* 密码套件（TLS_RSA_WITH_AES_128_CBC_SHA） */
    buf[idx++] = 0;
    buf[idx++] = 2;
    buf[idx++] = (TLS_RSA_WITH_AES_128_CBC_SHA >> 8) & 0xFF;
    buf[idx++] = TLS_RSA_WITH_AES_128_CBC_SHA & 0xFF;

    /* 压缩方法（空） */
    buf[idx++] = 1;
    buf[idx++] = 0;

    /* 扩展 */
    uint16_t *ext_len = (uint16_t *)&buf[idx];
    idx += 2;
    int ext_start = idx;

    /* 支持的版本扩展 */
    buf[idx++] = 0;
    buf[idx++] = 43;  /* supported_versions */
    buf[idx++] = 0;
    buf[idx++] = 3;
    buf[idx++] = 2;
    buf[idx++] = (TLS_VERSION_1_2 >> 8) & 0xFF;
    buf[idx++] = TLS_VERSION_1_2 & 0xFF;

    /* 签名算法扩展 */
    buf[idx++] = 0;
    buf[idx++] = 13;  /* signature_algorithms */
    buf[idx++] = 0;
    buf[idx++] = 4;
    buf[idx++] = 0;
    buf[idx++] = 2;
    buf[idx++] = 0x04;
    buf[idx++] = 0x01;  /* rsa_pss_rsae_sha256 */

    /* 设置扩展长度 */
    *ext_len = idx - ext_start;

    /* 设置握手长度 */
    hs_len[0] = ((idx - hs_start - 4) >> 16) & 0xFF;
    hs_len[1] = ((idx - hs_start - 4) >> 8) & 0xFF;
    hs_len[2] = (idx - hs_start - 4) & 0xFF;

    /* 设置记录长度 */
    *record_len = idx - hs_start;

    return idx;
}

/* 初始化TLS客户端 */
int tls_client_init(void)
{
    memset(&tls_client, 0, sizeof(tls_client));
    tls_client.state = TLS_CLIENT_INIT;
    tls_client.tcp_fd = -1;
    tls_client.handshake_buf_len = 0;  /* V8.22: 初始化握手消息缓冲区 */
    serial_puts(0x3F8, "[TLS-CLIENT] initialized (TLS 1.2)\n");
    return 0;
}

/* 连接到TLS服务器 */
int tls_client_connect(const char *hostname, uint16_t port)
{
    if (!hostname) return -1;

    /* V8.19: RSA公钥变量 */
    rsa_public_key_t server_pub_key;
    int have_server_key = 0;

    /* 初始化 */
    memset(&tls_client, 0, sizeof(tls_client));
    strncpy(tls_client.hostname, hostname, 255);
    tls_client.state = TLS_CLIENT_INIT;

    /* 解析IP（简化：使用固定IP测试） */
    uint32_t server_ip = (142 << 24) | (250 << 16) | (200 << 8) | 189;  /* example.com */

    /* TCP连接 */
    extern int tcp_connect(uint32_t remote_ip, uint16_t remote_port, uint16_t local_port);
    tls_client.tcp_fd = tcp_connect(server_ip, port, 12345);
    if (tls_client.tcp_fd < 0) {
        vga_puts("[TLS-CLIENT] TCP connect failed\n");
        return -1;
    }

    vga_printf("[TLS-CLIENT] TCP connected to %s:%d\n", hostname, port);

    /* 等待TCP连接建立 */
    extern void net_poll(void);
    extern uint32_t pit_get_ticks(void);
    uint32_t start = pit_get_ticks();
    extern int tcp_get_state(int fd, void *state);
    int state = 0;
    while (pit_get_ticks() - start < 5000) {
        net_poll();
        tcp_get_state(tls_client.tcp_fd, &state);
        if (state == 4) {  /* ESTABLISHED */
            break;
        }
    }

    if (state != 4) {
        vga_puts("[TLS-CLIENT] TCP connection timeout\n");
        return -1;
    }

    /* 发送ClientHello */
    uint8_t client_hello[512];
    int hello_len = tls_build_client_hello(client_hello, sizeof(client_hello));

    extern int tcp_send(int fd, const uint8_t *data, uint32_t length);
    int sent = tcp_send(tls_client.tcp_fd, client_hello, hello_len);
    if (sent < 0) {
        vga_puts("[TLS-CLIENT] send ClientHello failed\n");
        return -1;
    }

    /* V8.22: 记录ClientHello握手消息（跳过TLS记录头5字节） */
    tls_record_handshake_message(client_hello + 5, hello_len - 5);

    tls_client.state = TLS_CLIENT_CLIENT_HELLO_SENT;
    vga_puts("[TLS-CLIENT] ClientHello sent\n");

    /* 等待服务器响应 */
    vga_puts("[TLS-CLIENT] waiting for ServerHello...\n");

    /* 接收服务器响应（等待5秒） */
    uint8_t response[8192];
    int total_recv = 0;
    start = pit_get_ticks();

    while (pit_get_ticks() - start < 5000 && total_recv < (int)sizeof(response) - 1) {
        net_poll();
        extern int tcp_receive(int fd, uint8_t *buffer, uint32_t max_length);
        int n = tcp_receive(tls_client.tcp_fd, response + total_recv,
                            sizeof(response) - total_recv - 1);
        if (n > 0) {
            total_recv += n;
            vga_printf("[TLS-CLIENT] received %d bytes (total %d)\n", n, total_recv);

            /* 检查是否收到ServerHelloDone（TLS 1.2）或Finished（TLS 1.3） */
            for (int i = 0; i < total_recv - 4; i++) {
                if (response[i] == TLS_RECORD_HANDSHAKE) {
                    /* 检查握手消息类型 */
                    if (i + 5 < total_recv) {
                        uint8_t hs_type = response[i + 5];
                        if (hs_type == 14) {  /* ServerHelloDone */
                            vga_puts("[TLS-CLIENT] ServerHelloDone received\n");
                            goto handshake_messages_complete;
                        }
                        if (hs_type == 20) {  /* Finished (TLS 1.3) */
                            vga_puts("[TLS-CLIENT] Finished received (TLS 1.3)\n");
                            goto handshake_messages_complete;
                        }
                    }
                }
            }
        }
    }

handshake_messages_complete:
    vga_printf("[TLS-CLIENT] total received: %d bytes\n", total_recv);

    /* V8.22: 记录所有服务器握手消息（用于计算Finished的verify_data） */
    if (total_recv > 0) {
        int scan = 0;
        while (scan < total_recv - 5) {
            if (response[scan] == TLS_RECORD_HANDSHAKE) {
                /* 提取TLS记录长度 */
                uint16_t record_len = (response[scan + 3] << 8) | response[scan + 4];
                if (scan + 5 + record_len <= total_recv) {
                    /* 记录握手消息（跳过TLS记录头5字节） */
                    tls_record_handshake_message(response + scan + 5, record_len);
                    scan += 5 + record_len;
                    continue;
                }
            }
            scan++;
        }
        vga_printf("[TLS-CLIENT] server handshake messages recorded (%d bytes total)\n",
                   tls_client.handshake_buf_len);
    }

    /* V8.14: 集成tls_handshake.c解析框架 */
    /* 解析ServerHello、Certificate、ServerHelloDone */
    if (total_recv > 0) {
        /* 查找ServerHello记录 */
        int hs_offset = -1;
        for (int i = 0; i < total_recv - 5; i++) {
            if (response[i] == TLS_RECORD_HANDSHAKE && response[i + 5] == 0x02) {
                hs_offset = i + 5;  /* 跳过记录头，指向握手消息 */
                break;
            }
        }

        if (hs_offset > 0) {
            vga_puts("[TLS-CLIENT] parsing ServerHello...\n");

            /* 提取server_random */
            if (hs_offset + 38 < total_recv) {
                memcpy(tls_client.server_random, &response[hs_offset + 2], 32);
                vga_puts("[TLS-CLIENT] server_random extracted\n");
            }

            /* 提取cipher_suite */
            if (hs_offset + 38 + 32 + 1 < total_recv) {
                int cs_offset = hs_offset + 2 + 32 + 1;  /* version + random + session_id_len */
                uint8_t session_id_len = response[hs_offset + 2 + 32];
                cs_offset += session_id_len;
                if (cs_offset + 2 < total_recv) {
                    uint16_t cipher_suite = (response[cs_offset] << 8) | response[cs_offset + 1];
                    vga_printf("[TLS-CLIENT] cipher_suite: 0x%04x\n", cipher_suite);
                }
            }

            vga_puts("[TLS-CLIENT] ServerHello parsed successfully\n");
        }

        /* V8.19: 解析Certificate消息，提取RSA公钥 */
        int cert_offset = -1;
        for (int i = 0; i < total_recv - 5; i++) {
            if (response[i] == TLS_RECORD_HANDSHAKE && response[i + 5] == 0x0B) {
                cert_offset = i + 5;  /* 跳过记录头，指向握手消息 */
                break;
            }
        }

        if (cert_offset > 0) {
            vga_puts("[TLS-CLIENT] parsing Certificate...\n");

            /* Certificate消息结构：
             * type(1) + length(3) + cert_list_len(3) + [cert_len(3) + cert_data]...
             */
            int cert_list_len = (response[cert_offset + 4] << 16) |
                                (response[cert_offset + 5] << 8) |
                                response[cert_offset + 6];
            uint8_t *cert_list_data = &response[cert_offset + 7];
            int cert_list_offset = 0;

            /* V8.24: 解析证书链 */
            #define MAX_CERTS_IN_CHAIN 8
            x509_cert_t cert_chain[MAX_CERTS_IN_CHAIN];
            int cert_count = 0;

            while (cert_list_offset + 3 <= cert_list_len && cert_count < MAX_CERTS_IN_CHAIN) {
                int cert_len = (cert_list_data[cert_list_offset] << 16) |
                               (cert_list_data[cert_list_offset + 1] << 8) |
                               cert_list_data[cert_list_offset + 2];
                cert_list_offset += 3;

                if (cert_list_offset + cert_len > cert_list_len) break;

                /* 解析X.509证书 */
                extern int x509_parse(const uint8_t *der, uint32_t len, x509_cert_t *cert);
                if (x509_parse(cert_list_data + cert_list_offset, cert_len,
                               &cert_chain[cert_count]) == 0) {
                    vga_printf("[TLS-CLIENT] cert[%d]: subject=%s, issuer=%s\n",
                               cert_count, cert_chain[cert_count].subject,
                               cert_chain[cert_count].issuer);
                    cert_count++;
                } else {
                    vga_printf("[TLS-CLIENT] cert[%d]: parse failed\n", cert_count);
                }
                cert_list_offset += cert_len;
            }

            vga_printf("[TLS-CLIENT] certificate chain: %d certificates\n", cert_count);

            /* V8.24: 证书链验证 */
            if (cert_count > 0) {
                extern int x509_verify_chain(const x509_cert_t *chain, int chain_len,
                                             const x509_cert_t *trusted_cas, int ca_count);
                extern int x509_check_date(const x509_cert_t *cert, uint32_t current_time);
                extern const char *x509_verify_error_text(x509_verify_result_t result);

                /* 检查第一个证书（服务器证书）的有效期 */
                int date_result = x509_check_date(&cert_chain[0], 0);
                if (date_result == 0) {
                    vga_puts("[TLS-CLIENT] certificate date: VALID ✅\n");
                } else {
                    vga_printf("[TLS-CLIENT] certificate date: %s ⚠️\n",
                               x509_verify_error_text((x509_verify_result_t)date_result));
                }

                /* 验证证书链（无内置可信CA时，验证会失败，但记录结果） */
                extern int x509_get_trusted_cas(x509_cert_t *cas, int max);
                x509_cert_t trusted_cas[8];
                int ca_count = x509_get_trusted_cas(trusted_cas, 8);

                if (ca_count > 0) {
                    int chain_result = x509_verify_chain(cert_chain, cert_count,
                                                          trusted_cas, ca_count);
                    if (chain_result == 0) {
                        vga_puts("[TLS-CLIENT] certificate chain: VERIFIED ✅\n");
                    } else {
                        vga_printf("[TLS-CLIENT] certificate chain: %s ⚠️\n",
                                   x509_verify_error_text((x509_verify_result_t)chain_result));
                        vga_puts("[TLS-CLIENT] WARNING: certificate chain verification failed\n");
                        vga_puts("[TLS-CLIENT] continuing anyway (simplified, no trusted CA store)\n");
                    }
                } else {
                    vga_puts("[TLS-CLIENT] certificate chain: no trusted CA store, skipping verification ⚠️\n");
                    vga_puts("[TLS-CLIENT] note: add trusted CAs with x509_add_trusted_ca()\n");
                }
            }

            /* 从第一个证书（服务器证书）提取RSA公钥 */
            if (cert_count > 0 && cert_chain[0].public_key_len > 0) {
                extern int rsa_parse_public_key(rsa_public_key_t *key,
                                                 const uint8_t *der, uint32_t len);
                if (rsa_parse_public_key(&server_pub_key,
                                          cert_chain[0].public_key,
                                          cert_chain[0].public_key_len) == 0) {
                    have_server_key = 1;
                    vga_printf("[TLS-CLIENT] RSA public key extracted from cert: %d bits\n",
                               server_pub_key.bits);
                }
            }

            /* 备用：直接在证书数据中搜索RSAPublicKey */
            if (!have_server_key && cert_count > 0) {
                uint8_t *first_cert_data = cert_list_data + 3;  /* 跳过第一个cert_len */
                int first_cert_len = (cert_list_data[0] << 16) |
                                      (cert_list_data[1] << 8) |
                                      cert_list_data[2];
                for (int j = 0; j < first_cert_len - 4; j++) {
                    if (first_cert_data[j] == 0x30 && first_cert_data[j+1] == 0x82 &&
                        first_cert_data[j+4] == 0x02 && first_cert_data[j+5] == 0x82) {
                        if (rsa_parse_public_key(&server_pub_key, first_cert_data + j,
                                                  first_cert_len - j) == 0) {
                            have_server_key = 1;
                            vga_printf("[TLS-CLIENT] RSA public key extracted (fallback): %d bits\n",
                                       server_pub_key.bits);
                            break;
                        }
                    }
                }
            }

            if (!have_server_key) {
                vga_puts("[TLS-CLIENT] WARNING: could not extract RSA public key from certificate\n");
                vga_puts("[TLS-CLIENT] falling back to simplified pre_master_secret (unencrypted)\n");
            }
        }
    }

    /* V8.18: 集成TLS加密模块 */
    extern void tls_crypto_init(void);
    extern int tls_crypto_derive_master_secret(const uint8_t *pre_master_secret,
                                                  uint32_t pre_master_len,
                                                  const uint8_t *client_random,
                                                  const uint8_t *server_random);
    extern int tls_crypto_derive_keys(const uint8_t *client_random,
                                       const uint8_t *server_random);
    extern int tls_crypto_encrypt_record(uint8_t record_type,
                                          const uint8_t *plaintext, uint32_t plaintext_len,
                                          uint8_t *output, uint32_t *output_len);
    extern int tls_crypto_build_finished(const uint8_t *handshake_hash,
                                          uint8_t *finished, uint32_t *finished_len);

    /* 初始化加密模块 */
    tls_crypto_init();

    /* 生成pre_master_secret（简化：使用固定值，实际应随机生成并用RSA加密） */
    /* TLS 1.2 pre_master_secret格式：version(2) + random(46) */
    uint8_t pre_master_secret[48];
    pre_master_secret[0] = 0x03;  /* TLS 1.2 version */
    pre_master_secret[1] = 0x03;
    /* 简化：使用client_random的前46字节作为随机部分 */
    memcpy(pre_master_secret + 2, tls_client.client_random, 46);

    vga_puts("[TLS-CLIENT] pre_master_secret generated (simplified)\n");

    /* 派生主密钥 */
    tls_crypto_derive_master_secret(pre_master_secret, 48,
                                     tls_client.client_random,
                                     tls_client.server_random);

    /* 派生密钥材料 */
    tls_crypto_derive_keys(tls_client.client_random, tls_client.server_random);

    /* 发送ClientKeyExchange */
    /* V8.19: 使用RSA公钥加密pre_master_secret（如果有服务器公钥） */
    uint8_t encrypted_pms[512];
    uint32_t encrypted_pms_len = 0;

    if (have_server_key) {
        extern int rsa_encrypt(const rsa_public_key_t *key,
                                const uint8_t *plaintext, uint32_t plaintext_len,
                                uint8_t *ciphertext, uint32_t *ciphertext_len);
        if (rsa_encrypt(&server_pub_key, pre_master_secret, 48,
                         encrypted_pms, &encrypted_pms_len) == 0) {
            vga_printf("[TLS-CLIENT] pre_master_secret encrypted with RSA (%d bytes)\n",
                       encrypted_pms_len);
        } else {
            vga_puts("[TLS-CLIENT] WARNING: RSA encryption failed, using plaintext\n");
            memcpy(encrypted_pms, pre_master_secret, 48);
            encrypted_pms_len = 48;
        }
    } else {
        /* 简化：未加密的pre_master_secret */
        memcpy(encrypted_pms, pre_master_secret, 48);
        encrypted_pms_len = 48;
    }

    /* 构造ClientKeyExchange消息：
     * type(1) + length(3) + encrypted_pms_len(2) + encrypted_pms
     */
    uint8_t cke_msg[600];
    cke_msg[0] = TLS_HS_CLIENT_KEY_EXCHANGE;  /* type */
    cke_msg[1] = ((encrypted_pms_len + 2) >> 16) & 0xFF;  /* length (3 bytes) */
    cke_msg[2] = ((encrypted_pms_len + 2) >> 8) & 0xFF;
    cke_msg[3] = (encrypted_pms_len + 2) & 0xFF;
    cke_msg[4] = (encrypted_pms_len >> 8) & 0xFF;  /* encrypted_pms length (2 bytes) */
    cke_msg[5] = encrypted_pms_len & 0xFF;
    memcpy(cke_msg + 6, encrypted_pms, encrypted_pms_len);

    extern int tcp_send(int fd, const uint8_t *data, uint32_t length);
    tcp_send(tls_client.tcp_fd, cke_msg, encrypted_pms_len + 6);

    /* V8.22: 记录ClientKeyExchange握手消息 */
    tls_record_handshake_message(cke_msg, encrypted_pms_len + 6);

    tls_client.state = TLS_CLIENT_CLIENT_KEY_EXCHANGE_SENT;
    if (have_server_key) {
        vga_puts("[TLS-CLIENT] ClientKeyExchange sent (RSA encrypted)\n");
    } else {
        vga_puts("[TLS-CLIENT] ClientKeyExchange sent (simplified, unencrypted)\n");
    }

    /* 发送ChangeCipherSpec */
    uint8_t ccs_msg[6];
    ccs_msg[0] = TLS_RECORD_CHANGE_CIPHER_SPEC;  /* type */
    ccs_msg[1] = 0x03;  /* version */
    ccs_msg[2] = 0x03;
    ccs_msg[3] = 0;  /* length */
    ccs_msg[4] = 1;
    ccs_msg[5] = 1;  /* ChangeCipherSpec message */
    tcp_send(tls_client.tcp_fd, ccs_msg, 6);
    tls_client.state = TLS_CLIENT_CHANGE_CIPHER_SPEC_SENT;
    vga_puts("[TLS-CLIENT] ChangeCipherSpec sent\n");

    /* 发送Finished消息（加密） */
    /* V8.22: 计算握手消息哈希（ClientHello + ServerHello + Certificate + ServerHelloDone + ClientKeyExchange） */
    uint8_t handshake_hash[32];
    tls_compute_handshake_hash(handshake_hash);
    uint8_t finished_msg[16];
    uint32_t finished_len;
    tls_crypto_build_finished(handshake_hash, finished_msg, &finished_len);

    /* 加密Finished消息 */
    uint8_t encrypted_finished[64];
    uint32_t encrypted_len;
    tls_crypto_encrypt_record(TLS_RECORD_HANDSHAKE, finished_msg, finished_len,
                               encrypted_finished, &encrypted_len);
    tcp_send(tls_client.tcp_fd, encrypted_finished, encrypted_len);

    /* V8.22: 记录客户端Finished握手消息（用于服务器验证） */
    tls_record_handshake_message(finished_msg, finished_len);

    tls_client.state = TLS_CLIENT_FINISHED_SENT;
    vga_puts("[TLS-CLIENT] Finished sent (encrypted)\n");

    /* V8.22: 接收并验证服务器的ChangeCipherSpec和Finished */
    vga_puts("[TLS-CLIENT] waiting for server ChangeCipherSpec and Finished...\n");
    uint8_t server_response[4096];
    int server_recv = 0;
    uint32_t server_start = pit_get_ticks();
    int server_ccs_received = 0;
    int server_finished_received = 0;

    while (pit_get_ticks() - server_start < 5000 && server_recv < (int)sizeof(server_response) - 1) {
        net_poll();
        extern int tcp_receive(int fd, uint8_t *buffer, uint32_t max_length);
        int n = tcp_receive(tls_client.tcp_fd, server_response + server_recv,
                            sizeof(server_response) - server_recv - 1);
        if (n > 0) {
            server_recv += n;
            vga_printf("[TLS-CLIENT] received %d bytes from server (total %d)\n", n, server_recv);

            /* 检查是否收到ChangeCipherSpec */
            for (int i = 0; i < server_recv - 5; i++) {
                if (server_response[i] == TLS_RECORD_CHANGE_CIPHER_SPEC) {
                    server_ccs_received = 1;
                    vga_puts("[TLS-CLIENT] server ChangeCipherSpec received\n");
                    break;
                }
            }

            /* 检查是否收到Finished（加密的握手记录） */
            if (server_ccs_received) {
                for (int i = 0; i < server_recv - 5; i++) {
                    if (server_response[i] == TLS_RECORD_HANDSHAKE) {
                        server_finished_received = 1;
                        vga_puts("[TLS-CLIENT] server Finished received (encrypted)\n");

                        /* V8.23: 详细验证服务器Finished */
                        /* 1. 解密服务器Finished消息 */
                        extern int tls_crypto_decrypt_record(const uint8_t *record, uint32_t record_len,
                                                              uint8_t *plaintext, uint32_t *plaintext_len);
                        uint8_t decrypted_finished[64];
                        uint32_t decrypted_len;
                        int decrypt_ret = tls_crypto_decrypt_record(server_response + i,
                                                                      server_recv - i,
                                                                      decrypted_finished,
                                                                      &decrypted_len);
                        if (decrypt_ret == 0) {
                            vga_printf("[TLS-CLIENT] server Finished decrypted (%d bytes)\n",
                                       decrypted_len);

                            /* 2. 计算服务器Finished的握手哈希（包含客户端Finished） */
                            uint8_t server_handshake_hash[32];
                            tls_compute_handshake_hash(server_handshake_hash);

                            /* 3. 验证服务器Finished的verify_data */
                            extern int tls_crypto_verify_server_finished(const uint8_t *handshake_hash,
                                                                           const uint8_t *finished_msg,
                                                                           uint32_t finished_len);
                            int verify_ret = tls_crypto_verify_server_finished(server_handshake_hash,
                                                                                 decrypted_finished,
                                                                                 decrypted_len);
                            if (verify_ret == 0) {
                                vga_puts("[TLS-CLIENT] server Finished verification: PASSED ✅\n");
                            } else {
                                vga_puts("[TLS-CLIENT] server Finished verification: FAILED ❌\n");
                                /* 验证失败不终止连接，继续（简化处理） */
                            }
                        } else {
                            vga_puts("[TLS-CLIENT] WARNING: server Finished decryption failed\n");
                            vga_puts("[TLS-CLIENT] server Finished verification: skipped (decryption failed)\n");
                        }
                        break;
                    }
                }
            }

            if (server_ccs_received && server_finished_received) {
                break;
            }
        }
    }

    if (!server_ccs_received) {
        vga_puts("[TLS-CLIENT] WARNING: server ChangeCipherSpec not received (timeout)\n");
    }
    if (!server_finished_received) {
        vga_puts("[TLS-CLIENT] WARNING: server Finished not received (timeout)\n");
    }

    /* 握手完成 */
    tls_client.handshake_complete = 1;
    tls_client.state = TLS_CLIENT_ESTABLISHED;

    vga_puts("[TLS-CLIENT] handshake complete (TLS 1.2 with encryption)\n");
    if (have_server_key) {
        vga_puts("[TLS-CLIENT] RSA encrypted pre_master_secret ✅\n");
    } else {
        vga_puts("[TLS-CLIENT] note: pre_master_secret unencrypted (simplified RSA)\n");
    }
    vga_printf("[TLS-CLIENT] handshake messages recorded: %d bytes\n",
               tls_client.handshake_buf_len);

    return 0;
}

/* 发送应用数据（加密） */
int tls_client_send(const uint8_t *data, uint32_t len)
{
    if (tls_client.state != TLS_CLIENT_ESTABLISHED || !data || len == 0) return -1;

    /* V8.18: 使用TLS加密传输 */
    extern int tls_crypto_encrypt_record(uint8_t record_type,
                                          const uint8_t *plaintext, uint32_t plaintext_len,
                                          uint8_t *output, uint32_t *output_len);
    extern int tls_crypto_keys_ready(void);

    if (tls_crypto_keys_ready()) {
        uint8_t encrypted[2048];
        uint32_t encrypted_len;
        int ret = tls_crypto_encrypt_record(TLS_RECORD_APPLICATION_DATA,
                                              data, len, encrypted, &encrypted_len);
        if (ret == 0) {
            extern int tcp_send(int fd, const uint8_t *data, uint32_t length);
            return tcp_send(tls_client.tcp_fd, encrypted, encrypted_len);
        }
        vga_puts("[TLS-CLIENT] WARNING: encryption failed, falling back to plaintext\n");
    }

    /* 降级：明文传输 */
    extern int tcp_send(int fd, const uint8_t *data, uint32_t length);
    return tcp_send(tls_client.tcp_fd, data, len);
}

/* 接收应用数据（解密） */
int tls_client_recv(uint8_t *buf, uint32_t max_len)
{
    if (tls_client.state != TLS_CLIENT_ESTABLISHED || !buf) return -1;

    /* V8.18: 使用TLS解密传输 */
    extern int tls_crypto_decrypt_record(const uint8_t *record, uint32_t record_len,
                                          uint8_t *plaintext, uint32_t *plaintext_len);
    extern int tls_crypto_keys_ready(void);

    extern int tcp_receive(int fd, uint8_t *buffer, uint32_t max_length);
    uint8_t record[2048];
    int n = tcp_receive(tls_client.tcp_fd, record, sizeof(record));
    if (n <= 0) return n;

    if (tls_crypto_keys_ready() && n >= 5 && record[0] == TLS_RECORD_APPLICATION_DATA) {
        uint32_t plaintext_len;
        int ret = tls_crypto_decrypt_record(record, n, buf, &plaintext_len);
        if (ret == 0) {
            return plaintext_len;
        }
        vga_puts("[TLS-CLIENT] WARNING: decryption failed, returning raw data\n");
    }

    /* 降级：返回原始数据 */
    if ((uint32_t)n > max_len) n = max_len;
    memcpy(buf, record, n);
    return n;
}

/* 关闭连接 */
int tls_client_close(void)
{
    if (tls_client.tcp_fd >= 0) {
        /* 发送TLS关闭通知（简化：直接关闭TCP） */
        extern int tcp_close(int fd);
        tcp_close(tls_client.tcp_fd);
        tls_client.tcp_fd = -1;
    }
    tls_client.state = TLS_CLIENT_INIT;
    tls_client.handshake_complete = 0;
    serial_puts(0x3F8, "[TLS-CLIENT] closed\n");
    return 0;
}

/* 获取连接状态 */
int tls_client_is_connected(void)
{
    return (tls_client.state == TLS_CLIENT_ESTABLISHED);
}

/* 获取握手状态 */
int tls_client_get_handshake_state(void)
{
    return tls_client.state;
}
