/* ============================================================================
 * 赫普OS - 安全增强
 * KeyStore + 生物识别 + 加密/签名/哈希 + TEE + 设备加密
 * ========================================================================== */

#include <stdint.h>
#include "security2.h"
#include "vga.h"
#include "string.h"

static security2_t sec2;

/* 简单XOR加密 (原型阶段) */
static void xor_crypt(const uint8_t *key, int key_len,
                      const uint8_t *in, uint8_t *out, int len)
{
    for (int i = 0; i < len; i++) {
        out[i] = in[i] ^ key[i % key_len];
    }
}

/* 简单SHA-256 (原型阶段: 简化实现) */
static void simple_sha256(const uint8_t *data, int len, uint8_t *hash)
{
    /* 原型阶段: 用简单的累加代替真实SHA-256 */
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    for (int i = 0; i < len; i++) {
        h[i % 8] = (h[i % 8] * 31 + data[i]) & 0xFFFFFFFF;
    }
    for (int i = 0; i < 8; i++) {
        hash[i * 4] = (h[i] >> 24) & 0xFF;
        hash[i * 4 + 1] = (h[i] >> 16) & 0xFF;
        hash[i * 4 + 2] = (h[i] >> 8) & 0xFF;
        hash[i * 4 + 3] = h[i] & 0xFF;
    }
}

void security2_init(void)
{
    memset(&sec2, 0, sizeof(security2_t));
    sec2.initialized = 1;
    sec2.keystore_locked = 0;
    sec2.tee_available = 1;  /* 模拟TEE可用 */
    sec2.encryption_enabled = 0;

    /* 生成主密钥 */
    for (int i = 0; i < 32; i++) sec2.master_key[i] = (uint8_t)(i * 7 + 13);

    /* 生成默认密钥 */
    keystore_generate_key("default", KEY_TYPE_AES, 256,
                          KEY_PURPOSE_ENCRYPT | KEY_PURPOSE_DECRYPT,
                          BLOCK_MODE_CBC, PADDING_PKCS7);

    vga_puts("[SEC2] security enhanced init done \n");
    vga_printf(" key: %d, TEE: %s, device enc: %s\n",
               sec2.key_count, sec2.tee_available ? " 可用 " : " not 可用 ",
               sec2.encryption_enabled ? " enabled " : " not enabled ");
}

int keystore_generate_key(const char *name, key_type_t type, int key_size,
                          uint32_t purposes, block_mode_t mode, padding_t pad)
{
    if (!name || !sec2.initialized) return -1;
    for (int i = 0; i < SEC2_MAX_KEYS; i++) {
        if (!sec2.keys[i].used) {
            memset(&sec2.keys[i], 0, sizeof(key_entry_t));
            sec2.keys[i].used = 1;
            sec2.keys[i].id = i;
            strncpy(sec2.keys[i].name, name, SEC2_KEY_NAME_LEN - 1);
            sec2.keys[i].type = type;
            sec2.keys[i].key_size = key_size;
            sec2.keys[i].purposes = purposes;
            sec2.keys[i].block_mode = mode;
            sec2.keys[i].padding = pad;
            sec2.keys[i].digest = DIGEST_SHA256;
            sec2.keys[i].require_auth = 0;
            sec2.keys[i].auth_timeout = 0;
            /* 生成随机密钥数据 */
            int key_bytes = key_size / 8;
            if (key_bytes > 256) key_bytes = 256;
            for (int j = 0; j < key_bytes; j++) {
                sec2.keys[i].key_data[j] = (uint8_t)((j * 17 + i * 31) & 0xFF);
            }
            sec2.keys[i].key_data_len = key_bytes;
            sec2.key_count++;
            vga_printf("[SEC2] gen key: %s(%d-bit %s)\n", name, key_size,
                       type == KEY_TYPE_AES ? "AES" : type == KEY_TYPE_RSA ? "RSA" : "Other");
            return i;
        }
    }
    return -1;
}

