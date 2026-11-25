/* ============================================================================
 * 赫普OS 64 位 —— 真实 TLS 1.2 客户端握手（kernel/net 接入层）
 *
 * 真实 TCP 连接 → ClientHello → ServerHello 协商校验 → 证书链校验 →
 * RSA 密钥交换 → 双向 Finished 校验 → AES-128-GCM 记录层 → 加密 GET。
 * 任一阶段失败即关闭连接并记录阶段/原因，绝不置为"已加密"。
 *
 * 本次支持：
 *   - 套件 TLS_RSA_WITH_AES_128_GCM_SHA256 (0x009C)
 *   - 证书链：签名验签 / 有效期 / SAN(优先)+CN(兜底) / 信任锚
 *   - 真实 transcript(SHA-256)，双向 verify_data 比对
 * 未支持（边界）：TLS 1.3、ECDHE-* 套件、ECDSA 套件、ChaCha20、PSK、
 *   会话恢复、ALPN、压缩。
 *
 * 传输层复用 64 位 TCP API：tcp64_connect/send/recv/close/get_state +
 * net64_poll；DNS 走 dns64_resolve，点分十进制字面量直接解析。
 * ========================================================================== */

#include <stdint.h>
#include <string.h>
#include "tls12_client.h"
#include "tls.h"
#include "csprng.h"
#include "sha.h"
#include "hmac.h"
#include "aes.h"
#include "rsa.h"
#include "x509.h"
#include "tls12_trusted_ca.h"
#include "kstdio.h"
#include "vga.h"

/* ---- 64 位 TCP / DNS / 时钟接口（定义于 arch/x86_64） ---- */
extern uint32_t dns64_resolve(const char *hostname);
extern int  tcp64_connect(uint32_t remote_ip, uint16_t remote_port, uint16_t local_port);
extern int  tcp64_send(int sock, const uint8_t *data, int length);
extern int  tcp64_recv(int sock, uint8_t *buffer, int max_len);
extern int  tcp64_close(int sock);
extern int  tcp64_get_state(int sock);
extern void net64_poll(void);
extern uint64_t rtc64_get_timestamp(void);
extern uint16_t rtc64_get_year(void);
extern uint8_t  rtc64_get_month(void);
extern uint8_t  rtc64_get_day(void);
extern uint8_t  rtc64_get_hours(void);
extern uint8_t  rtc64_get_minutes(void);
extern uint8_t  rtc64_get_seconds(void);

#define TCP_ESTABLISHED 4

/* 密码套件 */
#define SUITE_RSA_AES128_GCM_SHA256       0x009C

/* 记录类型 */
#define RT_CHANGE_CIPHER_SPEC 20
#define RT_ALERT              21
#define RT_HANDSHAKE          22
#define RT_APPLICATION_DATA   23

/* 握手消息类型 */
#define HS_CLIENT_HELLO      1
#define HS_SERVER_HELLO      2
#define HS_CERTIFICATE       11
#define HS_SERVER_KEY_EXCH   12
#define HS_SERVER_HELLO_DONE 14
#define HS_CLIENT_KEY_EXCH   16
#define HS_FINISHED          20

/* 会话上下文（单会话：本次只维护一条测试连接） */
typedef struct {
    int      sock;
    char     host[TLS12_HOST_MAX + 1];
    uint16_t port;

    uint8_t  client_random[32];
    uint8_t  server_random[32];
    uint16_t cipher;

    /* 对端证书链 */
    x509_cert_t chain[X509_MAX_CHAIN_LEN];
    int      chain_len;

    /* 派生密钥 */
    uint8_t  master_secret[48];
    uint8_t  cw_key[16], sw_key[16];
    uint8_t  c_iv[4], s_iv[4];
    uint64_t c_seq, s_seq;

    /* 握手消息重组缓冲 */
    uint8_t  hs_accum[16384];
    uint32_t hs_accum_len;

    int      got_ccs;
    int      complete;

    tls12_result_t *result;
} tls12_sess_t;

/* ----------------------------------------------------------------------------
 * 基础工具
 * -------------------------------------------------------------------------- */

static void tls12_fail(tls12_sess_t *s, const char *stage, const char *reason)
{
    if (s->result) {
        strncpy(s->result->fail_stage, stage, TLS12_FAIL_STAGE_MAX);
        strncpy(s->result->fail_reason, reason, TLS12_FAIL_REASON_MAX);
        s->result->complete = 0;
    }
    vga_printf("[TLS12][FAIL] 阶段=%s 原因=%s\n", stage, reason);
}

