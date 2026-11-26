#!/bin/bash
# gate.sh — P0/P3 门禁：构建 + VFS 跨启动 + 带盘冒烟(p3-security 真实 2/2) + kunit 连续稳定性
#
# 判定口径（全过才算 PASS）：
#   1) 32/64 位构建 0 error
#   2) tools/verify_p1p3.sh 退出码 0（写阶段 PASS + 读阶段 PASS）
#   3) 带盘 32 位冒烟：p3-security 真实执行 2 passed, 0 failed, 0 skipped（独立判据，
#      带盘出现 p3-security SKIP 直接 FAIL；SKIP 不算 PASS）
#   4) 32 位连续启动 3 次，每次日志都有 "[KTEST] RESULT: ALL PASS"
#      且没有 "[KTEST] RESULT: FAIL" / "[KUNIT-FAIL-SUITE]"；日志同时记录
#      p3-security 是真实 PASS 还是 SKIP，便于排查盘是否挂上。
#
# 并行安全：
#   - 整段门禁在 /tmp/hepu-qemu.lock 外层串行化（本脚本开头自举 flock），
#     内层脚本不再重复取锁（HEPU_QEMU_LOCK_SKIP=1），避免同机并行任务被误杀。
#   - 每个 QEMU 实例只按自己的 pidfile 精确回收，不做全局 pkill。
#
# 用法：bash tools/gate.sh
# 输出最后一行固定为 "GATE: PASS" 或 "GATE: FAIL"，退出码 0/1。

set -u
SELF="$(cd "$(dirname "$0")" && pwd)/$(basename "$0")"
cd "$(dirname "$0")/.." || exit 1
ROOT=$(pwd)

# ---- 外层 QEMU 串行化：整段门禁包进 /tmp/hepu-qemu.lock，内层不再取锁 ----
if [ "${HEPU_QEMU_LOCK_SKIP:-0}" != "1" ]; then
  export HEPU_QEMU_LOCK_SKIP=1
  exec flock /tmp/hepu-qemu.lock bash "$SELF" "$@"
fi

TC=${HEPU_TOOLCHAIN:-$(dirname "$ROOT")/.toolchain/pkg/root2}
DD=${HEPU_DATADIR:-$(dirname "$ROOT")/.toolchain/pkg/datadir}
export PATH=$TC/sbin:$TC/usr/bin:$PATH
export LD_LIBRARY_PATH=$TC/usr/lib/x86_64-linux-gnu:$TC/lib/x86_64-linux-gnu
export QEMU_MODULE_DIR=$TC/usr/lib/x86_64-linux-gnu/qemu

ELF=build_v250/kernel.elf
DISK=/tmp/gate32disk.img
PIDFILE=/tmp/gate32-qemu.pid
GATE_LOG_DIR=/tmp/hepu_gate
rm -rf "$GATE_LOG_DIR"; mkdir -p "$GATE_LOG_DIR"

FAILED=0
fail() { echo "  ✗ $*"; FAILED=1; }

cleanup() {
  if [ -f "$PIDFILE" ]; then
    P=$(cat "$PIDFILE" 2>/dev/null)
    [ -n "${P:-}" ] && kill -9 "$P" 2>/dev/null
  fi
  rm -f "$PIDFILE"
}
trap cleanup EXIT

# ---- 1. 构建 ----
echo "[1/6] 构建 32 位"
flock /tmp/hepu-build.lock -c 'make -j2' >"$GATE_LOG_DIR/build32.log" 2>&1
RC=$?
if [ $RC -eq 0 ] && ! grep -q "error:" "$GATE_LOG_DIR/build32.log"; then
    echo "  ✓ 32 位构建 0 error"
else
    fail "32 位构建失败（rc=$RC，日志 $GATE_LOG_DIR/build32.log）"
fi

echo "[2/6] 构建 64 位"
flock /tmp/hepu-build.lock -c 'make -C kernel/arch/x86_64 -j2' >"$GATE_LOG_DIR/build64.log" 2>&1
RC=$?
if [ $RC -eq 0 ] && ! grep -q "error:" "$GATE_LOG_DIR/build64.log"; then
    echo "  ✓ 64 位构建 0 error"
else
    fail "64 位构建失败（rc=$RC，日志 $GATE_LOG_DIR/build64.log）"
fi

[ -f "$ELF" ] || { fail "缺少 $ELF"; echo "GATE: FAIL"; exit 1; }