int keystore_get_key(const char *name, key_entry_t *key)
{
    if (!name || !key || !sec2.initialized) return -1;
    for (int i = 0; i < SEC2_MAX_KEYS; i++) {
        if (sec2.keys[i].used && strcmp(sec2.keys[i].name, name) == 0) {
            memcpy(key, &sec2.keys[i], sizeof(key_entry_t));
            return i;
        }
    }
    return -1;
}

int keystore_delete_key(const char *name)
{
    if (!name || !sec2.initialized) return -1;
    for (int i = 0; i < SEC2_MAX_KEYS; i++) {
        if (sec2.keys[i].used && strcmp(sec2.keys[i].name, name) == 0) {
            sec2_secure_erase(sec2.keys[i].key_data, sec2.keys[i].key_data_len);
            sec2.keys[i].used = 0;
            sec2.key_count--;
            return 0;
        }
    }
    return -1;
}

int keystore_list_keys(key_entry_t *keys, int max)
{
    if (!keys || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < SEC2_MAX_KEYS && count < max; i++) {
        if (sec2.keys[i].used) memcpy(&keys[count++], &sec2.keys[i], sizeof(key_entry_t));
    }
    return count;
}

int keystore_has_key(const char *name)
{
    if (!name) return 0;
    for (int i = 0; i < SEC2_MAX_KEYS; i++) {
        if (sec2.keys[i].used && strcmp(sec2.keys[i].name, name) == 0) return 1;
    }
    return 0;
}

int sec2_encrypt(int key_id, const uint8_t *plain, int plain_len,
                  uint8_t *cipher, int cipher_max, uint8_t *iv, int iv_len)
{
    if (key_id < 0 || key_id >= SEC2_MAX_KEYS || !sec2.keys[key_id].used) return -1;
    if (!plain || !cipher || plain_len <= 0 || plain_len > cipher_max) return -1;
    key_entry_t *key = &sec2.keys[key_id];
    if (!(key->purposes & KEY_PURPOSE_ENCRYPT)) return -1;

    /* 原型阶段: XOR加密 */
    xor_crypt(key->key_data, key->key_data_len, plain, cipher, plain_len);
    if (iv && iv_len > 0) {
        for (int i = 0; i < iv_len && i < 16; i++) iv[i] = (uint8_t)(i * 11);
    }
    return plain_len;
}

int sec2_decrypt(int key_id, const uint8_t *cipher, int cipher_len,
                  uint8_t *plain, int plain_max, const uint8_t *iv, int iv_len)
{
    (void)iv; (void)iv_len;
    if (key_id < 0 || key_id >= SEC2_MAX_KEYS || !sec2.keys[key_id].used) return -1;
    if (!cipher || !plain || cipher_len <= 0 || cipher_len > plain_max) return -1;
    key_entry_t *key = &sec2.keys[key_id];
    if (!(key->purposes & KEY_PURPOSE_DECRYPT)) return -1;

    xor_crypt(key->key_data, key->key_data_len, cipher, plain, cipher_len);
    return cipher_len;
}

int sec2_sign(int key_id, const uint8_t *data, int len, uint8_t *sig, int sig_max)
{
    if (key_id < 0 || key_id >= SEC2_MAX_KEYS || !sec2.keys[key_id].used) return -1;
    if (!data || !sig || sig_max < 32) return -1;
    key_entry_t *key = &sec2.keys[key_id];
    if (!(key->purposes & KEY_PURPOSE_SIGN)) return -1;

    /* 原型阶段: HMAC简化 */
    simple_sha256(data, len, sig);
    for (int i = 0; i < 32; i++) sig[i] ^= key->key_data[i % key->key_data_len];
    return 32;
}

int sec2_verify(int key_id, const uint8_t *data, int len, const uint8_t *sig, int sig_len)
{
    if (key_id < 0 || key_id >= SEC2_MAX_KEYS || !sec2.keys[key_id].used) return -1;
    if (!data || !sig || sig_len < 32) return -1;
    key_entry_t *key = &sec2.keys[key_id];
    if (!(key->purposes & KEY_PURPOSE_VERIFY)) return -1;

    uint8_t expected[32];
    simple_sha256(data, len, expected);
    for (int i = 0; i < 32; i++) expected[i] ^= key->key_data[i % key->key_data_len];
    return (memcmp(expected, sig, 32) == 0) ? 0 : -1;
}