static int parse_literal_ip(const char *hostname, uint32_t *out)
{
    uint32_t parts[4] = {0};
    int idx = 0;
    const char *p = hostname;
    if (!*p) return 0;
    while (*p && idx < 4) {
        uint32_t v = 0; int digits = 0;
        while (*p >= '0' && *p <= '9') { v = v*10 + (*p-'0'); if (v > 255) return 0; p++; digits++; }
        if (!digits) return 0;
        parts[idx++] = v;
        if (*p == '.') { p++; if (idx == 4) return 0; }
        else if (*p != 0) return 0;
    }
    if (idx != 4 || *p != 0) return 0;
    *out = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return 1;
}

static int sess_recv(tls12_sess_t *s, uint8_t *buf, uint32_t n, uint32_t timeout_s)
{
    uint32_t got = 0;
    uint64_t t0 = rtc64_get_timestamp();
    while (got < n) {
        net64_poll();
        int r = tcp64_recv(s->sock, buf + got, (int)(n - got));
        if (r > 0) got += (uint32_t)r;
        else if (r < 0) return -1;
        if (rtc64_get_timestamp() - t0 > timeout_s) return -1;
        for (volatile int dw = 0; dw < 10000; dw++) { }
    }
    return 0;
}

static int read_record(tls12_sess_t *s, uint8_t *rec, uint32_t max,
                       uint32_t *rec_len, uint32_t timeout_s)
{
    uint8_t hdr[5];
    if (sess_recv(s, hdr, 5, timeout_s) != 0) return -1;
    uint16_t len = ((uint16_t)hdr[3] << 8) | hdr[4];
    if ((uint32_t)len + 5 > max) return -1;
    memcpy(rec, hdr, 5);
    if (len > 0 && sess_recv(s, rec + 5, len, timeout_s) != 0) return -1;
    *rec_len = (uint32_t)len + 5;
    return 0;
}

static int next_handshake_message(tls12_sess_t *s, uint8_t *msg, uint32_t msg_max,
                                  uint32_t *msg_len, uint32_t timeout_s)
{
    for (;;) {
        if (s->hs_accum_len >= 4) {
            uint32_t mlen = ((uint32_t)s->hs_accum[1] << 16) |
                            ((uint32_t)s->hs_accum[2] << 8) |
                            (uint32_t)s->hs_accum[3];
            if (s->hs_accum_len >= 4 + mlen) {
                if (4 + mlen > msg_max) return -1;
                memcpy(msg, s->hs_accum, 4 + mlen);
                *msg_len = 4 + mlen;
                uint32_t rem = s->hs_accum_len - 4 - mlen;
                if (rem > 0) memmove(s->hs_accum, s->hs_accum + 4 + mlen, rem);
                s->hs_accum_len = rem;
                return 0;
            }
        }

        uint8_t rec[4096];
        uint32_t rlen = 0;
        if (read_record(s, rec, sizeof(rec), &rlen, timeout_s) != 0) return -1;

        if (rec[0] == RT_CHANGE_CIPHER_SPEC) { s->got_ccs = 1; continue; }
        if (rec[0] == RT_ALERT) {
            vga_printf("[TLS12][FAIL] 收到对端 alert (level=%d desc=%d)\n",
                       rec[5], rlen > 6 ? rec[6] : 0);
            return -1;
        }
        if (rec[0] != RT_HANDSHAKE) return -1;

        uint32_t frag = rlen - 5;
        if (s->hs_accum_len + frag > sizeof(s->hs_accum)) return -1;
        memcpy(s->hs_accum + s->hs_accum_len, rec + 5, frag);
        s->hs_accum_len += frag;
    }
}

/* ----------------------------------------------------------------------------
 * 握手 transcript（SHA-256，覆盖双向握手消息字节）
 * -------------------------------------------------------------------------- */

static sha256_ctx_t g_transcript;

static void transcript_reset(void) { sha256_init(&g_transcript); }
static void transcript_add(const uint8_t *msg, uint32_t len)
{
    sha256_update(&g_transcript, msg, len);
}
static void transcript_digest(uint8_t out[32])
{
    /* 复制上下文再 final，避免破坏仍在追加的在线 transcript */
    sha256_ctx_t tmp = g_transcript;
    sha256_final(&tmp, out);
}

/* ----------------------------------------------------------------------------
 * PRF (TLS 1.2, HMAC-SHA256 P_hash)
 * -------------------------------------------------------------------------- */

