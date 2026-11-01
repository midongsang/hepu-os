#!/bin/bash
# ============================================================================
# 赫普OS - 可启动镜像总构建脚本
#
# 产出两套镜像(正式版 + 诊断版):
#   正式版: hepu-os-<VER>.iso / hepu-os-<VER>.img      (快速启动, 无调试)
#   诊断版: hepu-os-<VER>-debug.iso / .img             (逐阶段打印H1-H5+延时,
#            供真机排错/社区开发测试引导链路, 见DESIGN.md附录CF)
#
# 布局:
#   ISO : El Torito No-Emulation, 内核2048字节块
#   IMG : LBA0=MBR(含分区表), LBA1-3=stage2, LBA4+=内核
#   hybrid: MBR+stage2注入ISO前部, 内核物理扇区84起(兼容所有写入方式)
# ============================================================================
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
BLD=/tmp/hepu_boot
KERNEL=$ROOT/build_v250/hepu-kernel.bin
OUTDIR=$ROOT/build
# ===== 版本命名规则 =====
# SYSTEM_VER = 系统(内核)版本, 与内核 hepu_kernel_version 一致, 大版本才递增
# 镜像版本跟随系统版本: 镜像文件名=hepu-os-<SYSTEM_VER>.{iso,img}
# 文档版本(DESIGN.md)单独记录迭代历史, 不进镜像文件名
SYSTEM_VER=v1.02

