#!/bin/bash
# Hepu OS Secure Boot 密钥生成脚本
# 生成 PK/KEK/db 三级密钥体系

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Hepu OS Secure Boot 密钥生成 ==="

# 检查是否已有密钥
if [ -f "PK.key" ] && [ -f "db.key" ]; then
    echo "[WARN] 密钥已存在，是否覆盖？(y/N)"
    read -r answer
    if [ "$answer" != "y" ] && [ "$answer" != "Y" ]; then
        echo "取消"
        exit 0
    fi
fi

# 1. 生成PK（Platform Key）- 自签名
echo "[1/6] 生成PK（Platform Key）..."
openssl req -new -x509 -newkey rsa:2048 \
    -subj "/CN=Hepu OS Platform Key/" \
    -keyout PK.key -out PK.crt \
    -days 3650 -nodes -sha256 2>/dev/null

# 2. 生成KEK（Key Exchange Key）- 用PK签名
echo "[2/6] 生成KEK（Key Exchange Key）..."
openssl req -new -newkey rsa:2048 \
    -subj "/CN=Hepu OS Key Exchange Key/" \
    -keyout KEK.key -out KEK.csr \
    -nodes -sha256 2>/dev/null
openssl x509 -req -in KEK.csr -CA PK.crt -CAkey PK.key \
    -CAcreateserial -out KEK.crt -days 3650 -sha256 2>/dev/null

# 3. 生成db（Signature Database）- 用KEK签名
echo "[3/6] 生成db（Signature Database）..."
openssl req -new -newkey rsa:2048 \
    -subj "/CN=Hepu OS Signature Database/" \
    -keyout db.key -out db.csr \
    -nodes -sha256 2>/dev/null
openssl x509 -req -in db.csr -CA KEK.crt -CAkey KEK.key \
    -CAcreateserial -out db.crt -days 3650 -sha256 2>/dev/null

# 4. 生成EFI签名列表（.esl）
echo "[4/6] 生成EFI签名列表..."
GUID_PK="11111111-2222-3333-4444-555555555555"
GUID_KEK="22222222-3333-4444-5555-666666666666"
GUID_DB="33333333-4444-5555-6666-777777777777"

cert-to-efi-sig-list -g "$GUID_PK" PK.crt PK.esl 2>/dev/null
cert-to-efi-sig-list -g "$GUID_KEK" KEK.crt KEK.esl 2>/dev/null
cert-to-efi-sig-list -g "$GUID_DB" db.crt db.esl 2>/dev/null

# 5. 生成签名认证文件（.auth）
echo "[5/6] 生成签名认证文件..."
sign-efi-sig-list -k PK.key -c PK.crt PK PK.esl PK.auth 2>/dev/null
sign-efi-sig-list -k PK.key -c PK.crt KEK KEK.esl KEK.auth 2>/dev/null
sign-efi-sig-list -k KEK.key -c KEK.crt db db.esl db.auth 2>/dev/null

# 6. 清理临时文件
echo "[6/6] 清理临时文件..."
rm -f KEK.csr db.csr PK.srl KEK.srl

echo ""
echo "=== 密钥生成完成 ==="
echo ""
echo "密钥文件："
ls -lh *.key *.crt *.esl *.auth
echo ""
echo "安全提示："
echo "  - .key 文件是私钥，必须妥善保管，不要发布"
echo "  - .crt/.esl/.auth 是公钥，可以分发"
echo "  - 导入固件时使用 .auth 文件"