static void prf_sha256(const uint8_t *secret, uint32_t secret_len,
                        const char *label, const uint8_t *seed, uint32_t seed_len,
                        uint8_t *out, uint32_t out_len)
{
    uint8_t a0[256], a[32], h[32];
    uint32_t a0_len = 0, mlen = 0;
    uint32_t ll = (uint32_t)strlen(label);
    memcpy(a0, label, ll); a0_len = ll;
    memcpy(a0 + a0_len, seed, seed_len); a0_len += seed_len;

    hmac_compute(SHA_ALG_SHA256, secret, secret_len, a0, a0_len, a, &mlen);

    uint32_t off = 0;
    while (off < out_len) {
        uint8_t in[288]; uint32_t il = 0;
        memcpy(in, a, 32); il = 32;
        memcpy(in + il, a0, a0_len); il += a0_len;
        hmac_compute(SHA_ALG_SHA256, secret, secret_len, in, il, h, &mlen);
        uint32_t cp = out_len - off; if (cp > 32) cp = 32;
        memcpy(out + off, h, cp); off += cp;
        hmac_compute(SHA_ALG_SHA256, secret, secret_len, a, 32, a, &mlen);
    }
}

/* ----------------------------------------------------------------------------
 * ClientHello 构造
 * -------------------------------------------------------------------------- */

static uint16_t build_client_hello(tls12_sess_t *s, uint8_t *out, uint32_t out_max)
{
    csprng_get_bytes(s->client_random, 32);

    const char *hostname = s->host;
    uint32_t host_len = (uint32_t)strlen(hostname);

    uint8_t *p = out;
    *p++ = RT_HANDSHAKE; *p++ = 0x03; *p++ = 0x03;
    uint8_t *rec_len = p; p += 2;

    uint8_t *hs = p;
    *p++ = HS_CLIENT_HELLO;
    uint8_t *hs_len = p; p += 3;

    *p++ = 0x03; *p++ = 0x03;
    memcpy(p, s->client_random, 32); p += 32;

    *p++ = 0x00;                              /* session_id 空 */

    /* cipher suites: 长度=2（一个套件 2 字节），RSA_WITH_AES128_GCM_SHA256 */
    *p++ = 0x00; *p++ = 0x02;
    *p++ = (uint8_t)(SUITE_RSA_AES128_GCM_SHA256 >> 8);
    *p++ = (uint8_t)SUITE_RSA_AES128_GCM_SHA256;

    *p++ = 0x01; *p++ = 0x00;                /* compression null */

    uint8_t *ext_len = p; p += 2;
    uint8_t *ext_start = p;

    /* SNI (type 0) */
    {
        uint16_t sni_len = (uint16_t)(host_len + 5);
        *p++ = 0x00; *p++ = 0x00;
        *p++ = (uint8_t)(sni_len >> 8); *p++ = (uint8_t)sni_len;
        uint16_t list_len = (uint16_t)(host_len + 3);
        *p++ = (uint8_t)(list_len >> 8); *p++ = (uint8_t)list_len;
        *p++ = 0x00;
        *p++ = (uint8_t)(host_len >> 8); *p++ = (uint8_t)host_len;
        memcpy(p, hostname, host_len); p += host_len;
    }
    /* supported_groups (type 10): secp256r1(0x17), x25519(0x1D) */
    {
        *p++ = 0x00; *p++ = 0x0A;
        *p++ = 0x00; *p++ = 0x06;
        *p++ = 0x00; *p++ = 0x04;
        *p++ = 0x00; *p++ = 0x17;
        *p++ = 0x00; *p++ = 0x1D;
    }
    /* ec_point_formats (type 11): uncompressed */
    {
        *p++ = 0x00; *p++ = 0x0B;
        *p++ = 0x00; *p++ = 0x02;
        *p++ = 0x01; *p++ = 0x00;
    }
    /* signature_algorithms (type 13): rsa_pkcs1_sha256(0x0401),
     * ecdsa_secp256r1_sha256(0x0403) */
    {
        *p++ = 0x00; *p++ = 0x0D;
        *p++ = 0x00; *p++ = 0x06;
        *p++ = 0x00; *p++ = 0x04;
        *p++ = 0x04; *p++ = 0x01;
        *p++ = 0x04; *p++ = 0x03;
    }

    uint16_t ext_total = (uint16_t)(p - ext_start);
    *ext_len = (uint8_t)(ext_total >> 8); ext_len[1] = (uint8_t)ext_total;

    uint32_t hs_total = (uint32_t)(p - hs - 4);
    hs_len[0] = (uint8_t)(hs_total >> 16);
    hs_len[1] = (uint8_t)(hs_total >> 8);
    hs_len[2] = (uint8_t)hs_total;

    uint32_t rec_total = (uint32_t)(p - hs);
    rec_len[0] = (uint8_t)(rec_total >> 8);
    rec_len[1] = (uint8_t)rec_total;

    uint32_t total = (uint32_t)(p - out);
    if (total > out_max) return 0;
    return (uint16_t)total;
}

