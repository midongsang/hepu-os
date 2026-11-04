#!/bin/bash
# ============================================================================
# HepuOS 双内核 UEFI+GPT 镜像构建脚本
# 功能：GPT分区表 + ESP(FAT32) + ext2根分区
#       ESP包含BOOTX64.EFI + kernel64.bin + kernel32.bin + hepboot.cfg
# ============================================================================

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

VERSION="0.10.71"
IMG_NAME="/tmp/hepu_uefi_dual.img"
ESP_TMP="/tmp/hepu_esp_$$"

echo "=== HepuOS 双内核 UEFI 镜像构建 v${VERSION} ==="

# 1. 检查文件
echo "[1/5] 检查文件..."
[ -f tools/hepboot/uefi/hepboot_uefi.efi ] || { echo "ERROR: UEFI引导器不存在"; exit 1; }
[ -f build_x86_64/hepu-kernel64.bin ] || { echo "ERROR: 64位内核不存在"; exit 1; }
[ -f build_v250/hepu-kernel.bin ] || { echo "ERROR: 32位内核不存在"; exit 1; }
echo "  UEFI引导器: $(stat -c%s tools/hepboot/uefi/hepboot_uefi.efi) bytes"
echo "  64位内核: $(stat -c%s build_x86_64/hepu-kernel64.bin) bytes"
echo "  32位内核: $(stat -c%s build_v250/hepu-kernel.bin) bytes"

# 2. 准备ESP分区文件
echo "[2/5] 准备ESP分区文件..."
rm -rf "$ESP_TMP"
mkdir -p "$ESP_TMP/EFI/BOOT"
cp tools/hepboot/uefi/hepboot_uefi.efi "$ESP_TMP/EFI/BOOT/BOOTX64.EFI"
cp build_x86_64/hepu-kernel64.bin "$ESP_TMP/kernel64.bin"
cp build_v250/hepu-kernel.bin "$ESP_TMP/kernel32.bin"
cat > "$ESP_TMP/hepboot.cfg" << EOF
# HepuBoot UEFI Configuration v${VERSION}
timeout=5
default=0
kernel_arch=64
cmdline=
EOF

# 3. 创建GPT镜像
echo "[3/5] 创建GPT镜像 (256MB)..."
rm -f "$IMG_NAME"
dd if=/dev/zero of="$IMG_NAME" bs=1M count=256 2>/dev/null

# 使用parted创建GPT分区
parted -s "$IMG_NAME" mklabel gpt 2>/dev/null
parted -s "$IMG_NAME" mkpart ESP fat32 1MiB 128MiB 2>/dev/null
parted -s "$IMG_NAME" set 1 esp on 2>/dev/null
parted -s "$IMG_NAME" mkpart DATA ext2 128MiB 250MiB 2>/dev/null

# 4. 格式化ESP分区并写入文件
echo "[4/5] 格式化ESP分区并写入文件..."
mkfs.vfat -F 32 --offset=2048 "$IMG_NAME" >/dev/null 2>&1
mcopy -i "$IMG_NAME"@@1M -s "$ESP_TMP/EFI" ::/ 2>/dev/null
mcopy -o -i "$IMG_NAME"@@1M "$ESP_TMP/kernel64.bin" ::/ 2>/dev/null
mcopy -o -i "$IMG_NAME"@@1M "$ESP_TMP/kernel32.bin" ::/ 2>/dev/null
mcopy -o -i "$IMG_NAME"@@1M "$ESP_TMP/hepboot.cfg" ::/ 2>/dev/null

# 5. 格式化ext2分区
echo "[5/5] 格式化ext2根分区..."
# ext2分区从128MiB开始 = 262144扇区
EXT2_OFFSET=$((128 * 1024 * 1024))
dd if=/dev/zero of="$IMG_NAME" bs=1M seek=128 count=120 conv=notrunc 2>/dev/null
mkfs.ext2 -F -E offset=$EXT2_OFFSET "$IMG_NAME" 122880 >/dev/null 2>&1

# 写入字体到ext2分区
if [ -f kernel/fonts/DejaVuSans.ttf ]; then
    mkdir -p /tmp/ext2_mnt
    # 使用debugfs写入文件
    echo "mkdir /fonts" | debugfs -w "$IMG_NAME" -R "mkdir /fonts" >/dev/null 2>&1
    debugfs -w "$IMG_NAME" -R "write kernel/fonts/DejaVuSans.ttf /fonts/DejaVuSans.ttf" >/dev/null 2>&1
fi

# 清理
rm -rf "$ESP_TMP"

echo ""
echo "=== 构建完成 ==="
echo "  镜像: $IMG_NAME"
echo "  大小: $(stat -c%s "$IMG_NAME") bytes ($(du -h "$IMG_NAME" | cut -f1))"
echo ""
echo "镜像布局:"
echo "  GPT分区表"
echo "  ESP分区 (FAT32, 127MB): /EFI/BOOT/BOOTX64.EFI + kernel64.bin + kernel32.bin"
echo "  DATA分区 (ext2, 122MB): 根文件系统 + /fonts/"
echo ""
echo "测试命令 (UEFI):"
echo "  qemu-system-x86_64 -bios /usr/share/OVMF/OVMF_CODE.fd -drive format=raw,file=$IMG_NAME"
