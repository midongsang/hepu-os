/* ============================================================================
 * 赫普OS - 文件系统加密（完善版）
 * 支持AES-128-CBC和AES-256-CBC真实加密
 * V8.40: 从XOR占位符升级为真实AES加密
 * ========================================================================== */

#include <stdint.h>
#include "fscrypt.h"
#include "vga.h"
#include "string.h"

/* AES实现 */
#include "aes.h"

static uint8_t master_key[FSCRYPT_KEY_LEN];
static int key_set = 0;
static fscrypt_context_t contexts[FSCRYPT_MAX_FILES];
static int fscrypt_initialized = 0;
static fscrypt_policy_t current_policy = {
    .algorithm = FSCRYPT_ALG_AES_256_CBC,
    .use_padding = 1,
    .use_iv_rotation = 1,
};

/* 内存操作 */
extern void *memcpy(void *dst, const void *src, uint32_t n);
extern void *memset(void *dst, int val, uint32_t n);

/* 简单XOR加密（仅用于开发测试） */
static void xor_crypt(uint8_t *data, uint32_t len, uint8_t *key, uint32_t key_len)
{
    for (uint32_t i = 0; i < len; i++) {
        data[i] ^= key[i % key_len];
    }
}

/* 密钥派生：从主密钥派生文件专用密钥
 * 使用简单的HKDF-like机制（实际应使用HMAC-SHA256）
 */
int fscrypt_derive_key(uint32_t inode, const uint8_t *master,
                        uint8_t *derived, uint32_t len)
{
    if (!master || !derived || len == 0) return -1;

    /* 简化：主密钥 XOR inode号，然后重复 */
    uint8_t salt[4] = {
        (inode >> 24) & 0xFF,
        (inode >> 16) & 0xFF,
        (inode >> 8) & 0xFF,
        inode & 0xFF
    };

    for (uint32_t i = 0; i < len; i++) {
        derived[i] = master[i % FSCRYPT_KEY_LEN] ^ salt[i % 4];
        /* 添加一些混淆 */
        derived[i] = (derived[i] * 31 + i * 17) & 0xFF;
    }

    return 0;
}

/* 生成IV：基于inode号和块号 */
void fscrypt_generate_iv(uint32_t inode, uint64_t block_num, uint8_t *iv)
{
    if (!iv) return;

    memset(iv, 0, FSCRYPT_IV_LEN);

    /* 前4字节：inode号 */
    iv[0] = (inode >> 24) & 0xFF;
    iv[1] = (inode >> 16) & 0xFF;
    iv[2] = (inode >> 8) & 0xFF;
    iv[3] = inode & 0xFF;

    /* 后8字节：块号 */
    iv[4] = (block_num >> 56) & 0xFF;
    iv[5] = (block_num >> 48) & 0xFF;
    iv[6] = (block_num >> 40) & 0xFF;
    iv[7] = (block_num >> 32) & 0xFF;
    iv[8] = (block_num >> 24) & 0xFF;
    iv[9] = (block_num >> 16) & 0xFF;
    iv[10] = (block_num >> 8) & 0xFF;
    iv[11] = block_num & 0xFF;

    /* 最后4字节：魔数 */
    iv[12] = 0x48;  /* 'H' */
    iv[13] = 0x45;  /* 'E' */
    iv[14] = 0x50;  /* 'P' */
    iv[15] = 0x55;  /* 'U' */
}

/* PKCS#7填充 */
int fscrypt_pkcs7_pad(uint8_t *data, uint32_t len, uint32_t block_size)
{
    if (!data || block_size == 0) return -1;

    uint32_t pad_len = block_size - (len % block_size);
    if (pad_len == 0) pad_len = block_size;

    for (uint32_t i = 0; i < pad_len; i++) {
        data[len + i] = (uint8_t)pad_len;
    }

    return len + pad_len;
}

/* PKCS#7去填充 */
int fscrypt_pkcs7_unpad(uint8_t *data, uint32_t len)
{
    if (!data || len == 0) return -1;

    uint8_t pad_len = data[len - 1];
    if (pad_len == 0 || pad_len > FSCRYPT_BLOCK_SIZE) {
        return -1;  /* 无效填充 */
    }

    /* 验证填充 */
    for (uint32_t i = 0; i < pad_len; i++) {
        if (data[len - 1 - i] != pad_len) {
            return -1;  /* 填充损坏 */
        }
    }

    return len - pad_len;
}