# ===== clean 目标(V2.01新增): 清理全部构建产物与中间文件 =====
# 用法: ./build_hepu_images.sh clean
# 只删可再生的产物(build/*.bin/*.elf/*.iso/*.img/*.o 与 /tmp/hepu_boot),
# 绝不触碰源码/文档/initrd构建脚本
if [ "$1" = "clean" ]; then
    echo "=== 清理构建产物 ==="
    rm -rf /tmp/hepu_boot
    rm -f $ROOT/build_v250/*.bin $ROOT/build_v250/*.elf $ROOT/build_v250/*.iso $ROOT/build_v250/*.img $ROOT/build_v250/*.o
    echo "已清理: build/ 下 bin/elf/iso/img/o + /tmp/hepu_boot"
    exit 0
fi

mkdir -p $BLD $OUTDIR
echo "=== [0] 确认内核存在 ==="
if [ ! -f "$KERNEL" ]; then
    echo "未找到内核，先构建..."
    make >/dev/null 2>&1
fi
KSIZE=$(stat -c%s "$KERNEL")
echo "内核大小: $KSIZE 字节"
ISO_SECTORS=$(( (KSIZE + 2047) / 2048 ))
HDD_SECTORS=$(( (KSIZE + 511) / 512 ))
HYBRID_SEC=$(( (KSIZE + 511) / 512 ))
echo "ISO内核扇区: $ISO_SECTORS, HDD内核扇区: $HDD_SECTORS"
# 内核真正的入口点_start(跳过Multiboot头, 跳它否则执行垃圾数据→三重故障)
KENTRY=$(objdump -t $ROOT/build_v250/hepu-kernel.elf 2>/dev/null | awk '$5=="_start"{print "0x"$1; exit}')
if [ -z "$KENTRY" ]; then KENTRY=0x00100000; fi
echo "内核入口点: $KENTRY"

# ---------------------------------------------------------------------------
# build_variant: 构建一个变体 (正式版/诊断版)
#   $1 = 输出后缀 ("" 或 "-debug")
#   $2 = 编译调试标志 ("" 或 "-DDEBUG_BOOT")
# ---------------------------------------------------------------------------
build_variant() {
    local SUF="$1" DBG="$2"
    echo ""
    echo "========== 构建${SUF:+(诊断版)}镜像 =========="

    # 诊断版用诊断内核(HEPU_BOOT_DEBUG, 含K0-K4启动标记), 正式版用正式内核
    local VARIANT_KERNEL=$KERNEL
    local VARIANT_KELF=$ROOT/build_v250/hepu-kernel.elf
    if [ "$SUF" = "-debug" ] && [ -f "$ROOT/build_v250/hepu-kernel-debug.bin" ]; then
        VARIANT_KERNEL=$ROOT/build_v250/hepu-kernel-debug.bin
        VARIANT_KELF=$ROOT/build_v250/hepu-kernel-debug.elf
        echo "  [0] 使用诊断内核(HEPU_BOOT_DEBUG): $VARIANT_KERNEL"
    fi
    local VKSIZE=$(stat -c%s "$VARIANT_KERNEL")
    # V2.01修复: 各变体用"本变体内核"实际大小算扇区数,
    # 原用正式版KSIZE(436644→853块), 诊断内核更大(440740→861块)会被截断尾部
    local VISO_SEC=$(( (VKSIZE + 2047) / 2048 ))
    local VHDD_SEC=$(( (VKSIZE + 511) / 512 ))
    local VKENTRY=$(objdump -t $VARIANT_KELF 2>/dev/null | awk '$5=="_start"{print "0x"$1; exit}')
    if [ -z "$VKENTRY" ]; then VKENTRY=0x00101000; fi

    echo "  [1] 编译ISO变体引导器"
    gcc -m32 -DISOBoot -DKERNEL_SECTORS=$VISO_SEC $DBG \
        -DKERNEL_ENTRY=$VKENTRY -c $ROOT/tools/hepboot/hepboot.S -o $BLD/boot_iso${SUF}.o
    ld -T $ROOT/tools/hepboot/hepboot.ld -m elf_i386 $BLD/boot_iso${SUF}.o -o $BLD/boot_iso${SUF}.elf
    objcopy -O binary $BLD/boot_iso${SUF}.elf $BLD/boot_iso${SUF}.bin

    echo "  [2] 编译HDD变体引导器(stage2)"
    gcc -m32 -DKERNEL_SECTORS=$VHDD_SEC $DBG \
        -DKERNEL_ENTRY=$VKENTRY -c $ROOT/tools/hepboot/hepboot.S -o $BLD/boot_hdd${SUF}.o
    ld -T $ROOT/tools/hepboot/hepboot.ld -m elf_i386 $BLD/boot_hdd${SUF}.o -o $BLD/boot_hdd${SUF}.elf
    objcopy -O binary $BLD/boot_hdd${SUF}.elf $BLD/boot_stage2${SUF}.bin

    echo "  [3] 编译MBR引导桩"
    gcc -m32 $DBG -c $ROOT/tools/hepboot/hepboot_mbr.S -o $BLD/mbr_stub${SUF}.o
    # MBR 必须链接到 0x7C00 (BIOS加载地址), 不能用 bootloader.ld(0x8000)!
    ld -Ttext=0x7C00 -m elf_i386 $BLD/mbr_stub${SUF}.o -o $BLD/mbr_stub${SUF}.elf
    objcopy -O binary $BLD/mbr_stub${SUF}.elf $BLD/mbr_stub${SUF}.bin
    local S2SIZE=$(stat -c%s $BLD/boot_stage2${SUF}.bin)
    echo "  stage2大小: $S2SIZE (需<=4096)"
    if [ "$S2SIZE" -gt 4096 ]; then
        echo "  [错误] stage2超过2048字节限制!"; exit 1
    fi

    echo "  [4] 生成ISO (El Torito)"
    python3 $ROOT/tools/make_iso.py $BLD/boot_iso${SUF}.bin "$VARIANT_KERNEL" \
        $OUTDIR/hepu-os-${SYSTEM_VER}${SUF}.iso

    echo "  [5] 生成U盘raw镜像 (USB-HDD)"
    local IMG=$OUTDIR/hepu-os-${SYSTEM_VER}${SUF}.img
    : > "$IMG"
    dd if=$BLD/mbr_stub${SUF}.bin of="$IMG" bs=512 count=1 status=none
    dd if=$BLD/boot_stage2${SUF}.bin of="$IMG" bs=512 seek=1 status=none
    dd if="$VARIANT_KERNEL" of="$IMG" bs=512 seek=5 status=none
    # V4.73: 现场分区方案
    #   IMG只包含系统分区(2MB), swap和数据分区由系统第一次启动时自动创建
    #   这样镜像很小(2MB), 刷到U盘上后系统自己分剩余空间
    SYS_PART_SECTORS=4096
    # 不追加swap和数据分区, 镜像只有系统部分
    # MBR分区表（V4.73: 现场分区方案——只有系统分区，剩余空间系统启动时自动分）
    python3 - "$IMG" << 'PYEOF2'
import struct, sys
path = sys.argv[1]
data = bytearray(open(path, 'rb').read())
total_sectors = (len(data) + 511) // 512
# 系统分区 = 整个镜像大小（2MB左右）
sys_sectors = total_sectors

def make_chs(lba):
    end = lba - 1
    c = min(end // (63*255), 1023)
    h = (end // 63) % 255
    s = (end % 63) + 1
    return h, ((c >> 8) & 0xC0) | s, c & 0xFF

pt = bytearray(16)  # 只有1个系统分区
# 分区1: 系统分区 (活动, FAT12)
pt[0] = 0x80
pt[1:4] = b'\x00\x02\x00'
pt[4] = 0x1
h, s, c = make_chs(sys_sectors)
pt[5] = h
pt[6] = s
pt[7] = c
struct.pack_into('<I', pt, 8, 0)
struct.pack_into('<I', pt, 12, sys_sectors)
data[446:462] = pt
open(path, 'wb').write(bytes(data))
print(f"  img 现场分区方案: 系统分区={sys_sectors}扇区 = {sys_sectors*512//1024}KB")
print(f"  剩余空间留空, 系统第一次启动时自动分swap+数据分区")
PYEOF2

    echo "  [6] 生成isohybrid混合ISO"
    gcc -m32 -DKERNEL_LBA=84 -DKERNEL_SECTORS=$VHDD_SEC $DBG \
        -DKERNEL_ENTRY=$VKENTRY -c $ROOT/tools/hepboot/hepboot.S -o $BLD/boot_hybrid${SUF}.o
    ld -T $ROOT/tools/hepboot/hepboot.ld -m elf_i386 $BLD/boot_hybrid${SUF}.o -o $BLD/boot_hybrid${SUF}.elf
    objcopy -O binary $BLD/boot_hybrid${SUF}.elf $BLD/boot_hybrid${SUF}.bin
    gcc -m32 -DSTAGE2_LBA=1 -DSTAGE2_BLOCKS=8 $DBG \
        -c $ROOT/tools/hepboot/hepboot_mbr.S -o $BLD/mbr_hybrid${SUF}.o
    # MBR 必须链接到 0x7C00 (BIOS加载地址)
    ld -Ttext=0x7C00 -m elf_i386 $BLD/mbr_hybrid${SUF}.o -o $BLD/mbr_hybrid${SUF}.elf
    objcopy -O binary $BLD/mbr_hybrid${SUF}.elf $BLD/mbr_hybrid${SUF}.bin
    python3 $ROOT/tools/hepboot/make_hybrid.py $OUTDIR/hepu-os-${SYSTEM_VER}${SUF}.iso \
        $BLD/mbr_hybrid${SUF}.bin $BLD/boot_hybrid${SUF}.bin \
        $OUTDIR/hepu-os-${SYSTEM_VER}${SUF}.iso
    # V4.70: ISO保持小尺寸(只测试启动), 三分区只在IMG里做
    echo "  ISO 保持单分区(测试用), IMG三分区(正式用)"
}

# V4.79: 关掉DEBUG_BOOT, 省stage2空间给详细错误显示
# V4.99: 确认关闭DEBUG_BOOT, 给回实模式低地址桩代码腾空间
build_variant "" ""

echo ""
echo "========== 构建完成 =========="
ls -l $OUTDIR/hepu-os-${SYSTEM_VER}.iso $OUTDIR/hepu-os-${SYSTEM_VER}.img
echo ""
echo "校验: 内核Multiboot魔数应为 02b0 ad1b"
xxd -l 4 "$KERNEL"