/* ----------------------------------------------------------------------------
 * ServerHello 解析与协商校验
 * -------------------------------------------------------------------------- */

static const char *cipher_name(uint16_t suite)
{
    switch (suite) {
        case SUITE_RSA_AES128_GCM_SHA256: return "RSA-AES128-GCM-SHA256";
        default: return "UNKNOWN";
    }
}

static int parse_server_hello(tls12_sess_t *s, const uint8_t *msg, uint32_t msg_len)
{
    if (msg_len < 4 + 2 + 32 + 1 + 2 + 1) {
        tls12_fail(s, "ServerHello", "消息长度不足");
        return -1;
    }
    if (msg[0] != HS_SERVER_HELLO) {
        tls12_fail(s, "ServerHello", "握手类型不是 ServerHello");
        return -1;
    }

    uint32_t off = 4;
    uint16_t version = ((uint16_t)msg[off] << 8) | msg[off + 1];
    off += 2;

    if (version != TLS_VERSION_1_2) {
        vga_printf("[TLS12] 对端版本 0x%x（期望 0x0303 TLS1.2）\n", version);
        tls12_fail(s, "ServerHello", "对端未协商 TLS 1.2");
        return -1;
    }

    memcpy(s->server_random, msg + off, 32); off += 32;

    uint8_t sid_len = msg[off]; off += 1 + sid_len;
    if (off + 3 > msg_len) { tls12_fail(s, "ServerHello", "会话ID越界"); return -1; }

    uint16_t cipher = ((uint16_t)msg[off] << 8) | msg[off + 1]; off += 2;
    uint8_t comp = msg[off];

    if (cipher != SUITE_RSA_AES128_GCM_SHA256) {
        vga_printf("[TLS12] 套件 0x%x 不被本客户端支持\n", cipher);
        tls12_fail(s, "ServerHello", "协商套件不在客户端声明范围内");
        return -1;
    }
    if (comp != 0) {
        tls12_fail(s, "ServerHello", "对端要求非空压缩方法");
        return -1;
    }

    s->cipher = cipher;
    vga_printf("[TLS12] ServerHello: version=0x0303 协商成功 cipher=0x%x (%s)\n",
               cipher, cipher_name(cipher));
    return 0;
}

/* ----------------------------------------------------------------------------
 * Certificate 消息解析与证书链校验
 * -------------------------------------------------------------------------- */

static int parse_certificate(tls12_sess_t *s, const uint8_t *msg, uint32_t msg_len)
{
    if (msg_len < 4 + 3) {
        tls12_fail(s, "Certificate", "消息长度不足");
        return -1;
    }
    if (msg[0] != HS_CERTIFICATE) {
        tls12_fail(s, "Certificate", "握手类型不是 Certificate");
        return -1;
    }

    uint32_t off = 4;
    uint32_t list_len = ((uint32_t)msg[off] << 16) |
                        ((uint32_t)msg[off + 1] << 8) | msg[off + 2];
    off += 3;
    uint32_t list_end = off + list_len;
    if (list_end > msg_len) {
        tls12_fail(s, "Certificate", "证书列表长度越界");
        return -1;
    }

    /* 注意：TLS1.2 Certificate 列表里每张证书的长度前缀是 3 字节（2^24），
     * 不是 2 字节。 */
    while (off + 3 <= list_end && s->chain_len < X509_MAX_CHAIN_LEN) {
        uint32_t clen = ((uint32_t)msg[off] << 16) |
                        ((uint32_t)msg[off + 1] << 8) | msg[off + 2];
        off += 3;
        if (clen == 0 || off + clen > list_end) {
            tls12_fail(s, "Certificate", "单张证书长度非法");
            return -1;
        }
        if (x509_parse(msg + off, clen, &s->chain[s->chain_len]) == 0) {
            s->chain_len++;
        } else {
            vga_printf("[TLS12] 警告: 第 %d 张证书 DER 解析失败\n", s->chain_len);
        }
        off += clen;
    }

    if (s->chain_len == 0) {
        tls12_fail(s, "Certificate", "证书链为空");
        return -1;
    }

    vga_printf("[TLS12] Certificate: 收到 %d 张证书\n", s->chain_len);
    for (int i = 0; i < s->chain_len; i++) {
        vga_printf("[TLS12]   [#%d] subject=%s  issuer=%s\n",
                   i, s->chain[i].subject, s->chain[i].issuer);
    }
    return 0;
}

