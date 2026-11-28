#!/bin/bash
# ============================================================================
# M3 词频关闭端到端：Remember frequency Off 后继续输入，freq.txt 不得增长
#
# 三次启动同一份 writethrough 镜像：
#   1) ZH + freq On 输入 eng 选 1（鞥），停止后读 freq.txt 作为基线；
#   2) Settings 关闭 Remember frequency，回终端输入 zhongguo 选 1（中国），
#      停止后 freq.txt 应与基线逐字节一致；
#   3) 再重启，Input 页应仍显示 Off。
# 固定 1920x1080 / scale=100；QEMU 合成环境，不是真机。
# 用法: bash tools/regress/ime_freq_off_probe.sh <输出目录>
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"
source "$REGRESS_DIR/lib_qemu.sh"

OUT="${1:?用法: ime_freq_off_probe.sh <输出目录>}"
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
TEST_IMG="$OUT/freq-off.img"
PART_IMG="$OUT/freq-off-part.img"
SER="$OUT/ser.log"
INST="$RUN/inst/ime-freq-off"
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
  INST="$RUN/inst/ime-freq-off-$tag"
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

open_terminal() {
  python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 300 1920 1080 click
  sleep 2
  python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 60 172 1920 1080 click
  sleep 3
  python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 1000 400 1920 1080 click
  sleep 1
  python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 200 1920 1080 move
  sleep 1
}

open_settings_input() {
  python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 780 462 1920 1080 click
  sleep 2
  python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 200 1920 1080 move
  sleep 1
}

read_freq() {
  local dest="$1"
  dd if="$TEST_IMG" of="$PART_IMG" bs=512 skip="$PART_START" count="$PART_COUNT" status=none
  debugfs -R "cat /data/ime/freq.txt" "$PART_IMG" >"$dest" 2>"$OUT/debugfs-freq.err" || true
}

cleanup() { stop_vm; }
trap cleanup EXIT

# ---- 第 1 次：freq On，输入 eng 选 1，生成基线 ----
start_vm first 5912
unlock_desktop || exit 3
open_terminal
q_sendkey ctrl-spc
sleep 2
q_send_text "eng"
sleep 2
q_dump "$OUT/01-boot1-eng-candidates.ppm" "$OUT/01-boot1-eng-candidates.png" || exit 3
q_sendkey 1
sleep 2
q_dump "$OUT/02-boot1-eng-committed.ppm" "$OUT/02-boot1-eng-committed.png" || exit 3
stop_vm
read_freq "$OUT/freq-before.txt"
echo "freq before: $(tr '\n' ' ' <"$OUT/freq-before.txt")"

# ---- 第 2 次：关闭 Remember frequency，输入 zhongguo 选 1 ----
start_vm second 5913
unlock_desktop || exit 3
open_settings_input
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 950 306 1920 1080 click
sleep 2
q_dump "$OUT/03-boot2-freq-off.ppm" "$OUT/03-boot2-freq-off.png" || exit 3
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 1290 207 1920 1080 click
sleep 2
open_terminal
q_send_text "zhongguo"
sleep 2
q_dump "$OUT/04-boot2-zhongguo-candidates.ppm" "$OUT/04-boot2-zhongguo-candidates.png" || exit 3
q_sendkey 1
sleep 2
q_dump "$OUT/05-boot2-zhongguo-committed.ppm" "$OUT/05-boot2-zhongguo-committed.png" || exit 3
stop_vm
read_freq "$OUT/freq-after.txt"
echo "freq after:  $(tr '\n' ' ' <"$OUT/freq-after.txt")"
if cmp -s "$OUT/freq-before.txt" "$OUT/freq-after.txt"; then
  echo "freq unchanged: PASS"
else
  echo "freq unchanged: FAIL"
fi
if grep -aq "中国" "$OUT/freq-after.txt"; then
  echo "freq contains 中国: FAIL"
else
  echo "freq contains 中国: no"
fi

# ---- 第 3 次：重启后 Input 页仍为 Off ----
start_vm third 5914
unlock_desktop || exit 3
open_settings_input
q_dump "$OUT/06-boot3-freq-off-restored.ppm" "$OUT/06-boot3-freq-off-restored.png" || exit 3
stop_vm

echo "IME FREQ OFF PROBE DONE: $OUT"
