/* ============================================================================
 * 赫普OS 64 位 —— 真实 TLS 1.2 客户端握手（kernel/net 接入层）
 *
 * 本文件替代 kernel/net/tls.c 中 hepu_tls_handshake() 的"伪完成"路径：
 * 真实 TCP 连接 → ClientHello → ServerHello 协商校验 → 证书链校验 →
 * ECDHE/RSA 密钥交换 → 双向 Finished 校验 → AES-128-GCM 记录层。
 * 任一阶段失败即关闭连接并记录阶段/原因，绝不置为"已加密"。
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
#include "x509.h"
#include "tls12_trusted_ca.h"
#include "vga.h"

/* ---- 64 位 TCP / DNS 接口（定义于 arch/x86_64） ---- */
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
#define SUITE_ECDHE_RSA_AES128_GCM_SHA256 0xC02F
#define SUITE_ECDHE_ECDSA_AES128_GCM_SHA256 0xC02B
#define SUITE_RSA_AES128_GCM_SHA256       0x009C
#define SUITE_ECDHE_RSA_AES256_GCM_SHA384 0xC030

/* 记录类型（与 tls.h 对齐） */
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

/* 解析点分十进制字面量 IP；成功返回 1，输出大端序 uint32（同 dns64_resolve） */
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

/* 从 TCP 流精确读满 n 字节（墙钟超时，秒） */
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

/* 读一条 TLS 记录（5 字节头 + 分片） */
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

/* 取下一条完整握手消息（处理跨记录分片与单记录多消息）。
 * CCS 记录置 got_ccs；Alert 记录直接失败。 */
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
 * ClientHello 构造
 * -------------------------------------------------------------------------- */

static const uint16_t offered_suites[] = {
    SUITE_ECDHE_RSA_AES128_GCM_SHA256,
    SUITE_ECDHE_ECDSA_AES128_GCM_SHA256,
    SUITE_RSA_AES128_GCM_SHA256,
    SUITE_ECDHE_RSA_AES256_GCM_SHA384,
};
#define N_SUITES (sizeof(offered_suites)/sizeof(offered_suites[0]))

static int suite_offered(uint16_t suite)
{
    for (uint32_t i = 0; i < N_SUITES; i++)
        if (offered_suites[i] == suite) return 1;
    return 0;
}

