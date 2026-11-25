#ifndef TLS12_TRUSTED_CA_H
#define TLS12_TRUSTED_CA_H

/* ============================================================================
 * 测试信任锚（受信任 CA 证书 DER）注入点
 *
 * 本文件由 tools/tls12_e2e.sh 在联调前用本机生成的 CA 证书 DER 重新生成，
 * 内核编译期把信任锚编入镜像；运行期通过 x509_add_trusted_ca() 注入。
 *
 * 仓库默认版本为空锚：没有任何对端证书能通过链校验，避免把自签/未知 CA
 * 当成受信。端到端脚本会写入真实测试 CA 的 DER 数组。
 * ========================================================================== */

static const uint8_t tls12_trusted_ca_der[] = { 0 };
static const uint32_t tls12_trusted_ca_der_len = 0;

#endif /* TLS12_TRUSTED_CA_H */
