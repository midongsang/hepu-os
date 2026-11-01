/* ============================================================================
 * 赫普OS - 加密模块单元测试
 * 覆盖：AES/SHA/HMAC/HKDF
 * ========================================================================== */

#include <stdint.h>
#include "kunit.h"
#include "vga.h"
#include "string.h"
#include "sha.h"
#include "hmac.h"
#include "aes.h"

/* === AES测试 === */


static kunit_result_t test_aes128_ecb(void)
{
    /* NIST AES-128测试向量 */
    u8 key[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    u8 plaintext[16] = {
        0x32, 0x43, 0xf6, 0xa8, 0x88, 0x5a, 0x30, 0x8d,
        0x31, 0x31, 0x98, 0xa2, 0xe0, 0x37, 0x07, 0x34
    };
    u8 ciphertext[16];
    u8 decrypted[16];
    aes_ctx_t ctx;

    int ret = aes_set_key(&ctx, key, 16);
    KUNIT_ASSERT_EQ(ret, 0, "AES-128 key set ok ");

    ret = aes_encrypt_block(&ctx, plaintext, ciphertext);
    KUNIT_ASSERT_EQ(ret, 0, "AES encryption ok ");

    /* 解密回环测试 */
    ret = aes_decrypt_block(&ctx, ciphertext, decrypted);
    KUNIT_ASSERT_EQ(ret, 0, "AES 解密 ok ");

    int match = 1;
    for (int i = 0; i < 16; i++) {
        if (decrypted[i] != plaintext[i]) { match = 0; break; }
    }
    KUNIT_ASSERT(match, "AES 加解密回环 ");
    return KUNIT_PASS;
}

static kunit_result_t test_aes256_ecb(void)
{
    u8 key[32];
    u8 plaintext[16] = "HEPU_OS_TEST!!!";
    u8 ciphertext[16];
    u8 decrypted[16];
    aes_ctx_t ctx;

    for (int i = 0; i < 32; i++) key[i] = (u8)(i * 7 + 3);

    int ret = aes_set_key(&ctx, key, 32);
    KUNIT_ASSERT_EQ(ret, 0, "AES-256 key set ok ");

    ret = aes_encrypt_block(&ctx, plaintext, ciphertext);
    KUNIT_ASSERT_EQ(ret, 0, "AES-256 encryption ok ");

    ret = aes_decrypt_block(&ctx, ciphertext, decrypted);
    KUNIT_ASSERT_EQ(ret, 0, "AES-256 解密 ok ");

    KUNIT_ASSERT(memcmp(decrypted, plaintext, 16) == 0, "AES-256 回环 ");
    return KUNIT_PASS;
}

/* === SHA测试 === */


static kunit_result_t test_sha256_empty(void)
{
    u8 hash[32];
    /* SHA-256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 */
    u8 expected[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
    };

    int ret = sha_compute(SHA_ALG_SHA256, (const u8 *)"", 0, hash);
    KUNIT_ASSERT_EQ(ret, 0, "SHA-256 empty 串 ok ");
    KUNIT_ASSERT(memcmp(hash, expected, 32) == 0, "SHA-256 empty 串 result ");
    return KUNIT_PASS;
}

static kunit_result_t test_sha256_abc(void)
{
    u8 hash[32];
    /* SHA-256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad */
    u8 expected[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };

    int ret = sha_compute(SHA_ALG_SHA256, (const u8 *)"abc", 3, hash);
    KUNIT_ASSERT_EQ(ret, 0, "SHA-256 abc ok ");
    KUNIT_ASSERT(memcmp(hash, expected, 32) == 0, "SHA-256 abc result ");
    return KUNIT_PASS;
}

static kunit_result_t test_sha1_abc(void)
{
    u8 hash[20];
    /* SHA-1("abc") = a9993e364706816aba3e25717850c26c9cd0d89d */
    u8 expected[20] = {
        0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a,
        0xba, 0x3e, 0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c,
        0x9c, 0xd0, 0xd8, 0x9d
    };

    int ret = sha_compute(SHA_ALG_SHA1, (const u8 *)"abc", 3, hash);
    KUNIT_ASSERT_EQ(ret, 0, "SHA-1 abc ok ");
    KUNIT_ASSERT(memcmp(hash, expected, 20) == 0, "SHA-1 abc result ");
    return KUNIT_PASS;
}

/* === HMAC测试 === */


static kunit_result_t test_hmac_sha256(void)
{
    /* RFC 4231 Test Case 1 */
    u8 key[20];
    for (int i = 0; i < 20; i++) key[i] = 0x0b;
    u8 data[] = "Hi There";
    u8 expected[32] = {
        0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53,
        0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
        0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7,
        0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7
    };
    u8 mac[32];

    u32 mac_len = 0;
    int ret = hmac_compute(SHA_ALG_SHA256, key, 20, data, 8, mac, &mac_len);
    KUNIT_ASSERT_EQ(ret, 0, "HMAC-SHA256 ok ");
    KUNIT_ASSERT(memcmp(mac, expected, 32) == 0, "HMAC-SHA256 result ");
    return KUNIT_PASS;
}

/* === 测试套件注册 === */
static const kunit_test_t crypto_tests[] = {
    {"AES-128 ECB", test_aes128_ecb, "crypto"},
    {"AES-256 ECB", test_aes256_ecb, "crypto"},
    {"SHA-256 empty 串 ", test_sha256_empty, "crypto"},
    {"SHA-256 abc", test_sha256_abc, "crypto"},
    {"SHA-1 abc", test_sha1_abc, "crypto"},
    {"HMAC-SHA256", test_hmac_sha256, "crypto"},
};
static const kunit_suite_t kunit_crypto_suite = {
    " encryption algo ", crypto_tests, 6
};

void kunit_crypto_register(void)
{
    kunit_register_suite(&kunit_crypto_suite);
    vga_puts("[KUNIT] encryption test suite registered(6 cases)\n");
}
