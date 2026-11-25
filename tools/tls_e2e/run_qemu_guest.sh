#!/bin/bash
# ============================================================================
# TLS 端到端测试 —— 64 位镜像 QEMU 启动 + 串口日志驱动/断言脚本
#
# 子命令：
#   bash run_qemu_guest.sh start [镜像路径]
#       起中继 + QEMU（slirp e1000、-display none、串口落盘日志、monitor socket）。
#   bash run_qemu_guest.sh wait-for <正则> [超时秒]
#       轮询串口日志，命中正则返回 0，超时/进程退出返回 1。
#   bash run_qemu_guest.sh assert <固定串> [<固定串> ...]
#       逐个在串口日志里做固定字符串匹配；全部命中返回 0，否则返回 1。
#   bash run_qemu_guest.sh assert-wait <超时秒> <固定串> [<固定串> ...]
#       在超时内等待所有固定串陆续出现（供 TLS 自检构建断言握手结果用）。
#   bash run_qemu_guest.sh send "<命令行>"
#       经串口 FIFO 发一行（主通道；桌面构建下串口无 shell，仅作诊断保留）。
#   bash run_qemu_guest.sh sendkey "<文本>"
#       monitor sendkey 兜底通道（逐字符，0.12s 间隔，参考 tools/test_net32.sh）。
#   bash run_qemu_guest.sh stop
#       按 pidfile 只杀本脚本启动的 QEMU 与中继。
#   bash run_qemu_guest.sh alive
#       判断 QEMU 是否存活。
#
# 设计纪律：
#   - 镜像副本放在运行时目录（默认 /tmp/hepu-tls-e2e），不直接写
#     /tmp/hepu64_single.img，避免和并行构建冲突；
#   - -nic user,model=e1000 走 QEMU slirp，客户机经 10.0.2.2 访问宿主；
#   - pidfile 记录本实例，stop 不误伤他人进程；
#   - 断言模式与超时由调用方传入，本脚本不内置业务结论。
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
REL_ROOT="$(cd "$ROOT/.." && pwd)"
T="${HEPU_TOOLCHAIN:-$REL_ROOT/.toolchain/pkg/root2}"
DD="${HEPU_DATADIR:-$REL_ROOT/.toolchain/pkg/datadir}"
RUNTIME="${HEPU_TLS_RUNTIME:-/tmp/hepu-tls-e2e}"
IMG="${2:-$RUNTIME/hepu64.img}"

SER_SOCK="$RUNTIME/serial.sock"
SER_LOG="$RUNTIME/serial.log"
MON_SOCK="$RUNTIME/monitor.sock"
QPID="$RUNTIME/qemu.pid"
RELAY_PID="$RUNTIME/relay.pid"
FIFO="$RUNTIME/cmd.fifo"
QEMU_OUT="$RUNTIME/qemu.out"

export PATH="$T/sbin:$T/usr/bin:$PATH"
export LD_LIBRARY_PATH="$T/usr/lib/x86_64-linux-gnu:$T/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QEMU_MODULE_DIR="$T/usr/lib/x86_64-linux-gnu/qemu"

QEMU="${HEPU_QEMU:-$T/usr/bin/qemu-system-x86_64}"

mkdir -p "$RUNTIME"

qemu_pid() { [ -f "$QPID" ] && cat "$QPID" 2>/dev/null || true; }
relay_pid() { [ -f "$RELAY_PID" ] && cat "$RELAY_PID" 2>/dev/null || true; }

q_alive() {
  local p; p=$(qemu_pid)
  [ -n "$p" ] && kill -0 "$p" 2>/dev/null
}

q_send() {
  local line="$1"
  [ -p "$FIFO" ] || { echo "FIFO 不存在，QEMU 未启动"; return 1; }
  printf '%s\n' "$line" > "$FIFO"
}

q_sendkey() {
  local K="$1"
  python3 - "$MON_SOCK" "$K" <<'PY'
import socket, time, sys
sock, key = sys.argv[1], sys.argv[2]
s = socket.socket(socket.AF_UNIX)
s.connect(sock); time.sleep(0.12)
s.sendall(("sendkey %s\n" % key).encode()); time.sleep(0.10); s.close()
PY
}

q_send_text() {
  local TXT="$1" CH
  for (( i=0; i<${#TXT}; i++ )); do
    CH="${TXT:i:1}"
    case "$CH" in
      " ") q_sendkey spc ;;
      ".") q_sendkey dot ;;
      *)   q_sendkey "$CH" ;;
    esac
  done
  python3 - "$MON_SOCK" <<'PY'
import socket, time, sys
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); time.sleep(0.15)
s.sendall(b"sendkey ret\n"); time.sleep(0.12); s.close()
PY
}

