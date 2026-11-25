#ifndef TLS12_CLIENT_H
#define TLS12_CLIENT_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 64 位 —— 真实 TLS 1.2 客户端握手
 *
 * 与旧 kernel/net/tls.c 里的"模拟握手"不同：本模块走真实 TCP 连接，
 * 逐阶段解析对端握手消息，证书/密钥交换/Finished/记录层全部真实校验；
 * 任一阶段失败都会把状态标记为失败并记录阶段与原因，不会置为"已加密"。
 *
 * 支持范围（本次交付）：
 *   - TLS 1.2，AES-128-GCM-SHA256（ECDHE-RSA 为主，RSA 密钥交换兜底）
 *   - 证书链校验：签名验签 / 有效期 / SAN(优先)+CN(兜底) 主机名 / 信任锚
 *   - 真实 transcript 哈希，双向 Finished verify_data 比对
 * 未做：TLS 1.3、ECDSA 套件、ChaCha20、PSK、会话恢复、压缩、ALPN 协商。
 * ========================================================================== */

#define TLS12_OK            0
#define TLS12_FAIL         -1

#define TLS12_HOST_MAX      127
#define TLS12_FAIL_STAGE_MAX  63
#define TLS12_FAIL_REASON_MAX 127

typedef struct {
    int      complete;        /* 仅在服务端 Finished 验签通过后才为 1 */
    uint16_t cipher_suite;    /* 协商成功的密码套件 */
    int      cert_count;      /* 对端证书链条数（含叶子证书） */
    char     fail_stage[TLS12_FAIL_STAGE_MAX + 1];
    char     fail_reason[TLS12_FAIL_REASON_MAX + 1];
} tls12_result_t;

/* 跑完整握手；http_path 非空时，握手成功后发送一条加密 HTTP/1.1 GET，
 * 解密后的应用数据写入 resp（长度由 resp_len 带出）。
 * 返回 TLS12_OK / TLS12_FAIL；细节写入 result（可为 NULL）。 */
int tls12_client_run(const char *hostname, uint16_t port,
                      const char *http_path,
                      uint8_t *resp, uint32_t resp_max, uint32_t *resp_len,
                      tls12_result_t *result);

#endif /* TLS12_CLIENT_H */
