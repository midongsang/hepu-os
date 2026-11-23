#!/bin/bash
# gate.sh — P0 门禁：构建 + VFS 跨启动 + kunit 连续稳定性
#
# 判定口径（全过才算 PASS）：
#   1) 32/64 位构建 0 error
#   2) tools/verify_p1p3.sh 退出码 0（写阶段 PASS + 读阶段 PASS）
#   3) 32 位连续启动 3 次，每次日志都有 "[KTEST] RESULT: ALL PASS"
#      且没有 "[KTEST] RESULT: FAIL" / "[KUNIT-FAIL-SUITE]"
#
# 用法：bash tools/gate.sh
# 输出最后一行固定为 "GATE: PASS" 或 "GATE: FAIL"，退出码 0/1。

set -u
cd "$(dirname "$0")/.." || exit 1
ROOT=$(pwd)

TC=${HEPU_TOOLCHAIN:-$(dirname "$ROOT")/.toolchain/pkg/root2}
DD=${HEPU_DATADIR:-$(dirname "$ROOT")/.toolchain/pkg/datadir}
export PATH=$TC/sbin:$TC/usr/bin:$PATH
export LD_LIBRARY_PATH=$TC/usr/lib/x86_64-linux-gnu:$TC/lib/x86_64-linux-gnu
export QEMU_MODULE_DIR=$TC/usr/lib/x86_64-linux-gnu/qemu

ELF=build_v250/kernel.elf
DISK=/tmp/gate32disk.img
GATE_LOG_DIR=/tmp/hepu_gate
rm -rf "$GATE_LOG_DIR"; mkdir -p "$GATE_LOG_DIR"

FAILED=0
fail() { echo "  ✗ $*"; FAILED=1; }

cleanup() { pkill -9 -f "qemu-system-i386.*$DISK" 2>/dev/null; }
trap cleanup EXIT

# ---- 1. 构建 ----
echo "[1/5] 构建 32 位"
flock /tmp/hepu-build.lock -c 'make -j2' >"$GATE_LOG_DIR/build32.log" 2>&1
RC=$?
if [ $RC -eq 0 ] && ! grep -q "error:" "$GATE_LOG_DIR/build32.log"; then
    echo "  ✓ 32 位构建 0 error"
else
    fail "32 位构建失败（rc=$RC，日志 $GATE_LOG_DIR/build32.log）"
fi

echo "[2/5] 构建 64 位"
flock /tmp/hepu-build.lock -c 'make -C kernel/arch/x86_64 -j2' >"$GATE_LOG_DIR/build64.log" 2>&1
RC=$?
if [ $RC -eq 0 ] && ! grep -q "error:" "$GATE_LOG_DIR/build64.log"; then
    echo "  ✓ 64 位构建 0 error"
else
    fail "64 位构建失败（rc=$RC，日志 $GATE_LOG_DIR/build64.log）"
fi

[ -f "$ELF" ] || { fail "缺少 $ELF"; echo "GATE: FAIL"; exit 1; }

# ---- 3. VFS 跨启动 ----
echo "[3/5] VFS 跨启动（verify_p1p3.sh）"
bash tools/verify_p1p3.sh >"$GATE_LOG_DIR/verify.log" 2>&1
RC=$?
if [ $RC -eq 0 ] && grep -q "VFS-TEST. RESULT: PASS" "$GATE_LOG_DIR/verify.log"; then
    echo "  ✓ 写阶段 + 读阶段 PASS"
else
    fail "VFS 验证失败（rc=$RC，日志 $GATE_LOG_DIR/verify.log）"
fi

# 复用 verify 产出的 64MB ext2 盘；没有就补一张
if [ ! -f /tmp/vp32disk.img ]; then
    python3 - <<'PY'
import struct, subprocess
disk="/tmp/gate32disk.img"; part="/tmp/gate32part.img"
subprocess.run(["mke2fs","-q","-t","ext2","-b","1024","-F",part,"58368"],check=True)
with open(disk,"wb") as f: f.truncate(64*1024*1024)
mbr=bytearray(512)
start=2048; sectors=58368*1024//512
mbr[446:462]=struct.pack("<BBBBBBBBII",0,0xFE,0xFF,0xFF,0x83,0xFE,0xFF,0xFF,start,sectors)
mbr[510]=0x55; mbr[511]=0xAA
with open(disk,"r+b") as f: f.seek(0); f.write(bytes(mbr))
with open(part,"rb") as src, open(disk,"r+b") as dst:
    dst.seek(start*512); dst.write(src.read())
PY
    [ -f "$DISK" ] || cp /tmp/vp32disk.img "$DISK"
else
    cp /tmp/vp32disk.img "$DISK"
fi

# ---- 4. kunit 连续 3 次 ----
echo "[4/5] kunit 连续 3 次启动"
PASS_RUNS=0
for i in 1 2 3; do
    LOG="$GATE_LOG_DIR/kunit_$i.log"
    rm -f "$LOG"
    pkill -9 -f qemu-system-i386 2>/dev/null; sleep 1
    timeout 70 qemu-system-i386 -L "$DD" -kernel "$ELF" -m 256 -display none \
        -drive file="$DISK",format=raw,if=ide -serial file:"$LOG" >/dev/null 2>&1
    if grep -aFq "[KTEST] RESULT: ALL PASS" "$LOG" \
       && ! grep -aFq "[KUNIT-FAIL-SUITE]" "$LOG"; then
        echo "  ✓ 第 $i 次：ALL PASS"
        PASS_RUNS=$((PASS_RUNS + 1))
    else
        echo "  ✗ 第 $i 次：未全过"
        grep -aF -e "[KUNIT-FAIL-SUITE]" -e "[KTEST] RESULT" "$LOG" | sed 's/^/      /'
    fi
done
[ "$PASS_RUNS" -eq 3 ] || fail "kunit 连续通过 $PASS_RUNS/3"

# ---- 5. 结论 ----
echo "[5/5] 结论"
if [ "$FAILED" -eq 0 ]; then
    echo "GATE: PASS"
    exit 0
else
    echo "GATE: FAIL"
    exit 1
fi
