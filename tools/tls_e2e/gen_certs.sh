#!/bin/bash
# ============================================================================
# TLS 端到端测试 —— 本地 CA + 服务器证书生成（可重复执行，幂等）
#
# 产物固定放在 /tmp/hepu-tls-e2e/certs/：
#   ca.key / ca.crt                 本地 CA（RSA 2048，SHA-256 签名，自签）
#   server.key / server.crt         被测服务器证书（SAN: DNS:localhost, IP:10.0.2.2）
#   server-bad.key / server-bad.crt  负向对照证书（故意错误 SAN）
#
# 一条命令重建：
#   bash tools/tls_e2e/gen_certs.sh                  # 已存在则复用（指纹稳定）
#   HEPU_REBUILD_CERTS=1 bash tools/tls_e2e/gen_certs.sh   # 强制重建
#
# 结束打印全部产物绝对路径与 SHA-256 指纹。本脚本只在 /tmp 写证书，不改 kernel/。
# ============================================================================
set -euo pipefail

CERT_DIR="${HEPU_TLS_CERT_DIR:-/tmp/hepu-tls-e2e/certs}"
DAYS="${HEPU_TLS_CERT_DAYS:-2}"
REBUILD="${HEPU_REBUILD_CERTS:-0}"

mkdir -p "$CERT_DIR"
cd "$CERT_DIR"

CA_SUBJ="/CN=HepuOS-TLS-E2E-Local-CA,O=HepuOS-E2E"
SERVER_SUBJ="/CN=localhost,O=HepuOS-E2E"

if [ "$REBUILD" = "1" ]; then
  rm -f ca.key ca.crt ca.srl server.key server.crt server.csr \
        server-bad.key server-bad.crt server-bad.csr server-bad.srl
fi

# ---- 1. 本地 CA（RSA 2048 + SHA-256）----
if [ ! -f ca.crt ]; then
  openssl req -x509 -newkey rsa:2048 -sha256 -days "$DAYS" -nodes \
    -keyout ca.key -out ca.crt \
    -subj "$CA_SUBJ" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" >/dev/null 2>&1
fi

# ---- 2. 合法服务器证书（SAN: localhost + 10.0.2.2）----
cat > server-good.ext <<'EOF'
basicConstraints=CA:FALSE
keyUsage=digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=DNS:localhost,IP:10.0.2.2
EOF

if [ ! -f server.crt ]; then
  openssl req -newkey rsa:2048 -sha256 -nodes \
    -keyout server.key -out server.csr -subj "$SERVER_SUBJ" >/dev/null 2>&1
  openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
    -out server.crt -days "$DAYS" -sha256 \
    -extfile server-good.ext >/dev/null 2>&1
  rm -f server.csr ca.srl
fi

# ---- 3. 负向对照证书（故意错误 SAN，不匹配 10.0.2.2 / localhost）----
cat > server-bad.ext <<'EOF'
basicConstraints=CA:FALSE
keyUsage=digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=DNS:wrong.example.org,IP:192.0.2.1
EOF

if [ ! -f server-bad.crt ]; then
  openssl req -newkey rsa:2048 -sha256 -nodes \
    -keyout server-bad.key -out server-bad.csr \
    -subj "/CN=wrong.example.org,O=HepuOS-E2E" >/dev/null 2>&1
  openssl x509 -req -in server-bad.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
    -out server-bad.crt -days "$DAYS" -sha256 \
    -extfile server-bad.ext >/dev/null 2>&1
  rm -f server-bad.csr server-bad.srl
fi

echo "==== 证书产物（目录: $CERT_DIR）===="
for f in ca.crt server.crt server-bad.crt; do
  if [ -f "$CERT_DIR/$f" ]; then
    fp=$(openssl x509 -in "$CERT_DIR/$f" -fingerprint -sha256 -noout | sed 's/.*=//')
    echo "  $CERT_DIR/$f"
    echo "    SHA-256: $fp"
  fi
done
echo "  私钥: $CERT_DIR/ca.key  $CERT_DIR/server.key  $CERT_DIR/server-bad.key"
