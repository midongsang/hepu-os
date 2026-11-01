#!/bin/bash
# Hepu OS Secure Boot 签名脚本
# 用法: ./sign_bootloader.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KEY_DIR="$SCRIPT_DIR/secureboot"
UNSIGNED="$SCRIPT_DIR/hepboot_uefi.efi"
SIGNED="$SCRIPT_DIR/hepboot_uefi_signed.efi"

echo "=== Hepu OS Secure Boot 签名 ==="

# 检查密钥是否存在
if [ ! -f "$KEY_DIR/db.key" ] || [ ! -f "$KEY_DIR/db.crt" ]; then
    echo "[ERROR] 密钥不存在，先生成密钥"
    echo "运行: cd $KEY_DIR && ./generate_keys.sh"
    exit 1
fi

# 检查未签名引导器
if [ ! -f "$UNSIGNED" ]; then
    echo "[ERROR] 未签名引导器不存在: $UNSIGNED"
    echo "先运行 make 编译引导器"
    exit 1
fi

# 签名
echo "[1/3] 签名引导器..."
sbsign --key "$KEY_DIR/db.key" --cert "$KEY_DIR/db.crt" \
    --output "$SIGNED" "$UNSIGNED"

# 验证签名
echo "[2/3] 验证签名..."
if sbverify --cert "$KEY_DIR/db.crt" "$SIGNED" 2>&1 | grep -q "Signature verification OK"; then
    echo "  签名验证通过"
else
    echo "[ERROR] 签名验证失败"
    exit 1
fi

# 显示文件信息
echo "[3/3] 签名完成"
ls -lh "$UNSIGNED" "$SIGNED"
echo ""
echo "签名引导器: $SIGNED"
echo "将此文件复制到UEFI镜像的 /EFI/BOOT/BOOTX64.EFI"