/* AES-CBC加密 */
static int aes_cbc_encrypt(const uint8_t *key, int key_len,
                            const uint8_t *iv,
                            uint8_t *data, uint32_t len)
{
    aes_ctx_t ctx;
    if (aes_set_key(&ctx, key, key_len) != 0) {
        return -1;
    }

    return aes_encrypt_cbc(&ctx, iv, data, data, len);
}

/* AES-CBC解密 */
static int aes_cbc_decrypt(const uint8_t *key, int key_len,
                            const uint8_t *iv,
                            uint8_t *data, uint32_t len)
{
    aes_ctx_t ctx;
    if (aes_set_key(&ctx, key, key_len) != 0) {
        return -1;
    }

    return aes_decrypt_cbc(&ctx, iv, data, data, len);
}

void fscrypt_init(void)
{
    memset(contexts, 0, sizeof(contexts));
    memset(master_key, 0, sizeof(master_key));
    key_set = 0;
    fscrypt_initialized = 1;

    current_policy.algorithm = FSCRYPT_ALG_AES_256_CBC;
    current_policy.use_padding = 1;
    current_policy.use_iv_rotation = 1;

    vga_puts("[FSCRYPT] fs encryption init (AES-CBC)\n");
}

int fscrypt_set_key(const uint8_t *key, uint32_t len)
{
    if (!fscrypt_initialized || !key || len == 0) return -1;
    uint32_t copy_len = len < FSCRYPT_KEY_LEN ? len : FSCRYPT_KEY_LEN;
    memcpy(master_key, key, copy_len);
    key_set = 1;
    vga_printf("[FSCRYPT] master key set(%d bytes, AES-256)\n", copy_len);
    return 0;
}

/* 加密单个块（带块号IV） */
int fscrypt_encrypt_block(uint32_t inode, uint64_t block_num,
                           uint8_t *data, uint32_t len)
{
    if (!fscrypt_initialized || !key_set || !data || len == 0) return -1;

    /* 查找文件加密上下文 */
    fscrypt_context_t *ctx = NULL;
    for (int i = 0; i < FSCRYPT_MAX_FILES; i++) {
        if (contexts[i].magic == FSCRYPT_MAGIC &&
            contexts[i].inode_num == inode &&
            contexts[i].encrypted) {
            ctx = &contexts[i];
            break;
        }
    }
    if (!ctx) return 0;  /* 未加密，直接返回 */

    /* 派生文件专用密钥 */
    uint8_t file_key[FSCRYPT_KEY_LEN];
    fscrypt_derive_key(inode, master_key, file_key, FSCRYPT_KEY_LEN);

    /* 生成IV */
    uint8_t iv[FSCRYPT_IV_LEN];
    fscrypt_generate_iv(inode, block_num, iv);

    int key_len = (ctx->algorithm == FSCRYPT_ALG_AES_128_CBC) ? 16 : 32;

    switch (ctx->algorithm) {
        case FSCRYPT_ALG_XOR:
            xor_crypt(data, len, file_key, FSCRYPT_KEY_LEN);
            break;

        case FSCRYPT_ALG_AES_128_CBC:
        case FSCRYPT_ALG_AES_256_CBC: {
            /* 对齐到块大小 */
            uint32_t padded_len = len;
            if (current_policy.use_padding) {
                padded_len = fscrypt_pkcs7_pad(data, len, FSCRYPT_BLOCK_SIZE);
                if (padded_len < 0) return -1;
            }

            if (aes_cbc_encrypt(file_key, key_len, iv, data, padded_len) != 0) {
                return -1;
            }
            len = padded_len;
            break;
        }

        default:
            return -1;
    }

    ctx->encrypt_count++;
    return len;
}

/* 解密单个块（带块号IV） */
int fscrypt_decrypt_block(uint32_t inode, uint64_t block_num,
                           uint8_t *data, uint32_t len)
{
    if (!fscrypt_initialized || !key_set || !data || len == 0) return -1;

    /* 查找文件加密上下文 */
    fscrypt_context_t *ctx = NULL;
    for (int i = 0; i < FSCRYPT_MAX_FILES; i++) {
        if (contexts[i].magic == FSCRYPT_MAGIC &&
            contexts[i].inode_num == inode &&
            contexts[i].encrypted) {
            ctx = &contexts[i];
            break;
        }
    }
    if (!ctx) return 0;  /* 未加密，直接返回 */

    /* 派生文件专用密钥 */
    uint8_t file_key[FSCRYPT_KEY_LEN];
    fscrypt_derive_key(inode, master_key, file_key, FSCRYPT_KEY_LEN);

    /* 生成IV */
    uint8_t iv[FSCRYPT_IV_LEN];
    fscrypt_generate_iv(inode, block_num, iv);

    int key_len = (ctx->algorithm == FSCRYPT_ALG_AES_128_CBC) ? 16 : 32;

    switch (ctx->algorithm) {
        case FSCRYPT_ALG_XOR:
            xor_crypt(data, len, file_key, FSCRYPT_KEY_LEN);
            break;

        case FSCRYPT_ALG_AES_128_CBC:
        case FSCRYPT_ALG_AES_256_CBC: {
            if (aes_cbc_decrypt(file_key, key_len, iv, data, len) != 0) {
                return -1;
            }

            /* 去填充 */
            if (current_policy.use_padding) {
                int unpadded_len = fscrypt_pkcs7_unpad(data, len);
                if (unpadded_len < 0) return -1;
                len = unpadded_len;
            }
            break;
        }

        default:
            return -1;
    }

    ctx->decrypt_count++;
    return len;
}

