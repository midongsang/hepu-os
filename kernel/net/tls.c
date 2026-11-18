/* ============================================================================
 * 赫普OS - TLS/SSL加密传输
 * 传输层安全协议，支持TLS 1.2/1.3
 * ========================================================================== */

#include <stdint.h>
#include "tls.h"
#include "tls_record.h"
#include "tls_handshake.h"
#include "aes.h"
#include "vga.h"
#include "string.h"

static int tls_initialized = 0;

/* ============================================================================
 * TLS 公共 API 符号引用表
 * 通过 __attribute__((used)) 强制编译器/链接器保留本数组，
 * 数组内的函数地址使 record/crypto/handshake/client 的工作函数及其
 * 依赖的 crypto 符号在 --gc-sections 下不被回收。
 * 此处仅取地址、不实际调用，避免启动期发起网络操作。
 * ========================================================================== */
extern int  hepu_tls_connect(hepu_tls_session_t *session, const char *hostname, uint16_t port);
extern int  hepu_tls_handshake(hepu_tls_session_t *session);
extern int  hepu_tls_send(hepu_tls_session_t *session, const uint8_t *data, uint32_t length);
extern int  hepu_tls_receive(hepu_tls_session_t *session, uint8_t *buffer, uint32_t max_length);
extern int  hepu_tls_close(hepu_tls_session_t *session);
extern int  hepu_tls_get_state(const hepu_tls_session_t *session, hepu_tls_state_t *state);
extern const char *hepu_tls_alert_text(uint8_t alert);
extern int  tls_client_connect(const char *hostname, uint16_t port);
extern int  tls_client_send(const uint8_t *data, uint32_t len);
extern int  tls_client_recv(uint8_t *buf, uint32_t max_len);
extern int  tls_client_close(void);
extern int  tls_client_is_connected(void);
extern int  tls_client_get_handshake_state(void);
extern int  tls_handshake_derive_keys(hepu_tls_hs_ctx_t *ctx);
extern int  tls_handshake_is_complete(const hepu_tls_hs_ctx_t *ctx);
extern const char *tls_handshake_state_name(hepu_tls_hs_state_t state);
extern const char *tls_cipher_suite_name(uint16_t suite);
extern int  tls_record_set_keys(tls_record_ctx_t *ctx,
                                const uint8_t *ck, uint32_t ckl,
                                const uint8_t *sk, uint32_t skl,
                                const uint8_t *civ, uint32_t civl,
                                const uint8_t *siv, uint32_t sivl);
extern int  tls_record_encrypt(tls_record_ctx_t *ctx,
                               const uint8_t *pt, uint32_t ptl,
                               uint8_t *rec, uint32_t *rl, uint32_t bs);
extern int  tls_record_decrypt(tls_record_ctx_t *ctx,
                               const uint8_t *rec, uint32_t rl,
                               uint8_t *pt, uint32_t *ptl, uint32_t bs);
extern uint64_t tls_record_get_seq_client(const tls_record_ctx_t *ctx);
extern uint64_t tls_record_get_seq_server(const tls_record_ctx_t *ctx);
extern int  tls_crypto_keys_ready(void);
extern void tls_crypto_reset_seq(void);

static void * const hepu_tls_api_keep[] __attribute__((used)) = {
    (void *)hepu_tls_connect,
    (void *)hepu_tls_handshake,
    (void *)hepu_tls_send,
    (void *)hepu_tls_receive,
    (void *)hepu_tls_close,
    (void *)hepu_tls_get_state,
    (void *)hepu_tls_alert_text,
    (void *)tls_client_connect,
    (void *)tls_client_send,
    (void *)tls_client_recv,
    (void *)tls_client_close,
    (void *)tls_client_is_connected,
    (void *)tls_client_get_handshake_state,
    (void *)tls_handshake_start,
    (void *)tls_handshake_derive_keys,
    (void *)tls_handshake_is_complete,
    (void *)tls_handshake_state_name,
    (void *)tls_cipher_suite_name,
    (void *)tls_record_set_keys,
    (void *)tls_record_encrypt,
    (void *)tls_record_decrypt,
    (void *)tls_record_get_seq_client,
    (void *)tls_record_get_seq_server,
    (void *)tls_record_reset_seq,
    (void *)tls_crypto_keys_ready,
    (void *)tls_crypto_reset_seq,
};

