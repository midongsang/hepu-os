#!/bin/bash
# ============================================================================
# TLS 端到端测试 —— openssl s_server 启动/停止脚本
#
# 用法：
#   bash tools/tls_e2e/start_s_server.sh start [good|bad] [port]
#   bash tools/tls_e2e/start_s_server.sh stop
#   bash tools/tls_e2e/start_s_server.sh status
#
# 行为：
#   - -accept 4433（默认）-www（s_server 自带最小 HTTP 响应）-tls1_2 显式限定；
#     不限定的话 OpenSSL 3.0 默认先谈 TLS1.3，而客户机切片只按 1.2 解析。
#   - -msg 打开握手细节输出，日志落 /tmp/hepu-tls-e2e/s_server.log。
#   - pidfile 记录本脚本启动的 s_server，stop 只杀这个 pid，不误伤他人进程。
# ============================================================================
set -euo pipefail

RUNTIME="${HEPU_TLS_RUNTIME:-/tmp/hepu-tls-e2e}"
CERT_DIR="${HEPU_TLS_CERT_DIR:-$RUNTIME/certs}"
PORT="${3:-4433}"
MODE="${2:-good}"
LOG="$RUNTIME/s_server.log"
PIDFILE="$RUNTIME/s_server.pid"

mkdir -p "$RUNTIME"

srv_pid() { [ -f "$PIDFILE" ] && cat "$PIDFILE" 2>/dev/null || true; }

status() {
  local p; p=$(srv_pid)
  if [ -n "$p" ] && kill -0 "$p" 2>/dev/null; then
    echo "s_server RUNNING pid=$p port=$MODE:$PORT"
    return 0
  fi
  echo "s_server STOPPED"
  return 1
}

start() {
  if status >/dev/null 2>&1; then
    echo "s_server 已在运行 (pid=$(srv_pid))"
    return 0
  fi
  case "$MODE" in
    good) CERT="$CERT_DIR/server.crt"; KEY="$CERT_DIR/server.key" ;;
    bad)  CERT="$CERT_DIR/server-bad.crt"; KEY="$CERT_DIR/server-bad.key" ;;
    *) echo "未知 MODE=$MODE（应为 good|bad）"; exit 2 ;;
  esac
  [ -f "$CERT" ] || { echo "缺少证书 $CERT，请先跑 gen_certs.sh"; exit 2; }
  [ -f "$KEY" ]  || { echo "缺少私钥 $KEY"; exit 2; }

  : > "$LOG"
  echo "[s_server] $(date '+%F %T') start mode=$MODE port=$PORT cert=$CERT" >>"$LOG"

  # -www: 自带最小 HTTP 响应；-msg: 握手消息细节；-tls1_2: 限定协议版本
  nohup openssl s_server \
    -accept "0.0.0.0:$PORT" \
    -www \
    -tls1_2 \
    -cert "$CERT" \
    -key "$KEY" \
    -msg \
    >>"$LOG" 2>&1 &
  echo $! > "$PIDFILE"

  # 等待监听就绪（最多 10s）
  local i
  for i in $(seq 1 10); do
    if (echo > "/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then
      echo "[s_server] listening on $PORT (pid=$(srv_pid))"
      echo "[s_server] log: $LOG"
      return 0
    fi
    sleep 1
  done
  echo "[s_server] 监听超时，日志尾部："
  tail -20 "$LOG" || true
  stop
  exit 1
}

stop() {
  local p; p=$(srv_pid)
  if [ -n "$p" ]; then
    kill "$p" 2>/dev/null || true
    sleep 1
    kill -9 "$p" 2>/dev/null || true
    rm -f "$PIDFILE"
    echo "[s_server] stopped pid=$p"
  else
    echo "[s_server] 无 pidfile，未清理"
  fi
}

case "${1:-status}" in
  start)  start ;;
  stop)   stop ;;
  status) status ;;
  *) echo "用法: $0 start [good|bad] [port] | stop | status"; exit 2 ;;
esac
