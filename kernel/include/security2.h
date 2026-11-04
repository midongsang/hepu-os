#ifndef SECURITY2_H
#define SECURITY2_H

#include <stdint.h>

#define SEC2_MAX_KEYS 64
#define SEC2_MAX_CERTS 16
#define SEC2_KEY_NAME_LEN 64
#define SEC2_MAX_BIOMETRICS 8

/* 密钥类型 */
typedef enum {
    KEY_TYPE_AES = 0,
    KEY_TYPE_RSA,
    KEY_TYPE_EC,
    KEY_TYPE_HMAC,
    KEY_TYPE_DES,
    KEY_TYPE_CHACHA20,
} key_type_t;

/* 密钥用途 */
typedef enum {
    KEY_PURPOSE_ENCRYPT = 1,
    KEY_PURPOSE_DECRYPT = 2,
    KEY_PURPOSE_SIGN = 4,
    KEY_PURPOSE_VERIFY = 8,
    KEY_PURPOSE_WRAP = 16,
    KEY_PURPOSE_DERIVE = 32,
} key_purpose_t;

/* 密钥块模式 */
typedef enum {
    BLOCK_MODE_ECB = 0,
    BLOCK_MODE_CBC,
    BLOCK_MODE_CTR,
    BLOCK_MODE_GCM,
    BLOCK_MODE_CFB,
    BLOCK_MODE_OFB,
} block_mode_t;

/* 填充模式 */
typedef enum {
    PADDING_NONE = 0,
    PADDING_PKCS7,
    PADDING_PKCS1,
    PADDING_OAEP,
} padding_t;

/* 摘要算法 */
typedef enum {
    DIGEST_NONE = 0,
    DIGEST_MD5,
    DIGEST_SHA1,
    DIGEST_SHA224,
    DIGEST_SHA256,
    DIGEST_SHA384,
    DIGEST_SHA512,
} digest_t;

/* 密钥条目 */
typedef struct {
    int used;
    int id;
    char name[SEC2_KEY_NAME_LEN];
    key_type_t type;
    int key_size;  /* bits */
    uint32_t purposes;
    block_mode_t block_mode;
    padding_t padding;
    digest_t digest;
    int require_auth;  /* 需要用户认证 */
    int auth_timeout;  /* 秒 */
    uint64_t creation_time;
    uint64_t last_used;
    int use_count;
    /* 密钥数据 (加密存储) */
    uint8_t key_data[256];
    int key_data_len;
} key_entry_t;

/* 证书 */
typedef struct {
    int used;
    int id;
    char alias[SEC2_KEY_NAME_LEN];
    char subject[128];
    char issuer[128];
    uint64_t valid_from;
    uint64_t valid_to;
    int key_size;
    key_type_t key_type;
    uint8_t cert_data[512];
    int cert_len;
} cert_entry_t;

/* 生物识别类型 */
typedef enum {
    BIOMETRIC_FINGERPRINT = 0,
    BIOMETRIC_FACE,
    BIOMETRIC_IRIS,
    BIOMETRIC_VOICE,
    BIOMETRIC_PIN,
    BIOMETRIC_PATTERN,
    BIOMETRIC_PASSWORD,
} biometric_type_t;

/* 生物识别状态 */
typedef enum {
    BIOMETRIC_STATUS_UNKNOWN = 0,
    BIOMETRIC_STATUS_ENROLLED,
    BIOMETRIC_STATUS_VERIFYING,
    BIOMETRIC_STATUS_SUCCESS,
    BIOMETRIC_STATUS_FAILED,
    BIOMETRIC_STATUS_LOCKED,
} biometric_status_t;

/* 生物识别模板 */
typedef struct {
    int used;
    int id;
    biometric_type_t type;
    char name[64];
    biometric_status_t status;
    uint8_t template_data[512];
    int template_len;
    uint64_t enroll_time;
    uint64_t last_verify;
    int verify_count;
    int failed_count;
} biometric_template_t;

/* 加密上下文 */
typedef struct {
    int key_id;
    int encrypting;  /* 1=加密, 0=解密 */
    uint8_t iv[16];
    int iv_len;
    uint8_t aad[64];
    int aad_len;
    int initialized;
} cipher_context_t;

/* 安全增强状态 */
typedef struct {
    int initialized;
    key_entry_t keys[SEC2_MAX_KEYS];
    cert_entry_t certs[SEC2_MAX_CERTS];
    biometric_template_t biometrics[SEC2_MAX_BIOMETRICS];
    int key_count;
    int cert_count;
    int biometric_count;
    int keystore_locked;
    int tee_available;  /* 可信执行环境 */
    int encryption_enabled;
    uint8_t master_key[32];
} security2_t;

/* 初始化安全增强 */
void security2_init(void);

/* KeyStore */
int keystore_generate_key(const char *name, key_type_t type, int key_size,
                          uint32_t purposes, block_mode_t mode, padding_t pad);
int keystore_get_key(const char *name, key_entry_t *key);
int keystore_delete_key(const char *name);
int keystore_list_keys(key_entry_t *keys, int max);
int keystore_has_key(const char *name);

/* 加解密 */
int sec2_encrypt(int key_id, const uint8_t *plain, int plain_len,
                  uint8_t *cipher, int cipher_max, uint8_t *iv, int iv_len);
int sec2_decrypt(int key_id, const uint8_t *cipher, int cipher_len,
                  uint8_t *plain, int plain_max, const uint8_t *iv, int iv_len);

/* 签名验证 */
int sec2_sign(int key_id, const uint8_t *data, int len, uint8_t *sig, int sig_max);
int sec2_verify(int key_id, const uint8_t *data, int len, const uint8_t *sig, int sig_len);

/* 哈希 */
int sec2_hash(digest_t digest, const uint8_t *data, int len, uint8_t *hash, int hash_max);
int sec2_hmac(int key_id, const uint8_t *data, int len, uint8_t *hmac, int hmac_max);

/* 随机数 */
int sec2_random(uint8_t *buf, int len);

/* 证书管理 */
int sec2_install_cert(const char *alias, const uint8_t *cert, int len);
int sec2_get_cert(const char *alias, cert_entry_t *cert);
int sec2_delete_cert(const char *alias);
int sec2_list_certs(cert_entry_t *certs, int max);

/* 生物识别 */
int biometric_enroll(biometric_type_t type, const char *name, const uint8_t *template, int len);
int biometric_verify(biometric_type_t type, const uint8_t *sample, int len);
int biometric_remove(int id);
int biometric_list(biometric_template_t *list, int max);
int biometric_get_status(biometric_type_t type);
int biometric_cancel(void);

/* 设备加密 */
int sec2_enable_encryption(void);
int sec2_disable_encryption(void);
int sec2_is_encrypted(void);

/* TEE */
int sec2_tee_available(void);
int sec2_tee_execute(const char *command, uint8_t *input, int in_len,
                     uint8_t *output, int out_max);

/* 密钥派生 */
int sec2_derive_key(const char *password, const uint8_t *salt, int salt_len,
                    int iterations, uint8_t *key, int key_len);

/* 安全擦除 */
int sec2_secure_erase(void *buf, int len);

/* 统计 */
int sec2_key_count(void);
int sec2_biometric_count(void);

#endif /* SECURITY2_H */
