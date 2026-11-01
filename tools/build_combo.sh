#!/bin/bash
# ============================================================================
# 赫普OS - 二合一组合镜像构建 (V3.80)
#
# 产出: build/hepu-os-<VER>-combo.img
#   单文件写入 U 盘后 = 可引导系统 + 数据分区, 一块U盘搞定
#
# 布局 (LBA, 512B/扇区):
#   LBA0      : MBR (自研 stage1 引导桩 + 分区表 + 55AA)
#   LBA1-3    : stage2 (引导器, 固定LBA读内核)
#   LBA4..    : 内核 (HDD扇区数)
#   LBA<对齐后>: 数据分区 (type 0x83, ext2 内容 = ext2disk.img)
#
# 内核挂载逻辑 (V3.80): ide_ext2_mount 解析 MBR → 找 0x83 分区 → 挂载;
#   纯 ext2 盘(无0x83)回退整盘, 兼容旧数据盘
# ============================================================================
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
BLD=/tmp/hepu_boot
KERNEL=$ROOT/build_v250/hepu-kernel.bin
DATADISK=$ROOT/build/ext2disk.img
OUT=$ROOT/build
SYSTEM_VER=v1.02

mkdir -p $BLD $OUT
if [ ! -f "$KERNEL" ]; then echo "无内核, 先 make all"; exit 1; fi
if [ ! -f "$DATADISK" ]; then echo "无数据盘 $DATADISK"; exit 1; fi

KSIZE=$(stat -c%s "$KERNEL")
HDD_SECTORS=$(( (KSIZE + 511) / 512 ))
KENTRY=$(objdump -t $ROOT/build_v250/hepu-kernel.elf 2>/dev/null | awk '$5=="_start"{print "0x"$1; exit}')
[ -z "$KENTRY" ] && KENTRY=0x00100000
echo "内核: $KSIZE 字节, $HDD_SECTORS 扇区, 入口 $KENTRY"

echo "[1] 编译 HDD 引导器 (stage2, 带引导日志 DEBUG_BOOT)"
# V3.80: 正式镜像启用 DEBUG_BOOT 引导器——
#   a) 引导日志(G0-G3/P0-P2)是正式特性: 用户要求"用显示日志代替蜂鸣器";
#   b) 实测非DEBUG stage2 在 QEMU SeaBIOS 下 INT13 栈(0x7C00向下)覆盖
#      DAP(0x7B00)导致读盘卡死(真机BIOS栈浅偶发可过, QEMU必现);
#      DEBUG 版时序(INT10清屏+延时)使该问题不触发, 全链稳定.
gcc -m32 -DDEBUG_BOOT -DKERNEL_SECTORS=$HDD_SECTORS -DKERNEL_ENTRY=$KENTRY \
    -c $ROOT/tools/bootloader.S -o $BLD/boot_hdd.o
ld -T $ROOT/tools/bootloader.ld -m elf_i386 $BLD/boot_hdd.o -o $BLD/boot_hdd.elf
objcopy -O binary $BLD/boot_hdd.elf $BLD/boot_stage2.bin
S2SIZE=$(stat -c%s $BLD/boot_stage2.bin)
if [ "$S2SIZE" -gt 2048 ]; then echo "stage2 超限 $S2SIZE"; exit 1; fi
echo "  stage2: $S2SIZE 字节"

echo "[2] 编译 MBR 引导桩 (稳定重入式, 带H1日志)"
gcc -m32 -c $ROOT/tools/mbr_stub.S -o $BLD/mbr_stub.o
ld -T $ROOT/tools/bootloader.ld -m elf_i386 $BLD/mbr_stub.o -o $BLD/mbr_stub.elf
objcopy -O binary $BLD/mbr_stub.elf $BLD/mbr_stub.bin
# 回归校验: 引导代码区(0-445, 463-511)必须与已验证的 legacy MBR 一致
python3 - "$BLD/mbr_stub.bin" "$ROOT/tools/mbr_legacy.bin" << 'PYCHECK'
import sys
a = open(sys.argv[1], 'rb').read()
b = open(sys.argv[2], 'rb').read()
if a[0:446] != b[0:446] or a[463:512] != b[463:512]:
    print("MBR 回归校验失败: 引导代码与 legacy 不一致!")
    sys.exit(1)
print("  MBR 回归校验通过 (与已验证 legacy 引导代码一致)")
PYCHECK

echo "[3] 组装组合镜像"
# 数据分区起点: 内核末尾(4+HDD_SECTORS) 对齐到 2048 扇区
KERNEL_END=$((4 + HDD_SECTORS))
DATA_START=$(( ((KERNEL_END + 2047) / 2048) * 2048 ))
DATA_SECS=$(( $(stat -c%s "$DATADISK") / 512 ))
TOTAL=$((DATA_START + DATA_SECS))
echo "  数据分区: LBA $DATA_START, $DATA_SECS 扇区; 镜像总大小 $TOTAL 扇区"

COMBINED=$OUT/hepu-os-${SYSTEM_VER}-combo.img
: > "$COMBINED"
dd if=$BLD/mbr_stub.bin of="$COMBINED" bs=512 count=1 status=none
dd if=$BLD/boot_stage2.bin of="$COMBINED" bs=512 seek=1 status=none
dd if="$KERNEL" of="$COMBINED" bs=512 seek=4 status=none
# 数据分区: 直接按块拷贝 ext2 内容到 LBA DATA_START
dd if="$DATADISK" of="$COMBINED" bs=512 seek=$DATA_START status=none
truncate -s $((TOTAL * 512)) "$COMBINED"

echo "[4] 写入 MBR 分区表 (type 0x83, LBA $DATA_START)"
python3 - "$COMBINED" $DATA_START $DATA_SECS << 'PYEOF2'
import struct, sys
path, start, secs = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
data = bytearray(open(path, 'rb').read())

def lba2chs(lba):
    c = min(lba // (255 * 63), 1023)
    h = (lba // 63) % 255
    s = (lba % 63) + 1
    return h, ((c >> 8) & 0xC0) | s, c & 0xFF

pt = bytearray(16)
pt[0] = 0x80                 # 活动
pt[1:4] = bytes(lba2chs(start))
pt[4] = 0x83                 # Linux ext2
pt[5:8] = bytes(lba2chs(start + secs - 1))
struct.pack_into('<I', pt, 8, start)
struct.pack_into('<I', pt, 12, secs)
data[446:462] = pt
data[510] = 0x55; data[511] = 0xAA
open(path, 'wb').write(bytes(data))
print(f"  分区表: start={start} secs={secs} type=0x83")
PYEOF2

echo "[5] 校验"
xxd "$COMBINED" | tail -1
echo "========== 组合镜像完成: $COMBINED ($(stat -c%s $COMBINED) 字节) =========="
