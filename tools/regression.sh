#!/bin/bash
# ============================================================================
# 赫普OS 自动回归脚本 (E-43)
# 用法: bash tools/regression.sh [qemu_boot_seconds]
# 流程: 启动 QEMU -> 等 shell -> 跑 ktest(全部 KUnit+压力) -> schtest(调度压力)
#       -> 检查 QEMU 存活/canary -> 输出回归结论
# 依赖: python3 + QEMU 环境(与开发环境一致)
# ============================================================================
set -u
cd "$(dirname "$0")/.."
BOOT_SECONDS="${1:-188}"
PASS=0; FAIL=0

log() { echo "[REGR] $*"; }
fail() { log "FAIL: $*"; FAIL=$((FAIL+1)); }
ok()   { log "ok: $*"; PASS=$((PASS+1)); }

# 0) 清场
pkill -f "[q]emu-system" 2>/dev/null; sleep 1
rm -f /tmp/hmon.sock /tmp/hserial.log

# 1) 启动 QEMU
export LD_LIBRARY_PATH=$PWD/tools/qemu/x/usr/lib/x86_64-linux-gnu:$PWD/tools/qemu/x/usr/lib:$PWD/tools/qemu/x/lib/x86_64-linux-gnu
setsid $PWD/tools/qemu/x/usr/bin/qemu-system-x86_64 \
  -L $PWD/tools/qemu/x/usr/share/qemu -m 256M -nic none -vga std \
  -display none -kernel build_v250/kernel.elf -hda build/ext2disk.img \
  -monitor unix:/tmp/hmon.sock,server,nowait -serial file:/tmp/hserial.log \
  >/tmp/qemu_out.log 2>&1 < /dev/null &
log "QEMU started, waiting ${BOOT_SECONDS}s for shell..."
sleep "$BOOT_SECONDS"
if ! pgrep -f "[q]emu-system" >/dev/null; then fail "QEMU died before shell"; exit 1; fi
ok "QEMU alive after boot"

# 2) 交互 helper (python)
python3 - <<'PYEOF'
import socket, time
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect('/tmp/hmon.sock')
def sk(k, d=0.09):
    s.sendall(f'sendkey {k}\n'.encode()); time.sleep(d)
def line(txt):
    for ch in txt:
        sk('spc' if ch == ' ' else ch)
    sk('ret', 0.6)
line('ktest'); time.sleep(8)
line('schtest'); time.sleep(17)
line('ps'); time.sleep(1)
line('canary'); time.sleep(1.5)
s.close()
PYEOF

# 3) 屏幕断言
SCR=$(python3 tools/vgaread.py 2>/dev/null)
echo "$SCR" | grep -q "ISR 6" && fail "ISR 6 (invalid opcode) during regression" || ok "no ISR 6"
echo "$SCR" | grep -q "halted" && fail "kernel halted" || ok "no halt"
echo "$SCR" | grep -q "ALL PASS" && ok "ktest all pass" || fail "ktest did not report ALL PASS"
echo "$SCR" | grep -q "1 ok, 0 overflow" && ok "canary ok" || fail "canary not ok"
echo "$SCR" | grep -q "kmalloc_stress" && ok "stress suite ran" || fail "stress suite not shown"

# 4) 收尾
pkill -f "[q]emu-system" 2>/dev/null
log "=========== REGRESSION RESULT: PASS=$PASS FAIL=$FAIL ==========="
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
