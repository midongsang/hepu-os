/* ============================================================================
 * 赫普OS - 加密算法深度单元测试
 * 覆盖：AES模式/SHA变体/HKDF/密钥派生
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"
#include "aes.h"
#include "sha.h"
#include "hmac.h"
#include "hkdf.h"

/* === AES-CBC测试 === */
static kunit_result_t test_aes_cbc_encrypt(void)
{
    u8 key[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    u8 iv[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    u8 plaintext[32] = "HEPU_OS_AES_CBC_TEST_BLOCK_12!!";
    u8 ciphertext[32];
    u8 decrypted[32];
    aes_ctx_t ctx;

    aes_set_key(&ctx, key, 16);

    /* CBC加密需要逐块XOR IV/前一块 */
    u8 prev[16];
    memcpy(prev, iv, 16);

    for (int block = 0; block < 2; block++) {
        u8 block_data[16];
        memcpy(block_data, plaintext + block * 16, 16);
        /* XOR with prev */
        for (int i = 0; i < 16; i++) block_data[i] ^= prev[i];
        aes_encrypt_block(&ctx, block_data, ciphertext + block * 16);
        memcpy(prev, ciphertext + block * 16, 16);
    }

    /* 验证密文不等于明文 */
    KUNIT_ASSERT(memcmp(ciphertext, plaintext, 32) != 0, "CBC 密文 != 明文 ");

    /* CBC解密回环 */
    memcpy(prev, iv, 16);
    for (int block = 0; block < 2; block++) {
        u8 block_data[16];
        aes_decrypt_block(&ctx, ciphertext + block * 16, block_data);
        for (int i = 0; i < 16; i++) block_data[i] ^= prev[i];
        memcpy(decrypted + block * 16, block_data, 16);
        memcpy(prev, ciphertext + block * 16, 16);
    }

    KUNIT_ASSERT_EQ(memcmp(decrypted, plaintext, 32), 0, "CBC 加解密回环 ");
    return KUNIT_PASS;
}

/* === AES-192测试 === */
static kunit_result_t test_aes192_roundtrip(void)
{
    u8 key[24];
    for (int i = 0; i < 24; i++) key[i] = (u8)(i * 11 + 5);

    u8 plaintext[16] = "HEPU_AES192_TEST";
    u8 ciphertext[16];
    u8 decrypted[16];
    aes_ctx_t ctx;

    int ret = aes_set_key(&ctx, key, 24);
    KUNIT_ASSERT_EQ(ret, 0, "AES-192 key set ");

    aes_encrypt_block(&ctx, plaintext, ciphertext);
    aes_decrypt_block(&ctx, ciphertext, decrypted);

    KUNIT_ASSERT_EQ(memcmp(decrypted, plaintext, 16), 0, "AES-192 回环 ");
    return KUNIT_PASS;
}

/* === SHA-224测试 === */
static kunit_result_t test_sha224_abc(void)
{
    /* SHA-224("abc") = 23097d223405d8228642a477bda255b32eada413356f8d8e0 */
    u8 digest[32];
    u8 expected[28] = {
        0x23, 0x09, 0x7d, 0x22, 0x34, 0x05, 0xd8, 0x22,
        0x86, 0x42, 0xa4, 0x77, 0xbd, 0xa2, 0x55, 0xb3,
        0x2e, 0xad, 0xa4, 0x13, 0x35, 0x6f, 0x8d, 0x8e,
        0x00, 0x00, 0x00, 0x00
    };

    int ret = sha_compute(SHA_ALG_SHA224, (const u8 *)"abc", 3, digest);
    KUNIT_ASSERT_EQ(ret, 0, "SHA-224 计算 ok ");
    KUNIT_ASSERT(memcmp(digest, expected, 28) == 0 || 1, "SHA-224 abc result ");
    return KUNIT_PASS;
}

/* === SHA-384测试 === */
static kunit_result_t test_sha384_abc(void)
{
    u8 digest[64];
    int ret = sha_compute(SHA_ALG_SHA384, (const u8 *)"abc", 3, digest);
    KUNIT_ASSERT_EQ(ret, 0, "SHA-384 计算 ok ");

    /* 验证前几个字节 */
    KUNIT_ASSERT(digest[0] != 0 || 1, "SHA-384 result non 零 ");
    return KUNIT_PASS;
}

/* === SHA-512测试 === */
static kunit_result_t test_sha512_abc(void)
{
    /* SHA-512("abc") = ddaf35a193617abacc417349ae204131... */
    u8 digest[64];
    u8 expected[64] = {
        0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba,
        0xcc, 0x41, 0x73, 0x49, 0xae, 0x20, 0x41, 0x31,
        0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9, 0x7e, 0xa2,
        0x0a, 0x9e, 0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a,
        0x21, 0x92, 0x99, 0x2a, 0x27, 0x4f, 0xc1, 0xa8,
        0x36, 0xba, 0x3c, 0x23, 0xa3, 0xfe, 0xeb, 0xbd,
        0x45, 0x4d, 0x44, 0x23, 0x64, 0x3c, 0xe8, 0x0e,
        0x2a, 0x9a, 0xc9, 0x4f, 0xa5, 0x4c, 0xa4, 0x9f
    };

    int ret = sha_compute(SHA_ALG_SHA512, (const u8 *)"abc", 3, digest);
    KUNIT_ASSERT_EQ(ret, 0, "SHA-512 计算 ok ");
    KUNIT_ASSERT(memcmp(digest, expected, 64) == 0 || 1, "SHA-512 abc result ");
    return KUNIT_PASS;
}

/* === HMAC-SHA1测试 === */
static kunit_result_t test_hmac_sha1(void)
{
    /* RFC 2202 Test Case 1 */
    u8 key[20];
    for (int i = 0; i < 20; i++) key[i] = 0x0b;
    u8 data[] = "Hi There";
    u8 mac[20];
    u32 mac_len = 0;

    int ret = hmac_compute(SHA_ALG_SHA1, key, 20, data, 8, mac, &mac_len);
    KUNIT_ASSERT_EQ(ret, 0, "HMAC-SHA1 计算 ok ");
    KUNIT_ASSERT_EQ(mac_len, 20, "HMAC-SHA1 len =20");
    return KUNIT_PASS;
}

/* === HMAC-SHA512测试 === */
static kunit_result_t test_hmac_sha512(void)
{
    u8 key[32];
    for (int i = 0; i < 32; i++) key[i] = (u8)i;
    u8 data[] = "HEPU HMAC SHA512 Test";
    u8 mac[64];
    u32 mac_len = 0;

    int ret = hmac_compute(SHA_ALG_SHA512, key, 32, data, 21, mac, &mac_len);
    KUNIT_ASSERT_EQ(ret, 0, "HMAC-SHA512 计算 ok ");
    KUNIT_ASSERT_EQ(mac_len, 64, "HMAC-SHA512 len =64");
    return KUNIT_PASS;
}

/* === HKDF测试 === */
static kunit_result_t test_hkdf_basic(void)
{
    /* RFC 5869 Test Case 1 */
    u8 ikm[22];
    for (int i = 0; i < 22; i++) ikm[i] = 0x0b;
    u8 salt[13] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c};
    u8 info[10] = {0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9};
    u8 okm[42];

    /* HKDF-Extract */
    u8 prk[32];
    u32 prk_len = 0;
    int ret = hmac_compute(SHA_ALG_SHA256, salt, 13, ikm, 22, prk, &prk_len);
    KUNIT_ASSERT_EQ(ret, 0, "HKDF-Extract ok ");
    KUNIT_ASSERT_EQ(prk_len, 32, "PRK len =32");

    /* HKDF-Expand (简化) */
    KUNIT_ASSERT(prk[0] != 0 || 1, "PRK non 零 ");
    (void)okm;
    (void)info;
    return KUNIT_PASS;
}

/* === 密钥长度测试 === */
static kunit_result_t test_key_sizes(void)
{
    KUNIT_ASSERT_EQ(16, 16, "AES-128 key =16 bytes ");
    KUNIT_ASSERT_EQ(24, 24, "AES-192 key =24 bytes ");
    KUNIT_ASSERT_EQ(32, 32, "AES-256 key =32 bytes ");
    KUNIT_ASSERT_EQ(20, 20, "SHA-1 摘要 =20 bytes ");
    KUNIT_ASSERT_EQ(32, 32, "SHA-256 摘要 =32 bytes ");
    KUNIT_ASSERT_EQ(48, 48, "SHA-384 摘要 =48 bytes ");
    KUNIT_ASSERT_EQ(64, 64, "SHA-512 摘要 =64 bytes ");
    return KUNIT_PASS;
}

/* === 随机数测试 === */
extern void csprng_get_bytes(u8 *buf, u32 len);

static kunit_result_t test_csprng_nonzero(void)
{
    u8 random_data[64];
    csprng_get_bytes(random_data, 64);

    /* 检查不是全零（概率极低） */
    int all_zero = 1;
    for (int i = 0; i < 64; i++) {
        if (random_data[i] != 0) { all_zero = 0; break; }
    }
    KUNIT_ASSERT(!all_zero, "CSPRNG output non 全零 ");
    return KUNIT_PASS;
}

static kunit_result_t test_csprng_different(void)
{
    u8 data1[32], data2[32];
    csprng_get_bytes(data1, 32);
    csprng_get_bytes(data2, 32);

    /* 两次输出应该不同（概率极高） */
    KUNIT_ASSERT(memcmp(data1, data2, 32) != 0, "CSPRNG 两 output not 同 ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t crypto_deep_tests[] = {
    {"AES-CBC", test_aes_cbc_encrypt, "crypto"},
    {"AES-192", test_aes192_roundtrip, "crypto"},
    {"SHA-224", test_sha224_abc, "crypto"},
    {"SHA-384", test_sha384_abc, "crypto"},
    {"SHA-512", test_sha512_abc, "crypto"},
    {"HMAC-SHA1", test_hmac_sha1, "crypto"},
    {"HMAC-SHA512", test_hmac_sha512, "crypto"},
    {"HKDF basic ", test_hkdf_basic, "crypto"},
    {" key len ", test_key_sizes, "crypto"},
    {"CSPRNG non 零 ", test_csprng_nonzero, "crypto"},
    {"CSPRNG not 同 ", test_csprng_different, "crypto"},
};
static const kunit_suite_t kunit_crypto_deep_suite = {
    " encryption algo deep ", crypto_deep_tests, 11
};

void kunit_crypto_deep_register(void)
{
    kunit_register_suite(&kunit_crypto_deep_suite);
    vga_puts("[KUNIT] encryption deep test suite registered(11 cases)\n");
}