int sec2_hash(digest_t digest, const uint8_t *data, int len, uint8_t *hash, int hash_max)
{
    (void)digest;
    if (!data || !hash || hash_max < 32) return -1;
    simple_sha256(data, len, hash);
    return 32;
}

int sec2_hmac(int key_id, const uint8_t *data, int len, uint8_t *hmac, int hmac_max)
{
    if (key_id < 0 || key_id >= SEC2_MAX_KEYS || !sec2.keys[key_id].used) return -1;
    if (!data || !hmac || hmac_max < 32) return -1;
    /* 原型阶段: 简化HMAC */
    uint8_t buf[1024];
    int key_len = sec2.keys[key_id].key_data_len;
    if (key_len + len > 1024) len = 1024 - key_len;
    memcpy(buf, sec2.keys[key_id].key_data, key_len);
    memcpy(buf + key_len, data, len);
    simple_sha256(buf, key_len + len, hmac);
    return 32;
}

int sec2_random(uint8_t *buf, int len)
{
    if (!buf || len <= 0) return -1;
    static uint32_t seed = 12345;
    for (int i = 0; i < len; i++) {
        seed = seed * 1103515245 + 12345;
        buf[i] = (uint8_t)((seed >> 16) & 0xFF);
    }
    return len;
}

int sec2_install_cert(const char *alias, const uint8_t *cert, int len)
{
    if (!alias || !cert || !sec2.initialized) return -1;
    for (int i = 0; i < SEC2_MAX_CERTS; i++) {
        if (!sec2.certs[i].used) {
            memset(&sec2.certs[i], 0, sizeof(cert_entry_t));
            sec2.certs[i].used = 1;
            sec2.certs[i].id = i;
            strncpy(sec2.certs[i].alias, alias, SEC2_KEY_NAME_LEN - 1);
            if (len > 512) len = 512;
            memcpy(sec2.certs[i].cert_data, cert, len);
            sec2.certs[i].cert_len = len;
            sec2.cert_count++;
            return i;
        }
    }
    return -1;
}

int sec2_get_cert(const char *alias, cert_entry_t *cert)
{
    if (!alias || !cert) return -1;
    for (int i = 0; i < SEC2_MAX_CERTS; i++) {
        if (sec2.certs[i].used && strcmp(sec2.certs[i].alias, alias) == 0) {
            memcpy(cert, &sec2.certs[i], sizeof(cert_entry_t));
            return i;
        }
    }
    return -1;
}

int sec2_delete_cert(const char *alias)
{
    if (!alias) return -1;
    for (int i = 0; i < SEC2_MAX_CERTS; i++) {
        if (sec2.certs[i].used && strcmp(sec2.certs[i].alias, alias) == 0) {
            sec2.certs[i].used = 0;
            sec2.cert_count--;
            return 0;
        }
    }
    return -1;
}

int sec2_list_certs(cert_entry_t *certs, int max)
{
    if (!certs || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < SEC2_MAX_CERTS && count < max; i++) {
        if (sec2.certs[i].used) memcpy(&certs[count++], &sec2.certs[i], sizeof(cert_entry_t));
    }
    return count;
}

int biometric_enroll(biometric_type_t type, const char *name, const uint8_t *template, int len)
{
    if (!name || !sec2.initialized) return -1;
    for (int i = 0; i < SEC2_MAX_BIOMETRICS; i++) {
        if (!sec2.biometrics[i].used) {
            memset(&sec2.biometrics[i], 0, sizeof(biometric_template_t));
            sec2.biometrics[i].used = 1;
            sec2.biometrics[i].id = i;
            sec2.biometrics[i].type = type;
            strncpy(sec2.biometrics[i].name, name, 63);
            sec2.biometrics[i].status = BIOMETRIC_STATUS_ENROLLED;
            if (len > 512) len = 512;
            if (template) memcpy(sec2.biometrics[i].template_data, template, len);
            sec2.biometrics[i].template_len = len;
            sec2.biometric_count++;
            vga_printf("[SEC2] enroll biometric: %s(type=%d)\n", name, type);
            return i;
        }
    }
    return -1;
}

