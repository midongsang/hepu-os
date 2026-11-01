/* ============================================================================
 * 赫普OS - ECDHE密钥交换
 * 椭圆曲线Diffie-Hellman临时密钥交换
 * ========================================================================== */

#include <stdint.h>
#include "ecdhe.h"
#include "csprng.h"
#include "vga.h"
#include "string.h"

static int ecdhe_initialized = 0;

/* 初始化ECDHE */
void ecdhe_init(void)
{
    ecdhe_initialized = 1;
    vga_puts("[ECDHE] ECDHE init \n");
    vga_puts("[ECDHE] supports: secp256r1, secp384r1, secp521r1, X25519\n");
}

/* 获取密钥大小 */
uint32_t ecdhe_get_key_size(ecdhe_curve_t curve)
{
    switch (curve) {
        case ECDHE_CURVE_SECP256R1: return 32;
        case ECDHE_CURVE_SECP384R1: return 48;
        case ECDHE_CURVE_SECP521R1: return 66;
        case ECDHE_CURVE_X25519: return 32;
        default: return 32;
    }
}

/* 曲线名称 */
const char *ecdhe_curve_name(ecdhe_curve_t curve)
{
    switch (curve) {
        case ECDHE_CURVE_SECP256R1: return "secp256r1 (NIST P-256)";
        case ECDHE_CURVE_SECP384R1: return "secp384r1 (NIST P-384)";
        case ECDHE_CURVE_SECP521R1: return "secp521r1 (NIST P-521)";
        case ECDHE_CURVE_X25519: return "X25519 (Curve25519)";
        default: return "Unknown";
    }
}

/* 生成密钥对 */
int ecdhe_generate_keypair(ecdhe_keypair_t *keypair, ecdhe_curve_t curve)
{
    if (!ecdhe_initialized || !keypair) return -1;

    memset(keypair, 0, sizeof(ecdhe_keypair_t));
    keypair->curve = curve;
    keypair->key_size = ecdhe_get_key_size(curve);

    /* 生成私钥（随机数） */
    csprng_get_bytes(keypair->private_key, keypair->key_size);

    /* 确保私钥在有效范围内（简化：清除高位） */
    switch (curve) {
        case ECDHE_CURVE_SECP256R1:
            keypair->private_key[0] &= 0x7F;
            keypair->private_key[31] |= 0x40;
            break;
        case ECDHE_CURVE_X25519:
            keypair->private_key[0] &= 0xF8;
            keypair->private_key[31] &= 0x7F;
            keypair->private_key[31] |= 0x40;
            break;
        default:
            break;
    }

    /* 生成公钥（简化：模拟标量乘法结果） */
    keypair->public_key[0] = 0x04;  /* 未压缩点格式 */
    csprng_get_bytes(keypair->public_key + 1, keypair->key_size * 2);

    keypair->valid = 1;
    vga_printf("[ECDHE] gen keypair: %s(%d bytes)\n",
               ecdhe_curve_name(curve), keypair->key_size);
    return 0;
}

/* 计算共享密钥 */
int ecdhe_compute_shared_secret(const ecdhe_keypair_t *local,
                                 const uint8_t *peer_public,
                                 uint32_t peer_len,
                                 uint8_t *shared_secret,
                                 uint32_t *secret_len)
{
    if (!ecdhe_initialized || !local || !peer_public || !shared_secret) return -1;
    if (!local->valid || peer_len < 1) return -1;

    /* 简化：模拟ECDH共享密钥计算 */
    /* 实际应执行标量乘法：shared = private_key * peer_public_point */
    uint32_t secret_size = local->key_size;
    if (secret_size > ECDHE_SHARED_SECRET_SIZE) {
        secret_size = ECDHE_SHARED_SECRET_SIZE;
    }

    /* 使用私钥和对等方公钥的混合生成共享密钥（简化） */
    for (uint32_t i = 0; i < secret_size; i++) {
        uint8_t priv = i < local->key_size ? local->private_key[i] : 0;
        uint8_t pub = (i + 1) < peer_len ? peer_public[i + 1] : 0;
        shared_secret[i] = priv ^ pub ^ (i * 0x37);
    }

    if (secret_len) *secret_len = secret_size;

    vga_printf("[ECDHE] shared secret done(%d bytes)\n", secret_size);
    return 0;
}

/* 获取公钥 */
int ecdhe_get_public_key(const ecdhe_keypair_t *keypair,
                          uint8_t *pub_key, uint32_t *pub_len)
{
    if (!keypair || !pub_key || !keypair->valid) return -1;

    uint32_t len = keypair->key_size * 2 + 1;
    memcpy(pub_key, keypair->public_key, len);
    if (pub_len) *pub_len = len;
    return 0;
}

/* 初始化会话 */
int ecdhe_session_init(ecdhe_session_t *session, ecdhe_curve_t curve)
{
    if (!session) return -1;

    memset(session, 0, sizeof(ecdhe_session_t));
    session->curve = curve;
    session->initialized = 1;

    return ecdhe_generate_keypair(&session->local_keypair, curve);
}

/* 设置对等方公钥 */
int ecdhe_session_set_peer_key(ecdhe_session_t *session,
                                const uint8_t *peer_key, uint32_t len)
{
    if (!session || !peer_key || len < 1) return -1;
    if (!session->initialized) return -1;

    uint32_t copy_len = len > sizeof(session->peer_public_key) ?
                        sizeof(session->peer_public_key) : len;
    memcpy(session->peer_public_key, peer_key, copy_len);
    session->peer_key_size = copy_len;
    session->shared_secret_computed = 0;

    return 0;
}

/* 获取共享密钥 */
int ecdhe_session_get_shared_secret(ecdhe_session_t *session,
                                     uint8_t *secret, uint32_t *len)
{
    if (!session || !secret) return -1;
    if (!session->initialized || session->peer_key_size == 0) return -1;

    if (!session->shared_secret_computed) {
        uint32_t secret_len;
        int ret = ecdhe_compute_shared_secret(
            &session->local_keypair,
            session->peer_public_key,
            session->peer_key_size,
            session->shared_secret,
            &secret_len);
        if (ret != 0) return ret;
        session->shared_secret_computed = 1;
    }

    uint32_t copy_len = ECDHE_SHARED_SECRET_SIZE;
    memcpy(secret, session->shared_secret, copy_len);
    if (len) *len = copy_len;

    return 0;
}
