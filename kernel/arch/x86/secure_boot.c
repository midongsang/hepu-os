/* ============================================================================
 * 赫普OS - 安全启动
 * ========================================================================== */

#include <stdint.h>
#include "secure_boot.h"
#include "vga.h"
#include "string.h"
#include "kstdio.h"

static secure_boot_t sb;

void secure_boot_init(void)
{
    memset(&sb, 0, sizeof(secure_boot_t));
    sb.initialized = 1;
    sb.active = 1;
    sb.config.enabled = 1;
    sb.config.strict_mode = 0;
    sb.config.allow_unsigned = 0;
    sb.config.rollback_protection = 1;
    sb.config.secure_boot_version = 1;
    sb.config.minimum_version = 1;
    sb.current_stage = SB_STAGE_ROM;

    /* 安装根密钥 */
    sb_key_install("HEPU Root Key", SB_KEY_ROOT, 1, NULL, 256);
    sb_key_install("HEPU Platform Key", SB_KEY_PLATFORM, 1, NULL, 256);
    sb_key_install("HEPU Signing Key", SB_KEY_SIGNING, 1, NULL, 256);

    vga_puts("[SECBOOT] secure boot init done \n");
    vga_printf(" version: v%d, key: %d, mode: %s\n",
               sb.config.secure_boot_version, sb.key_count,
               sb.config.strict_mode ? " strict " : " standard ");
}

/* 启动流程 */
int sb_start_boot(void)
{
    sb.boot_start_time = 1;
    sb.boot_verified = 0;
    sb.boot_failed = 0;
    sb.fail_reason[0] = 0;
    sb.current_stage = SB_STAGE_ROM;
    sb.total_boots++;
    return 0;
}

int sb_verify_stage(sb_stage_t stage, const char *component,
                    const uint8_t *data, uint64_t size)
{
    sb.current_stage = stage;
    /* 度量 */
    sb_measure(stage, component, data, size);
    /* 简化: 所有阶段验证通过 */
    sb_verify_result_t result = SB_VERIFY_OK;
    if (result != SB_VERIFY_OK) {
        sb.boot_failed = 1;
        snprintf(sb.fail_reason, 256, " orders seg %d verify failed ", stage);
        if (sb.config.strict_mode) return -1;
    }
    return 0;
}

int sb_complete_boot(void)
{
    sb.boot_complete_time = 2;
    if (!sb.boot_failed) {
        sb.boot_verified = 1;
        sb.verified_boots++;
    } else {
        sb.failed_boots++;
    }
    return sb.boot_verified ? 0 : -1;
}

int sb_get_boot_status(int *verified, sb_stage_t *stage)
{
    if (verified) *verified = sb.boot_verified;
    if (stage) *stage = sb.current_stage;
    return 0;
}

/* 验证 */
int sb_verify_signature(const uint8_t *data, uint64_t size,
                        const uint8_t *signature, int key_id)
{
    (void)data; (void)size; (void)signature;
    if (key_id < 0 || key_id >= SB_MAX_KEYS || !sb.keys[key_id].used) return -1;
    if (sb.keys[key_id].revoked) return -1;
    return 0;
}

int sb_verify_hash(const uint8_t *data, uint64_t size,
                   const uint8_t *expected_hash, int algo)
{
    (void)data; (void)size; (void)expected_hash; (void)algo;
    return 0;
}

int sb_verify_certificate(int cert_id)
{
    if (cert_id < 0 || cert_id >= SB_MAX_CERTS || !sb.certs[cert_id].used) return -1;
    if (sb.certs[cert_id].revoked) return -1;
    return sb.certs[cert_id].trusted ? 0 : -1;
}

/* 密钥管理 */
int sb_key_install(const char *name, sb_key_type_t type, int algorithm,
                   const uint8_t *pubkey, int key_size)
{
    for (int i = 0; i < SB_MAX_KEYS; i++) {
        if (!sb.keys[i].used) {
            memset(&sb.keys[i], 0, sizeof(sb_key_t));
            sb.keys[i].used = 1;
            sb.keys[i].id = i;
            if (name) strncpy(sb.keys[i].name, name, 127);
            sb.keys[i].type = type;
            sb.keys[i].algorithm = algorithm;
            if (pubkey) memcpy(sb.keys[i].public_key, pubkey, key_size < 512 ? key_size : 512);
            sb.keys[i].key_size = key_size;
            sb.keys[i].created = 1;
            sb.key_count++;
            return i;
        }
    }
    return -1;
}

int sb_key_revoke(int key_id)
{
    if (key_id < 0 || key_id >= SB_MAX_KEYS || !sb.keys[key_id].used) return -1;
    sb.keys[key_id].revoked = 1;
    return 0;
}

