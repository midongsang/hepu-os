#ifndef FSCRYPT_H
#define FSCRYPT_H

#include <stdint.h>

#define FSCRYPT_MAGIC 0x46534352  /* "FSCR" */
#define FSCRYPT_KEY_LEN 32        /* AES-256密钥长度 */
#define FSCRYPT_BLOCK_SIZE 16     /* AES块大小 */
#define FSCRYPT_MAX_FILES 256
#define FSCRYPT_IV_LEN 16        /* IV长度 */
#define FSCRYPT_SALT_LEN 16      /* 盐长度 */

typedef enum {
    FSCRYPT_ALG_XOR = 0,    /* 简化XOR加密（开发用） */
    FSCRYPT_ALG_AES_128_CBC,    /* AES-128-CBC */
    FSCRYPT_ALG_AES_256_CBC,    /* AES-256-CBC */
    FSCRYPT_ALG_AES_256_XTS,    /* AES-256-XTS（未来） */
} fscrypt_alg_t;

typedef struct {
    uint32_t magic;
    uint8_t  algorithm;
    uint8_t  key[FSCRYPT_KEY_LEN];
    uint8_t  salt[FSCRYPT_SALT_LEN];
    uint32_t inode_num;
    int      encrypted;
    uint32_t encrypt_count;
    uint32_t decrypt_count;
} fscrypt_context_t;

/* 加密策略 */
typedef struct {
    fscrypt_alg_t algorithm;
    int           use_padding;      /* PKCS#7填充 */
    int           use_iv_rotation;  /* 每个块不同IV */
} fscrypt_policy_t;

void fscrypt_init(void);
int fscrypt_set_key(const uint8_t *key, uint32_t len);
int fscrypt_encrypt(uint32_t inode, uint8_t *data, uint32_t len);
int fscrypt_decrypt(uint32_t inode, uint8_t *data, uint32_t len);
int fscrypt_encrypt_block(uint32_t inode, uint64_t block_num,
                           uint8_t *data, uint32_t len);
int fscrypt_decrypt_block(uint32_t inode, uint64_t block_num,
                           uint8_t *data, uint32_t len);
int fscrypt_enable(uint32_t inode, fscrypt_alg_t alg);
int fscrypt_disable(uint32_t inode);
int fscrypt_is_encrypted(uint32_t inode);
int fscrypt_status(uint32_t *encrypted_files, uint32_t *total);
int fscrypt_set_policy(const fscrypt_policy_t *policy);
int fscrypt_get_policy(fscrypt_policy_t *policy);
int fscrypt_derive_key(uint32_t inode, const uint8_t *master,
                        uint8_t *derived, uint32_t len);
void fscrypt_generate_iv(uint32_t inode, uint64_t block_num,
                          uint8_t *iv);
int fscrypt_pkcs7_pad(uint8_t *data, uint32_t len, uint32_t block_size);
int fscrypt_pkcs7_unpad(uint8_t *data, uint32_t len);

#endif /* FSCRYPT_H */
