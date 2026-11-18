#!/bin/sh
# aarch64 开发环境自愈：本环境的 /usr 层每轮会重置，/home/user 持久。
# 用法: sh tools/a64env.sh   (需要已装可用)
set -e
need=0
for c in aarch64-linux-gnu-gcc qemu-system-aarch64 sgdisk mkfs.vfat mcopy; do
    command -v $c >/dev/null 2>&1 || need=1
done
[ -f /usr/share/qemu-efi-aarch64/QEMU_EFI.fd ] || need=1
if [ "$need" = "1" ]; then
    echo "[a64env] 补齐工具链..."
    sudo apt-get update -qq >/dev/null 2>&1 || true
    sudo apt-get install -y -qq gcc-aarch64-linux-gnu qemu-system-arm qemu-efi-aarch64 \
         dosfstools mtools gdisk >/dev/null 2>&1
fi
echo "[a64env] gcc: $(command -v aarch64-linux-gnu-gcc || echo MISSING)"
echo "[a64env] qemu: $(command -v qemu-system-aarch64 || echo MISSING)"
echo "[a64env] efi: $(ls /usr/share/qemu-efi-aarch64/QEMU_EFI.fd 2>/dev/null || echo MISSING)"
