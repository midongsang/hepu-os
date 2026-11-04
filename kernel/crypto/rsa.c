/* ============================================================================
 * 赫普OS - RSA签名验证
 * 支持PKCS#1 v1.5，大数模幂运算
 * ========================================================================== */

#include <stdint.h>
#include "rsa.h"
#include "sha.h"
#include "vga.h"
#include "string.h"

static int rsa_initialized = 0;

/* ============================================================================
 * 大数运算（以32位字为单位，小端序存储）
 * ========================================================================== */

/* 比较两个大数：返回1如果a>b, 0如果a==b, -1如果a<b */
static int rsa_big_cmp(const uint32_t *a, uint32_t a_len,
                       const uint32_t *b, uint32_t b_len)
{
    if (a_len != b_len) return a_len > b_len ? 1 : -1;
    for (int i = (int)a_len - 1; i >= 0; i--) {
        if (a[i] != b[i]) return a[i] > b[i] ? 1 : -1;
    }
    return 0;
}

/* 大数加法：result = a + b，返回进位 */
static uint32_t __attribute__((unused)) rsa_big_add(uint32_t *result, const uint32_t *a, uint32_t a_len,
                            const uint32_t *b, uint32_t b_len)
{
    uint64_t carry = 0;
    uint32_t max_len = a_len > b_len ? a_len : b_len;

    for (uint32_t i = 0; i < max_len; i++) {
        uint64_t sum = carry;
        if (i < a_len) sum += a[i];
        if (i < b_len) sum += b[i];
        result[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    return (uint32_t)carry;
}

/* 大数减法：result = a - b，假设a >= b */
static void rsa_big_sub(uint32_t *result, const uint32_t *a, uint32_t a_len,
                        const uint32_t *b, uint32_t b_len)
{
    int64_t borrow = 0;
    for (uint32_t i = 0; i < a_len; i++) {
        int64_t diff = (int64_t)a[i] - borrow;
        if (i < b_len) diff -= b[i];
        if (diff < 0) {
            diff += (int64_t)1 << 32;
            borrow = 1;
        } else {
            borrow = 0;
        }
        result[i] = (uint32_t)diff;
    }
}

/* 大数乘法：result = a * b，result需要2*max_len空间 */
static void rsa_big_mul(uint32_t *result, const uint32_t *a, uint32_t a_len,
                        const uint32_t *b, uint32_t b_len)
{
    memset(result, 0, (a_len + b_len) * sizeof(uint32_t));

    for (uint32_t i = 0; i < a_len; i++) {
        uint64_t carry = 0;
        for (uint32_t j = 0; j < b_len; j++) {
            uint64_t prod = (uint64_t)a[i] * b[j] + result[i + j] + carry;
            result[i + j] = (uint32_t)prod;
            carry = prod >> 32;
        }
        result[i + b_len] += (uint32_t)carry;
    }
}

/* 大数左移一位 */
static void rsa_big_shl1(uint32_t *a, uint32_t len)
{
    uint32_t carry = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint32_t new_carry = a[i] >> 31;
        a[i] = (a[i] << 1) | carry;
        carry = new_carry;
    }
}

/* 大数右移一位 */
static void __attribute__((unused)) rsa_big_shr1(uint32_t *a, uint32_t len)
{
    uint32_t carry = 0;
    for (int i = (int)len - 1; i >= 0; i--) {
        uint32_t new_carry = a[i] & 1;
        a[i] = (a[i] >> 1) | (carry << 31);
        carry = new_carry;
    }
}

/* 大数取模：result = a % m */
static void rsa_big_mod(uint32_t *result, const uint32_t *a, uint32_t a_len,
                        const uint32_t *m, uint32_t m_len)
{
    uint32_t temp[RSA_MAX_KEY_WORDS * 2];
    uint32_t remainder[RSA_MAX_KEY_WORDS * 2];
    memset(remainder, 0, sizeof(remainder));

    /* 从最高位开始逐位处理 */
    for (int i = (int)a_len - 1; i >= 0; i--) {
        for (int bit = 31; bit >= 0; bit--) {
            /* remainder = (remainder << 1) | ((a[i] >> bit) & 1) */
            rsa_big_shl1(remainder, m_len + 1);
            remainder[0] |= (a[i] >> bit) & 1;

            /* V7.69安全修复: 修正模运算比较错误
             * 原代码: rsa_big_cmp(remainder, m_len, m, m_len)
             * 问题: remainder左移后可能有第m_len字进位，但比较只看前m_len字
             *       若remainder[m_len]!=0，则remainder >= 2^(32*m_len) > m，必然>=m
             * 修复: 先检查最高字，再比较前m_len字；减法用m_len+1字处理借位 */
            if (remainder[m_len] != 0 ||
                rsa_big_cmp(remainder, m_len, m, m_len) >= 0) {
                rsa_big_sub(remainder, remainder, m_len + 1, m, m_len);
            }
        }
    }

    memcpy(result, remainder, m_len * sizeof(uint32_t));
    (void)temp;
}

/* 大数模幂：result = base^exp mod m */
static int rsa_big_modpow(uint32_t *result, const uint32_t *base, uint32_t base_len,
                          const uint32_t *exp, uint32_t exp_len,
                          const uint32_t *m, uint32_t m_len)
{
    uint32_t current_base[RSA_MAX_KEY_WORDS * 2];
    uint32_t temp[RSA_MAX_KEY_WORDS * 2];
    uint32_t result_val[RSA_MAX_KEY_WORDS * 2];

    /* result = 1 */
    memset(result_val, 0, sizeof(result_val));
    result_val[0] = 1;

    /* current_base = base mod m */
    memset(current_base, 0, sizeof(current_base));
    rsa_big_mod(current_base, base, base_len, m, m_len);

    /* V7.69安全修复: 恒定时间平方-乘算法，防时序侧信道
     * 原代码: if ((exp[i] >> bit) & 1) { 乘法 } - 分支依赖指数位，执行时间泄露指数
     * 修复: 总是执行乘法，用条件选择（无分支）决定是否更新结果
     * 注意: 当前只用于公钥运算(e=65537)影响有限，私钥运算时必须用此恒定时间版本 */
    for (uint32_t i = 0; i < exp_len; i++) {
        for (int bit = 0; bit < 32; bit++) {
            /* 总是计算 result * current_base，防时序侧信道 */
            uint32_t temp_result[RSA_MAX_KEY_WORDS * 2];
            rsa_big_mul(temp, result_val, m_len, current_base, m_len);
            rsa_big_mod(temp_result, temp, m_len * 2, m, m_len);

            /* 恒定时间条件选择: 指数位为1则用temp_result，否则保持result_val
             * mask = 0xFFFFFFFF (位为1) 或 0 (位为0) */
            uint32_t mask = -((exp[i] >> bit) & 1);
            for (uint32_t j = 0; j < m_len; j++) {
                result_val[j] = (result_val[j] & ~mask) | (temp_result[j] & mask);
            }

            /* current_base = (current_base * current_base) mod m */
            rsa_big_mul(temp, current_base, m_len, current_base, m_len);
            rsa_big_mod(current_base, temp, m_len * 2, m, m_len);
        }
    }

    memcpy(result, result_val, m_len * sizeof(uint32_t));
    return 0;
}

/* ============================================================================
 * RSA操作
 * ========================================================================== */

void rsa_init(void)
{
    rsa_initialized = 1;
    vga_puts("[RSA] sig verify init \n");
    vga_puts("[RSA] supports: PKCS#1 v1.5, SHA1/SHA256/SHA384/SHA512\n");
}

/* 解析RSA公钥（简化：直接读取n和e） */
/* V7.86: ASN.1 DER辅助函数
 * 用于解析RSAPublicKey的DER编码 */

/* 读取DER长度（支持短格式和长格式）
 * 返回：长度值，*offset更新到长度字段之后 */
static uint32_t asn1_read_length(const uint8_t *data, uint32_t len, uint32_t *offset)
{
    if (*offset >= len) return 0;
    uint8_t first = data[*offset];
    (*offset)++;

    if ((first & 0x80) == 0) {
        /* 短格式：长度直接在第一个字节 */
        return first;
    } else {
        /* 长格式：第一个字节的低7位表示后续长度字节数 */
        uint32_t num_bytes = first & 0x7F;
        if (num_bytes == 0 || num_bytes > 4 || *offset + num_bytes > len) return 0;
        uint32_t length = 0;
        for (uint32_t i = 0; i < num_bytes; i++) {
            length = (length << 8) | data[*offset + i];
        }
        *offset += num_bytes;
        return length;
    }
}

/* 解析DER INTEGER，返回值的大端序字节（去除前导0x00）
 * *value_offset指向INTEGER值的起始位置，*value_len返回值长度 */
static int asn1_read_integer(const uint8_t *data, uint32_t len, uint32_t *offset,
                              const uint8_t **value, uint32_t *value_len)
{
    if (*offset >= len || data[*offset] != 0x02) return -1;
    (*offset)++;  /* 跳过tag */

    uint32_t int_len = asn1_read_length(data, len, offset);
    if (int_len == 0 || *offset + int_len > len) return -1;

    /* 去除前导0x00（用于表示正数） */
    const uint8_t *p = data + *offset;
    while (int_len > 1 && p[0] == 0x00) {
        p++;
        int_len--;
    }

    *value = p;
    *value_len = int_len;
    *offset += int_len;
    return 0;
}

/* 将大端序字节转换为RSA大数字（小端序，按字存储） */
static void rsa_bytes_to_big(const uint8_t *bytes, uint32_t byte_len,
                              uint32_t *big, uint32_t *big_len)
{
    memset(big, 0, RSA_MAX_KEY_WORDS * sizeof(uint32_t));
    for (uint32_t i = 0; i < byte_len; i++) {
        uint32_t word_idx = (byte_len - 1 - i) / 4;
        uint32_t byte_idx = (byte_len - 1 - i) % 4;
        big[word_idx] |= ((uint32_t)bytes[i]) << (byte_idx * 8);
    }
    *big_len = (byte_len + 3) / 4;
}

int rsa_parse_public_key(rsa_public_key_t *key, const uint8_t *der, uint32_t len)
{
    if (!rsa_initialized || !key || !der || len < 1) return -1;

    memset(key, 0, sizeof(rsa_public_key_t));

    /* V7.86: 支持两种格式：
     * 1. ASN.1 DER格式的RSAPublicKey（标准格式）
     * 2. 原始n+e字节（向后兼容旧代码）
     *
     * 检测方法：如果第一个字节是0x30（SEQUENCE tag），则为DER格式 */

    if (der[0] == 0x30) {
        /* ASN.1 DER格式的RSAPublicKey
         * RSAPublicKey ::= SEQUENCE {
         *     modulus           INTEGER,  -- n
         *     publicExponent    INTEGER   -- e
         * }
         */
        uint32_t offset = 1;  /* 跳过SEQUENCE tag */
        uint32_t seq_len = asn1_read_length(der, len, &offset);
        if (seq_len == 0 || offset + seq_len > len) {
            vga_puts("[RSA] DER SEQUENCE parse failed\n");
            return -1;
        }

        /* 解析modulus (n) */
        const uint8_t *n_bytes;
        uint32_t n_len;
        if (asn1_read_integer(der, len, &offset, &n_bytes, &n_len) != 0) {
            vga_puts("[RSA] DER INTEGER (n) parse failed\n");
            return -1;
        }

        /* 解析publicExponent (e) */
        const uint8_t *e_bytes;
        uint32_t e_len;
        if (asn1_read_integer(der, len, &offset, &e_bytes, &e_len) != 0) {
            vga_puts("[RSA] DER INTEGER (e) parse failed\n");
            return -1;
        }

        /* 转换为RSA大数字 */
        rsa_bytes_to_big(n_bytes, n_len, key->n, &key->n_len);
        rsa_bytes_to_big(e_bytes, e_len, key->e, &key->e_len);
        key->bits = n_len * 8;

        vga_printf("[RSA] DER pubkey parse ok: %d bits (n=%d bytes, e=%d bytes)\n",
                   key->bits, n_len, e_len);
    } else {
        /* 向后兼容：原始n+e字节（大端序） */
        uint32_t key_bytes = len / 2;
        if (key_bytes > RSA_MAX_KEY_BYTES) key_bytes = RSA_MAX_KEY_BYTES;

        key->n_len = (key_bytes + 3) / 4;
        key->e_len = (key_bytes + 3) / 4;
        key->bits = key_bytes * 8;

        /* 转换n（大端序转小端序） */
        for (uint32_t i = 0; i < key_bytes; i++) {
            uint32_t word_idx = (key_bytes - 1 - i) / 4;
            uint32_t byte_idx = (key_bytes - 1 - i) % 4;
            key->n[word_idx] |= ((uint32_t)der[i]) << (byte_idx * 8);
        }

        /* 转换e */
        for (uint32_t i = 0; i < key_bytes && (i + key_bytes) < len; i++) {
            uint32_t word_idx = (key_bytes - 1 - i) / 4;
            uint32_t byte_idx = (key_bytes - 1 - i) % 4;
            key->e[word_idx] |= ((uint32_t)der[i + key_bytes]) << (byte_idx * 8);
        }

        vga_printf("[RSA] raw pubkey parse ok: %d bits (legacy format)\n", key->bits);
    }

    key->valid = 1;
    return 0;
}

/* PKCS#1 v1.5 DigestInfo前缀 */
static const uint8_t rsa_digestinfo_prefix_sha1[] = {
    0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2B, 0x0E,
    0x03, 0x02, 0x1A, 0x05, 0x00, 0x04, 0x14
};

static const uint8_t rsa_digestinfo_prefix_sha256[] = {
    0x30, 0x31, 0x30, 0x0D, 0x06, 0x09, 0x60, 0x86,
    0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05,
    0x00, 0x04, 0x20
};

static const uint8_t rsa_digestinfo_prefix_sha384[] = {
    0x30, 0x41, 0x30, 0x0D, 0x06, 0x09, 0x60, 0x86,
    0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02, 0x05,
    0x00, 0x04, 0x30
};

static const uint8_t rsa_digestinfo_prefix_sha512[] = {
    0x30, 0x51, 0x30, 0x0D, 0x06, 0x09, 0x60, 0x86,
    0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03, 0x05,
    0x00, 0x04, 0x40
};

/* RSA验证签名 */
int rsa_verify_signature(const rsa_public_key_t *key,
                         const uint8_t *signature, uint32_t sig_len,
                         const uint8_t *digest, uint32_t digest_len,
                         rsa_sig_alg_t alg)
{
    if (!rsa_initialized || !key || !key->valid) return -1;
    if (!signature || sig_len == 0 || !digest) return -1;

    uint32_t key_bytes = key->bits / 8;
    if (sig_len != key_bytes) return -1;

    /* 将签名字节转换为大数（小端序） */
    uint32_t sig_big[RSA_MAX_KEY_WORDS];
    memset(sig_big, 0, sizeof(sig_big));
    for (uint32_t i = 0; i < sig_len; i++) {
        uint32_t word_idx = (sig_len - 1 - i) / 4;
        uint32_t byte_idx = (sig_len - 1 - i) % 4;
        sig_big[word_idx] |= ((uint32_t)signature[i]) << (byte_idx * 8);
    }

    /* 模幂：decrypted = signature^e mod n */
    uint32_t decrypted[RSA_MAX_KEY_WORDS];
    rsa_big_modpow(decrypted, sig_big, key->n_len, key->e, key->e_len,
                   key->n, key->n_len);

    /* 转换为字节（大端序） */
    uint8_t decrypted_bytes[RSA_MAX_KEY_BYTES];
    memset(decrypted_bytes, 0, sizeof(decrypted_bytes));
    for (uint32_t i = 0; i < key_bytes; i++) {
        uint32_t word_idx = (key_bytes - 1 - i) / 4;
        uint32_t byte_idx = (key_bytes - 1 - i) % 4;
        decrypted_bytes[i] = (decrypted[word_idx] >> (byte_idx * 8)) & 0xFF;
    }

    /* 验证PKCS#1 v1.5填充 */
    /* 格式: 0x00 0x01 FF...FF 0x00 DigestInfo */
    if (decrypted_bytes[0] != 0x00 || decrypted_bytes[1] != 0x01) return -1;

    uint32_t idx = 2;
    while (idx < key_bytes && decrypted_bytes[idx] == 0xFF) idx++;
    if (idx >= key_bytes || decrypted_bytes[idx] != 0x00) return -1;
    idx++;

    /* 验证DigestInfo前缀 */
    const uint8_t *prefix;
    uint32_t prefix_len;
    switch (alg) {
        case RSA_PKCS1_SHA1:
            prefix = rsa_digestinfo_prefix_sha1;
            prefix_len = sizeof(rsa_digestinfo_prefix_sha1);
            break;
        case RSA_PKCS1_SHA256:
            prefix = rsa_digestinfo_prefix_sha256;
            prefix_len = sizeof(rsa_digestinfo_prefix_sha256);
            break;
        case RSA_PKCS1_SHA384:
            prefix = rsa_digestinfo_prefix_sha384;
            prefix_len = sizeof(rsa_digestinfo_prefix_sha384);
            break;
        case RSA_PKCS1_SHA512:
            prefix = rsa_digestinfo_prefix_sha512;
            prefix_len = sizeof(rsa_digestinfo_prefix_sha512);
            break;
        default:
            return -1;
    }

    if (idx + prefix_len > key_bytes) return -1;
    if (memcmp(decrypted_bytes + idx, prefix, prefix_len) != 0) return -1;
    idx += prefix_len;

    /* 验证摘要 */
    if (idx + digest_len > key_bytes) return -1;
    if (memcmp(decrypted_bytes + idx, digest, digest_len) != 0) return -1;

    vga_puts("[RSA] sig verify ok \n");
    return 0;
}

/* RSA加密 */
int rsa_encrypt(const rsa_public_key_t *key,
                const uint8_t *plaintext, uint32_t plaintext_len,
                uint8_t *ciphertext, uint32_t *ciphertext_len)
{
    if (!rsa_initialized || !key || !key->valid) return -1;
    if (!plaintext || plaintext_len == 0 || !ciphertext) return -1;

    uint32_t key_bytes = key->bits / 8;
    if (plaintext_len > key_bytes - 11) return -1;  /* PKCS#1 v1.5填充需要11字节 */

    /* PKCS#1 v1.5填充: 0x00 0x02 随机非零字节 0x00 plaintext */
    uint8_t padded[RSA_MAX_KEY_BYTES];
    padded[0] = 0x00;
    padded[1] = 0x02;
    for (uint32_t i = 2; i < key_bytes - plaintext_len - 1; i++) {
        padded[i] = 0xFF;  /* 简化：使用固定填充 */
    }
    padded[key_bytes - plaintext_len - 1] = 0x00;
    memcpy(padded + key_bytes - plaintext_len, plaintext, plaintext_len);

    /* 转换为大数 */
    uint32_t plain_big[RSA_MAX_KEY_WORDS];
    memset(plain_big, 0, sizeof(plain_big));
    for (uint32_t i = 0; i < key_bytes; i++) {
        uint32_t word_idx = (key_bytes - 1 - i) / 4;
        uint32_t byte_idx = (key_bytes - 1 - i) % 4;
        plain_big[word_idx] |= ((uint32_t)padded[i]) << (byte_idx * 8);
    }

    /* 模幂：ciphertext = plaintext^e mod n */
    uint32_t cipher_big[RSA_MAX_KEY_WORDS];
    rsa_big_modpow(cipher_big, plain_big, key->n_len, key->e, key->e_len,
                   key->n, key->n_len);

    /* 转换为字节 */
    for (uint32_t i = 0; i < key_bytes; i++) {
        uint32_t word_idx = (key_bytes - 1 - i) / 4;
        uint32_t byte_idx = (key_bytes - 1 - i) % 4;
        ciphertext[i] = (cipher_big[word_idx] >> (byte_idx * 8)) & 0xFF;
    }

    if (ciphertext_len) *ciphertext_len = key_bytes;
    return 0;
}

uint32_t rsa_get_key_size(const rsa_public_key_t *key)
{
    return key ? key->bits : 0;
}

const char *rsa_algorithm_name(rsa_sig_alg_t alg)
{
    switch (alg) {
        case RSA_PKCS1_SHA1: return "RSA-PKCS1-SHA1";
        case RSA_PKCS1_SHA256: return "RSA-PKCS1-SHA256";
        case RSA_PKCS1_SHA384: return "RSA-PKCS1-SHA384";
        case RSA_PKCS1_SHA512: return "RSA-PKCS1-SHA512";
        default: return "Unknown";
    }
}
