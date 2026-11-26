#!/bin/bash
# ============================================================================
# 32 位冒烟回归（multiboot 直启 kernel.elf）：串口六项标记 + KUnit 结果 + p3-security 判定。
#
# 两种模式（判据严格区分，SKIP 永远不算 PASS）：
#
#   带盘模式（默认）：
#     先调 make_smoke32_disk.sh 生成一张确定性 ext2 测试盘，IDE 挂盘启动。
#     p3-security 必须真实执行：套件结果为 2 passed, 0 failed, 0 skipped，
#     且日志里不允许出现 p3-security 的 [KUNIT-SKIP]；出现 SKIP 判 FAIL
#     （说明盘没挂上/后端不可用）。
#
#   无盘模式（--no-disk 或 SMOKE32_NO_DISK=1）：
#     走原无盘启动逻辑；ext2 后端不可用，p3-security 两项显式 SKIP 属预期，
#     skip_cases=2 必须打印出来；SKIP 不计 PASS，也不删/改任何断言。
#
# 只起自己的 i386 实例、按 pidfile 退出；不做全局 pkill。
#
# 用法: bash tools/regress/smoke32.sh [--no-disk|--disk] [BOOT_SEC]
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"

MODE=disk
BOOT=70
for arg in "$@"; do
  case "$arg" in
    --no-disk) MODE=nodisk ;;
    --disk)    MODE=disk ;;
    *)         BOOT="$arg" ;;
  esac
done
[ "${SMOKE32_NO_DISK:-0}" = "1" ] && MODE=nodisk

DISK="${HEPU_SMOKE32_DISK:-/tmp/hepu-smoke32-test.img}"
OUT="$EV/tests"
SER="$OUT/ser32.log"
INST32="$RUN/inst/regress32-smoke"
PID="$INST32/qemu.pid"
MON="$INST32/mon.sock"
mkdir -p "$OUT" "$INST32"
rm -f "$SER" "$PID" "$MON"

# ---- 带盘模式：先确定性造盘（每次从干净盘开始） ----
if [ "$MODE" = "disk" ]; then
  log "带盘模式：生成 ext2 测试盘 $DISK"
  bash "$REGRESS_DIR/make_smoke32_disk.sh" || { log "✗ 测试盘生成失败"; exit 1; }
  [ -f "$DISK" ] || { log "✗ 测试盘不存在: $DISK"; exit 1; }
else
  log "无盘模式：不挂 IDE 盘（p3-security 预期显式 SKIP）"
fi

# 若调用方已在外层持有 /tmp/hepu-qemu.lock（如 tools/gate.sh 整段门禁串行化），
# 内层不再重复取锁；flock 同文件两次 open 会互斥自死锁。
if [ "${HEPU_QEMU_LOCK_SKIP:-0}" != "1" ]; then
  exec {qfd}>"$QEMU_LOCK"
  flock -w 2400 "$qfd" || { log "等不到 QEMU 锁"; exit 1; }
fi

if [ -f "$PID" ]; then OLD=$(cat "$PID" 2>/dev/null); [ -n "${OLD:-}" ] && kill -9 "$OLD" 2>/dev/null; fi

DRIVE=()
[ "$MODE" = "disk" ] && DRIVE=(-drive file="$DISK",format=raw,if=ide)

log "32-bit smoke boot ${BOOT}s (i386 multiboot, mode=$MODE)"
# qemu 子进程关闭锁 fd：避免脚本被异常终止后孤儿 qemu 继续持有 /tmp/hepu-qemu.lock。
if [ "${HEPU_QEMU_LOCK_SKIP:-0}" = "1" ]; then
  qemu-system-i386 -L "$CHAT/.toolchain/pkg/datadir" \
    -kernel "$WT/build_v250/kernel.elf" -m 256 -smp 1 -display none \
    -monitor unix:$MON,server,nowait \
    "${DRIVE[@]}" \
    -serial file:$SER -no-reboot >/dev/null 2>&1 &
else
  qemu-system-i386 -L "$CHAT/.toolchain/pkg/datadir" \
    -kernel "$WT/build_v250/kernel.elf" -m 256 -smp 1 -display none \
    -monitor unix:$MON,server,nowait \
    "${DRIVE[@]}" \
    -serial file:$SER -no-reboot {qfd}>&- >/dev/null 2>&1 &
fi
echo $! > "$PID"

sleep "$BOOT"
if [ -f "$PID" ]; then P=$(cat "$PID" 2>/dev/null); [ -n "${P:-}" ] && kill -9 "$P" 2>/dev/null; fi

strings "$SER" > "$OUT/ser32.txt" 2>/dev/null || cp -f "$SER" "$OUT/ser32.txt"

have_serial=0; have_initrd=0; have_fault=0; have_net=0; have_desk=0
grep -qF '[SERIAL] COM1 16550 UART ready' "$OUT/ser32.txt" && have_serial=1
grep -qF 'head=0x55504548' "$OUT/ser32.txt" && have_initrd=1
grep -qE 'PANIC|#GP|#PF|fatal error' "$OUT/ser32.txt" && have_fault=1
grep -qF '[NET-TEST] === Self-Test Complete ===' "$OUT/ser32.txt" && have_net=1
grep -qF '[DESKTOP] elastic free desktop init done' "$OUT/ser32.txt" && have_desk=1

