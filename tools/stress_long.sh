#!/bin/bash
# stress_long.sh — P2 内存长稳压测（默认 30 分钟）
#
# 验的是什么：
#   buddy/slab/kmalloc/page-cache 在 30 分钟反复分配/释放下不触发断言、
#   不明显泄漏空闲页。不是只跑一遍 kunit stress 套件。
#
# 怎么验：
#   1) 以 P2_LONG_STRESS=<秒> 构建 32 位内核；
#   2) 造一张最小 MBR + ext2 盘；
#   3) QEMU 启动，普通 kunit 跑完后进入长稳循环，结束自动 poweroff；
#   4) 串口断言 [P2STRESS] PASS，并拒绝断言/异常/panic。
#
# 用法：bash tools/stress_long.sh [秒数]
#   例：bash tools/stress_long.sh 60     # 冒烟
#       bash tools/stress_long.sh        # 30 分钟（1800 秒）

set -u
cd "$(dirname "$0")/.." || exit 1
ROOT=$(pwd)
SECS=${1:-1800}
case "$SECS" in
    ''|*[!0-9]*) echo "秒数必须是整数"; exit 2 ;;
esac
[ "${SECS:-0}" -ge 10 ] || { echo "秒数至少 10"; exit 2; }

TC=${HEPU_TOOLCHAIN:-$(dirname "$ROOT")/.toolchain/pkg/root2}
DD=${HEPU_DATADIR:-$(dirname "$ROOT")/.toolchain/pkg/datadir}
export PATH=$TC/sbin:$TC/usr/bin:$PATH
export LD_LIBRARY_PATH=$TC/usr/lib/x86_64-linux-gnu:$TC/lib/x86_64-linux-gnu
export QEMU_MODULE_DIR=$TC/usr/lib/x86_64-linux-gnu/qemu

DISK=/tmp/p2long32disk.img
PART=/tmp/p2long32part.img
LOG=/tmp/p2_long_stress.log
ELF=build_v250/kernel.elf
START_TS=$(date +%s)

cleanup() {
    pkill -9 -f "qemu-system-i386.*$DISK" 2>/dev/null
}
trap cleanup EXIT

echo "[1/5] 构建 32 位长稳内核（P2_LONG_STRESS=$SECS）"
make clean >/dev/null 2>&1 || true
if ! make -j2 P2_LONG_STRESS=$SECS > /tmp/p2_long_build.log 2>&1; then
    echo "  构建失败，日志 /tmp/p2_long_build.log"
    tail -20 /tmp/p2_long_build.log
    exit 1
fi
[ -f "$ELF" ] || { echo "  缺少 $ELF"; exit 1; }
echo "  构建 OK"

echo "[2/5] 造盘 $DISK"
python3 - <<PY
import struct, subprocess
disk="$DISK"; part="$PART"
subprocess.run(["mke2fs","-q","-t","ext2","-b","1024","-F",part,"58368"],check=True)
with open(disk,"wb") as f: f.truncate(64*1024*1024)
mbr=bytearray(512)
start=2048; sectors=58368*1024//512
mbr[446:462]=struct.pack("<BBBBBBBBII",0x00,0xFE,0xFF,0xFF,0x83,0xFE,0xFF,0xFF,start,sectors)
mbr[510]=0x55; mbr[511]=0xAA
with open(disk,"r+b") as f:
    f.seek(0); f.write(bytes(mbr))
with open(part,"rb") as src, open(disk,"r+b") as dst:
    dst.seek(start*512); dst.write(src.read())
PY
[ -f "$DISK" ] || { echo "  造盘失败"; exit 1; }

echo "[3/5] QEMU 启动（最长 $((SECS + 180)) 秒）"
rm -f "$LOG"
timeout $((SECS + 180)) qemu-system-i386 -L "$DD" -kernel "$ELF" -m 256 \
    -display none -no-reboot \
    -drive file="$DISK",format=raw,if=ide \
    -serial file:"$LOG" >/dev/null 2>&1
QEMU_RC=$?
cleanup

echo "[4/5] 串口断言"
if [ ! -f "$LOG" ]; then
    echo "  ✗ 没有串口日志（QEMU rc=$QEMU_RC）"
    exit 1
fi

grep -a "\[P2STRESS\]" "$LOG" | tail -12 | sed 's/^/  /'

fail=0
assert_log() {
    if ! grep -aqF "$1" "$LOG"; then
        echo "  ✗ 缺少：$1"
        fail=1
    fi
}
refute_log() {
    if grep -aqF "$1" "$LOG"; then
        echo "  ✗ 出现禁止标记：$1"
        fail=1
    fi
}

SECS_HEX=$(printf '0x%08X' "$SECS")
assert_log "[P2STRESS] PASS seconds=$SECS_HEX"
assert_log "[P2STRESS] RESULT: PASS"
assert_log "[KTEST] RESULT: ALL PASS"
refute_log "[KUNIT-ASSERT]"
refute_log "[ASSERT] FAILED"
refute_log "[PANIC]"
refute_log "[EXCEPTION]"

ELAPSED=$(( $(date +%s) - START_TS ))

echo "[5/5] 结论"
if [ "$QEMU_RC" -ne 0 ] && [ "$QEMU_RC" -ne 124 ]; then
    echo "  ✗ QEMU 异常退出（rc=$QEMU_RC）"
    fail=1
fi

if [ "$fail" -eq 0 ]; then
    echo "  ✓ P2 长稳通过：${SECS}s（总耗时 ${ELAPSED}s）"
    echo "    日志：$LOG"
    RESULT=0
else
    echo "  ✗ P2 长稳失败（QEMU rc=$QEMU_RC，总耗时 ${ELAPSED}s）"
    echo "    日志：$LOG"
    RESULT=1
fi

# 恢复默认构建，避免后续门禁误用长稳内核。
echo "[cleanup] 恢复默认 32 位内核"
make clean >/dev/null 2>&1 || true
make -j2 > /tmp/p2_long_rebuild.log 2>&1 || echo "  默认重建失败，见 /tmp/p2_long_rebuild.log"

exit $RESULT
