#!/bin/bash
# ============================================================================
# 赫普OS 混合引导镜像制作脚本（BIOS + UEFI）
# 布局：保护性MBR(含引导代码) + GPT + Stage2 + ESP分区 + 内核 + ext2根分区
# ============================================================================

PROJECT=/home/user/Doubao/chats/38442532206397698/hepu_os_src/hepu-os
IMG=/tmp/hepu64_hybrid.img
TMPDIR=/tmp/hepu64_hybrid_build

# 布局常量（与BIOS引导兼容，内核放在固定LBA）
STAGE2_LBA=34        # Stage2起始LBA（GPT分区表占LBA1-33）
KERNEL64_LBA=2000    # 64位内核起始LBA
KERNEL32_LBA=3000    # 32位内核起始LBA
ESP_START_MB=2       # ESP分区起始（MB）
ESP_SIZE_MB=50       # ESP分区大小（MB）
EXT2_START_MB=60     # ext2分区起始（MB）

# 清理
rm -f $IMG
mkdir -p $TMPDIR

echo "=== 赫普OS 混合引导镜像构建 ==="

# 1. 创建320MB镜像
dd if=/dev/zero of=$IMG bs=1M count=320 2>/dev/null
echo "[1/8] 镜像创建完成（320MB）"

# 2. 创建GPT分区表
parted -s $IMG mklabel gpt 2>/dev/null
# ESP分区（FAT32，用于UEFI引导）
parted -s $IMG mkpart ESP fat32 ${ESP_START_MB}MiB $((ESP_START_MB + ESP_SIZE_MB))MiB 2>/dev/null
parted -s $IMG set 1 esp on 2>/dev/null
# ext2根分区
parted -s $IMG mkpart ROOT ext2 ${EXT2_START_MB}MiB 100% 2>/dev/null
echo "[2/8] GPT分区表创建完成（ESP + ext2）"

# 3. 写入保护性MBR引导代码（保留GPT的0xEE分区标识）
# 先保存GPT的保护性MBR分区表（偏移446-511），然后写入引导代码
dd if=$PROJECT/tools/hepboot/hepboot_mbr.bin of=$IMG bs=446 count=1 conv=notrunc 2>/dev/null
# 确保MBR签名
python3 -c "
with open('$IMG', 'r+b') as f:
    f.seek(510)
    f.write(b'\x55\xAA')
"
echo "[3/8] 保护性MBR引导代码写入完成"

# 4. 写入Stage2（LBA34开始，32扇区=16KB）
dd if=$PROJECT/tools/hepboot/x86_64/stage2_64.bin of=$IMG bs=512 seek=$STAGE2_LBA count=64 conv=notrunc 2>/dev/null
echo "[4/8] Stage2写入完成（LBA$STAGE2_LBA，32KB）"

# 5. 写入64位内核
KERNEL64_SIZE=$(stat -c%s $PROJECT/build_x86_64/hepu-kernel64.bin)
dd if=$PROJECT/build_x86_64/hepu-kernel64.bin of=$IMG bs=512 seek=$KERNEL64_LBA conv=notrunc
echo "[5/8] 64位内核写入完成（LBA$KERNEL64_LBA，${KERNEL64_SIZE}字节）"

# 5b. 写入32位内核
if [ -f $PROJECT/build_v250/hepu-kernel.bin ]; then
    dd if=$PROJECT/build_v250/hepu-kernel.bin of=$IMG bs=512 seek=$KERNEL32_LBA conv=notrunc
    echo "[5b] 32位内核写入完成（LBA$KERNEL32_LBA）"
fi