static int validate_certificate_chain(tls12_sess_t *s)
{
    uint64_t now = x509_make_sort_time(rtc64_get_year(), rtc64_get_month(),
                                         rtc64_get_day(), rtc64_get_hours(),
                                         rtc64_get_minutes(), rtc64_get_seconds());

    for (int i = 0; i < s->chain_len; i++) {
        int r = x509_check_date_sort(&s->chain[i], now);
        if (r != X509_VALID) {
            tls12_fail(s, "Certificate",
                       r == X509_ERR_EXPIRED ? "证书已过期" : "证书尚未生效");
            return -1;
        }
    }

    if (x509_check_hostname(&s->chain[0], s->host) != 0) {
        tls12_fail(s, "Certificate", "主机名与 SAN/CN 不匹配");
        return -1;
    }
    vga_puts("[TLS12] 主机名校验通过（SAN 优先 / CN 兜底）\n");

    if (tls12_trusted_ca_der_len == 0) {
        tls12_fail(s, "Certificate", "内核未编入信任锚 CA");
        return -1;
    }
    x509_cert_t anchor;
    if (x509_parse(tls12_trusted_ca_der, tls12_trusted_ca_der_len, &anchor) != 0) {
        tls12_fail(s, "Certificate", "信任锚解析失败");
        return -1;
    }
    x509_add_trusted_ca(&anchor);

    x509_cert_t trusted[X509_MAX_CHAIN_LEN];
    int n = x509_get_trusted_cas(trusted, X509_MAX_CHAIN_LEN);
    int r = x509_verify_chain(s->chain, s->chain_len, trusted, n);
    if (r != X509_VALID) {
        tls12_fail(s, "Certificate", "证书链验签失败或不受信（unknown CA）");
        return -1;
    }
    vga_puts("[TLS12] 证书链信任锚校验通过\n");
    return 0;
}

/* ----------------------------------------------------------------------------
 * 记录层：TLS 1.2 AES-128-GCM
 * -------------------------------------------------------------------------- */

static int tls12_write_record(tls12_sess_t *s, uint8_t type,
                              const uint8_t *pt, uint32_t pt_len)
{
    uint8_t rec[1600];
    uint32_t body_len = 8 + pt_len + 16;
    if (5 + body_len > sizeof(rec)) return -1;

    uint8_t nonce[12];
    memcpy(nonce, s->c_iv, 4);
    nonce[4] = (uint8_t)(s->c_seq >> 56);
    nonce[5] = (uint8_t)(s->c_seq >> 48);
    nonce[6] = (uint8_t)(s->c_seq >> 40);
    nonce[7] = (uint8_t)(s->c_seq >> 32);
    nonce[8] = (uint8_t)(s->c_seq >> 24);
    nonce[9] = (uint8_t)(s->c_seq >> 16);
    nonce[10]= (uint8_t)(s->c_seq >> 8);
    nonce[11]= (uint8_t)(s->c_seq);

    uint8_t aad[13];
    for (int i = 0; i < 8; i++) aad[i] = (uint8_t)(s->c_seq >> (56 - 8*i));
    aad[8] = type;
    aad[9] = 0x03; aad[10] = 0x03;
    /* AAD 长度字段：明文/密文长度，不含显式 nonce(8) 与认证 tag(16) */
    uint16_t aad_len_field = (uint16_t)pt_len;
    aad[11] = (uint8_t)(aad_len_field >> 8);
    aad[12] = (uint8_t)aad_len_field;

    uint8_t ct[1500], tag[16];
    if (aes_gcm_encrypt(s->cw_key, 16, nonce, 12, aad, 13, pt, ct, pt_len, tag, 16) != 0) {
        tls12_fail(s, "Record", "GCM 加密失败");
        return -1;
    }

    rec[0] = type; rec[1] = 0x03; rec[2] = 0x03;
    rec[3] = (uint8_t)(body_len >> 8); rec[4] = (uint8_t)body_len;
    memcpy(rec + 5, nonce + 4, 8);
    memcpy(rec + 13, ct, pt_len);
    memcpy(rec + 13 + pt_len, tag, 16);

    if (tcp64_send(s->sock, rec, 5 + body_len) < 0) {
        tls12_fail(s, "Record", "记录发送失败");
        return -1;
    }
    s->c_seq++;
    return 0;
}

