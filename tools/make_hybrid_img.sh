#!/bin/bash
# ============================================================================
# 赫普OS 混合引导镜像制作脚本（BIOS + UEFI）
# 布局：保护性MBR(含引导代码) + GPT + Stage2 + ESP分区 + 内核 + ext2根分区
# ============================================================================

PROJECT="$(cd "$(dirname "$0")/.." && pwd)"
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
truncate -s 320M $IMG 2>/dev/null   # V0.13.55: 稀疏创建
echo "[1/8] 镜像创建完成（320MB）"

# 2b. 保护性MBR补 0x83 分区项（内核按MBR 0x83定位ext2，GPT分区表内核不支持）
python3 - "$IMG" <<'PYEOF'
import struct, sys
img = sys.argv[1]
with open(img, 'r+b') as f:
    f.seek(446)
    mbr = bytearray(f.read(64))
    # 分区项2: type=0x83 (Linux ext2), 起始LBA=60MB*2048=122880, 大小=320MB-60MB=260MB=532480扇区
    entry = bytearray(16)
    entry[0] = 0x00; entry[1] = 0x00; entry[2] = 0x02; entry[3] = 0x00
    entry[4] = 0x83
    entry[5] = 0x00; entry[6] = 0x03; entry[7] = 0x00
    entry[8:12] = struct.pack('<I', 122880)
    entry[12:16] = struct.pack('<I', 532480)
    mbr[16:32] = entry
    f.seek(446)
    f.write(mbr)
PYEOF
echo "[2b] 保护性MBR已补 0x83 ext2 分区项（LBA122880, 供内核定位）"

# 2. GPT分区表(容器内parted不可用, 用python手写GPT: 保护性MBR 0xEE + ESP + ext2项)
echo "[2/8] 创建GPT分区表(python)..."


# 2b2. python 生成 GPT(替代 parted, 容器内可靠)
SIZE_MB=320
EXT2_SECTORS=$((100 * 2048))
python3 $PROJECT/tools/make_gpt.py $IMG $SIZE_MB $ESP_START_MB $ESP_SIZE_MB $((EXT2_START_MB * 2048)) $EXT2_SECTORS $((EXT2_START_MB * 2048))

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

# 6. 生成ESP(FAT32)并写入(容器无losetup/mtools, 用fat32_generator直写偏移)
echo "[6/8] 生成ESP分区(FAT32)并写入UEFI引导器..."
python3 $PROJECT/tools/build_esp.py $IMG $ESP_START_MB $ESP_SIZE_MB \
    $PROJECT/tools/hepboot/uefi/hepboot_uefi.efi $PROJECT/build_x86_64/hepu-kernel64.bin

# 7. 创建ext2根文件系统（宿主mkfs + debugfs直写，容器内无loop）
EXT2_LBA=$((EXT2_START_MB * 2048))
ROOT_EXT2=$TMPDIR/rootfs.ext2
rm -f $ROOT_EXT2
truncate -s 100M $ROOT_EXT2 2>/dev/null   # V0.13.55: 稀疏
mkfs.ext2 -q -F -b 1024 $ROOT_EXT2 2>/dev/null
if [ -f "$PROJECT/rootfs/etc/hepushell/config.hsqml" ]; then
    echo "mkdir /etc" | debugfs -w $ROOT_EXT2 2>/dev/null
    echo "mkdir /etc/hepushell" | debugfs -w $ROOT_EXT2 2>/dev/null
    echo "write $PROJECT/rootfs/etc/hepushell/config.hsqml /etc/hepushell/config.hsqml" | debugfs -w $ROOT_EXT2 2>/dev/null
fi
echo "mkdir /home" | debugfs -w $ROOT_EXT2 2>/dev/null
echo "mkdir /bin" | debugfs -w $ROOT_EXT2 2>/dev/null
echo "write $PROJECT/build_x86_64/hepu-kernel64.elf /bin/hepu-kernel64.elf" | debugfs -w $ROOT_EXT2 2>/dev/null
printf "HepuOS v0.13.45 Hybrid (BIOS+UEFI)\nBuilt: %s\n" "$(date)" > $TMPDIR/version.txt
echo "write $TMPDIR/version.txt /etc/version" | debugfs -w $ROOT_EXT2 2>/dev/null
for f in linuxhello.elf winhello.exe winargs.exe macho_demo; do
    if [ -f "$PROJECT/build_x86_64/$f" ]; then
        echo "write $PROJECT/build_x86_64/$f /bin/$f" | debugfs -w $ROOT_EXT2 2>/dev/null
    fi
done
for f in hello.elf shell.elf; do
    if [ -f "$PROJECT/user/build/$f" ]; then
        echo "write $PROJECT/user/build/$f /bin/$f" | debugfs -w $ROOT_EXT2 2>/dev/null
    fi
done
# V0.13.49: 安装引导流工具
for f in mbrtool.elf install64.elf; do
    if [ -f "$PROJECT/build_x86_64/$f" ]; then
        echo "write $PROJECT/build_x86_64/$f /bin/$f" | debugfs -w $ROOT_EXT2 2>/dev/null
    fi
done
dd if=$ROOT_EXT2 of=$IMG bs=512 seek=$EXT2_LBA conv=notrunc,sparse 2>/dev/null   # V0.13.55: 稀疏写(空洞跳过,数据保留)
echo "[7/8] ext2根文件系统写入完成（debugfs直写 @ LBA$EXT2_LBA）"

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