# 正则等待：$1=正则 $2=超时秒
q_wait_for() {
  local RE="$1" MAX="${2:-90}" i
  for i in $(seq 1 "$MAX"); do
    if grep -aqE "$RE" "$SER_LOG" 2>/dev/null; then
      echo "[qemu] 命中 /$RE/（第 ${i}s）"
      return 0
    fi
    if grep -aqE "\[EXCEPTION\]|\[PANIC\]|\[ASSERT\] FAILED" "$SER_LOG" 2>/dev/null; then
      echo "[qemu] 串口出现异常/断言，提前退出等待"
      return 1
    fi
    if ! q_alive; then
      echo "[qemu] QEMU 进程已退出"
      return 1
    fi
    sleep 1
  done
  echo "[qemu] 等待 /$RE/ 超时（${MAX}s）"
  return 1
}

# 固定串断言：$@=模式列表；立即检查，不等待
q_assert() {
  local pat miss=0
  for pat in "$@"; do
    if grep -aqF "$pat" "$SER_LOG" 2>/dev/null; then
      echo "[qemu] 断言命中: $pat"
    else
      echo "[qemu] 断言缺失: $pat"
      miss=1
    fi
  done
  return $miss
}

# 超时等待所有固定串：$1=超时秒，其余为模式
q_assert_wait() {
  local MAX="$1"; shift
  local i pat missing
  for i in $(seq 1 "$MAX"); do
    missing=0
    for pat in "$@"; do
      grep -aqF "$pat" "$SER_LOG" 2>/dev/null || missing=1
    done
    [ "$missing" -eq 0 ] && { echo "[qemu] 全部断言命中（第 ${i}s）"; return 0; }
    q_alive || { echo "[qemu] QEMU 进程已退出"; return 1; }
    sleep 1
  done
  echo "[qemu] 断言等待超时（${MAX}s）"
  for pat in "$@"; do
    grep -aqF "$pat" "$SER_LOG" 2>/dev/null || echo "[qemu] 最终缺失: $pat"
  done
  return 1
}

q_start() {
  if q_alive; then echo "[qemu] 已在运行 pid=$(qemu_pid)"; return 0; fi
  [ -f "$IMG" ] || { echo "镜像不存在: $IMG（先跑 build_image.sh）"; exit 2; }
  [ -x "$QEMU" ] || { echo "QEMU 不存在: $QEMU"; exit 2; }
  [ -d "$DD" ]  || { echo "datadir 不存在: $DD"; exit 2; }

  rm -f "$SER_SOCK" "$MON_SOCK" "$SER_LOG" "$QPID" "$RELAY_PID" "$FIFO" "$QEMU_OUT"
  mkfifo "$FIFO"

  # 先起中继（独占串口 socket），再起 QEMU
  python3 "$ROOT/tools/tls_e2e/serial_relay.py" "$SER_SOCK" "$SER_LOG" "$FIFO" \
    >"$RUNTIME/relay.out" 2>&1 &
  echo $! > "$RELAY_PID"

  "$QEMU" -L "$DD" -m 256 -smp 1 -cpu max -no-reboot \
    -drive file="$IMG",format=raw,if=ide,snapshot=on,file.locking=off \
    -display none -vga std \
    -monitor unix:"$MON_SOCK",server,nowait \
    -serial unix:"$SER_SOCK",server,nowait \
    -nic user,model=e1000 \
    -daemonize -pidfile "$QPID" >"$QEMU_OUT" 2>&1 \
    || { echo "[qemu] 启动失败"; cat "$QEMU_OUT"; q_stop; exit 2; }

  echo "[qemu] started pid=$(qemu_pid) img=$IMG"
  echo "[qemu] serial log: $SER_LOG"
}

q_stop() {
  local p; p=$(qemu_pid)
  if [ -n "$p" ]; then
    kill "$p" 2>/dev/null || true
    sleep 1
    kill -9 "$p" 2>/dev/null || true
    rm -f "$QPID"
    echo "[qemu] stopped pid=$p"
  else
    echo "[qemu] 无 pidfile"
  fi
  local r; r=$(relay_pid)
  if [ -n "$r" ]; then
    kill "$r" 2>/dev/null || true
    kill -9 "$r" 2>/dev/null || true
    rm -f "$RELAY_PID"
    echo "[relay] stopped pid=$r"
  fi
  rm -f "$FIFO" "$SER_SOCK" "$MON_SOCK"
}

case "${1:-}" in
  start)       q_start ;;
  stop)        q_stop ;;
  send)        q_send "${2:-}" ;;
  sendkey)     q_send_text "${2:-}" ;;
  wait-for)    q_wait_for "${2:-}" "${3:-90}" ;;
  assert)       shift; q_assert "$@" ;;
  assert-wait)  MAX="${2:-60}"; shift 2; q_assert_wait "$MAX" "$@" ;;
  alive)       q_alive ;;
  *) echo "用法: $0 start|stop|send|sendkey|wait-for|assert|assert-wait|alive [参数]"; exit 2 ;;
esac