/* 读一条加密记录并解密；输出明文与内层记录类型。跳过对端 CCS 记录。 */
static int tls12_read_decrypted(tls12_sess_t *s, uint8_t *pt, uint32_t pt_max,
                                 uint32_t *pt_len, uint8_t *inner_type, uint32_t timeout_s)
{
    uint8_t rec[4096];
    uint32_t rlen = 0;
    for (;;) {
        if (read_record(s, rec, sizeof(rec), &rlen, timeout_s) != 0) return -1;
        if (rec[0] == RT_CHANGE_CIPHER_SPEC) { s->got_ccs = 1; continue; }
        break;
    }

    if (rec[0] == RT_ALERT && rlen == 7) {
        int level = rec[5], desc = rlen > 6 ? rec[6] : 0;
        if (level == 1 && desc == 0) {
            vga_puts("[TLS12] 收到 close_notify（明文）\n");
            return 1;
        }
        vga_printf("[TLS12][FAIL] 对端 alert level=%d desc=%d\n", level, desc);
        tls12_fail(s, "Alert", "对端告警关闭");
        return -1;
    }
    if (rec[0] != RT_APPLICATION_DATA && rec[0] != RT_HANDSHAKE &&
        rec[0] != RT_ALERT) return -1;

    uint16_t body_len = ((uint16_t)rec[3] << 8) | rec[4];
    if (body_len < 8 + 16) return -1;
    uint32_t ct_len = body_len - 8 - 16;

    uint8_t nonce[12];
    memcpy(nonce, s->s_iv, 4);
    memcpy(nonce + 4, rec + 5, 8);

    uint8_t aad[13];
    for (int i = 0; i < 8; i++) aad[i] = (uint8_t)(s->s_seq >> (56 - 8*i));
    aad[8] = rec[0];
    aad[9] = 0x03; aad[10] = 0x03;
    /* AAD 长度字段：明文/密文长度，不含显式 nonce(8) 与 tag(16) */
    aad[11] = (uint8_t)(ct_len >> 8);
    aad[12] = (uint8_t)ct_len;

    uint8_t tag[16];
    memcpy(tag, rec + 5 + 8 + ct_len, 16);
    if (ct_len > pt_max) return -1;
    if (aes_gcm_decrypt(s->sw_key, 16, nonce, 12, aad, 13,
                        rec + 13, pt, ct_len, tag, 16) != 0) {
        tls12_fail(s, "Record", "GCM 解密/标签校验失败");
        return -1;
    }
    *pt_len = ct_len;
    *inner_type = rec[0];
    s->s_seq++;
    if (rec[0] == RT_ALERT) {
        if (ct_len >= 2 && pt[0] == 1 && pt[1] == 0) {
            vga_puts("[TLS12] 收到 close_notify（加密）\n");
            return 1;
        }
        vga_printf("[TLS12][FAIL] 对端 alert level=%d desc=%d\n",
                   ct_len > 0 ? pt[0] : -1, ct_len > 1 ? pt[1] : -1);
        tls12_fail(s, "Alert", "对端告警关闭");
        return -1;
    }
    return 0;
}

/* ----------------------------------------------------------------------------
 * 密钥交换 / Finished
 * -------------------------------------------------------------------------- */

static int do_client_key_exchange(tls12_sess_t *s)
{
    /* RSA 密钥交换：PMS = 0x0303 + 46 字节随机，用叶子公钥加密。 */
    uint8_t pms[48];
    pms[0] = 0x03; pms[1] = 0x03;
    csprng_get_bytes(pms + 2, 46);

    rsa_public_key_t pk;
    if (rsa_parse_public_key(&pk, s->chain[0].public_key,
                             s->chain[0].public_key_len) != 0) {
        tls12_fail(s, "ClientKeyExchange", "叶子证书公钥解析失败");
        return -1;
    }

    uint8_t ct[256]; uint32_t ct_len = 0;
    if (rsa_encrypt(&pk, pms, 48, ct, &ct_len) != 0) {
        tls12_fail(s, "ClientKeyExchange", "PMS RSA 加密失败");
        return -1;
    }

    /* 握手消息：type=16, len=2+ct_len */
    uint8_t msg[300];
    uint32_t m = 0;
    msg[m++] = HS_CLIENT_KEY_EXCH;
    msg[m++] = (uint8_t)((2 + ct_len) >> 16);
    msg[m++] = (uint8_t)((2 + ct_len) >> 8);
    msg[m++] = (uint8_t)(2 + ct_len);
    msg[m++] = (uint8_t)(ct_len >> 8);
    msg[m++] = (uint8_t)ct_len;
    memcpy(msg + m, ct, ct_len); m += ct_len;

    transcript_add(msg, m);

    /* 明文握手记录：16 03 03 <len> + 握手消息体 */
    uint8_t rec[300];
    uint32_t r = 0;
    rec[r++] = RT_HANDSHAKE; rec[r++] = 0x03; rec[r++] = 0x03;
    uint16_t rl = (uint16_t)m;
    rec[r++] = (uint8_t)(rl >> 8); rec[r++] = (uint8_t)rl;
    memcpy(rec + r, msg, m); r += m;

    if (tcp64_send(s->sock, rec, (int)r) < 0) {
        tls12_fail(s, "ClientKeyExchange", "发送失败");
        return -1;
    }
    vga_puts("[TLS12] ClientKeyExchange 已发送（RSA 加密 PMS）\n");

    /* PMS -> master_secret -> key_block */
    uint8_t seed[64];
    memcpy(seed, s->client_random, 32);
    memcpy(seed + 32, s->server_random, 32);
    prf_sha256(pms, 48, "master secret", seed, 64, s->master_secret, 48);

    uint8_t kb[40];
    uint8_t seed2[64];
    memcpy(seed2, s->server_random, 32);
    memcpy(seed2 + 32, s->client_random, 32);
    prf_sha256(s->master_secret, 48, "key expansion", seed2, 64, kb, 40);
    memcpy(s->cw_key, kb, 16);
    memcpy(s->sw_key, kb + 16, 16);
    memcpy(s->c_iv, kb + 32, 4);
    memcpy(s->s_iv, kb + 36, 4);
    vga_puts("[TLS12] 密钥派生完成（MS + client/server write key/IV）\n");
    return 0;
}

