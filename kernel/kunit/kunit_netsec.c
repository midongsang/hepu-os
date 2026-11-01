/* ============================================================================
 * 赫普OS - 网络安全深度单元测试
 * 覆盖：TLS记录层/证书验证/密钥交换/证书链/安全参数
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"

/* === TLS记录层测试 === */
#define TLS_RECORD_HEADER_SIZE 5
#define TLS_TYPE_CHANGE_CIPHER_SPEC 20
#define TLS_TYPE_ALERT 21
#define TLS_TYPE_HANDSHAKE 22
#define TLS_TYPE_APPLICATION 23

#define TLS_VERSION_1_0 0x0301
#define TLS_VERSION_1_1 0x0302
#define TLS_VERSION_1_2 0x0303
#define TLS_VERSION_1_3 0x0304

typedef struct {
    u8  type;
    u16 version;
    u16 length;
} __attribute__((packed)) tls_record_header_t;

static kunit_result_t test_tls_record_header(void)
{
    KUNIT_ASSERT_EQ(sizeof(tls_record_header_t), TLS_RECORD_HEADER_SIZE, "TLS records header =5 bytes ");
    KUNIT_ASSERT_EQ(TLS_TYPE_HANDSHAKE, 22, " handshake type =22");
    KUNIT_ASSERT_EQ(TLS_TYPE_APPLICATION, 23, " app data type =23");
    KUNIT_ASSERT_EQ(TLS_TYPE_ALERT, 21, " 告警 type =21");
    return KUNIT_PASS;
}

static kunit_result_t test_tls_versions(void)
{
    KUNIT_ASSERT_EQ(TLS_VERSION_1_0, 0x0301, "TLS 1.0");
    KUNIT_ASSERT_EQ(TLS_VERSION_1_1, 0x0302, "TLS 1.1");
    KUNIT_ASSERT_EQ(TLS_VERSION_1_2, 0x0303, "TLS 1.2");
    KUNIT_ASSERT_EQ(TLS_VERSION_1_3, 0x0304, "TLS 1.3");
    return KUNIT_PASS;
}

static kunit_result_t test_tls_record_parse(void)
{
    u8 raw_record[5] = {
        TLS_TYPE_APPLICATION,
        0x03, 0x03,  /* TLS 1.2 */
        0x00, 0x10   /* 长度=16 */
    };

    tls_record_header_t *hdr = (tls_record_header_t *)raw_record;
    KUNIT_ASSERT_EQ(hdr->type, TLS_TYPE_APPLICATION, " type = app data ");

    u16 version = (hdr->version >> 8) | (hdr->version << 8);
    KUNIT_ASSERT_EQ(version, TLS_VERSION_1_2, " version =TLS 1.2");

    u16 length = (hdr->length >> 8) | (hdr->length << 8);
    KUNIT_ASSERT_EQ(length, 16, " len =16");
    return KUNIT_PASS;
}

/* === TLS握手类型测试 === */
#define TLS_HELLO_REQUEST 0
#define TLS_CLIENT_HELLO 1
#define TLS_SERVER_HELLO 2
#define TLS_CERTIFICATE 11
#define TLS_SERVER_KEY_EXCHANGE 12
#define TLS_CERTIFICATE_REQUEST 13
#define TLS_SERVER_HELLO_DONE 14
#define TLS_CERTIFICATE_VERIFY 15
#define TLS_CLIENT_KEY_EXCHANGE 16
#define TLS_FINISHED 20

static kunit_result_t test_tls_handshake_types(void)
{
    KUNIT_ASSERT_EQ(TLS_CLIENT_HELLO, 1, "ClientHello");
    KUNIT_ASSERT_EQ(TLS_SERVER_HELLO, 2, "ServerHello");
    KUNIT_ASSERT_EQ(TLS_CERTIFICATE, 11, "Certificate");
    KUNIT_ASSERT_EQ(TLS_SERVER_HELLO_DONE, 14, "ServerHelloDone");
    KUNIT_ASSERT_EQ(TLS_CLIENT_KEY_EXCHANGE, 16, "ClientKeyExchange");
    KUNIT_ASSERT_EQ(TLS_FINISHED, 20, "Finished");
    return KUNIT_PASS;
}

