#!/bin/bash
# ============================================================================
# M3 编辑器取证：拼音分段（nihao/zhongguo）+ yi 左右翻页 + 鼠标选字。
# 用法: bash tools/regress/ime_m3_editor_probe.sh <输出目录>
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"
source "$REGRESS_DIR/lib_qemu.sh"

OUT="${1:?用法: ime_m3_editor_probe.sh <输出目录>}"
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
SER="$OUT/ser.log"
INST="$RUN/inst/ime-m3-editor"
MON="$INST/mon.sock"; QMP="$INST/qmp.sock"; PID="$INST/qemu.pid"; FD="$INST/V.fd"
VNCPORT=5908
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

# 打开抽屉 -> Editor；随后点桌面关闭抽屉，再点编辑区确保焦点在 Editor。
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 300 1920 1080 click
sleep 2
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 144 252 1920 1080 click
sleep 3
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 1600 600 1920 1080 click
sleep 1
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 500 260 1920 1080 click
sleep 1
q_dump "$OUT/01-editor-focus.png.ppm" "$OUT/01-editor-focus.png" || exit 3

q_sendkey ctrl-spc
sleep 2
q_send_text "nihao"
sleep 2
q_dump "$OUT/02-nihao-segmented.png.ppm" "$OUT/02-nihao-segmented.png" || exit 3

# 数字 1 选第 1 候选（避免候选条多行时坐标漂移；鼠标点选由 M2 脚本覆盖）
q_sendkey 1
sleep 2
q_dump "$OUT/03-nihao-committed.png.ppm" "$OUT/03-nihao-committed.png" || exit 3

# yi：默认 9/页 => 99 候选/11 页；左右各一次
q_send_text "yi"
sleep 2
q_dump "$OUT/04-yi-page1.png.ppm" "$OUT/04-yi-page1.png" || exit 3
q_sendkey right
sleep 2
q_dump "$OUT/05-yi-page2.png.ppm" "$OUT/05-yi-page2.png" || exit 3
q_sendkey left
sleep 2
q_dump "$OUT/06-yi-page1-back.png.ppm" "$OUT/06-yi-page1-back.png" || exit 3

q_sendkey esc
sleep 1
q_send_text "zhongguo"
sleep 2
q_dump "$OUT/07-zhongguo-segmented.png.ppm" "$OUT/07-zhongguo-segmented.png" || exit 3

q_sendkey 1
sleep 2
q_dump "$OUT/08-zhongguo-committed.png.ppm" "$OUT/08-zhongguo-committed.png" || exit 3

echo "IME M3 EDITOR PROBE DONE: $OUT"