static int send_finished(tls12_sess_t *s)
{
    uint8_t h[32];
    transcript_digest(h);
    uint8_t vd[12];
    prf_sha256(s->master_secret, 48, "client finished", h, 32, vd, 12);

    uint8_t msg[16];
    msg[0] = HS_FINISHED;
    msg[1] = 0; msg[2] = 0; msg[3] = 12;
    memcpy(msg + 4, vd, 12);
    transcript_add(msg, 16);

    if (tls12_write_record(s, RT_HANDSHAKE, msg, 16) != 0) return -1;
    vga_puts("[TLS12] 客户端 Finished 已发送\n");
    return 0;
}

static int verify_server_finished(tls12_sess_t *s)
{
    uint8_t pt[256]; uint32_t pt_len = 0; uint8_t inner = 0;
    if (tls12_read_decrypted(s, pt, sizeof(pt), &pt_len, &inner, 5) != 0) {
        tls12_fail(s, "Finished", "未收到服务端 Finished");
        return -1;
    }
    if (pt_len != 16 || pt[0] != HS_FINISHED) {
        tls12_fail(s, "Finished", "服务端 Finished 格式非法");
        return -1;
    }

    uint8_t h[32];
    transcript_digest(h);
    uint8_t expect[12];
    prf_sha256(s->master_secret, 48, "server finished", h, 32, expect, 12);


    if (memcmp(expect, pt + 4, 12) != 0) {
        tls12_fail(s, "Finished", "服务端 verify_data 校验失败");
        return -1;
    }
    vga_puts("[TLS12] 服务端 Finished verify_data 校验通过\n");
    return 0;
}

/* ----------------------------------------------------------------------------
 * 主流程
 * -------------------------------------------------------------------------- */