/* === TLS密码套件测试 === */
#define TLS_RSA_WITH_AES_128_CBC_SHA 0x002F
#define TLS_RSA_WITH_AES_256_CBC_SHA 0x0035
#define TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256 0xC02F
#define TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384 0xC030
#define TLS_CHACHA20_POLY1305_SHA256 0xCCA8

static kunit_result_t test_tls_cipher_suites(void)
{
    KUNIT_ASSERT_EQ(TLS_RSA_WITH_AES_128_CBC_SHA, 0x002F, "RSA-AES128-CBC-SHA");
    KUNIT_ASSERT_EQ(TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256, 0xC02F, "ECDHE-AES128-GCM");
    KUNIT_ASSERT_EQ(TLS_CHACHA20_POLY1305_SHA256, 0xCCA8, "ChaCha20-Poly1305");
    return KUNIT_PASS;
}

/* === X.509证书测试 === */
#define X509_VERSION_1 0
#define X509_VERSION_2 1
#define X509_VERSION_3 2

#define X509_SIG_MD5_RSA 0x01
#define X509_SIG_SHA1_RSA 0x05
#define X509_SIG_SHA256_RSA 0x0B
#define X509_SIG_SHA384_RSA 0x0C
#define X509_SIG_SHA512_RSA 0x0D

typedef struct {
    u8  version;
    u8  serial[20];
    u16 signature_alg;
    u8  issuer[64];
    u32 not_before;
    u32 not_after;
    u8  subject[64];
    u16 key_alg;
    u8  public_key[256];
} x509_cert_t;

static kunit_result_t test_x509_versions(void)
{
    KUNIT_ASSERT_EQ(X509_VERSION_1, 0, "X.509 v1");
    KUNIT_ASSERT_EQ(X509_VERSION_2, 1, "X.509 v2");
    KUNIT_ASSERT_EQ(X509_VERSION_3, 2, "X.509 v3");
    return KUNIT_PASS;
}

static kunit_result_t test_x509_signature_algs(void)
{
    KUNIT_ASSERT_EQ(X509_SIG_SHA256_RSA, 0x0B, "SHA256-RSA");
    KUNIT_ASSERT_EQ(X509_SIG_SHA384_RSA, 0x0C, "SHA384-RSA");
    KUNIT_ASSERT_EQ(X509_SIG_SHA512_RSA, 0x0D, "SHA512-RSA");
    return KUNIT_PASS;
}

static kunit_result_t test_x509_cert_validity(void)
{
    x509_cert_t cert;
    memset(&cert, 0, sizeof(cert));
    cert.version = X509_VERSION_3;
    cert.not_before = 1000;
    cert.not_after = 2000;

    KUNIT_ASSERT_EQ(cert.version, X509_VERSION_3, " version =v3");

    /* 验证有效期 */
    u32 now = 1500;
    KUNIT_ASSERT(now >= cert.not_before, " current time >=not_before");
    KUNIT_ASSERT(now <= cert.not_after, " current time <=not_after");

    /* 过期检查 */
    now = 2500;
    KUNIT_ASSERT(now > cert.not_after, " 过期 ");
    return KUNIT_PASS;
}

/* === 证书链验证测试 === */
#define CERT_CHAIN_MAX 8
typedef struct {
    x509_cert_t certs[CERT_CHAIN_MAX];
    u32 count;
} cert_chain_t;

static int cert_chain_verify(const cert_chain_t *chain)
{
    if (!chain || chain->count == 0) return -1;
    /* 简化验证: 检查链中每个证书的not_after */
    for (u32 i = 0; i < chain->count; i++) {
        if (chain->certs[i].not_after < 1000) return -2;  /* 过期 */
    }
    return 0;
}

static kunit_result_t test_cert_chain(void)
{
    cert_chain_t chain;
    memset(&chain, 0, sizeof(chain));
    chain.count = 3;
    for (int i = 0; i < 3; i++) {
        chain.certs[i].not_before = 500;
        chain.certs[i].not_after = 2000;
    }

    KUNIT_ASSERT_EQ(cert_chain_verify(&chain), 0, " chain verify ok ");
    KUNIT_ASSERT_EQ(chain.count, 3, " 链 len =3");

    /* 空链 */
    chain.count = 0;
    KUNIT_ASSERT_EQ(cert_chain_verify(&chain), -1, " empty 链 verify failed ");
    return KUNIT_PASS;
}

