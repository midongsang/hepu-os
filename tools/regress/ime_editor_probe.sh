#!/bin/bash
# ============================================================================
# 编辑器中文输入取证（M1 扩展）
# 固定条件: 1920x1080, scale=100; 解锁后直接打开 Editor，避免 Terminal 焦点干扰。
# 用法: bash tools/regress/ime_editor_probe.sh <输出目录>
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"
source "$REGRESS_DIR/lib_qemu.sh"

OUT="${1:?用法: ime_editor_probe.sh <输出目录>}"
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
SER="$OUT/ser.log"

INST="$RUN/inst/ime-editor"
MON="$INST/mon.sock"; QMP="$INST/qmp.sock"; PID="$INST/qemu.pid"; FD="$INST/V.fd"
VNCPORT=5905
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
sleep 1

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

python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 300 1920 1080 click
sleep 2
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 144 252 1920 1080 click
sleep 3
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 200 1920 1080 move
sleep 1
q_dump "$OUT/01-editor.png.ppm" "$OUT/01-editor.png" || exit 3

q_sendkey ctrl-spc
sleep 2
q_send_text "nihao"
sleep 2
q_dump "$OUT/02-editor-candidates.ppm" "$OUT/02-editor-candidates.png" || exit 3

# M2: 鼠标点选第 1 候选“你好”（命中区与绘制同一套几何）
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 465 315 1920 1080 click
sleep 2
q_dump "$OUT/03-editor-committed.ppm" "$OUT/03-editor-committed.png" || exit 3

# ---- 生僻字兜底：e 的第 5 候选「呃」/ 第 7 候选「鞥」都不在 TTF 子集 ---- 
q_send_text "e"
sleep 2
q_dump "$OUT/04-editor-rare-candidates.ppm" "$OUT/04-editor-rare-candidates.png" || exit 3
# 先选 7「鞥」；上屏后词频会把「鞥」前移，第二轮改选第 6「呃」。
q_sendkey 7
sleep 1
q_dump "$OUT/05-editor-rare-committed.ppm" "$OUT/05-editor-rare-committed.png" || exit 3

q_send_text "e"
sleep 2
q_dump "$OUT/06-editor-rare2-candidates.ppm" "$OUT/06-editor-rare2-candidates.png" || exit 3
q_sendkey 6
sleep 1
q_dump "$OUT/07-editor-rare2-committed.ppm" "$OUT/07-editor-rare2-committed.png" || exit 3

# M2: 光标逐码点移动 —— Home 到行首，Right 两次跨过“你”“好”，
# 再输入拼音；候选条应重新锚到“好”之后，选字也插在该处。
q_sendkey home
sleep 1
q_sendkey right
q_sendkey right
sleep 1
q_send_text "e"
sleep 2
q_dump "$OUT/08-editor-caret-candidates.ppm" "$OUT/08-editor-caret-candidates.png" || exit 3
q_sendkey 1
sleep 1
q_dump "$OUT/09-editor-caret-committed.ppm" "$OUT/09-editor-caret-committed.png" || exit 3

echo "IME EDITOR PROBE DONE: $OUT"
