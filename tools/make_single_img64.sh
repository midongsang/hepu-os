#!/bin/bash
# 赫普OS 双内核镜像制作脚本
# 布局：MBR + Stage2 + 64位内核 + 32位内核 + ext2根分区

PROJECT=$(cd "$(dirname "$0")/.." && pwd)
IMG=/tmp/hepu64_single.img
TMPDIR=/tmp/hepu64_build
SIZE_MB=${HEPU_IMG_SIZE_MB:-256}   # V0.13.53: 出厂镜像=试用+安装介质, 256MB(体积减半)
SYS_MB=${HEPU_SYS_MB:-224}          # V0.13.53: 系统分区(ext2), 适配256MB镜像(从LBA13000起, 上限~248MB)
# V0.13.52: 配置/自由分区不在出厂镜像预分配(镜像=整盘拷贝, 预分配会让镜像膨胀)
# 分区大小只在安装向导(install64)写目标硬盘时分配: P1可增大/ P2默认512MB / P3剩余, 只受硬盘容量限制

# 布局常量
EXT2_LBA=13000         # ext2分区起始LBA（V0.54.6: 32位内核占至LBA11631，故后移）
SYS_SECTORS=$((SYS_MB * 2048))      # 系统分区扇区数（三区制 P1）
KERNEL64_LBA=2000      # 64位内核起始LBA（避开ISO元数据区域，兼容CD-ROM引导）
KERNEL32_LBA=3400      # 32位内核起始LBA（V0.54.6: 原3000与64位内核重叠，后移）

# 清理
rm -f $IMG
mkdir -p $TMPDIR

# 1. 创建256MB镜像
truncate -s ${SIZE_MB}M $IMG 2>/dev/null   # V0.13.55: 稀疏创建, 物理占用=实际数据
echo "[1/6] 镜像创建完成（${SIZE_MB}MB）"

# 2. 写入MBR
# 构建链加固：镜像依赖 hepboot_mbr.bin，但没有任何 Makefile/CI 目标自动构建它，
# 缺失时 dd 会静默失败，导致镜像前 446 字节无 MBR 引导代码、无法从 MBR 启动。
# 打包前若缺失则自动构建（gcc -m32 → ld 0x7C00 → objcopy）。
MBR_BIN=$PROJECT/tools/hepboot/hepboot_mbr.bin
if [ ! -f "$MBR_BIN" ]; then
    echo "[2a] 构建 MBR 引导桩..."
    gcc -m32 -c "$PROJECT/tools/hepboot/hepboot_mbr.S" -o "$TMPDIR/mbr.o" \
        && ld -Ttext=0x7C00 -m elf_i386 "$TMPDIR/mbr.o" -o "$TMPDIR/mbr.elf" \
        && objcopy -O binary "$TMPDIR/mbr.elf" "$MBR_BIN" \
        || { echo "[错误] MBR 引导桩构建失败，中止（检查 TMPDIR 权限与 32 位工具链）"; exit 1; }
fi
dd if=$MBR_BIN of=$IMG bs=512 count=1 conv=notrunc \
    || { echo "[错误] MBR 写入失败，中止"; exit 1; }
echo "[2/6] MBR引导桩写入完成"

# 3. 创建MBR分区表（ext2从LBA9000开始）
#    ⚠️ CHS 字段不能留 0 —— 部分 BIOS 混合模式下查 CHS 确认分区边界
python3 << PYEOF
import struct
IMG='$IMG'
EXT2_LBA=$EXT2_LBA
SYS_SECTORS=$SYS_SECTORS
END_LBA=EXT2_LBA+SYS_SECTORS-1

# MBR CHS 公式（LBA 转 1023/255/63 兼容表示法）
# sector 从 1 起，cylinder 10 bit，sector 6 bit，head 8 bit
def lba_to_chs(lba):
    if lba >= 1023*255*63:   # 超出 CHS 上限，填 1023/254/63（LBA 标记）
        return (254, 63, 1023)
    hpc, spt = 255, 63
    cylinder = lba // (hpc * spt)
    head     = (lba % (hpc * spt)) // spt
    sector   = (lba % spt) + 1
    return (head, sector, cylinder)

def pack_chs(head, sector, cylinder):
    # byte[1]=head, byte[2]=sector低6bit | cylinder高2bit, byte[3]=cylinder低8bit
    return bytes([head, ((sector & 0x3F) | ((cylinder & 0x300) >> 2)), (cylinder & 0xFF)])

