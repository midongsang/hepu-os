#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>

/* XTEA加密常量 */
#define XTEA_ROUNDS      32     /* 加密轮数 */
#define XTEA_KEY_SIZE    16     /* 密钥大小（128位） */
#define XTEA_BLOCK_SIZE  8      /* XTEA块大小（64位） */

/* 加密模式 */
#define CRYPTO_MODE_ECB  0  /* 电子密码本（简单，不安全） */
#define CRYPTO_MODE_XTS  1  /* XTS模式（更安全，需要两个密钥） */

/* 加密上下文 */
typedef struct {
    uint32_t key[4];       /* 密钥（128位） */
    uint32_t tweak_key[4]; /* XTS调整密钥（可选） */
    int      mode;          /* 加密模式 */
    int      initialized;   /* 是否初始化 */
} crypto_ctx_t;

/* 初始化加密上下文 */
int crypto_init(crypto_ctx_t *ctx, const uint8_t *key, int mode);

/* 设置XTS调整密钥 */
int crypto_set_tweak_key(crypto_ctx_t *ctx, const uint8_t *tweak_key);

/* XTEA加密一个64位块 */
void xtea_encrypt_block(const uint32_t key[4], uint32_t *v0, uint32_t *v1);

/* XTEA解密一个64位块 */
void xtea_decrypt_block(const uint32_t key[4], uint32_t *v0, uint32_t *v1);

/* 加密数据（ECB模式，长度必须是8的倍数） */
int crypto_encrypt_ecb(crypto_ctx_t *ctx, const void *src, void *dst, uint32_t len);

/* 解密数据（ECB模式） */
int crypto_decrypt_ecb(crypto_ctx_t *ctx, const void *src, void *dst, uint32_t len);

/* 加密数据（XTS模式，len可以是任意长度，最后一个块用Ciphertext Stealing） */
int crypto_encrypt_xts(crypto_ctx_t *ctx, const void *src, void *dst,
                         uint32_t len, uint64_t sector);

/* 解密数据（XTS模式） */
int crypto_decrypt_xts(crypto_ctx_t *ctx, const void *src, void *dst,
                         uint32_t len, uint64_t sector);

/* 加密一个4KB页（使用XTS模式，页号作为tweak） */
int crypto_encrypt_page(crypto_ctx_t *ctx, const void *src, void *dst, uint32_t page_no);

/* 解密一个4KB页 */
int crypto_decrypt_page(crypto_ctx_t *ctx, const void *src, void *dst, uint32_t page_no);

/* 生成随机密钥 */
void crypto_generate_key(uint8_t *key, uint32_t seed);

/* 自测 */
void crypto_selftest(void);

#endif
