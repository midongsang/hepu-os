#!/bin/bash
# ============================================================================
# QEMU 实例辅助库（被各取证脚本 source）
#
# 纪律：
#   - 每个取证脚本的完整起停生命周期由调用方包在 `flock $QEMU_LOCK` 内；
#   - 实例目录 $INST，pidfile $INST/qemu.pid；退出只 kill 自己 pidfile 里的 pid；
#   - 镜像 snapshot=on 只读共享；OVMF_VARS 复制到实例私有副本。
# ============================================================================

q_start() {
  # $1 = 串口日志路径（默认 $INST/ser.log）
  local SER=${1:-$INST/ser.log}
  MON="$INST/mon.sock"
  PID="$INST/qemu.pid"
  FD="$INST/V.fd"
  rm -f "$SER" "$MON" "$PID"
  cp -f "$V/OVMF_VARS_4M.fd" "$FD"

  if [ -f "$PID" ]; then
    local OLD; OLD=$(cat "$PID" 2>/dev/null)
    [ -n "${OLD:-}" ] && kill -9 "$OLD" 2>/dev/null
  fi

  qemu-system-x86_64 -L "$CHAT/.toolchain/pkg/datadir" \
    -rtc base=localtime \
    -drive if=pflash,format=raw,readonly=on,file="$V/OVMF_CODE_4M.fd" \
    -drive if=pflash,format=raw,file="$FD" \
    -drive format=raw,file="$IMG",file.locking=off,snapshot=on \
    -m 1024 -smp 2 -vnc 127.0.0.1:$VNCPORT \
    -monitor unix:$MON,server,nowait \
    -serial file:$SER \
    -device qemu-xhci -device usb-kbd -device usb-mouse -vga std \
    -nic user,model=e1000 -daemonize -pidfile "$PID" 2>&1 | head -2
  sleep 1
}

q_wait_for() {
  # $1 = 串口 grep -E 正则；$2 = 最大秒数（默认 60）；返回 0 命中 / 1 超时
  local RE="$1" MAX="${2:-60}" SER="${3:-$INST/ser.log}" i
  for i in $(seq 1 "$MAX"); do
    sleep 1
    grep -aqE "$RE" "$SER" 2>/dev/null && return 0
  done
  return 1
}

q_sendkey() {
  local K="$1"
  python3 - "$INST/mon.sock" <<PY
import socket,time,sys
s=socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); time.sleep(0.2)
s.sendall(("sendkey %s\n" % "$K").encode()); time.sleep(0.15)
s.close()
PY
}

q_send_text() {
  # $1 = 文本；逐字符 sendkey，空格用 spc
  local TXT="$1" CH
  for (( i=0; i<${#TXT}; i++ )); do
    CH=${TXT:i:1}
    if [ "$CH" = " " ]; then q_sendkey spc; else q_sendkey "$CH"; fi
  done
}

q_alive() {
  local PID="$INST/qemu.pid"
  [ -f "$PID" ] || return 1
  local P; P=$(cat "$PID" 2>/dev/null)
  [ -n "${P:-}" ] && kill -0 "$P" 2>/dev/null
}

q_dump() {
  # $1 = ppm 路径；$2 = png 路径；输出 W,H,NONBLACK 到 stdout；QEMU 已退时返回 1
  local PPM="$1" PNG="$2"
  q_alive || { echo "QEMU dead, skip screendump"; return 1; }
  python3 - "$INST/mon.sock" "$PPM" "$PNG" <<'PY'
import os, socket, sys, time
from PIL import Image
mon, ppm, png = sys.argv[1], sys.argv[2], sys.argv[3]
try:
    s = socket.socket(socket.AF_UNIX); s.connect(mon); time.sleep(0.3)
    s.sendall(("screendump %s\n" % ppm).encode()); time.sleep(2.5)
    try: s.recv(4096)
    except Exception: pass
    s.close()
except Exception as e:
    print("monitor unreachable:", e); sys.exit(1)
if not os.path.exists(ppm) or os.path.getsize(ppm) == 0:
    print("screendump ppm missing"); sys.exit(1)
im = Image.open(ppm).convert("RGB")
im.save(png)
non = sum(1 for p in im.getdata() if p != (0, 0, 0))
print("W,H =", im.size, "NONBLACK =", non)
PY
}

q_stop() {
  local PID="$INST/qemu.pid"
  if [ -f "$PID" ]; then
    local P; P=$(cat "$PID" 2>/dev/null)
    [ -n "${P:-}" ] && kill -9 "$P" 2>/dev/null
  fi
  sleep 1
}