sh, ss, sc = lba_to_chs(EXT2_LBA)
eh, es, ec = lba_to_chs(END_LBA)
chs_start = pack_chs(sh, ss, sc)
chs_end   = pack_chs(eh, es, ec)

print(f"  MBR PTE CHS: start=H{sh} S{ss} C{sc}  end=H{eh} S{es} C{ec}  LBA={EXT2_LBA}..{END_LBA}")

with open(IMG, 'r+b') as f:
    f.seek(446)
    entry = bytes([0x80]) + chs_start + bytes([0x83]) + chs_end \
          + struct.pack('<I', EXT2_LBA) + struct.pack('<I', SYS_SECTORS)
    assert len(entry) == 16
    f.write(entry)
    f.write(b'\x00' * 48)   # PTE 2-4 空
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

# 5b. 写入32位内核（LBA3400+，最多4.1MB）
if [ -f $PROJECT/build_v250/hepu-kernel.bin ]; then
    K32_SECTORS=$((($(stat -c%s $PROJECT/build_v250/hepu-kernel.bin) + 511) / 512))
    K32_END=$((KERNEL32_LBA + K32_SECTORS))
    if [ "$K32_END" -gt "$EXT2_LBA" ]; then
        echo "错误: 32位内核占用至 LBA$K32_END，与 ext2(LBA$EXT2_LBA)重叠" >&2
        exit 1
    fi
    dd if=$PROJECT/build_v250/hepu-kernel.bin of=$IMG bs=512 seek=$KERNEL32_LBA conv=notrunc
    echo "[5b] 32位内核写入完成（LBA$KERNEL32_LBA，至LBA$K32_END）"
fi

