#!/bin/bash
# D3 真掉电基线：cache=unsafe + 写入中 kill QEMU + 宿主机 e2fsck 判定
# 这不是回归门禁，是“测量脚本”：输出人可读的损伤报告，永远退出 0（除非环境坏了）。
set -u
PROJECT=$(cd "$(dirname "$0")/.." && pwd)
T=/home/user/Doubao/chats/38452875115179010/.toolchain/pkg/root2
DD=/home/user/Doubao/chats/38452875115179010/.toolchain/pkg/datadir
export PATH=$T/sbin:$T/usr/bin:/usr/sbin:$PATH
export LD_LIBRARY_PATH=$T/usr/lib/x86_64-linux-gnu:$T/lib/x86_64-linux-gnu
export QEMU_MODULE_DIR=$T/usr/lib/x86_64-linux-gnu/qemu

IMG=/tmp/pl_test.img
PART=/tmp/pl_part.img
PARTFIX=/tmp/pl_part_fix.img
LOG=/tmp/pl_serial.log
BOOT_SECS=${BOOT_SECS:-45}

say() { echo "[PL] $*"; }

kill_qemu() {
    ps -eo pid,comm | awk '$2 ~ /qemu-system/ {print $1}' | while read -r p; do
        kill -9 "$p" 2>/dev/null
    done
    sleep 1
}

part_out() { dd if="$IMG" of="$PART" bs=512 skip=13000 count=458752 status=none; }
part_in()  { dd if="$PART" of="$IMG" bs=512 seek=13000 conv=notrunc status=none; }

# 1. 构建 32 位
say "build 32-bit"
flock /tmp/hepu-build.lock -c "cd '$PROJECT' && make -j2" > /tmp/pl_build32.log 2>&1 || {
    say "BUILD FAIL"; tail -5 /tmp/pl_build32.log; exit 1;
}

# 2. 干净镜像 + 标记
say "make fresh image"
bash "$PROJECT/tools/make_single_img64.sh" > /tmp/pl_img.log 2>&1 || {
    say "IMAGE FAIL"; tail -5 /tmp/pl_img.log; exit 1;
}
cp --sparse=always /tmp/hepu64_single.img "$IMG"
part_out
printf 'p' > /tmp/pl_marker
debugfs -w -R "write /tmp/pl_marker /powerloss_test" "$PART" >/dev/null 2>&1
part_in

# 3. 写入中掉电
kill_qemu
rm -f "$LOG"
say "boot with cache=unsafe; kill 2s after first chunk"
qemu-system-i386 -L "$DD" -kernel "$PROJECT/build_v250/kernel.elf" \
    -m 256 -display none -drive file="$IMG",format=raw,if=ide,cache=unsafe \
    -serial file:"$LOG" >/dev/null 2>&1 &
QPID=$!
for _ in $(seq 1 "${BOOT_SECS}"); do
    grep -aqF "[POWERLOSS] chunk=" "$LOG" 2>/dev/null && { sleep 2; break; }
    sleep 1
done
kill -9 "$QPID" 2>/dev/null
kill_qemu

STARTED=0; CHUNKS=0; DONE=0
grep -aqF "[POWERLOSS] start" "$LOG" && STARTED=1
CHUNKS=$(grep -aF "[POWERLOSS] chunk=" "$LOG" | tail -1 | sed 's/.*chunk=//' | tr -dc '0-9')
CHUNKS=${CHUNKS:-0}
grep -aqF "[POWERLOSS] done" "$LOG" && DONE=1
say "guest_started=$STARTED chunks_written=$CHUNKS workload_finished=$DONE"

if [ "$STARTED" -eq 0 ] || [ "$DONE" -eq 1 ]; then
    say "RESULT: INCONCLUSIVE（负载没跑起来或已自然结束，不是掉电样本）"
    exit 0
fi

# 4. 宿主机判定
part_out
cp --sparse=always "$PART" "$PARTFIX"
say "e2fsck read-only on crashed image"
e2fsck -f -n "$PART" > /tmp/pl_fsck_ro.txt 2>&1
RO=$?
say "e2fsck -n exit=$RO"
grep -aE "^(Pass|Free blocks|Free inodes|Block bitmap|Inode bitmap|Directory inode|Inode table|.*difference)" /tmp/pl_fsck_ro.txt | tail -12

say "e2fsck repair dry-run (on copy, -y)"
e2fsck -f -y "$PARTFIX" > /tmp/pl_fsck_fix.txt 2>&1
FIX=$?
say "e2fsck -y exit=$FIX (0=nothing to fix, 1=fixed, 4=unfixed, 8=operational error)"

say "what was wrong / what it fixed:"
grep -aE "Fix|cleared|reconnect|Multiply|Salvage|Free blocks|Inode|Directory" /tmp/pl_fsck_fix.txt | head -20

say "pl.dat present in repaired image:"
debugfs -R "ls -l /" "$PARTFIX" 2>/dev/null | grep -a "pl.dat" || say "  (not found)"

say "RESULT: SAMPLE_CAPTURED（损伤详情见 /tmp/pl_fsck_ro.txt 与 /tmp/pl_fsck_fix.txt）"
exit 0
