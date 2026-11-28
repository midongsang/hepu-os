#!/bin/bash
# ============================================================================
# M2 持久化取证：sys.ime_mode + /data/ime/freq.txt 跨 QEMU 重启
#
# 与其它 probe 不同：这里复制一份镜像并关闭 snapshot，使用 writethrough，
# 第一次启动切到 ZH、输入 eng 并选第 1 候选“鞥”，然后杀进程；第二次启动
# 不再按 Ctrl+Space，直接输入 eng，验证：
#   1) 模式仍是 ZH（否则字母只会进正文、不弹候选）；
#   2) 词频已从 /data/ime/freq.txt 加载（“鞥”前移）。
# 停止后从镜像分区直接读 freq.txt / registry.dat 作为磁盘层证据。
# 固定 1920x1080 / scale=100；QEMU 合成环境，不是真机。
# 用法: bash tools/regress/ime_persist_probe.sh <输出目录>
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"
source "$REGRESS_DIR/lib_qemu.sh"

OUT="${1:?用法: ime_persist_probe.sh <输出目录>}"
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
TEST_IMG="$OUT/hepu-persist.img"
PART_IMG="$OUT/hepu-part.img"
SER="$OUT/ser.log"
VNCPORT=5906
INST="$RUN/inst/ime-persist"
MON="$INST/mon.sock"; QMP="$INST/qmp.sock"; PID="$INST/qemu.pid"; FD="$INST/V.fd"
mkdir -p "$INST"; rm -f "$SER" "$MON" "$QMP" "$PID"

cp -f "$IMG" "$TEST_IMG"
# 分区起点从 MBR 读，避免把 264192 写死在脚本里。
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
  INST="$RUN/inst/ime-persist-$tag"
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
  python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 200 1920 1080 move
  sleep 1
}

cleanup() { stop_vm; }
trap cleanup EXIT

# ---- 第一次启动：先在 Editor 完成“输入→保存”，再在 Terminal 制造词频 ----
start_vm first 5906
unlock_desktop || exit 3

# Editor 是第一个打开的交互窗口，避免 Terminal 残留焦点把上屏抢走。
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 300 1920 1080 click
sleep 2
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 144 252 1920 1080 click
sleep 3
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 200 1920 1080 move
q_dump "$OUT/01-boot1-editor-en.ppm" "$OUT/01-boot1-editor-en.png" || exit 3

q_sendkey ctrl-spc
sleep 2
q_send_text "nihao"
sleep 2
q_dump "$OUT/02-boot1-editor-candidates.ppm" "$OUT/02-boot1-editor-candidates.png" || exit 3
q_sendkey 1
sleep 2
q_sendkey f5
sleep 2
q_dump "$OUT/03-boot1-editor-saved.ppm" "$OUT/03-boot1-editor-saved.png" || exit 3

# 关闭 Editor（标题栏关闭键），再开 Terminal 输入 eng 产生独立词频。
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 1290 207 1920 1080 click
sleep 2
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 300 1920 1080 click
sleep 2
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 60 172 1920 1080 click
sleep 3
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 200 1920 1080 move
sleep 1
q_send_text "eng"
sleep 2
q_dump "$OUT/04-boot1-terminal-candidates.ppm" "$OUT/04-boot1-terminal-candidates.png" || exit 3
q_sendkey 1
sleep 2
q_dump "$OUT/05-boot1-terminal-committed.ppm" "$OUT/05-boot1-terminal-committed.png" || exit 3
stop_vm

# ---- 磁盘层取证：模式键与词频文件 ----
dd if="$TEST_IMG" of="$PART_IMG" bs=512 skip="$PART_START" count="$PART_COUNT" status=none
debugfs -R "cat /data/ime/freq.txt" "$PART_IMG" >"$OUT/freq.txt" 2>"$OUT/debugfs-freq.err" || true
debugfs -R "cat /etc/registry.dat" "$PART_IMG" >"$OUT/registry.dat" 2>"$OUT/debugfs-reg.err" || true
debugfs -R "cat /etc/hepushell/config.hsqml" "$PART_IMG" >"$OUT/saved-config.txt" 2>"$OUT/debugfs-config.err" || true
grep -a "sys.ime_mode" "$OUT/registry.dat" >"$OUT/mode-line.txt" || true
echo "disk freq: $(cat "$OUT/freq.txt" 2>/dev/null | tr '\n' ' ')"
echo "disk mode: $(cat "$OUT/mode-line.txt" 2>/dev/null)"

# ---- 第二次启动：不再切模式，直接 e；候选出现 + “鞥”前移即加载成功 ----
start_vm second 5907
unlock_desktop || exit 3
open_terminal
q_dump "$OUT/06-boot2-en-or-zh.ppm" "$OUT/06-boot2-en-or-zh.png" || exit 3
q_send_text "eng"
sleep 2
q_dump "$OUT/07-boot2-candidates.ppm" "$OUT/07-boot2-candidates.png" || exit 3
q_sendkey esc
sleep 1
q_dump "$OUT/08-boot2-esc-cancelled.ppm" "$OUT/08-boot2-esc-cancelled.png" || exit 3

# 模式已持久化为 ZH；切回 EN 后打开 Editor，确认保存的中文首行跨重启可读回。
q_sendkey ctrl-spc
sleep 2
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 300 1920 1080 click
sleep 2
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 144 252 1920 1080 click
sleep 3
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 200 1920 1080 move
q_dump "$OUT/09-boot2-editor-saved.ppm" "$OUT/09-boot2-editor-saved.png" || exit 3
stop_vm

echo "IME PERSIST PROBE DONE: $OUT"