static int handshake_run(tls12_sess_t *s)
{
    /* 1. 地址解析 */
    uint32_t ip = dns64_resolve(s->host);
    if (ip == 0) {
        if (!parse_literal_ip(s->host, &ip)) {
            tls12_fail(s, "DNS", "主机名解析失败");
            return -1;
        }
        vga_puts("[TLS12] 使用字面量 IP\n");
    }

    /* 2. TCP 连接 */
    s->sock = tcp64_connect(ip, s->port, 12345);
    if (s->sock < 0) {
        tls12_fail(s, "TCP", "connect 失败");
        return -1;
    }
    uint64_t t0 = rtc64_get_timestamp();
    while (rtc64_get_timestamp() - t0 < 3) {
        net64_poll();
        if (tcp64_get_state(s->sock) == TCP_ESTABLISHED) break;
        for (volatile int dw = 0; dw < 10000; dw++) { }
    }
    if (tcp64_get_state(s->sock) != TCP_ESTABLISHED) {
        tls12_fail(s, "TCP", "连接未在超时内建立");
        return -1;
    }
    vga_puts("[TLS12] TCP 已建立\n");

    transcript_reset();

    /* 3. ClientHello */
    uint8_t ch[512];
    uint16_t ch_len = build_client_hello(s, ch, sizeof(ch));
    if (ch_len == 0) {
        tls12_fail(s, "ClientHello", "构造失败");
        return -1;
    }
    if (tcp64_send(s->sock, ch, ch_len) < 0) {
        tls12_fail(s, "ClientHello", "发送失败");
        return -1;
    }
    transcript_add(ch + 5, ch_len - 5);
    vga_printf("[TLS12] sent ClientHello 已发送 (%d 字节)\n", (int)ch_len);

    /* 4. ServerHello */
    uint8_t msg[4096];
    uint32_t msg_len = 0;
    if (next_handshake_message(s, msg, sizeof(msg), &msg_len, 5) != 0) {
        tls12_fail(s, "ServerHello", "未收到合法 ServerHello");
        return -1;
    }
    transcript_add(msg, msg_len);
    if (parse_server_hello(s, msg, msg_len) != 0) return -1;

    /* 5. Certificate */
    if (next_handshake_message(s, msg, sizeof(msg), &msg_len, 5) != 0) {
        tls12_fail(s, "Certificate", "未收到 Certificate 消息");
        return -1;
    }
    transcript_add(msg, msg_len);
    if (parse_certificate(s, msg, msg_len) != 0) return -1;
    if (validate_certificate_chain(s) != 0) return -1;
    if (s->result) s->result->cert_count = s->chain_len;

    /* 6. ServerHelloDone（RSA 密钥交换无 ServerKeyExchange） */
    if (next_handshake_message(s, msg, sizeof(msg), &msg_len, 5) != 0) {
        tls12_fail(s, "ServerHelloDone", "未收到 ServerHelloDone");
        return -1;
    }
    if (msg[0] != HS_SERVER_HELLO_DONE) {
        tls12_fail(s, "ServerHelloDone", "握手类型不是 ServerHelloDone");
        return -1;
    }
    transcript_add(msg, msg_len);
    vga_puts("[TLS12] 收到 ServerHelloDone\n");

    /* 7. ClientKeyExchange + 派生 */
    if (do_client_key_exchange(s) != 0) return -1;

    /* 8. ChangeCipherSpec -> Finished（CCS 记录：14 03 03 00 01 01） */
    uint8_t ccs_rec[6] = { RT_CHANGE_CIPHER_SPEC, 0x03, 0x03, 0x00, 0x01, 0x01 };
    if (tcp64_send(s->sock, ccs_rec, 6) < 0) {
        tls12_fail(s, "ChangeCipherSpec", "发送失败");
        return -1;
    }
    vga_puts("[TLS12] 已发送 ChangeCipherSpec（客户端）\n");

    if (send_finished(s) != 0) return -1;

    /* 9. 服务端 Finished 校验 */
    if (verify_server_finished(s) != 0) return -1;

    s->complete = 1;
    vga_puts("[TLS12] 双向 Finished 校验通过，加密通道建立\n");
    return 0;
}

int tls12_client_run(const char *hostname, uint16_t port,
                      const char *http_path,
                      uint8_t *resp, uint32_t resp_max, uint32_t *resp_len,
                      tls12_result_t *result)
{
    /* 单会话：上下文（含证书链缓冲与 16KB 握手重组缓冲）放静态区，避免
     * 在内核栈上放大帧；本次只维护一条测试连接，不需要可重入。 */
    static tls12_sess_t sess;
    memset(&sess, 0, sizeof(sess));
    sess.port = port;
    sess.result = result;
    if (result) memset(result, 0, sizeof(*result));
    if (hostname) strncpy(sess.host, hostname, TLS12_HOST_MAX);

    vga_printf("[TLS12] 开始真实握手: %s:%d\n", sess.host, (int)port);

    int rc = handshake_run(&sess);

    /* 10. 握手成功后发送加密 HTTP GET（可选） */
    if (rc == 0 && sess.complete && http_path && resp && resp_len) {
        char req[320];
        int rl = 0;
        rl += snprintf(req + rl, sizeof(req) - rl, "GET %s HTTP/1.1\r\n", http_path);
        rl += snprintf(req + rl, sizeof(req) - rl, "Host: %s\r\n", sess.host);
        rl += snprintf(req + rl, sizeof(req) - rl, "Connection: close\r\n\r\n");

        if (tls12_write_record(&sess, RT_APPLICATION_DATA, (uint8_t *)req, (uint32_t)rl) != 0) {
            vga_puts("[TLS12][FAIL] 加密 GET 发送失败\n");
        } else {
            vga_puts("[TLS12] 加密 HTTP GET 已发送，等待响应...\n");
            uint32_t total = 0;
            for (int i = 0; i < 20; i++) {
                uint8_t pt[4096]; uint32_t pt_len = 0; uint8_t inner = 0;
                if (tls12_read_decrypted(&sess, pt, sizeof(pt), &pt_len, &inner, 20) != 0) break;
                if (total + pt_len > resp_max) pt_len = resp_max - total;
                memcpy(resp + total, pt, pt_len);
                total += pt_len;
                if (total >= resp_max) break;
            }
            *resp_len = total;
            vga_printf("[TLS12] 收到解密应用数据 %u 字节\n", total);
        }
    }

    tcp64_close(sess.sock);
    if (result) {
        result->complete = sess.complete;
        result->cipher_suite = sess.cipher;
    }
    return rc == 0 ? TLS12_OK : TLS12_FAIL;
}