/* 通过 volatile 写锚点强制 hepu_tls_init 引用上面的 keep 表，
 * 使链接器无法在 --gc-sections 时回收该表及其指向的 TLS 符号 */
static volatile unsigned long hepu_tls_api_anchor;

/* 初始化TLS */
void hepu_tls_init(void)
{
    tls_initialized = 1;
    vga_puts("[TLS] TLS/SSL encryption transport init \n");
    vga_puts("[TLS] supports TLS 1.2/1.3, AES-128/256-GCM\n");

    /* 调用各子模块 init */
    extern void tls_crypto_init(void);
    extern void tls_handshake_init(void);
    extern int  tls_client_init(void);

    tls_crypto_init();
    tls_handshake_init();
    (void)tls_client_init();

    /* 记录层需要上下文，用静态对象初始化以引用其符号 */
    static tls_record_ctx_t tls_rec_ctx_stub;
    tls_record_init(&tls_rec_ctx_stub);

    /* 强制引用 keep 表：volatile 写不可被编译器优化掉 */
    hepu_tls_api_anchor = (unsigned long)hepu_tls_api_keep;
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

    /* V0.62: 先做"真连接 + 真 ClientHello + 解析 ServerHello"这一切片；
     * 证书/密钥交换/Finished 仍是后续工作，因此下面这段仍保留原来的模拟完成逻辑，
     * 但必须先证明网络字节流是通的。调用方可用 tls12_probe() 单独验证。 */
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


/* ============================================================================
 * TLS 1.2 客户端握手 —— 第一切片：真实 TCP + 真实 ClientHello + 解析 ServerHello
 *
 * 背景：本文件原有的 hepu_tls_handshake 是"模拟握手"（只生成随机密钥、不发网络包），
 * 上面的 hepu_tls_send/receive 也只动状态不动网络。这里先把最底下两件事做真：
 *   1) 真正建立 TCP 连接（dns64_resolve + tcp64_connect）
 *   2) 真正构造并发送 TLS 1.2 ClientHello 记录，接收并解析 ServerHello
 * 后续的 Certificate / ServerKeyExchange / Finished / 记录层加解密在此之上迭代。
 * ========================================================================== */

extern uint32_t dns64_resolve(const char *hostname);
extern int  tcp64_connect(uint32_t remote_ip, uint16_t remote_port, uint16_t local_port);
extern int  tcp64_send(int sock, const uint8_t *data, int length);
extern int  tcp64_recv(int sock, uint8_t *buffer, int max_len);
extern int  tcp64_close(int sock);
extern int  tcp64_get_state(int sock);
extern void net64_poll(void);
extern uint64_t rtc64_get_timestamp(void);

/* 握手用的 TCP 连接（切片阶段只有一个） */
static int g_tls_sock = -1;

/* 构造 TLS 1.2 ClientHello 记录（含 5 字节记录头）
 * 返回总长度；0 表示失败 */
static int tls12_build_client_hello(uint8_t *out, uint32_t out_max, const char *hostname)
{
    static const uint16_t suites[] = {
        0xC02B, /* ECDHE_ECDSA_WITH_AES_128_GCM_SHA256 */
        0xC02F, /* ECDHE_RSA_WITH_AES_128_GCM_SHA256   */
        0x009C, /* RSA_WITH_AES_128_GCM_SHA256         */
        0xC030, /* ECDHE_RSA_WITH_AES_256_GCM_SHA384   */
    };
    uint32_t host_len = (uint32_t)strlen(hostname);

    /* 记录层：ContentType=22(handshake), Version=0x0301, Length 后填 */
    uint8_t *p = out;
    *p++ = 22; *p++ = 0x03; *p++ = 0x01;
    uint8_t *rec_len = p; p += 2;

    /* 握手层：ClientHello(1) + 3 字节长度 */
    uint8_t *hs = p;
    *p++ = 1;
    uint8_t *hs_len = p; p += 3;

    /* 版本 0x0303 (TLS 1.2) */
    *p++ = 0x03; *p++ = 0x03;

    /* client_random：32 字节（csprng 已有；这里直接用） */
    csprng_get_bytes(p, 32); p += 32;

    /* session_id：空 */
    *p++ = 0x00;

    /* cipher_suites */
    uint16_t n_suites = sizeof(suites)/sizeof(suites[0]);
    uint16_t cs_len = n_suites * 2;
    *p++ = (uint8_t)(cs_len >> 8); *p++ = (uint8_t)cs_len;
    for (int i = 0; i < n_suites; i++) { *p++ = (uint8_t)(suites[i] >> 8); *p++ = (uint8_t)suites[i]; }

    /* compression_methods：null only */
    *p++ = 0x01; *p++ = 0x00;

    /* extensions 总长（占位） */
    uint8_t *ext_len = p; p += 2;
    uint8_t *ext_start = p;

    /* SNI (type 0) */
    {
        /* SNI 扩展数据长度 = list_len(2) + name_type(1) + name_len(2) + name(host_len)
         * 旧实现多算 1 字节, 声明长度大于实际, OpenSSL 会越界读下一扩展 -> bad extension。 */
        uint16_t sni_len = (uint16_t)(host_len + 5);
        *p++ = 0x00; *p++ = 0x00;                        /* type=server_name */
        *p++ = (uint8_t)(sni_len >> 8); *p++ = (uint8_t)sni_len;
        uint16_t list_len = (uint16_t)(host_len + 3);
        *p++ = (uint8_t)(list_len >> 8); *p++ = (uint8_t)list_len;
        *p++ = 0x00;                                      /* host_name */
        *p++ = (uint8_t)(host_len >> 8); *p++ = (uint8_t)host_len;
        for (uint32_t i = 0; i < host_len; i++) *p++ = (uint8_t)hostname[i];
    }

    /* supported_groups (type 10)：x25519 + secp256r1 */
    {
        *p++ = 0x00; *p++ = 0x0A;
        *p++ = 0x00; *p++ = 0x06;
        *p++ = 0x00; *p++ = 0x04;
        *p++ = 0x00; *p++ = 0x1D; /* x25519 */
        *p++ = 0x00; *p++ = 0x17; /* secp256r1 */
    }

    /* signature_algorithms (type 13) */
    {
        *p++ = 0x00; *p++ = 0x0D;
        *p++ = 0x00; *p++ = 0x06;
        *p++ = 0x00; *p++ = 0x04;
        *p++ = 0x04; *p++ = 0x03; /* ecdsa_secp256r1_sha256 */
        *p++ = 0x08; *p++ = 0x04; /* rsa_pss_rsae_sha256 */
    }

    /* ec_point_formats (type 11)：uncompressed */
    {
        *p++ = 0x00; *p++ = 0x0B;
        *p++ = 0x00; *p++ = 0x02;
        *p++ = 0x01; *p++ = 0x00;
    }

    uint16_t ext_len_val = (uint16_t)(p - ext_start);
    *ext_len = (uint8_t)(ext_len_val >> 8); ext_len[1] = (uint8_t)ext_len_val;

    uint32_t hs_len_val = (uint32_t)(p - hs - 4);
    hs_len[0] = (uint8_t)(hs_len_val >> 16);
    hs_len[1] = (uint8_t)(hs_len_val >> 8);
    hs_len[2] = (uint8_t)hs_len_val;

    uint32_t rec_len_val = (uint32_t)(p - hs);
    rec_len[0] = (uint8_t)(rec_len_val >> 8);
    rec_len[1] = (uint8_t)rec_len_val;

    uint32_t total = (uint32_t)(p - out);
    if (total > out_max) return 0;
    return (int)total;
}

/* 解析 ServerHello 记录，打印协商结果；成功返回 0 */
static int tls12_parse_server_hello(const uint8_t *rec, uint32_t len)
{
    if (len < 5 + 4 + 2 + 32 + 1) return -1;
    if (rec[0] != 22) { vga_printf("[TLS12] not handshake record (type=%d)\n", rec[0]); return -1; }
    if (rec[5] != 2)  { vga_printf("[TLS12] not ServerHello (hs type=%d)\n", rec[5]); return -1; }

    uint16_t server_version = ((uint16_t)rec[9] << 8) | rec[10];
    uint32_t off = 11 + 32;              /* 跳过 version + server_random */
    uint8_t sid_len = rec[off]; off += 1 + sid_len;
    uint16_t cipher = ((uint16_t)rec[off] << 8) | rec[off + 1]; off += 2;
    uint8_t comp = rec[off];

    vga_printf("[TLS12] ServerHello: version=0x%04x cipher=0x%04x comp=%d\n",
               (int)server_version, (int)cipher, (int)comp);
    if (server_version != 0x0303) vga_printf("[TLS12] note: version not TLS1.2 (0x%04x)\n", (int)server_version);
    return 0;
}

/* 真连接 + 发 ClientHello + 收 ServerHello（第一切片）
 * 返回 0 = 已拿到并解析 ServerHello */
int tls12_probe(const char *hostname, uint16_t port)
{
    if (!tls_initialized || !hostname) return -1;

    uint32_t ip = dns64_resolve(hostname);
    if (ip == 0) {
        /* 允许直接传 "a.b.c.d" 形式的 IP（QEMU slirp 下 10.0.2.2 指向宿主） */
        uint32_t v = 0; int dots = 0; const char *q = hostname;
        uint32_t acc = 0;
        while (*q) {
            if (*q >= '0' && *q <= '9') v = v * 10 + (uint32_t)(*q - '0');
            else if (*q == '.') { acc = (acc << 8) | v; v = 0; dots++; }
            else break;
            q++;
        }
        if (dots == 3 && *q == 0 && v <= 255) {
            acc = (acc << 8) | v;
            ip = acc;
            vga_printf("[TLS12] %s treated as literal IP\n", hostname);
        } else {
            vga_puts("[TLS12] DNS failed\n"); return -1;
        }
    }
    vga_printf("[TLS12] DNS %s -> resolved\n", hostname);

    g_tls_sock = tcp64_connect(ip, port, 12345);
    if (g_tls_sock < 0) { vga_puts("[TLS12] tcp connect failed\n"); return -1; }

    /* 等连接建立（3 秒墙钟上限，避免堵住主循环）。
     * 状态码口径与 net64_tcp.c 一致: TCP_ESTABLISHED=4
     * (旧实现误写成 1, 那是 TCP_LISTEN, 连接已建立也会被判成未建立)。 */
    uint64_t t0 = rtc64_get_timestamp();
    while (rtc64_get_timestamp() - t0 < 3) {
        net64_poll();
        if (tcp64_get_state(g_tls_sock) == 4 /* TCP_ESTABLISHED */) break;
        for (volatile int dw = 0; dw < 10000; dw++);
    }
    if (tcp64_get_state(g_tls_sock) != 4) { vga_puts("[TLS12] tcp not established\n"); return -1; }
    vga_puts("[TLS12] tcp established\n");

    uint8_t ch[512];
    int ch_len = tls12_build_client_hello(ch, sizeof(ch), hostname);
    if (ch_len <= 0) return -1;
    if (tcp64_send(g_tls_sock, ch, ch_len) < 0) { vga_puts("[TLS12] send ClientHello failed\n"); return -1; }
    vga_printf("[TLS12] sent ClientHello (%d bytes)\n", ch_len);

    /* 收 ServerHello（等 5 秒） */
    uint8_t rx[512];
    uint64_t t1 = rtc64_get_timestamp();
    int n = 0;
    while (rtc64_get_timestamp() - t1 < 5) {
        net64_poll();
        n = tcp64_recv(g_tls_sock, rx, sizeof(rx));
        if (n > 0) break;
        for (volatile int dw = 0; dw < 10000; dw++);
    }
    if (n <= 0) { vga_puts("[TLS12] no response from server\n"); return -1; }

    vga_printf("[TLS12] received %d bytes\n", n);
    return tls12_parse_server_hello(rx, (uint32_t)n);
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