int sb_key_get(int key_id, sb_key_t *key)
{
    if (key_id < 0 || key_id >= SB_MAX_KEYS || !sb.keys[key_id].used || !key) return -1;
    memcpy(key, &sb.keys[key_id], sizeof(sb_key_t));
    return 0;
}

int sb_key_list(sb_key_t *keys, int max)
{
    if (!keys || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < SB_MAX_KEYS && count < max; i++) {
        if (sb.keys[i].used) memcpy(&keys[count++], &sb.keys[i], sizeof(sb_key_t));
    }
    return count;
}

int sb_key_get_by_type(sb_key_type_t type, sb_key_t *key)
{
    if (!key) return -1;
    for (int i = 0; i < SB_MAX_KEYS; i++) {
        if (sb.keys[i].used && sb.keys[i].type == type && !sb.keys[i].revoked) {
            memcpy(key, &sb.keys[i], sizeof(sb_key_t));
            return i;
        }
    }
    return -1;
}

/* 度量 */
int sb_measure(sb_stage_t stage, const char *component,
               const uint8_t *data, uint64_t size)
{
    (void)data;
    for (int i = 0; i < SB_MAX_MEASUREMENTS; i++) {
        if (!sb.measurements[i].used) {
            memset(&sb.measurements[i], 0, sizeof(sb_measurement_t));
            sb.measurements[i].used = 1;
            sb.measurements[i].index = i;
            sb.measurements[i].stage = stage;
            if (component) strncpy(sb.measurements[i].component, component, 127);
            sb.measurements[i].size = size;
            sb.measurements[i].timestamp = 1;
            sb.measurements[i].verify_result = SB_VERIFY_OK;
            /* 简化: 填充假hash */
            for (int j = 0; j < 32; j++) sb.measurements[i].sha256[j] = j;
            sb.measurement_count++;
            return i;
        }
    }
    return -1;
}

int sb_get_measurements(sb_measurement_t *measurements, int max)
{
    if (!measurements || max <= 0) return 0;
    int count = sb.measurement_count < max ? sb.measurement_count : max;
    for (int i = 0; i < count; i++)
        memcpy(&measurements[i], &sb.measurements[i], sizeof(sb_measurement_t));
    return count;
}

int sb_get_measurement(int index, sb_measurement_t *m)
{
    if (index < 0 || index >= SB_MAX_MEASUREMENTS || !sb.measurements[index].used || !m) return -1;
    memcpy(m, &sb.measurements[index], sizeof(sb_measurement_t));
    return 0;
}

/* 证书管理 */
int sb_cert_install(const char *subject, const char *issuer, int algorithm)
{
    for (int i = 0; i < SB_MAX_CERTS; i++) {
        if (!sb.certs[i].used) {
            memset(&sb.certs[i], 0, sizeof(sb_certificate_t));
            sb.certs[i].used = 1;
            sb.certs[i].id = i;
            if (subject) strncpy(sb.certs[i].subject, subject, 127);
            if (issuer) strncpy(sb.certs[i].issuer, issuer, 127);
            sb.certs[i].algorithm = algorithm;
            sb.certs[i].trusted = 1;
            sb.cert_count++;
            return i;
        }
    }
    return -1;
}

int sb_cert_revoke(int cert_id)
{
    if (cert_id < 0 || cert_id >= SB_MAX_CERTS || !sb.certs[cert_id].used) return -1;
    sb.certs[cert_id].revoked = 1;
    return 0;
}

int sb_cert_list(sb_certificate_t *certs, int max)
{
    if (!certs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < SB_MAX_CERTS && count < max; i++) {
        if (sb.certs[i].used) memcpy(&certs[count++], &sb.certs[i], sizeof(sb_certificate_t));
    }
    return count;
}

/* 配置 */
int sb_set_config(const sb_config_t *config)
{
    if (!config) return -1;
    memcpy(&sb.config, config, sizeof(sb_config_t));
    return 0;
}

int sb_get_config(sb_config_t *config)
{
    if (!config) return -1;
    memcpy(config, &sb.config, sizeof(sb_config_t));
    return 0;
}

int sb_set_enabled(int enable)
{
    sb.config.enabled = enable ? 1 : 0;
    return 0;
}

int sb_set_strict_mode(int strict)
{
    sb.config.strict_mode = strict ? 1 : 0;
    return 0;
}

int sb_set_rollback_protection(int enable)
{
    sb.config.rollback_protection = enable ? 1 : 0;
    return 0;
}

/* 防回滚 */
int sb_check_rollback(int version)
{
    if (!sb.config.rollback_protection) return 0;
    return version >= sb.config.minimum_version ? 0 : -1;
}

int sb_update_version(int version)
{
    if (version > sb.config.secure_boot_version)
        sb.config.secure_boot_version = version;
    return 0;
}

/* 统计 */
int sb_get_stats(secure_boot_t *stats)
{
    if (!stats) return -1;
    memcpy(stats, &sb, sizeof(secure_boot_t));
    return 0;
}