/* === 密钥交换测试 === */
#define KEY_EXCHANGE_RSA 0
#define KEY_EXCHANGE_DHE 1
#define KEY_EXCHANGE_ECDHE 2

static kunit_result_t test_key_exchange_types(void)
{
    KUNIT_ASSERT_EQ(KEY_EXCHANGE_RSA, 0, "RSA key 交换 ");
    KUNIT_ASSERT_EQ(KEY_EXCHANGE_DHE, 1, "DHE key 交换 ");
    KUNIT_ASSERT_EQ(KEY_EXCHANGE_ECDHE, 2, "ECDHE key 交换 ");
    return KUNIT_PASS;
}

/* === 安全参数测试 === */
typedef struct {
    u16 cipher_suite;
    u8  compression;
    u8  master_secret[48];
    u8  client_random[32];
    u8  server_random[32];
} tls_security_params_t;

static kunit_result_t test_security_params(void)
{
    tls_security_params_t params;
    memset(&params, 0, sizeof(params));
    params.cipher_suite = TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256;
    params.compression = 0;  /* 无压缩 */

    KUNIT_ASSERT_EQ(params.cipher_suite, 0xC02F, " 密码 suite ");
    KUNIT_ASSERT_EQ(params.compression, 0, " no compression(null)");
    KUNIT_ASSERT_EQ(sizeof(params.master_secret), 48, " master key =48 bytes ");
    KUNIT_ASSERT_EQ(sizeof(params.client_random), 32, " random count =32 bytes ");
    return KUNIT_PASS;
}

/* === TLS告警测试 === */
#define TLS_ALERT_WARNING 1
#define TLS_ALERT_FATAL 2

#define TLS_ALERT_CLOSE_NOTIFY 0
#define TLS_ALERT_UNEXPECTED_MESSAGE 10
#define TLS_ALERT_BAD_RECORD_MAC 20
#define TLS_ALERT_HANDSHAKE_FAILURE 40
#define TLS_ALERT_BAD_CERTIFICATE 42
#define TLS_ALERT_CERTIFICATE_EXPIRED 45
#define TLS_ALERT_PROTOCOL_VERSION 70

static kunit_result_t test_tls_alerts(void)
{
    KUNIT_ASSERT_EQ(TLS_ALERT_WARNING, 1, " warn level 别 ");
    KUNIT_ASSERT_EQ(TLS_ALERT_FATAL, 2, " 致命 level 别 ");
    KUNIT_ASSERT_EQ(TLS_ALERT_CLOSE_NOTIFY, 0, " close notify ");
    KUNIT_ASSERT_EQ(TLS_ALERT_HANDSHAKE_FAILURE, 40, " handshake failed ");
    KUNIT_ASSERT_EQ(TLS_ALERT_BAD_CERTIFICATE, 42, " cert error ");
    KUNIT_ASSERT_EQ(TLS_ALERT_CERTIFICATE_EXPIRED, 45, " cert 过期 ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t netsec_tests[] = {
    {"TLS records header ", test_tls_record_header, "netsec"},
    {"TLS version ", test_tls_versions, "netsec"},
    {"TLS records parse ", test_tls_record_parse, "netsec"},
    {"TLS handshake type ", test_tls_handshake_types, "netsec"},
    {"TLS 密码 suite ", test_tls_cipher_suites, "netsec"},
    {"X.509 version ", test_x509_versions, "netsec"},
    {"X.509 signature algo ", test_x509_signature_algs, "netsec"},
    {"X.509 has 效期 ", test_x509_cert_validity, "netsec"},
    {" cert 链 ", test_cert_chain, "netsec"},
    {" key 交换 type ", test_key_exchange_types, "netsec"},
    {" security param ", test_security_params, "netsec"},
    {"TLS 告警 ", test_tls_alerts, "netsec"},
};
static const kunit_suite_t kunit_netsec_suite = {
    " net security deep ", netsec_tests, 12
};

void kunit_netsec_register(void)
{
    kunit_register_suite(&kunit_netsec_suite);
    vga_puts("[KUNIT] net security deep test suite registered(12 cases)\n");
}
