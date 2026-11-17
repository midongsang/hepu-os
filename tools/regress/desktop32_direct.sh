#!/bin/bash
# ============================================================================
# 32 位 multiboot 直启图形桌面补充取证：绕过 UEFI 引导菜单，避免崩溃重启后
# 画面回灌菜单污染桌面帧。仅用于复核 32 位桌面 NONBLACK；菜单链路仍以 boot32.sh 为准。
# 用法: bash tools/regress/desktop32_direct.sh
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"

OUT="$EV/frames32-direct"
SER="$OUT/ser.log"
INST32="$RUN/inst/regress32-direct"
MON="$INST32/mon.sock"
PID="$INST32/qemu.pid"
mkdir -p "$OUT" "$INST32"
rm -f "$SER" "$MON" "$PID"

exec {qfd}>"$QEMU_LOCK"
flock -w 2400 "$qfd" || { log "等不到 QEMU 锁"; exit 1; }

if [ -f "$PID" ]; then OLD=$(cat "$PID" 2>/dev/null); [ -n "${OLD:-}" ] && kill -9 "$OLD" 2>/dev/null; fi

qemu-system-i386 -L "$CHAT/.toolchain/pkg/datadir" \
  -kernel "$WT/build_v250/kernel.elf" \
  -m 256 -smp 1 -vnc 127.0.0.1:$VNCPORT \
  -monitor unix:$MON,server,nowait -serial file:$SER -no-reboot \
  -daemonize -pidfile "$PID" 2>&1 | head -2

alive() { [ -f "$PID" ] && P=$(cat "$PID" 2>/dev/null) && [ -n "$P" ] && kill -0 "$P" 2>/dev/null; }

DESKTOP=0
for i in $(seq 1 60); do
  grep -aq "elastic free desktop init done" "$SER" 2>/dev/null && { DESKTOP=1; break; }
  alive || break
  sleep 1
done
[ "$DESKTOP" = "1" ] && log "direct 32 desktop marker hit"

for i in $(seq 1 8); do
  alive || { log "QEMU 已退出"; break; }
  python3 - "$MON" "$OUT/desktop_$i.ppm" "$OUT/desktop_$i.png" <<'PY'
import os, socket, sys, time
from PIL import Image
mon, ppm, png = sys.argv[1], sys.argv[2], sys.argv[3]
try:
    s = socket.socket(socket.AF_UNIX); s.connect(mon); time.sleep(0.3)
    s.sendall(("screendump %s\n" % ppm).encode()); time.sleep(2.0)
    try: s.recv(4096)
    except Exception: pass
    s.close()
except Exception as e:
    print("monitor unreachable:", e); sys.exit(1)
if not os.path.exists(ppm) or os.path.getsize(ppm) == 0:
    print("ppm missing"); sys.exit(1)
im = Image.open(ppm).convert("RGB"); im.save(png)
print("W,H =", im.size, "NONBLACK =", sum(1 for p in im.getdata() if p != (0, 0, 0)))
PY
  sleep 1.5
done

alive || true
[ -f "$PID" ] && { P=$(cat "$PID" 2>/dev/null); [ -n "$P" ] && kill -9 "$P" 2>/dev/null; }
cp -f "$SER" "$OUT/ser.log" 2>/dev/null || true

BOOT=$(grep -ac "32-bit" "$SER" 2>/dev/null || echo 0)
CRASH=0; alive || CRASH=1
{
  echo "DESKTOP=$DESKTOP"
  echo "BOOT_COUNT=$BOOT"
  echo "CRASHED=$CRASH"
} > "$OUT/state.txt"
log "32 direct desktop done -> $OUT"
