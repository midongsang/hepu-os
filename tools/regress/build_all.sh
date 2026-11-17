#!/bin/bash
# ============================================================================
# 串行构建：gnu-efi -> UEFI 引导器 -> 64 位内核 -> 32 位内核 -> 双内核镜像 -> 注入配置
# 全程在 /tmp/hepu-build.lock 内串行（2 核、无 KVM，编译也不并行抢核）。
#
# 用法: bash tools/regress/build_all.sh
# 产物: $EV/build/hepu.img（含 /system.conf /accounts.conf /display.conf）
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"

exec {blfd}>"$BUILD_LOCK"
flock -w 900 "$blfd" || { log "等不到构建锁"; exit 1; }

cd "$WT"

echo "==== [1/5] gnu-efi ===="
make -C tools/gnu-efi -j2 > "$EV/build/gnu-efi.log" 2>&1 || { echo "GNUEFI FAIL"; tail -20 "$EV/build/gnu-efi.log"; exit 2; }

echo "==== [2/5] UEFI 引导器 ===="
make -C tools/hepboot/uefi -B -j2 > "$EV/build/uefi.log" 2>&1 || { echo "UEFI FAIL"; tail -20 "$EV/build/uefi.log"; exit 2; }

echo "==== [3/5] 64 位内核 ===="
make -C kernel/arch/x86_64 -j2 > "$EV/build/build64.log" 2>&1 || { echo "BUILD64 FAIL"; tail -30 "$EV/build/build64.log"; exit 2; }

echo "==== [4/5] 32 位内核 ===="
make -j2 > "$EV/build/build32.log" 2>&1 || { echo "BUILD32 FAIL"; tail -30 "$EV/build/build32.log"; exit 2; }

echo "==== [5/5] 双内核镜像 + 注入配置 ===="
HEPU_IMG="$IMG" python3 tools/build_uefi_dual.py > "$EV/build/build-image.log" 2>&1 || { echo "IMAGE FAIL"; tail -30 "$EV/build/build-image.log"; exit 2; }

# ---- ext2 根分区覆盖配置（LBA 264192, 120MB = 204800 扇区）----
EXT="$EV/build/_r.ext2"
HASH=$(printf 'hepu123' | sha256sum | awk '{print $1}')
# 注意：默认保持开机锁屏（不写 lock=0），链路取证需要先到锁屏页。
printf 'oobe_done=1\nlang=0\n' > "$EV/build/_sys.conf"
printf 'user=dev\nrole=2\nhash=%s\nadminhash=%s\n' "$HASH" "$HASH" > "$EV/build/_acc.conf"
printf 'scale=150\nresolution=1920x1080\nshadow=1\ncursor=0\n' > "$EV/build/_disp.conf"

dd if="$IMG" of="$EXT" bs=512 skip=264192 count=204800 status=none
for f in _sys.conf:/system.conf _acc.conf:/accounts.conf _disp.conf:/display.conf; do
  src=${f%%:*}; dst=${f##*:}
  debugfs -w -R "rm $dst" "$EXT" >/dev/null 2>&1 || true
  debugfs -w -R "write $EV/build/$src $dst" "$EXT" >/dev/null 2>&1
done
dd if="$EXT" of="$IMG" bs=512 seek=264192 conv=notrunc status=none
rm -f "$EXT"

# ---- 产物大小记录 ----
{
  echo "IMG=$IMG"
  ls -l "$WT/build_x86_64/hepu-kernel64.bin" 2>/dev/null | awk '{print "kernel64.bin", $5}'
  ls -l "$WT/build_v250/hepu-kernel.bin" 2>/dev/null | awk '{print "kernel32.bin", $5}'
  ls -l "$WT/build_v250/kernel.elf" 2>/dev/null | awk '{print "kernel32.elf", $5}'
  ls -l "$WT/tools/hepboot/uefi/hepboot_uefi.efi" 2>/dev/null | awk '{print "bootx64.efi", $5}'
  ls -l "$IMG" | awk '{print "image", $5}'
} | tee "$EV/build/size.txt"

echo "BUILD DONE"
