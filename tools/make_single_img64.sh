#!/bin/bash
# 赫普OS 双内核镜像制作脚本
# 布局：MBR + Stage2 + 64位内核 + 32位内核 + ext2根分区

PROJECT=/home/user/Doubao/chats/38442532206397698/hepu_os_src/hepu-os
IMG=/tmp/hepu64_single.img
TMPDIR=/tmp/hepu64_build

# 布局常量
EXT2_LBA=9000          # ext2分区起始LBA
KERNEL64_LBA=2000      # 64位内核起始LBA（避开ISO元数据区域，兼容CD-ROM引导）
KERNEL32_LBA=3000      # 32位内核起始LBA

# 清理
rm -f $IMG
mkdir -p $TMPDIR

# 1. 创建256MB镜像
dd if=/dev/zero of=$IMG bs=1M count=256 2>/dev/null
echo "[1/6] 镜像创建完成（256MB）"

# 2. 写入MBR
dd if=$PROJECT/tools/hepboot/hepboot_mbr.bin of=$IMG bs=512 count=1 conv=notrunc 2>/dev/null
echo "[2/6] MBR引导桩写入完成"

# 3. 创建MBR分区表（ext2从LBA9000开始）
python3 << PYEOF
import struct
with open('$IMG', 'r+b') as f:
    f.seek(446)
    # 分区1: ext2, 可引导, LBA=$EXT2_LBA
    entry = bytes([0x80, 0x00, 0x00, 0x00, 0x83, 0x00, 0x00, 0x00])
    entry += struct.pack('<I', $EXT2_LBA)
    entry += struct.pack('<I', 524288 - $EXT2_LBA)
    f.write(entry)
    f.write(b'\x00' * 48)
    f.seek(510)
    f.write(b'\x55\xAA')
PYEOF
echo "[3/6] MBR分区表创建完成（ext2 @ LBA$EXT2_LBA）"

# 4. 写入Stage2（LBA34-65，16KB）
dd if=$PROJECT/tools/hepboot/x86_64/stage2_64.bin of=$IMG bs=512 seek=34 count=32 conv=notrunc 2>/dev/null
echo "[4/6] Stage2写入完成（16KB）"

# 5. 写入64位内核（LBA100+）
KERNEL64_SIZE=$(stat -c%s $PROJECT/build_x86_64/hepu-kernel64.bin)
dd if=$PROJECT/build_x86_64/hepu-kernel64.bin of=$IMG bs=512 seek=$KERNEL64_LBA conv=notrunc
echo "[5/6] 64位内核写入完成（LBA$KERNEL64_LBA，${KERNEL64_SIZE}字节）"

# 5b. 写入32位内核（LBA300+，4.1MB）
if [ -f $PROJECT/build_v250/hepu-kernel.bin ]; then
    dd if=$PROJECT/build_v250/hepu-kernel.bin of=$IMG bs=512 seek=$KERNEL32_LBA conv=notrunc
    echo "[5b] 32位内核写入完成（LBA$KERNEL32_LBA）"
fi

