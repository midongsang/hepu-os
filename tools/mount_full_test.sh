#!/bin/bash
# mount_full_test.sh — P1 `fs mount -f` shell 分支端到端
# 造干净盘 → 注入 /mountfull_test='f' → 启动 → 抓 [MOUNTFULL] RESULT。
set -u
cd "$(dirname "$0")/.." || exit 1
ROOT=$(pwd)
TC=${HEPU_TOOLCHAIN:-$(dirname "$ROOT")/.toolchain/pkg/root2}
DD=${HEPU_DATADIR:-$(dirname "$ROOT")/.toolchain/pkg/datadir}
export PATH=$TC/sbin:$TC/usr/bin:$PATH
export LD_LIBRARY_PATH=$TC/usr/lib/x86_64-linux-gnu:$TC/lib/x86_64-linux-gnu
export QEMU_MODULE_DIR=$TC/usr/lib/x86_64-linux-gnu/qemu

ELF=build_v250/kernel.elf
DISK=/tmp/mountfull.img
PART=/tmp/mountfull_part.img
LOG=/tmp/mountfull.log
[ -f "$ELF" ] || { echo "缺少 $ELF，先 make"; echo "MOUNT-FULL: FAIL"; exit 1; }

echo "[1/3] 造干净 ext2 盘"
rm -f "$DISK" "$PART" "$LOG"
python3 - <<PY
import struct, subprocess
part="$PART"; disk="$DISK"
subprocess.run(["mke2fs","-q","-t","ext2","-b","1024","-F",part,"58368"],check=True)
open(disk,"wb").truncate(64*1024*1024)
mbr=bytearray(512); start=2048; sectors=58368*1024//512
mbr[446:462]=struct.pack("<BBBBBBBBII",0,0xFE,0xFF,0xFF,0x83,0xFE,0xFF,0xFF,start,sectors)
mbr[510]=0x55; mbr[511]=0xAA
open(disk,"r+b").write(mbr)
with open(part,"rb") as s, open(disk,"r+b") as d:
    d.seek(start*512); d.write(s.read())
PY

echo "[2/3] 注入 /mountfull_test='f' 并启动"
printf 'f' > /tmp/mountfull_marker
debugfs -w -R "write /tmp/mountfull_marker /mountfull_test" "$PART" >/dev/null 2>&1
dd if="$PART" of="$DISK" bs=512 seek=2048 conv=notrunc status=none
pkill -9 -f qemu-system-i386 2>/dev/null; sleep 1
timeout 90 qemu-system-i386 -L "$DD" -kernel "$ELF" -m 256 -display none \
    -drive file="$DISK",format=raw,if=ide -serial file:"$LOG" >/dev/null 2>&1

echo "[3/3] 判定"
grep -a "MOUNTFULL" "$LOG" | sed 's/^/  /'
if grep -aq "MOUNTFULL.*RESULT: PASS" "$LOG"; then
    echo "  ✓ fs mount -f 两次调用幂等，根节点子项数稳定"
    echo "MOUNT-FULL: PASS"
    exit 0
fi
echo "  ✗ 未看到 MOUNTFULL PASS"
echo "MOUNT-FULL: FAIL"
exit 1