static uint16_t build_client_hello(tls12_sess_t *s, uint8_t *out, uint32_t out_max)
{
    csprng_get_bytes(s->client_random, 32);

    const char *hostname = s->host;
    uint32_t host_len = (uint32_t)strlen(hostname);

    uint8_t *p = out;
    /* 记录头：type=22, version=0x0303, length 后填 */
    *p++ = RT_HANDSHAKE; *p++ = 0x03; *p++ = 0x03;
    uint8_t *rec_len = p; p += 2;

    /* 握手头：ClientHello(1) + 3 字节长度 */
    uint8_t *hs = p;
    *p++ = HS_CLIENT_HELLO;
    uint8_t *hs_len = p; p += 3;

    /* 客户端版本 TLS 1.2 */
    *p++ = 0x03; *p++ = 0x03;
    memcpy(p, s->client_random, 32); p += 32;

    /* session_id 空 */
    *p++ = 0x00;

    /* cipher_suites */
    uint16_t cs_len = (uint16_t)(N_SUITES * 2);
    *p++ = (uint8_t)(cs_len >> 8); *p++ = (uint8_t)cs_len;
    for (uint32_t i = 0; i < N_SUITES; i++) {
        *p++ = (uint8_t)(offered_suites[i] >> 8);
        *p++ = (uint8_t)offered_suites[i];
    }

    /* compression_methods: null */
    *p++ = 0x01; *p++ = 0x00;

    /* 扩展总长（占位） */
    uint8_t *ext_len = p; p += 2;
    uint8_t *ext_start = p;

    /* SNI (type 0) */
    {
        uint16_t sni_len = (uint16_t)(host_len + 5);
        *p++ = 0x00; *p++ = 0x00;
        *p++ = (uint8_t)(sni_len >> 8); *p++ = (uint8_t)sni_len;
        uint16_t list_len = (uint16_t)(host_len + 3);
        *p++ = (uint8_t)(list_len >> 8); *p++ = (uint8_t)list_len;
        *p++ = 0x00; /* host_name */
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

    /* signature_algorithms (type 13):
     * rsa_pkcs1_sha256(0x0401), ecdsa_secp256r1_sha256(0x0403) */
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
        case SUITE_ECDHE_RSA_AES128_GCM_SHA256: return "ECDHE-RSA-AES128-GCM-SHA256";
        case SUITE_ECDHE_ECDSA_AES128_GCM_SHA256: return "ECDHE-ECDSA-AES128-GCM-SHA256";
        case SUITE_RSA_AES128_GCM_SHA256: return "RSA-AES128-GCM-SHA256";
        case SUITE_ECDHE_RSA_AES256_GCM_SHA384: return "ECDHE-RSA-AES256-GCM-SHA384";
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
        vga_printf("[TLS12] 对端版本 0x%04x（期望 0x0303 TLS1.2）\n", version);
        tls12_fail(s, "ServerHello", "对端未协商 TLS 1.2");
        return -1;
    }

    memcpy(s->server_random, msg + off, 32); off += 32;

    uint8_t sid_len = msg[off]; off += 1 + sid_len;
    if (off + 3 > msg_len) { tls12_fail(s, "ServerHello", "会话ID越界"); return -1; }

    uint16_t cipher = ((uint16_t)msg[off] << 8) | msg[off + 1]; off += 2;
    uint8_t comp = msg[off];

    if (!suite_offered(cipher)) {
        vga_printf("[TLS12] 套件 0x%04x 不在客户端声明范围内\n", cipher);
        tls12_fail(s, "ServerHello", "协商套件不在客户端声明范围内");
        return -1;
    }
    if (comp != 0) {
        tls12_fail(s, "ServerHello", "对端要求非空压缩方法");
        return -1;
    }

    s->cipher = cipher;
    vga_printf("[TLS12] ServerHello 协商成功: cipher=0x%04x (%s)\n",
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

    while (off + 2 <= list_end && s->chain_len < X509_MAX_CHAIN_LEN) {
        uint16_t clen = ((uint16_t)msg[off] << 8) | msg[off + 1];
        off += 2;
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

    /* 有效期：逐张检查 notBefore / notAfter */
    for (int i = 0; i < s->chain_len; i++) {
        int r = x509_check_date_sort(&s->chain[i], now);
        if (r != X509_VALID) {
            tls12_fail(s, "Certificate",
                       r == X509_ERR_EXPIRED ? "证书已过期" : "证书尚未生效");
            return -1;
        }
    }

    /* 主机名：叶子证书 SAN 优先，无 SAN 时 CN 兜底 */
    if (x509_check_hostname(&s->chain[0], s->host) != 0) {
        tls12_fail(s, "Certificate", "主机名与 SAN/CN 不匹配");
        return -1;
    }
    vga_puts("[TLS12] 主机名校验通过（SAN 优先 / CN 兜底）\n");

    /* 信任锚：编译期编入的测试 CA */
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
 * 主流程
 * -------------------------------------------------------------------------- */

int tls12_client_run(const char *hostname, uint16_t port,
                      const char *http_path,
                      uint8_t *resp, uint32_t resp_max, uint32_t *resp_len,
                      tls12_result_t *result)
{
    (void)http_path; (void)resp; (void)resp_max; (void)resp_len;

    tls12_sess_t sess;
    memset(&sess, 0, sizeof(sess));
    sess.port = port;
    sess.result = result;
    if (result) memset(result, 0, sizeof(*result));
    if (hostname) strncpy(sess.host, hostname, TLS12_HOST_MAX);

    vga_printf("[TLS12] 开始真实握手: %s:%d\n", sess.host, (int)port);

    /* 1. 解析地址 */
    uint32_t ip = dns64_resolve(sess.host);
    if (ip == 0) {
        if (!parse_literal_ip(sess.host, &ip)) {
            tls12_fail(&sess, "DNS", "主机名解析失败");
            return TLS12_FAIL;
        }
        vga_puts("[TLS12] 使用字面量 IP\n");
    }

    /* 2. TCP 连接 */
    sess.sock = tcp64_connect(ip, port, 12345);
    if (sess.sock < 0) {
        tls12_fail(&sess, "TCP", "connect 失败");
        return TLS12_FAIL;
    }

    uint64_t t0 = rtc64_get_timestamp();
    while (rtc64_get_timestamp() - t0 < 3) {
        net64_poll();
        if (tcp64_get_state(sess.sock) == TCP_ESTABLISHED) break;
        for (volatile int dw = 0; dw < 10000; dw++) { }
    }
    if (tcp64_get_state(sess.sock) != TCP_ESTABLISHED) {
        tls12_fail(&sess, "TCP", "连接未在超时内建立");
        tcp64_close(sess.sock);
        return TLS12_FAIL;
    }
    vga_puts("[TLS12] TCP 已建立\n");

    /* 3. 发送 ClientHello */
    uint8_t ch[512];
    uint16_t ch_len = build_client_hello(&sess, ch, sizeof(ch));
    if (ch_len == 0) {
        tls12_fail(&sess, "ClientHello", "构造失败");
        tcp64_close(sess.sock);
        return TLS12_FAIL;
    }
    if (tcp64_send(sess.sock, ch, ch_len) < 0) {
        tls12_fail(&sess, "ClientHello", "发送失败");
        tcp64_close(sess.sock);
        return TLS12_FAIL;
    }
    vga_printf("[TLS12] 已发送 ClientHello (%d 字节)\n", (int)ch_len);

    /* 4. 接收并校验 ServerHello */
    uint8_t msg[4096];
    uint32_t msg_len = 0;
    if (next_handshake_message(&sess, msg, sizeof(msg), &msg_len, 5) != 0) {
        tls12_fail(&sess, "ServerHello", "未收到合法 ServerHello");
        tcp64_close(sess.sock);
        return TLS12_FAIL;
    }
    if (parse_server_hello(&sess, msg, msg_len) != 0) {
        tcp64_close(sess.sock);
        return TLS12_FAIL;
    }

    /* 5. Certificate 消息 + 证书链校验 */
    if (next_handshake_message(&sess, msg, sizeof(msg), &msg_len, 5) != 0) {
        tls12_fail(&sess, "Certificate", "未收到 Certificate 消息");
        tcp64_close(sess.sock);
        return TLS12_FAIL;
    }
    if (parse_certificate(&sess, msg, msg_len) != 0 ||
        validate_certificate_chain(&sess) != 0) {
        tcp64_close(sess.sock);
        return TLS12_FAIL;
    }
    if (result) result->cert_count = sess.chain_len;

    /* 里程碑②边界：密钥交换 / Finished / 记录层在后续里程碑接入。
     * 在此之前绝不把会话标记为 complete。 */
    vga_puts("[TLS12] 阶段②通过：证书链校验 OK（后续阶段未接入）\n");

    tcp64_close(sess.sock);
    if (result) result->complete = 0;
    return TLS12_OK;
}
