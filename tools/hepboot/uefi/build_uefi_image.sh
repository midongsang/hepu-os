#!/bin/bash
# ============================================================================
# HepuBoot UEFI 统一构建脚本
# 功能：编译UEFI引导器 + 内核 + 构建四件套（IMG/ISO/数据盘）
# 用法：./build_uefi_image.sh [版本号]
# 示例：./build_uefi_image.sh 0.07
# ============================================================================

set -e

# 项目根目录
PROJECT_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$PROJECT_ROOT"

# 版本号（从源码提取或参数指定）
if [ -n "$1" ]; then
    VERSION="$1"
else
    VERSION=$(grep '#define HEPBOOT_UEFI_VERSION' tools/hepboot/uefi/hepboot_uefi.c | sed 's/.*"\(.*\)".*/\1/')
fi
echo "=== HepuBoot UEFI v${VERSION} 统一构建 ==="

# 1. 编译内核
echo "[1/6] 编译内核..."
make -C "$PROJECT_ROOT" all >/dev/null 2>&1
cp build_v250/hepu-kernel.bin build/hepu-kernel.bin
echo "  内核: $(ls -la build/hepu-kernel.bin | awk '{print $5}') bytes"

# 2. 编译UEFI引导器
echo "[2/6] 编译UEFI引导器..."
make -C tools/hepboot/uefi clean >/dev/null 2>&1
make -C tools/hepboot/uefi 2>&1 | grep -E "error|Build complete"
echo "  引导器: $(ls -la tools/hepboot/uefi/hepboot_uefi.efi | awk '{print $5}') bytes"

# 3. 准备ESP分区文件
echo "[3/6] 准备ESP分区文件..."
ESP_TMP="/tmp/hepu_esp_$$"
rm -rf "$ESP_TMP"
mkdir -p "$ESP_TMP/EFI/BOOT"
cp tools/hepboot/uefi/hepboot_uefi.efi "$ESP_TMP/EFI/BOOT/BOOTX64.EFI"
cp build/hepu-kernel.bin "$ESP_TMP/hepu-kernel.bin"
cat > "$ESP_TMP/hepboot.cfg" << EOF
# HepuBoot UEFI Configuration v${VERSION}
timeout=3
default=normal
cmdline=
EOF

# 4. 构建UEFI GPT IMG镜像
echo "[4/6] 构建UEFI GPT IMG镜像..."
IMG_NAME="build/hepu-os-uefi-v${VERSION}.img"
rm -f "$IMG_NAME"
dd if=/dev/zero of="$IMG_NAME" bs=1M count=256 2>/dev/null
parted -s "$IMG_NAME" mklabel gpt 2>/dev/null
parted -s "$IMG_NAME" mkpart ESP fat32 1MiB 100MiB 2>/dev/null
parted -s "$IMG_NAME" set 1 esp on 2>/dev/null
parted -s "$IMG_NAME" mkpart DATA ext2 100MiB 250MiB 2>/dev/null
mkfs.vfat -F 32 --offset=2048 "$IMG_NAME" >/dev/null 2>&1
mcopy -i "$IMG_NAME"@@1M -s "$ESP_TMP/EFI" ::/ 2>/dev/null
mcopy -o -i "$IMG_NAME"@@1M "$ESP_TMP/hepu-kernel.bin" ::/ 2>/dev/null
mcopy -o -i "$IMG_NAME"@@1M "$ESP_TMP/hepboot.cfg" ::/ 2>/dev/null
echo "  IMG: $(ls -la "$IMG_NAME" | awk '{print $5}') bytes"

# 5. 构建UEFI ISO镜像
echo "[5/6] 构建UEFI ISO镜像..."
ISO_NAME="build/hepu-os-uefi-v${VERSION}.iso"
dd if="$IMG_NAME" of=/tmp/hepu_esp_$$.img bs=512 skip=2048 count=$((99*1024*2)) 2>/dev/null
mkdir -p /tmp/hepu_iso_$$/boot
cp /tmp/hepu_esp_$$.img /tmp/hepu_iso_$$/boot/efi.img
rm -f "$ISO_NAME"
xorriso -as mkisofs -R -J -V "HEPU_UEFI" -e boot/efi.img -no-emul-boot \
    -o "$ISO_NAME" /tmp/hepu_iso_$$ 2>&1 | tail -1
echo "  ISO: $(ls -la "$ISO_NAME" | awk '{print $5}') bytes"

# 6. 构建数据盘
echo "[6/6] 构建数据盘..."
DATA_NAME="build/ext2_disk_uefi.img"
rm -f "$DATA_NAME"
dd if=/dev/zero of="$DATA_NAME" bs=1M count=64 2>/dev/null
mkfs.ext2 -F "$DATA_NAME" >/dev/null 2>&1
echo "  数据盘: $(ls -la "$DATA_NAME" | awk '{print $5}') bytes"

# 清理临时文件
rm -rf "$ESP_TMP" /tmp/hepu_esp_$$.img /tmp/hepu_iso_$$

echo ""
echo "=== 构建完成 v${VERSION} ==="
echo "  源码:  tools/hepboot/uefi/"
echo "  IMG:   $IMG_NAME"
echo "  ISO:   $ISO_NAME"
echo "  数据盘: $DATA_NAME"
echo ""
echo "测试命令:"
echo "  qemu-system-x86_64 -bios /usr/share/OVMF/OVMF_CODE.fd -cdrom $ISO_NAME -hda $DATA_NAME"