# 6. 格式化ESP分区并复制UEFI引导器
ESP_OFFSET=$((ESP_START_MB * 1024 * 1024))
LOOP_ESP=$(losetup -f --show -o $ESP_OFFSET --sizelimit ${ESP_SIZE_MB}M $IMG 2>/dev/null)
if [ -n "$LOOP_ESP" ]; then
    mkfs.vfat -F 32 -n HEPUEFI $LOOP_ESP >/dev/null 2>&1
    # 创建EFI目录结构
    mmd -i $LOOP_ESP ::/EFI 2>/dev/null
    mmd -i $LOOP_ESP ::/EFI/BOOT 2>/dev/null
    # 复制UEFI引导器
    if [ -f $PROJECT/tools/hepboot/uefi/hepboot_uefi.efi ]; then
        mcopy -o -i $LOOP_ESP $PROJECT/tools/hepboot/uefi/hepboot_uefi.efi ::/EFI/BOOT/BOOTX64.EFI 2>/dev/null
        echo "[6/8] ESP分区格式化完成，UEFI引导器已复制"
    else
        echo "[6/8] ESP分区格式化完成（未找到UEFI引导器，使用默认）"
    fi
    # 复制内核到ESP（供UEFI引导器加载）
    mcopy -o -i $LOOP_ESP $PROJECT/build_x86_64/hepu-kernel64.bin ::/hepu-kernel64.bin 2>/dev/null
    losetup -d $LOOP_ESP 2>/dev/null
else
    echo "[6/8] ESP分区：无法挂载loop设备，跳过"
fi

# 7. 创建ext2文件系统
EXT2_OFFSET=$((EXT2_START_MB * 1024 * 1024))
LOOP_EXT2=$(losetup -f --show -o $EXT2_OFFSET $IMG 2>/dev/null)
if [ -n "$LOOP_EXT2" ]; then
    mkfs.ext2 -q -F -b 1024 $LOOP_EXT2 2>/dev/null
    # 写入配置文件
    mkdir -p $TMPDIR/mnt
    mount $LOOP_EXT2 $TMPDIR/mnt 2>/dev/null
    if [ $? -eq 0 ]; then
        mkdir -p $TMPDIR/mnt/etc/hepushell
        mkdir -p $TMPDIR/mnt/bin
        mkdir -p $TMPDIR/mnt/home
        # 复制默认配置
        if [ -f $PROJECT/rootfs/etc/hepushell/config.hsqml ]; then
            cp $PROJECT/rootfs/etc/hepushell/config.hsqml $TMPDIR/mnt/etc/hepushell/ 2>/dev/null
        fi
        # 创建版本文件
        echo "HepuOS v0.10.171 Hybrid (BIOS+UEFI)" > $TMPDIR/mnt/etc/version
        echo "Built: $(date)" >> $TMPDIR/mnt/etc/version
        umount $TMPDIR/mnt 2>/dev/null
    fi
    losetup -d $LOOP_EXT2 2>/dev/null
    echo "[7/8] ext2根文件系统创建完成"
else
    echo "[7/8] ext2：无法挂载loop设备，跳过"
fi

# 8. 验证
echo "[8/8] 验证镜像..."
fdisk -l $IMG 2>/dev/null | head -15
echo ""
echo "=== 混合引导镜像创建完成 ==="
echo "镜像: $IMG"
echo "大小: $(du -h $IMG | cut -f1)"
echo "布局:"
echo "  保护性MBR: LBA0 (含BIOS引导代码)"
echo "  GPT表头:   LBA1-33"
echo "  Stage2:    LBA$STAGE2_LBA (32KB)"
echo "  64位内核:  LBA$KERNEL64_LBA (${KERNEL64_SIZE}字节)"
echo "  32位内核:  LBA$KERNEL32_LBA"
echo "  ESP分区:   ${ESP_START_MB}MiB - $((ESP_START_MB + ESP_SIZE_MB))MiB (FAT32, UEFI引导)"
echo "  ext2根分区: ${EXT2_START_MB}MiB - 末尾"
echo ""
echo "引导方式:"
echo "  BIOS: MBR引导代码 -> Stage2 -> 内核（固定LBA）"
echo "  UEFI: ESP/EFI/BOOT/BOOTX64.EFI -> 内核"
echo "  Rufus: 使用DD模式写入（推荐）"
