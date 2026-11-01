#!/bin/bash
# ============================================================================
# 赫普OS - 引导器构建脚本
# 生成: boot_iso.bin (El Torito引导映像) / mbr_stub.bin (512B MBR)
#        boot_stage2.bin (HDD引导器主体)
# ============================================================================
set -e
cd "$(dirname "$0")"
BLD=/tmp/hepu_boot
mkdir -p $BLD

echo "[1/3] 编译 ISO 变体引导器..."
gcc -m32 -DISOBoot -c bootloader.S -o $BLD/boot_iso.o
ld -T bootloader.ld -m elf_i386 $BLD/boot_iso.o -o $BLD/boot_iso.elf
objcopy -O binary $BLD/boot_iso.elf $BLD/boot_iso.bin

echo "[2/3] 编译 HDD 变体引导器(stage2)..."
gcc -m32 -c bootloader.S -o $BLD/boot_hdd.o
ld -T bootloader.ld -m elf_i386 $BLD/boot_hdd.o -o $BLD/boot_hdd.elf
objcopy -O binary $BLD/boot_hdd.elf $BLD/boot_stage2.bin

echo "[3/3] 编译 MBR 引导桩..."
gcc -m32 -c mbr_stub.S -o $BLD/mbr_stub.o
ld -T bootloader.ld -m elf_i386 $BLD/mbr_stub.o -o $BLD/mbr_stub.elf
objcopy -O binary $BLD/mbr_stub.elf $BLD/mbr_stub.bin

echo ""
echo "产物:"
ls -l $BLD/boot_iso.bin $BLD/boot_stage2.bin $BLD/mbr_stub.bin
echo ""
echo "MBR大小: $(stat -c%s $BLD/mbr_stub.bin) (应为512)"
echo "stage2大小: $(stat -c%s $BLD/boot_stage2.bin)"
echo "ISO引导大小: $(stat -c%s $BLD/boot_iso.bin)"
