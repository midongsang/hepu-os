#!/bin/bash
# ============================================================================
# 32 位冒烟回归（multiboot 直启 kernel.elf）：串口六项标记 + KUnit 结果。
# 只起自己的 i386 实例、按 pidfile 退出；不做全局 pkill。
# 用法: bash tools/regress/smoke32.sh [BOOT_SEC]
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"

BOOT=${1:-70}
OUT="$EV/tests"
SER="$OUT/ser32.log"
INST32="$RUN/inst/regress32-smoke"
PID="$INST32/qemu.pid"
MON="$INST32/mon.sock"
mkdir -p "$OUT" "$INST32"
rm -f "$SER" "$PID" "$MON"

exec {qfd}>"$QEMU_LOCK"
flock -w 2400 "$qfd" || { log "等不到 QEMU 锁"; exit 1; }

if [ -f "$PID" ]; then OLD=$(cat "$PID" 2>/dev/null); [ -n "${OLD:-}" ] && kill -9 "$OLD" 2>/dev/null; fi

log "32-bit smoke boot ${BOOT}s (i386 multiboot)"
qemu-system-i386 -L "$CHAT/.toolchain/pkg/datadir" \
  -kernel "$WT/build_v250/kernel.elf" -m 256 -smp 1 -display none \
  -monitor unix:$MON,server,nowait \
  -serial file:$SER -no-reboot >/dev/null 2>&1 &
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
have_ktest=0
[ "${KTEST:-NONE}" = "PASS" ] && have_ktest=1
lines=$(wc -l < "$OUT/ser32.txt")

{
  echo "serial=$have_serial initrd=$have_initrd net=$have_net desk=$have_desk fault=$have_fault lines=$lines"
  echo "ktest_result=${KTEST:-NONE} ktest=$have_ktest fail_suites=$FAIL_SUITES fail_cases=$FAIL_CASES skip_cases=$SKIP_CASES"
  echo "--- failed suites ---"
  grep 'KUNIT-FAIL-SUITE' "$OUT/ser32.txt" || echo "(none)"
  echo "--- failed cases ---"
  grep 'KUNIT-FAIL]' "$OUT/ser32.txt" || echo "(none)"
  echo "--- skipped cases ---"
  grep 'KUNIT-SKIP]' "$OUT/ser32.txt" || echo "(none)"
} | tee "$OUT/smoke32.txt"

# 六项全过、无 fault 且 KTEST 为 PASS 才算冒烟 PASS
if [ "$have_serial" -eq 1 ] && [ "$have_initrd" -eq 1 ] && [ "$have_net" -eq 1 ] \
   && [ "$have_desk" -eq 1 ] && [ "$have_fault" -eq 0 ] \
   && [ "$have_ktest" -eq 1 ]; then
  exit 0
else
  exit 1
fi