KTEST=$(grep -E '\[KTEST\] RESULT:' "$OUT/ser32.txt" | tail -1 | awk '{print $NF}')
FAIL_SUITES=$(grep -c 'KUNIT-FAIL-SUITE' "$OUT/ser32.txt")
FAIL_CASES=$(grep -c 'KUNIT-FAIL]' "$OUT/ser32.txt")
SKIP_CASES=$(grep -c 'KUNIT-SKIP]' "$OUT/ser32.txt")
P3_SKIP=$(grep -c 'KUNIT-SKIP] p3-security/' "$OUT/ser32.txt")
have_ktest=0
[ "${KTEST:-NONE}" = "PASS" ] && have_ktest=1

# ext2 挂载标记（带盘应 mounted；无盘应 no disk, skip）
EXT2_MOUNTED=0; EXT2_NODISK=0
grep -qF '[EXT2] selftest: mounted (ext2 fs on IDE disk)' "$OUT/ser32.txt" && EXT2_MOUNTED=1
grep -qF '[EXT2] selftest: no disk, skip' "$OUT/ser32.txt" && EXT2_NODISK=1

# 按套件归属解析 p3-security 自己的 result 行（数字为 0x 前缀十六进制）。
# 输出: P3_PASSED P3_FAILED P3_SKIPPED；套件块缺失时为 -1 -1 -1。
read -r P3_PASSED P3_FAILED P3_SKIPPED <<<"$(awk '
  BEGIN { p = -1; f = -1; s = -1 }
  /\[KUNIT\] suite: / { cur = $3 }
  /\[KUNIT\] result: / {
    if (cur == "p3-security") {
      n = 0; line = $0
      while (match(line, /0x[0-9A-Fa-f]+/)) {
        n++; v = strtonum(substr(line, RSTART, RLENGTH))
        if (n == 1) p = v; else if (n == 2) f = v; else s = v
        line = substr(line, RSTART + RLENGTH)
      }
      exit
    }
  }
  END { printf "%d %d %d\n", p, f, s }
' "$OUT/ser32.txt")"

lines=$(wc -l < "$OUT/ser32.txt")

{
  echo "mode=$MODE disk=${DISK:-<none>}"
  echo "serial=$have_serial initrd=$have_initrd net=$have_net desk=$have_desk fault=$have_fault lines=$lines"
  echo "ext2_mounted=$EXT2_MOUNTED ext2_nodisk=$EXT2_NODISK"
  echo "ktest_result=${KTEST:-NONE} ktest=$have_ktest fail_suites=$FAIL_SUITES fail_cases=$FAIL_CASES skip_cases=$SKIP_CASES p3_skip=$P3_SKIP"
  echo "p3-security: passed=${P3_PASSED} failed=${P3_FAILED} skipped=${P3_SKIPPED}"
  echo "--- p3-security cases ---"
  grep -F 'p3-security/' "$OUT/ser32.txt" || echo "(none)"
  echo "--- failed suites ---"
  grep 'KUNIT-FAIL-SUITE' "$OUT/ser32.txt" || echo "(none)"
  echo "--- failed cases ---"
  grep 'KUNIT-FAIL]' "$OUT/ser32.txt" || echo "(none)"
  echo "--- skipped cases ---"
  grep 'KUNIT-SKIP]' "$OUT/ser32.txt" || echo "(none)"
} | tee "$OUT/smoke32.txt"

# ---- 判据 ----
BASE_OK=0
if [ "$have_serial" -eq 1 ] && [ "$have_initrd" -eq 1 ] && [ "$have_net" -eq 1 ] \
   && [ "$have_desk" -eq 1 ] && [ "$have_fault" -eq 0 ] \
   && [ "$have_ktest" -eq 1 ]; then
  BASE_OK=1
fi

CASE_OK=0
if [ "$MODE" = "disk" ]; then
  # 带盘：必须真实挂上盘，p3-security 2/0/0，且不允许任何 p3 SKIP。
  if [ "$EXT2_MOUNTED" -eq 1 ] && [ "$P3_PASSED" = "2" ] && [ "$P3_FAILED" = "0" ] \
     && [ "$P3_SKIPPED" = "0" ] && [ "$P3_SKIP" -eq 0 ]; then
    CASE_OK=1
  fi
else
  # 无盘：p3-security 两项显式 SKIP 属预期；SKIP 不是 PASS。
  if [ "$EXT2_NODISK" -eq 1 ] && [ "$P3_SKIP" -eq 2 ]; then
    CASE_OK=1
  fi
fi

if [ "$BASE_OK" -eq 1 ] && [ "$CASE_OK" -eq 1 ]; then
  log "✓ 冒烟通过（mode=$MODE）"
  exit 0
else
  log "✗ 冒烟未过（base_ok=$BASE_OK case_ok=$CASE_OK mode=$MODE）"
  exit 1
fi
