#ifndef X509_H
#define X509_H

#include <stdint.h>

#define X509_MAX_CERT_SIZE 8192
#define X509_MAX_CHAIN_LEN 8
#define X509_MAX_SERIAL_LEN 20
#define X509_MAX_NAME_LEN 256

/* X.509版本 */
#define X509_VERSION_1 0
#define X509_VERSION_2 1
#define X509_VERSION_3 2

/* 签名算法 */
typedef enum {
    X509_SIG_MD5_RSA = 0,
    X509_SIG_SHA1_RSA = 1,
    X509_SIG_SHA256_RSA = 2,
    X509_SIG_SHA384_RSA = 3,
    X509_SIG_SHA512_RSA = 4,
    X509_SIG_ECDSA_SHA256 = 5,
    X509_SIG_ECDSA_SHA384 = 6
} x509_sig_alg_t;

/* 公钥算法 */
typedef enum {
    X509_KEY_RSA = 0,
    X509_KEY_ECDSA = 1,
    X509_KEY_ED25519 = 2
} x509_key_alg_t;

/* X.509证书 */
typedef struct {
    uint8_t  version;
    uint8_t  serial[X509_MAX_SERIAL_LEN];
    int      serial_len;
    x509_sig_alg_t sig_alg;
    x509_key_alg_t key_alg;
    char     issuer[X509_MAX_NAME_LEN];
    char     subject[X509_MAX_NAME_LEN];
    uint32_t not_before;
    uint32_t not_after;
    uint8_t  public_key[512];
    int      public_key_len;
    uint8_t  signature[512];
    int      signature_len;
    int      is_ca;
    int      path_len_constraint;
    int      key_usage;
    int      valid;
    /* V7.76: TBSCertificate位置（用于SHA256哈希计算和RSA验签） */
    const uint8_t *tbs_data;
    uint32_t tbs_len;
} x509_cert_t;

/* 证书验证结果 */
typedef enum {
    X509_VALID = 0,
    X509_ERR_EXPIRED = -1,
    X509_ERR_NOT_YET_VALID = -2,
    X509_ERR_SIGNATURE = -3,
    X509_ERR_CHAIN = -4,
    X509_ERR_UNTRUSTED = -5,
    X509_ERR_REVOKED = -6,
    X509_ERR_PARSE = -7
} x509_verify_result_t;

void x509_init(void);
int x509_parse(const uint8_t *der, uint32_t len, x509_cert_t *cert);
int x509_verify(const x509_cert_t *cert, const x509_cert_t *ca_cert);
int x509_verify_chain(const x509_cert_t *chain, int chain_len,
                      const x509_cert_t *trusted_cas, int ca_count);
int x509_check_date(const x509_cert_t *cert, uint32_t current_time);
int x509_check_hostname(const x509_cert_t *cert, const char *hostname);
int x509_add_trusted_ca(const x509_cert_t *ca);
int x509_get_trusted_cas(x509_cert_t *cas, int max);
const char *x509_verify_error_text(x509_verify_result_t result);

#endif /* X509_H */