# 6. 创建ext2文件系统（从LBA9000开始）
EXT2_OFFSET=$((EXT2_LBA * 512))
LOOPDEV=$(losetup -f --show -o $EXT2_OFFSET $IMG 2>/dev/null)
if [ -n "$LOOPDEV" ]; then
    mkfs.ext2 -F -L "HEPU_ROOT" $LOOPDEV 2>/dev/null
    # 复制字体文件到ext2分区
    mkdir -p /tmp/hepu_root_mnt
    mount $LOOPDEV /tmp/hepu_root_mnt 2>/dev/null
    if [ -d /tmp/hepu_root_mnt ]; then
        mkdir -p /tmp/hepu_root_mnt/fonts
        mkdir -p /tmp/hepu_root_mnt/boot
        mkdir -p /tmp/hepu_root_mnt/etc/hepushell
        cp /home/user/Doubao/chats/38442532206397698/hepu_os_src/hepu-os/kernel/fonts/DejaVuSans.ttf /tmp/hepu_root_mnt/fonts/ 2>/dev/null
        # 复制HepuShell配置文件
        if [ -f /home/user/Doubao/chats/38442532206397698/hepu_os_src/hepu-os/rootfs/etc/hepushell/config.hsqml ]; then
            cp /home/user/Doubao/chats/38442532206397698/hepu_os_src/hepu-os/rootfs/etc/hepushell/config.hsqml /tmp/hepu_root_mnt/etc/hepushell/ 2>/dev/null
        fi
        # 复制32位内核到ext2分区（为双内核启动做准备）
        if [ -f /home/user/Doubao/chats/38442532206397698/hepu_os_src/hepu-os/build_v250/hepu-kernel.bin ]; then
            cp /home/user/Doubao/chats/38442532206397698/hepu_os_src/hepu-os/build_v250/hepu-kernel.bin /tmp/hepu_root_mnt/boot/kernel32.bin 2>/dev/null
            echo "  32位内核已复制到ext2 (/boot/kernel32.bin)"
        fi
        sync
        umount /tmp/hepu_root_mnt 2>/dev/null
    fi
    losetup -d $LOOPDEV 2>/dev/null
    echo "[6/6] ext2根分区创建完成（LBA$EXT2_LBA+，含字体）"
else
    # loop设备不可用，用dd方式创建ext2
    echo "[6/6] loop设备不可用，使用备用方式..."
    EXT2_SIZE_MB=$((256 - EXT2_LBA * 512 / 1024 / 1024))
    dd if=/dev/zero of=$TMPDIR/rootfs.ext2 bs=1M count=$EXT2_SIZE_MB 2>/dev/null
    mkfs.ext2 -F -L "HEPU_ROOT" $TMPDIR/rootfs.ext2 2>/dev/null
    # 使用debugfs写入字体文件和用户态程序
    if command -v debugfs &>/dev/null; then
        echo "mkdir /fonts" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
        echo "write $PROJECT/kernel/fonts/DejaVuSans.ttf /fonts/DejaVuSans.ttf" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
        # 写入HepuShell配置文件
        if [ -f "$PROJECT/rootfs/etc/hepushell/config.hsqml" ]; then
            echo "mkdir /etc" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            echo "mkdir /etc/hepushell" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            echo "write $PROJECT/rootfs/etc/hepushell/config.hsqml /etc/hepushell/config.hsqml" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
        fi
        # 写入用户态Hello World程序和Shell
        if [ -f "$PROJECT/user/build/hello.elf" ]; then
            echo "mkdir /bin" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            echo "write $PROJECT/user/build/hello.elf /bin/hello.elf" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            if [ -f "$PROJECT/user/build/shell.elf" ]; then
                echo "write $PROJECT/user/build/shell.elf /bin/shell.elf" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            fi
            echo "[6/6] Font + user apps written via debugfs"
        else
            echo "[6/6] Font written via debugfs (user apps not built)"
        fi
    fi
    dd if=$TMPDIR/rootfs.ext2 of=$IMG bs=512 seek=$EXT2_LBA conv=notrunc 2>/dev/null
    echo "[6/6] ext2根分区创建完成（备用方式，LBA$EXT2_LBA）"
fi

echo ""
echo "=== 双内核镜像创建完成 ==="
echo "镜像: $IMG"
echo "大小: $(ls -lh $IMG | awk '{print $5}')"
echo "布局:"
echo "  MBR:        LBA0 (512字节)"
echo "  Stage2:     LBA34-49 (8KB)"
echo "  64位内核:   LBA$KERNEL64_LBA+ (124KB)"
echo "  32位内核:   LBA$KERNEL32_LBA+ (4.1MB)"
echo "  ext2根分区: LBA$EXT2_LBA+ (剩余空间)"
echo ""

# 显示分区表
fdisk -l $IMG 2>/dev/null | tail -10