# 6. 创建ext2文件系统（从LBA13000开始）
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
        # V0.54.13: Xcursor 光标主题 (/usr/share/cursors/*.xcur)
        if [ -d ${PROJECT}/assets/cursors ]; then
            mkdir -p /tmp/hepu_root_mnt/usr/share/cursors
            cp ${PROJECT}/assets/cursors/*.xcur /tmp/hepu_root_mnt/usr/share/cursors/ 2>/dev/null
        fi
        cp ${PROJECT}/kernel/fonts/DejaVuSans.ttf /tmp/hepu_root_mnt/fonts/ 2>/dev/null
        # 复制桌面配置文件
        if [ -f ${PROJECT}/rootfs/etc/hepushell/config.hsqml ]; then
            cp ${PROJECT}/rootfs/etc/hepushell/config.hsqml /tmp/hepu_root_mnt/etc/hepushell/ 2>/dev/null
        fi
        # 复制32位内核到ext2分区（为双内核启动做准备）
        if [ -f ${PROJECT}/build_v250/hepu-kernel.bin ]; then
            cp ${PROJECT}/build_v250/hepu-kernel.bin /tmp/hepu_root_mnt/boot/kernel32.bin 2>/dev/null
            echo "  32位内核已复制到ext2 (/boot/kernel32.bin)"
        fi
        # P1-7: glibc 动态链接运行时 + 动态 hello（宿主 gcc 默认产物）
        # glibc 运行时优先级：1) dynhello/ 目录（可离线）  2) 宿主系统 glibc（自动 fallback）
        DYN=$PROJECT/user/apps/dynhello
        HOST_LD=$(ldconfig -p 2>/dev/null | grep "ld-linux-x86-64\.so\.2$" | awk '{print $NF; exit}')
        HOST_LIBC=$(ldconfig -p 2>/dev/null | grep "libc\.so\.6$" | awk '{print $NF; exit}')
        LD_SRC="$DYN/ld-linux-x86-64.so.2"
        LIBC_SRC="$DYN/libc.so.6"
        [ -f "$LD_SRC" ] || LD_SRC="$HOST_LD"
        [ -f "$LIBC_SRC" ] || LIBC_SRC="$HOST_LIBC"
        if [ -f "$DYN/hello_dyn.elf" ] && [ -n "$LD_SRC" ] && [ -n "$LIBC_SRC" ]; then
            mkdir -p /tmp/hepu_root_mnt/lib64 /tmp/hepu_root_mnt/lib/x86_64-linux-gnu /tmp/hepu_root_mnt/bin
            cp "$LD_SRC"   /tmp/hepu_root_mnt/lib64/ld-linux-x86-64.so.2            2>/dev/null && echo "  ld-linux-x86-64.so.2  ($LD_SRC)"
            cp "$LIBC_SRC" /tmp/hepu_root_mnt/lib/x86_64-linux-gnu/libc.so.6       2>/dev/null && echo "  libc.so.6            ($LIBC_SRC)"
            cp "$DYN/hello_dyn.elf" /tmp/hepu_root_mnt/bin/glibchello.elf          2>/dev/null
            echo "  动态链接运行时已复制（glibc 动态 hello: /bin/glibchello.elf）"
        fi
        sync
        umount /tmp/hepu_root_mnt 2>/dev/null
    fi
    losetup -d $LOOPDEV 2>/dev/null
    echo "[6/6] ext2根分区创建完成（LBA$EXT2_LBA+，含字体）"
else
    # loop设备不可用，用dd方式创建ext2
    echo "[6/6] loop设备不可用，使用备用方式..."
    EXT2_SIZE_MB=$SYS_MB
    truncate -s ${EXT2_SIZE_MB}M $TMPDIR/rootfs.ext2 2>/dev/null   # V0.13.55: 稀疏
    mkfs.ext2 -F -L "HEPU_ROOT" $TMPDIR/rootfs.ext2 2>/dev/null
    # 使用debugfs写入字体文件和用户态程序
    if command -v debugfs &>/dev/null; then
        echo "mkdir /fonts" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
        # V0.54.13: Xcursor 光标主题
        if [ -d "$PROJECT/assets/cursors" ]; then
            echo "mkdir /usr" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            echo "mkdir /usr/share" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            echo "mkdir /usr/share/cursors" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            for xc in "$PROJECT"/assets/cursors/*.xcur; do
                [ -f "$xc" ] || continue
                echo "write $xc /usr/share/cursors/$(basename $xc)" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            done
            echo "  光标主题已写入 /usr/share/cursors ($(ls "$PROJECT"/assets/cursors/*.xcur 2>/dev/null | wc -l) 个)"
        fi
        echo "write $PROJECT/kernel/fonts/DejaVuSans.ttf /fonts/DejaVuSans.ttf" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
        # 写入桌面配置文件
        if [ -f "$PROJECT/rootfs/etc/hepushell/config.hsqml" ]; then
            echo "mkdir /etc" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            echo "mkdir /drivers" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            if [ -f "$PROJECT/build_x86_64/testdrv_pic.o" ]; then
                echo "write $PROJECT/build_x86_64/testdrv_pic.o /drivers/testdrv.o" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            fi
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
            if [ -f "$PROJECT/user/build/sec_test.elf" ]; then
                echo "write $PROJECT/user/build/sec_test.elf /bin/sec_test.elf" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            fi
            if [ -f "$PROJECT/user/build/forktest.elf" ]; then
                echo "write $PROJECT/user/build/forktest.elf /bin/forktest.elf" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            fi
            if [ -f "$PROJECT/user/build/browser.elf" ]; then
                echo "write $PROJECT/user/build/browser.elf /bin/browser.elf" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            fi
            if [ -f "$PROJECT/user/build/scrapling.elf" ]; then
                echo "write $PROJECT/user/build/scrapling.elf /bin/scrapling.elf" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            fi
            if [ -f "$PROJECT/user/build/comptest.elf" ]; then
                echo "write $PROJECT/user/build/comptest.elf /bin/comptest.elf" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            fi
            if [ -f "$PROJECT/user/build/ps.elf" ]; then
                echo "write $PROJECT/user/build/ps.elf /bin/ps.elf" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            fi
            if [ -f "$PROJECT/user/build/pipe_test.elf" ]; then
                echo "write $PROJECT/user/build/pipe_test.elf /bin/pipe_test.elf" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            fi
            # V0.33: Linux 兼容层 PIE 测试程序（宿主 gcc -static-pie 产物）
            if [ -f "$PROJECT/user/apps/linux_pie_demo/linux_pie_demo.elf" ]; then
                echo "write $PROJECT/user/apps/linux_pie_demo/linux_pie_demo.elf /bin/linux_pie_demo.elf" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            fi
            # V0.33: freestanding static-PIE 探针（无 libc，只用 write/exit）
            if [ -f "$PROJECT/user/apps/pie_probe/pie_probe.elf" ]; then
                echo "write $PROJECT/user/apps/pie_probe/pie_probe.elf /bin/pie_probe.elf" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            fi
            # V0.33 SEC-12: 历史测试程序 —— 源码已补齐（user/apps/legacy/），
            # 产物在 user/build/legacy/，这里统一写入，取代当年「无源码的历史二进制」。
            # crash_jump.elf 是**故意**跳坏地址的异常测试（SEC-07 覆盖）。
            if [ -d "$PROJECT/user/build/legacy" ]; then
                for f in hello_static hello_linux hello2 hello3 hello4 hello_c2 \
                         crash_jump glibc_test hello_libc dynhello dynhello2; do
                    if [ -f "$PROJECT/user/build/legacy/$f.elf" ]; then
                        echo "write $PROJECT/user/build/legacy/$f.elf /bin/$f.elf" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
                    fi
                done
                echo "mkdir /lib" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
                for f in libtest libtest2; do
                    if [ -f "$PROJECT/user/build/legacy/$f.so" ]; then
                        echo "write $PROJECT/user/build/legacy/$f.so /lib/$f.so" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
                    fi
                done
            fi
            # P1-7: glibc 动态链接运行时 + 动态 hello（debugfs fallback 路径）
            DYN="$PROJECT/user/apps/dynhello"
            HOST_LD=$(ldconfig -p 2>/dev/null | grep "ld-linux-x86-64\.so\.2$" | awk '{print $NF; exit}')
            HOST_LIBC=$(ldconfig -p 2>/dev/null | grep "libc\.so\.6$" | awk '{print $NF; exit}')
            LD_SRC="$DYN/ld-linux-x86-64.so.2"
            LIBC_SRC="$DYN/libc.so.6"
            [ -f "$LD_SRC" ] || LD_SRC="$HOST_LD"
            [ -f "$LIBC_SRC" ] || LIBC_SRC="$HOST_LIBC"
            if [ -f "$DYN/hello_dyn.elf" ] && [ -n "$LD_SRC" ] && [ -n "$LIBC_SRC" ]; then
                echo "mkdir /lib64" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
                echo "mkdir /lib/x86_64-linux-gnu" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
                echo "write $LD_SRC /lib64/ld-linux-x86-64.so.2"            | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
                echo "write $LIBC_SRC /lib/x86_64-linux-gnu/libc.so.6"     | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
                echo "write $DYN/hello_dyn.elf /bin/glibchello.elf"        | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
                echo "  动态链接运行时已写入 ext2（glibc 动态 hello: /bin/glibchello.elf）"
            fi
            # V0.13.43: 兼容层测试程序写入 /bin/ (ELF32/PE/Mach-O, 供 run 命令加载)
            for f in linuxhello.elf winhello.exe winargs.exe macho_demo; do
                if [ -f "$PROJECT/build_x86_64/$f" ]; then
                    echo "write $PROJECT/build_x86_64/$f /bin/$f" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
                fi
            done
            # V0.13.46: 进程管理测试程序（死循环，验证随时中断/未响应结束）
            if [ -f "$PROJECT/build_x86_64/loop64.elf" ]; then
                echo "write $PROJECT/build_x86_64/loop64.elf /bin/loop64.elf" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            fi
            if [ -f "$PROJECT/build_x86_64/mbrtool.elf" ]; then
                echo "write $PROJECT/build_x86_64/mbrtool.elf /bin/mbrtool.elf" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            fi
            if [ -f "$PROJECT/build_x86_64/install64.elf" ]; then
                echo "write $PROJECT/build_x86_64/install64.elf /bin/install64.elf" | debugfs -w $TMPDIR/rootfs.ext2 2>/dev/null
            fi
            echo "[6/6] Font + user apps written via debugfs"
        else
            echo "[6/6] Font written via debugfs (user apps not built)"
        fi
    fi
    dd if=$TMPDIR/rootfs.ext2 of=$IMG bs=512 seek=$EXT2_LBA conv=notrunc,sparse 2>/dev/null   # V0.13.55: 稀疏写(空洞跳过,数据保留)
    echo "[6/6] ext2根分区创建完成（备用方式，LBA$EXT2_LBA）"
fi

echo ""
echo "=== 双内核镜像创建完成 ==="
echo "镜像: $IMG"
echo "大小: $(ls -lh $IMG | awk '{print $5}')"
echo "布局:"
echo "  MBR:        LBA0 (512字节)"
echo "  Stage2:     LBA34-49 (8KB)"
echo "  64位内核:   LBA$KERNEL64_LBA+ (最多640KB)"
echo "  32位内核:   LBA$KERNEL32_LBA+ (4.1MB)"
echo "  ext2根分区: LBA$EXT2_LBA+ (剩余空间)"
echo ""

# 显示分区表
fdisk -l $IMG 2>/dev/null | tail -10
