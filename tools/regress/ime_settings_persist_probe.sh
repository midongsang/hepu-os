#!/bin/bash
# ============================================================================
# M3 Settings 持久化取证：候选数 / Remember frequency 跨 QEMU 重启
#
# 复制镜像并关闭 snapshot，使用 writethrough；第一次启动把
# Candidates/page 从 9 改为 7、Remember frequency 从 On 改为 Off，
# 停止后从 /etc/registry.dat 读回；第二次启动不再点击，直接看 Input 页。
# 固定 1920x1080 / scale=100；QEMU 合成环境，不是真机。
# 用法: bash tools/regress/ime_settings_persist_probe.sh <输出目录>
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"
source "$REGRESS_DIR/lib_qemu.sh"

OUT="${1:?用法: ime_settings_persist_probe.sh <输出目录>}"
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
TEST_IMG="$OUT/settings-persist.img"
PART_IMG="$OUT/settings-part.img"
SER="$OUT/ser.log"
INST="$RUN/inst/ime-settings-persist"
MON="$INST/mon.sock"; QMP="$INST/qmp.sock"; PID="$INST/qemu.pid"; FD="$INST/V.fd"
mkdir -p "$INST"; rm -f "$SER" "$MON" "$QMP" "$PID"

cp -f "$IMG" "$TEST_IMG"
PART_START=$(python3 - "$TEST_IMG" <<'PY'
import struct, sys
f=open(sys.argv[1],'rb'); mbr=f.read(512)
for i in range(4):
    e=mbr[446+i*16:446+(i+1)*16]
    if e[4] in (0x83,0x0b,0x0c):
        print(struct.unpack('<I', e[8:12])[0]); break
PY
)
PART_COUNT=$(( (268435456 - PART_START * 512) / 512 ))

exec {qfd}>"$QEMU_LOCK"
flock -w 2400 "$qfd" || { log "等不到 QEMU 锁"; exit 1; }

start_vm() {
  local tag="$1" vnc="$2"
  INST="$RUN/inst/ime-settings-persist-$tag"
  MON="$INST/mon.sock"; QMP="$INST/qmp.sock"; PID="$INST/qemu.pid"; FD="$INST/V.fd"
  mkdir -p "$INST"; rm -f "$SER" "$MON" "$QMP" "$PID"
  cp -f "$V/OVMF_VARS_4M.fd" "$FD"
  qemu-system-x86_64 -L "$CHAT/.toolchain/pkg/datadir" \
    -rtc base=2026-11-27T12:00:00 \
    -drive if=pflash,format=raw,readonly=on,file="$V/OVMF_CODE_4M.fd" \
    -drive if=pflash,format=raw,file="$FD" \
    -drive format=raw,file="$TEST_IMG",file.locking=off,snapshot=off,cache=writethrough \
    -m 1024 -smp 2 -vnc 127.0.0.1:$vnc \
    -monitor unix:$MON,server,nowait -qmp unix:$QMP,server,nowait \
    -serial file:"$SER" -usb -device usb-kbd -device usb-tablet \
    -vga std -nic none -daemonize -pidfile "$PID" 2>&1 | head -2
  sleep 1
}

stop_vm() {
  if [ -f "$PID" ]; then
    local P; P=$(cat "$PID" 2>/dev/null)
    [ -n "${P:-}" ] && kill -9 "$P" 2>/dev/null
  fi
  sleep 1
}

unlock_desktop() {
  q_sendkey ret
  if ! q_wait_for "lock -> login" 20 "$SER"; then q_wait_for "login -> desktop" 20 "$SER" || true; fi
  q_sendkey ret
  q_wait_for "login: auth failed" 8 "$SER" || true
  sleep 1
  q_send_text "$FIXTURE_PASSWORD"
  sleep 2
  q_sendkey ret
  q_wait_for "login -> desktop" 30 "$SER" || { log "FAIL: 解锁失败"; return 1; }
  q_wait_for "Desktop running" 30 "$SER" || { log "FAIL: 桌面未运行"; return 1; }
  sleep 12
}

open_input() {
  python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 780 462 1920 1080 click
  sleep 2
  python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 200 1920 1080 move
  sleep 1
}

cleanup() { stop_vm; }
trap cleanup EXIT

# ---- 第一次启动：改候选 7、词频 Off，不恢复 ----
start_vm first 5910
unlock_desktop || exit 3
open_input
q_dump "$OUT/01-boot1-default.ppm" "$OUT/01-boot1-default.png" || exit 3

python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 950 278 1920 1080 click
sleep 2
q_dump "$OUT/02-boot1-cand7.ppm" "$OUT/02-boot1-cand7.png" || exit 3

python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 950 306 1920 1080 click
sleep 2
q_dump "$OUT/03-boot1-freq-off.ppm" "$OUT/03-boot1-freq-off.png" || exit 3
stop_vm

# ---- 磁盘层：直接读回注册表 ----
dd if="$TEST_IMG" of="$PART_IMG" bs=512 skip="$PART_START" count="$PART_COUNT" status=none
debugfs -R "cat /etc/registry.dat" "$PART_IMG" >"$OUT/registry.dat" 2>"$OUT/debugfs-reg.err" || true
grep -a "sys.ime_candidates" "$OUT/registry.dat" >"$OUT/candidates-line.txt" || true
grep -a "sys.ime_freq" "$OUT/registry.dat" >"$OUT/freq-line.txt" || true
echo "disk candidates: $(cat "$OUT/candidates-line.txt" 2>/dev/null)"
echo "disk freq: $(cat "$OUT/freq-line.txt" 2>/dev/null)"

# ---- 第二次启动：不点击，Input 页应读回 7 / Off ----
start_vm second 5911
unlock_desktop || exit 3
open_input
q_dump "$OUT/04-boot2-Input-restored.ppm" "$OUT/04-boot2-Input-restored.png" || exit 3
stop_vm

echo "IME SETTINGS PERSIST PROBE DONE: $OUT"
