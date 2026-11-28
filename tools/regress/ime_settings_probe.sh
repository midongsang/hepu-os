#!/bin/bash
# ============================================================================
# M3 Settings/Input 真开关取证：Mode / Candidates/page / Remember frequency。
# 用法: bash tools/regress/ime_settings_probe.sh <输出目录>
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"
source "$REGRESS_DIR/lib_qemu.sh"

OUT="${1:?用法: ime_settings_probe.sh <输出目录>}"
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
SER="$OUT/ser.log"
INST="$RUN/inst/ime-settings"
MON="$INST/mon.sock"; QMP="$INST/qmp.sock"; PID="$INST/qemu.pid"; FD="$INST/V.fd"
VNCPORT=5909
mkdir -p "$INST"; rm -f "$SER" "$MON" "$QMP" "$PID"

exec {qfd}>"$QEMU_LOCK"
flock -w 2400 "$qfd" || { log "等不到 QEMU 锁"; exit 1; }
cleanup() { [ -f "$PID" ] && kill -9 "$(cat "$PID" 2>/dev/null)" 2>/dev/null; }
trap cleanup EXIT

cp -f "$V/OVMF_VARS_4M.fd" "$FD"
qemu-system-x86_64 -L "$CHAT/.toolchain/pkg/datadir" \
  -rtc base=2026-11-27T12:00:00 \
  -drive if=pflash,format=raw,readonly=on,file="$V/OVMF_CODE_4M.fd" \
  -drive if=pflash,format=raw,file="$FD" \
  -drive format=raw,file="$IMG",file.locking=off,snapshot=on \
  -m 1024 -smp 2 -vnc 127.0.0.1:$VNCPORT \
  -monitor unix:$MON,server,nowait -qmp unix:$QMP,server,nowait \
  -serial file:"$SER" -usb -device usb-kbd -device usb-tablet \
  -vga std -nic none -daemonize -pidfile "$PID" 2>&1 | head -2

q_sendkey ret
if ! q_wait_for "lock -> login" 20 "$SER"; then q_wait_for "login -> desktop" 20 "$SER" || true; fi
q_sendkey ret
q_wait_for "login: auth failed" 8 "$SER" || true
sleep 1
q_send_text "$FIXTURE_PASSWORD"
sleep 2
q_sendkey ret
q_wait_for "login -> desktop" 30 "$SER" || { log "FAIL: 解锁失败"; exit 3; }
q_wait_for "Desktop running" 30 "$SER" || { log "FAIL: 桌面未运行"; exit 3; }
sleep 12

# Settings 已在桌面；点左侧 Input 导航。
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 780 462 1920 1080 click
sleep 2
q_dump "$OUT/01-input-default.png.ppm" "$OUT/01-input-default.png" || exit 3

# Mode：English (EN) -> 中文 (ZH)
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 950 250 1920 1080 click
sleep 2
q_dump "$OUT/02-input-mode-zh.png.ppm" "$OUT/02-input-mode-zh.png" || exit 3

# Candidates/page：9 -> 7（行中心比 Mode 低 28）
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 950 278 1920 1080 click
sleep 2
q_dump "$OUT/03-input-cand7.png.ppm" "$OUT/03-input-cand7.png" || exit 3

# Remember frequency：On -> Off
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 950 306 1920 1080 click
sleep 2
q_dump "$OUT/04-input-freq-off.png.ppm" "$OUT/04-input-freq-off.png" || exit 3

# 恢复 9/页 + On（候选 7->5->9 两次，频率一次）
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 950 278 1920 1080 click
sleep 1
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 950 278 1920 1080 click
sleep 1
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 950 306 1920 1080 click
sleep 1
q_dump "$OUT/05-input-restored.png.ppm" "$OUT/05-input-restored.png" || exit 3

echo "IME SETTINGS PROBE DONE: $OUT"