# ---- 2. VFS 跨启动 ----
echo "[3/6] VFS 跨启动（verify_p1p3.sh）"
bash tools/verify_p1p3.sh >"$GATE_LOG_DIR/verify.log" 2>&1
RC=$?
if [ $RC -eq 0 ] && grep -q "VFS-TEST. RESULT: PASS" "$GATE_LOG_DIR/verify.log"; then
    echo "  ✓ 写阶段 + 读阶段 PASS"
else
    fail "VFS 验证失败（rc=$RC，日志 $GATE_LOG_DIR/verify.log）"
fi

# ---- 3. 带盘 32 位冒烟：p3-security 真实 2/2/0（独立判据）----
echo "[4/6] 带盘冒烟（p3-security 必须 2 passed, 0 failed, 0 skipped）"
SMOKE_EV="$GATE_LOG_DIR/smoke32"
rm -rf "$SMOKE_EV"; mkdir -p "$SMOKE_EV"
HEPU_EV="$SMOKE_EV" HEPU_SMOKE32_DISK="$DISK" \
  bash tools/regress/smoke32.sh --disk 70 >"$GATE_LOG_DIR/smoke32.log" 2>&1
RC=$?
SMOKE_SUMMARY="$SMOKE_EV/tests/smoke32.txt"
if [ $RC -eq 0 ] && [ -f "$SMOKE_SUMMARY" ] \
   && grep -qF "p3-security: passed=2 failed=0 skipped=0" "$SMOKE_SUMMARY" \
   && ! grep -qF "KUNIT-SKIP] p3-security/" "$SMOKE_SUMMARY"; then
    echo "  ✓ p3-security 带盘真实 2/2/0"
    grep -F "p3-security:" "$SMOKE_SUMMARY" | sed 's/^/    /'
else
    fail "带盘冒烟未达 p3-security 2/2/0（rc=$RC，日志 $GATE_LOG_DIR/smoke32.log）"
    grep -aF -e "p3-security" -e "KUNIT-SKIP" "$GATE_LOG_DIR/smoke32.log" | sed 's/^/    /'
fi

# ---- 4. kunit 连续 3 次启动（挂确定性测试盘，日志记录 p3 SKIP/PASS 状态）----
echo "[5/6] kunit 连续 3 次启动（挂盘）"
# 3 次稳定性启动也用干净的确定性 ext2 盘，不依赖 verify 残留镜像。
HEPU_SMOKE32_DISK="$DISK" bash tools/regress/make_smoke32_disk.sh >"$GATE_LOG_DIR/make_disk.log" 2>&1 \
  || fail "测试盘生成失败（日志 $GATE_LOG_DIR/make_disk.log）"

PASS_RUNS=0
for i in 1 2 3; do
    LOG="$GATE_LOG_DIR/kunit_$i.log"
    rm -f "$LOG" "$PIDFILE"
    timeout 70 qemu-system-i386 -L "$DD" -kernel "$ELF" -m 256 -display none \
        -drive file="$DISK",format=raw,if=ide -pidfile "$PIDFILE" \
        -serial file:"$LOG" >/dev/null 2>&1 &
    QPID=$!
    wait "$QPID"

    P3_SKIP=$(grep -ac 'KUNIT-SKIP] p3-security/' "$LOG")
    # 只取 p3-security 套件块自己的 result 行（数字为 0x 前缀十六进制）。
    P3_RESULT=$(awk '
      /\[KUNIT\] suite: / { cur=$3 }
      /\[KUNIT\] result: / {
        if (cur == "p3-security") { print; exit }
      }' "$LOG")
    if grep -aFq "[KTEST] RESULT: ALL PASS" "$LOG" \
       && ! grep -aFq "[KUNIT-FAIL-SUITE]" "$LOG"; then
        echo "  ✓ 第 $i 次：ALL PASS（p3_skip=$P3_SKIP）"
        [ -n "$P3_RESULT" ] && echo "      $P3_RESULT"
        PASS_RUNS=$((PASS_RUNS + 1))
    else
        echo "  ✗ 第 $i 次：未全过（p3_skip=$P3_SKIP）"
        grep -aF -e "[KUNIT-FAIL-SUITE]" -e "[KTEST] RESULT" "$LOG" | sed 's/^/      /'
    fi
done
[ "$PASS_RUNS" -eq 3 ] || fail "kunit 连续通过 $PASS_RUNS/3"

# ---- 5. 结论 ----
echo "[6/6] 结论"
if [ "$FAILED" -eq 0 ]; then
    echo "GATE: PASS"
    exit 0
else
    echo "GATE: FAIL"
    exit 1
fi
