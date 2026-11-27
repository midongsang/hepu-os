#!/bin/bash
# ============================================================================
# 电池状态取证：启动 64 位桌面，抓串口 [BAT] 行 + 桌面截图。
#
# 用法:
#   bash tools/regress/battery_state_probe.sh <输出目录> [acpitable文件]
#
# 第二个参数可选：注入一个合成 SSDT（含 PNP0C0A 声明），用于验证
# DECLARED 分支；不传则验证默认 QEMU（应判 ABSENT）。
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"
source "$REGRESS_DIR/lib_qemu.sh"

OUT="${1:?用法: battery_state_probe.sh <输出目录> [acpitable文件]}"
ACPITABLE="${2:-}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
SER="$OUT/ser.log"

exec {qfd}>"$QEMU_LOCK"
flock -w 2400 "$qfd" || { log "等不到 QEMU 锁"; exit 1; }

q_start_probe() {
  local SER_PATH="$1" ACPI_FILE="${2:-}"
  MON="$INST/mon.sock"
  PID="$INST/qemu.pid"
  FD="$INST/V.fd"
  rm -f "$SER_PATH" "$MON" "$PID"
  cp -f "$V/OVMF_VARS_4M.fd" "$FD"

  local extra=()
  [ -n "$ACPI_FILE" ] && extra=(-acpitable "file=$ACPI_FILE")

  qemu-system-x86_64 -L "$CHAT/.toolchain/pkg/datadir" \
    -rtc base=localtime \
    -drive if=pflash,format=raw,readonly=on,file="$V/OVMF_CODE_4M.fd" \
    -drive if=pflash,format=raw,file="$FD" \
    -drive format=raw,file="$IMG",file.locking=off,snapshot=on \
    -m 1024 -smp 2 -vnc 127.0.0.1:$VNCPORT \
    -monitor unix:$MON,server,nowait \
    -serial file:"$SER_PATH" \
    -device qemu-xhci -device usb-kbd -device usb-mouse -vga std \
    -nic user,model=e1000 "${extra[@]}" -daemonize -pidfile "$PID" 2>&1 | head -2
  sleep 1
}

q_start_probe "$SER" "$ACPITABLE"
log "QEMU started, inst=$INST vnc=:$VNCPORT acpitable=${ACPITABLE:-none}"

sleep 5
q_sendkey 1
q_wait_for "step1 done" 30 "$SER" || log "WARN: step1 未出现"
q_wait_for "booting to lock" 60 "$SER" || log "WARN: lock 未出现"
sleep 5
q_sendkey ret
q_wait_for "lock -> login" 20 "$SER" || log "WARN: login 未出现"
sleep 2
q_sendkey ret
if q_wait_for "login: auth failed" 10 "$SER"; then
  log "empty Enter rejected; typing fixture password"
  sleep 2
  q_send_text "$FIXTURE_PASSWORD"
  sleep 2
  q_sendkey ret
  if ! q_wait_for "login -> desktop" 30 "$SER"; then
    log "WARN: first password attempt failed; retry"
    q_send_text "$FIXTURE_PASSWORD"
    sleep 2
    q_sendkey ret
  fi
else
  log "WARN: 空 Enter 未触发 auth failed"
fi
q_wait_for "login -> desktop" 30 "$SER" || log "WARN: desktop 迁移未出现"
q_wait_for "Desktop running" 60 "$SER" || log "WARN: Desktop running 未出现"
sleep 6

q_dump "$OUT/desktop_1.ppm" "$OUT/desktop_1.png"
sleep 2
q_dump "$OUT/desktop_2.ppm" "$OUT/desktop_2.png"

q_stop

log "== [BAT] lines =="
grep -a '\[BAT\]' "$SER" | tee "$OUT/bat-lines.txt" || true
grep -aE '\[AML\]|\[BAT\]|\[DESK\]|\[SETTINGS\]' "$SER" | tee "$OUT/desk-lines.txt" || true

log "== ACPI/Power lines =="
grep -aE 'ACPI|acpi|Battery|battery|BAT' "$SER" | tail -40 | tee "$OUT/acpi-lines.txt" || true