/* 加密（兼容旧接口，使用块号0） */
int fscrypt_encrypt(uint32_t inode, uint8_t *data, uint32_t len)
{
    return fscrypt_encrypt_block(inode, 0, data, len);
}

/* 解密（兼容旧接口，使用块号0） */
int fscrypt_decrypt(uint32_t inode, uint8_t *data, uint32_t len)
{
    return fscrypt_decrypt_block(inode, 0, data, len);
}

int fscrypt_enable(uint32_t inode, fscrypt_alg_t alg)
{
    if (!fscrypt_initialized) return -1;

    /* 查找空闲上下文 */
    fscrypt_context_t *ctx = NULL;
    for (int i = 0; i < FSCRYPT_MAX_FILES; i++) {
        if (contexts[i].magic != FSCRYPT_MAGIC) {
            ctx = &contexts[i];
            break;
        }
    }
    if (!ctx) return -1;

    ctx->magic = FSCRYPT_MAGIC;
    ctx->algorithm = alg;
    ctx->inode_num = inode;
    ctx->encrypted = 1;
    ctx->encrypt_count = 0;
    ctx->decrypt_count = 0;

    /* 生成盐 */
    for (int i = 0; i < FSCRYPT_SALT_LEN; i++) {
        ctx->salt[i] = (uint8_t)(inode * 31 + i * 17 + 0x5A);
    }

    memcpy(ctx->key, master_key, FSCRYPT_KEY_LEN);

    const char *alg_name = "UNKNOWN";
    switch (alg) {
        case FSCRYPT_ALG_XOR: alg_name = "XOR"; break;
        case FSCRYPT_ALG_AES_128_CBC: alg_name = "AES-128-CBC"; break;
        case FSCRYPT_ALG_AES_256_CBC: alg_name = "AES-256-CBC"; break;
    }

    vga_printf("[FSCRYPT] enable: inode=%d alg=%s\n", inode, alg_name);
    return 0;
}

int fscrypt_disable(uint32_t inode)
{
    for (int i = 0; i < FSCRYPT_MAX_FILES; i++) {
        if (contexts[i].magic == FSCRYPT_MAGIC &&
            contexts[i].inode_num == inode) {
            memset(&contexts[i], 0, sizeof(fscrypt_context_t));
            vga_printf("[FSCRYPT] disable: inode=%d\n", inode);
            return 0;
        }
    }
    return -1;
}

int fscrypt_is_encrypted(uint32_t inode)
{
    for (int i = 0; i < FSCRYPT_MAX_FILES; i++) {
        if (contexts[i].magic == FSCRYPT_MAGIC &&
            contexts[i].inode_num == inode &&
            contexts[i].encrypted) {
            return 1;
        }
    }
    return 0;
}

int fscrypt_status(uint32_t *encrypted_files, uint32_t *total)
{
    uint32_t count = 0;
    for (int i = 0; i < FSCRYPT_MAX_FILES; i++) {
        if (contexts[i].magic == FSCRYPT_MAGIC && contexts[i].encrypted) {
            count++;
        }
    }
    if (encrypted_files) *encrypted_files = count;
    if (total) *total = FSCRYPT_MAX_FILES;
    return count;
}

int fscrypt_set_policy(const fscrypt_policy_t *policy)
{
    if (!policy) return -1;
    memcpy(&current_policy, policy, sizeof(fscrypt_policy_t));
    return 0;
}

int fscrypt_get_policy(fscrypt_policy_t *policy)
{
    if (!policy) return -1;
    memcpy(policy, &current_policy, sizeof(fscrypt_policy_t));
    return 0;
}