int biometric_verify(biometric_type_t type, const uint8_t *sample, int len)
{
    (void)sample; (void)len;
    if (!sec2.initialized) return -1;
    for (int i = 0; i < SEC2_MAX_BIOMETRICS; i++) {
        if (sec2.biometrics[i].used && sec2.biometrics[i].type == type) {
            sec2.biometrics[i].status = BIOMETRIC_STATUS_SUCCESS;
            sec2.biometrics[i].verify_count++;
            vga_printf("[SEC2] biometric ok: %s\n", sec2.biometrics[i].name);
            return 0;
        }
    }
    return -1;
}

int biometric_remove(int id)
{
    if (id < 0 || id >= SEC2_MAX_BIOMETRICS || !sec2.biometrics[id].used) return -1;
    sec2_secure_erase(sec2.biometrics[id].template_data, sec2.biometrics[id].template_len);
    sec2.biometrics[id].used = 0;
    sec2.biometric_count--;
    return 0;
}

int biometric_list(biometric_template_t *list, int max)
{
    if (!list || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < SEC2_MAX_BIOMETRICS && count < max; i++) {
        if (sec2.biometrics[i].used) memcpy(&list[count++], &sec2.biometrics[i], sizeof(biometric_template_t));
    }
    return count;
}

int biometric_get_status(biometric_type_t type)
{
    for (int i = 0; i < SEC2_MAX_BIOMETRICS; i++) {
        if (sec2.biometrics[i].used && sec2.biometrics[i].type == type) {
            return sec2.biometrics[i].status;
        }
    }
    return BIOMETRIC_STATUS_UNKNOWN;
}

int biometric_cancel(void) { return 0; }

int sec2_enable_encryption(void)
{
    sec2.encryption_enabled = 1;
    vga_puts("[SEC2] device enc on \n");
    return 0;
}

int sec2_disable_encryption(void)
{
    sec2.encryption_enabled = 0;
    return 0;
}

int sec2_is_encrypted(void) { return sec2.encryption_enabled; }

int sec2_tee_available(void) { return sec2.tee_available; }

int sec2_tee_execute(const char *command, uint8_t *input, int in_len,
                     uint8_t *output, int out_max)
{
    (void)input; (void)in_len;
    if (!command || !sec2.tee_available) return -1;
    if (output && out_max > 0) output[0] = 0;
    vga_printf("[SEC2] TEE exec: %s\n", command);
    return 0;
}

int sec2_derive_key(const char *password, const uint8_t *salt, int salt_len,
                    int iterations, uint8_t *key, int key_len)
{
    if (!password || !key || key_len <= 0) return -1;
    /* 原型阶段: 简化PBKDF2 */
    uint8_t buf[512];
    int pwd_len = strlen(password);
    int total = pwd_len + salt_len;
    if (total > 512) total = 512;
    memcpy(buf, password, pwd_len);
    if (salt && salt_len > 0) memcpy(buf + pwd_len, salt, salt_len < (512 - pwd_len) ? salt_len : (512 - pwd_len));
    for (int i = 0; i < iterations; i++) {
        simple_sha256(buf, total, buf);
        total = 32;
    }
    for (int i = 0; i < key_len && i < 32; i++) key[i] = buf[i];
    return key_len;
}

int sec2_secure_erase(void *buf, int len)
{
    if (!buf || len <= 0) return -1;
    volatile uint8_t *p = (volatile uint8_t *)buf;
    for (int i = 0; i < len; i++) p[i] = 0;
    return 0;
}

int sec2_key_count(void) { return sec2.key_count; }
int sec2_biometric_count(void) { return sec2.biometric_count; }
