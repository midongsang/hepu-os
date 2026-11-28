#!/bin/bash
# ============================================================================
# 中文输入闭环取证（M1）
#
# 固定条件: 1920x1080, scale=100, 无网络; 解锁后打开 Terminal:
#   EN 模式输入 "echo " -> Ctrl+Space 切 ZH -> 拼音 nihao -> 候选条
#   数字 1 上屏 "你好" -> 回车执行 -> 终端输出 "你好"
#
# 用法: bash tools/regress/ime_probe.sh <输出目录>
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"
source "$REGRESS_DIR/lib_qemu.sh"

OUT="${1:?用法: ime_probe.sh <输出目录>}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
SER="$OUT/ser.log"

INST="$RUN/inst/ime-probe"
MON="$INST/mon.sock"
QMP="$INST/qmp.sock"
PID="$INST/qemu.pid"
FD="$INST/V.fd"
VNCPORT=5904
mkdir -p "$INST"
rm -f "$SER" "$MON" "$QMP" "$PID"

exec {qfd}>"$QEMU_LOCK"
flock -w 2400 "$qfd" || { log "等不到 QEMU 锁"; exit 1; }

cleanup() {
  if [ -f "$PID" ]; then
    local P; P=$(cat "$PID" 2>/dev/null)
    [ -n "${P:-}" ] && kill -9 "$P" 2>/dev/null
  fi
}
trap cleanup EXIT

cp -f "$V/OVMF_VARS_4M.fd" "$FD"
qemu-system-x86_64 -L "$CHAT/.toolchain/pkg/datadir" \
  -rtc base=2026-11-27T12:00:00 \
  -drive if=pflash,format=raw,readonly=on,file="$V/OVMF_CODE_4M.fd" \
  -drive if=pflash,format=raw,file="$FD" \
  -drive format=raw,file="$IMG",file.locking=off,snapshot=on \
  -m 1024 -smp 2 -vnc 127.0.0.1:$VNCPORT \
  -monitor unix:$MON,server,nowait \
  -qmp unix:$QMP,server,nowait \
  -serial file:"$SER" \
  -usb -device usb-kbd -device usb-tablet \
  -vga std -nic none -daemonize -pidfile "$PID" 2>&1 | head -2
sleep 1

# ---- 解锁 (与 desktop_visual.sh 同一套 fixture) ----
q_sendkey ret
if ! q_wait_for "lock -> login" 20 "$SER"; then
  q_wait_for "login -> desktop" 20 "$SER" || true
fi
q_sendkey ret
q_wait_for "login: auth failed" 8 "$SER" || true
sleep 1
q_send_text "$FIXTURE_PASSWORD"
sleep 2
q_sendkey ret
q_wait_for "login -> desktop" 30 "$SER" || { log "FAIL: 解锁失败"; exit 3; }
q_wait_for "Desktop running" 30 "$SER" || { log "FAIL: 桌面未运行"; exit 3; }
sleep 12

# ---- 打开应用抽屉 -> Terminal (100% 坐标) ----
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 300 1920 1080 click
sleep 2
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 60 172 1920 1080 click
sleep 3
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 200 1920 1080 move
sleep 1
q_dump "$OUT/01-terminal-en.ppm" "$OUT/01-terminal-en.png" || exit 3

# ---- EN 模式输入 echo + 空格 ----
q_send_text "echo "
sleep 1
q_dump "$OUT/02-terminal-echo-en.ppm" "$OUT/02-terminal-echo-en.png" || exit 3

# ---- Ctrl+Space 切中文；输入 nihao ----
q_sendkey ctrl-spc
sleep 2
q_send_text "nihao"
sleep 2
q_dump "$OUT/03-candidates.ppm" "$OUT/03-candidates.png" || exit 3

# ---- Esc 取消组合：候选条消失但终端不退出 ----
q_sendkey esc
sleep 2
q_dump "$OUT/03b-esc-cancelled.ppm" "$OUT/03b-esc-cancelled.png" || exit 3
q_send_text "nihao"
sleep 2

# ---- 数字 1 上屏 -> 回车执行 ----
q_sendkey 1
sleep 1
q_dump "$OUT/04-committed.ppm" "$OUT/04-committed.png" || exit 3
q_sendkey ret
sleep 2
q_dump "$OUT/05-echo-output.ppm" "$OUT/05-echo-output.png" || exit 3

# ---- 切回 EN，确认 ASCII 原样 ----
q_sendkey ctrl-spc
sleep 2
q_send_text "abc"
sleep 1
q_dump "$OUT/06-en-ascii.ppm" "$OUT/06-en-ascii.png" || exit 3

# ---- 编辑器中文输入：关终端 -> 点 Editor -> 切 ZH -> 上屏 ----
q_sendkey ctrl-w
sleep 2
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 144 252 1920 1080 click
sleep 3
q_sendkey ctrl-spc
sleep 2
q_send_text "nihao"
sleep 2
q_dump "$OUT/07-editor-candidates.ppm" "$OUT/07-editor-candidates.png" || exit 3
q_sendkey 1
sleep 1
q_dump "$OUT/08-editor-committed.ppm" "$OUT/08-editor-committed.png" || exit 3

echo "IME PROBE DONE: $OUT"
