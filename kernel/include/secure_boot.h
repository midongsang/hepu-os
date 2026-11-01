#ifndef SECURE_BOOT_H
#define SECURE_BOOT_H

#include <stdint.h>

/* ============================================================================
 * 赫普OS 安全启动
 * 信任链验证 / 密钥管理 / 度量 / 防回滚
 * ========================================================================== */

#define SB_MAX_KEYS 16
#define SB_MAX_MEASUREMENTS 32
#define SB_MAX_CERTS 16
#define SB_NAME_LEN 128

/* 启动阶段 */
typedef enum {
    SB_STAGE_ROM = 0,        /* 固化ROM */
    SB_STAGE_BOOTLOADER,     /* 引导加载器 */
    SB_STAGE_KERNEL,         /* 内核 */
    SB_STAGE_INITRD,         /* 初始内存盘 */
    SB_STAGE_MODULES,        /* 内核模块 */
    SB_STAGE_USERSPACE,      /* 用户空间 */
    SB_STAGE_APPS,           /* 应用 */
} sb_stage_t;

/* 验证结果 */
typedef enum {
    SB_VERIFY_OK = 0,
    SB_VERIFY_FAIL_SIGNATURE,
    SB_VERIFY_FAIL_HASH,
    SB_VERIFY_FAIL_CERT,
    SB_VERIFY_FAIL_REVOCATION,
    SB_VERIFY_FAIL_ROLLBACK,
    SB_VERIFY_FAIL_UNKNOWN,
} sb_verify_result_t;

/* 密钥类型 */
typedef enum {
    SB_KEY_ROOT = 0,
    SB_KEY_PLATFORM,
    SB_KEY_KEK,
    SB_KEY_DB,
    SB_KEY_DBX,
    SB_KEY_SIGNING,
    SB_KEY_VERIFICATION,
} sb_key_type_t;

/* 密钥 */
typedef struct {
    int used;
    int id;
    char name[SB_NAME_LEN];
    sb_key_type_t type;
    int algorithm;  /* 0=RSA-2048,1=RSA-4096,2=ECDSA-P256,3=SM2 */
    uint8_t public_key[512];
    int key_size;
    int revoked;
    uint64_t created;
    uint64_t expires;
} sb_key_t;

/* 度量记录 */
typedef struct {
    int used;
    int index;
    sb_stage_t stage;
    char component[SB_NAME_LEN];
    uint8_t sha256[32];
    uint8_t sha512[64];
    uint64_t size;
    uint64_t timestamp;
    sb_verify_result_t verify_result;
} sb_measurement_t;

/* 证书 */
typedef struct {
    int used;
    int id;
    char subject[SB_NAME_LEN];
    char issuer[SB_NAME_LEN];
    uint64_t valid_from;
    uint64_t valid_to;
    int algorithm;
    int trusted;
    int revoked;
} sb_certificate_t;

/* 安全启动配置 */
typedef struct {
    int enabled;
    int strict_mode;  /* 严格模式: 任何验证失败都中止 */
    int allow_unsigned;  /* 允许未签名组件(开发模式) */
    int rollback_protection;
    int secure_boot_version;
    int minimum_version;  /* 防回滚最低版本 */
} sb_config_t;

/* 安全启动状态 */
typedef struct {
    int initialized;
    int active;
    sb_config_t config;
    sb_stage_t current_stage;
    /* 密钥 */
    sb_key_t keys[SB_MAX_KEYS];
    int key_count;
    /* 度量 */
    sb_measurement_t measurements[SB_MAX_MEASUREMENTS];
    int measurement_count;
    /* 证书 */
    sb_certificate_t certs[SB_MAX_CERTS];
    int cert_count;
    /* 状态 */
    int boot_verified;
    int boot_failed;
    char fail_reason[256];
    uint64_t boot_start_time;
    uint64_t boot_complete_time;
    /* 统计 */
    uint64_t total_boots;
    uint64_t verified_boots;
    uint64_t failed_boots;
} secure_boot_t;

/* 初始化 */
void secure_boot_init(void);

/* 启动流程 */
int sb_start_boot(void);
int sb_verify_stage(sb_stage_t stage, const char *component,
                    const uint8_t *data, uint64_t size);
int sb_complete_boot(void);
int sb_get_boot_status(int *verified, sb_stage_t *stage);

/* 验证 */
int sb_verify_signature(const uint8_t *data, uint64_t size,
                        const uint8_t *signature, int key_id);
int sb_verify_hash(const uint8_t *data, uint64_t size,
                   const uint8_t *expected_hash, int algo);
int sb_verify_certificate(int cert_id);

/* 密钥管理 */
int sb_key_install(const char *name, sb_key_type_t type, int algorithm,
                   const uint8_t *pubkey, int key_size);
int sb_key_revoke(int key_id);
int sb_key_get(int key_id, sb_key_t *key);
int sb_key_list(sb_key_t *keys, int max);
int sb_key_get_by_type(sb_key_type_t type, sb_key_t *key);

/* 度量 */
int sb_measure(sb_stage_t stage, const char *component,
               const uint8_t *data, uint64_t size);
int sb_get_measurements(sb_measurement_t *measurements, int max);
int sb_get_measurement(int index, sb_measurement_t *m);

/* 证书管理 */
int sb_cert_install(const char *subject, const char *issuer, int algorithm);
int sb_cert_revoke(int cert_id);
int sb_cert_list(sb_certificate_t *certs, int max);

/* 配置 */
int sb_set_config(const sb_config_t *config);
int sb_get_config(sb_config_t *config);
int sb_set_enabled(int enable);
int sb_set_strict_mode(int strict);
int sb_set_rollback_protection(int enable);

/* 防回滚 */
int sb_check_rollback(int version);
int sb_update_version(int version);

/* 统计 */
int sb_get_stats(secure_boot_t *stats);

#endif /* SECURE_BOOT_H */
