#!/bin/bash
# ============================================================================
# TLS 端到端测试 —— 总驱动脚本（一条命令：证书 → s_server → QEMU → 断言 → 清理）
#
# 用法：
#   bash tools/tls_e2e/run_tls_e2e.sh                # 正向会话（默认）
#   bash tools/tls_e2e/run_tls_e2e.sh negative       # 负向对照（不起 s_server）
#   HEPU_SKIP_BUILD=1 bash tools/tls_e2e/run_tls_e2e.sh
#   HEPU_BOOT_TIMEOUT=90 HEPU_EXPECT_TLS=1 bash tools/tls_e2e/run_tls_e2e.sh
#
# 环境变量：
#   HEPU_SKIP_BUILD=1     复用已有 /tmp 镜像，不重新构建
#   HEPU_BOOT_TIMEOUT=90  等待进桌面/自检标记的秒数
#   HEPU_EXPECT_TLS=1     期望串口出现 TLS12 握手断言（供带自检的客户机构建用；
#                          基线 73753fa 桌面构建不开此开关）
#   HEPU_PORT=4433        s_server 端口
#
# 清理纪律：
#   - 4433 联调整个会话包在 flock /tmp/hepu-tls4433.lock 内，不和其他 TLS 线抢端口；
#   - 退出时按 pidfile 只杀本脚本启动的 qemu / relay / s_server；
#   - 所有运行时产物落在 /tmp/hepu-tls-e2e/，不写 kernel/ 源码。
#
# 已知边界（详见 README.md）：基线 73753fa 的 64 位镜像直接进桌面，没有串口
# shell 提示符，本脚本在基线上只取证“启动 + slirp 出站可达 + 宿主 s_server
# 握手正常”；客户机侧 `tls12 10.0.2.2 4433` 命令要等 TLS 分支在自检构建里
# 加默认关闭的开机探针后，由 HEPU_EXPECT_TLS=1 这条断言链路取证。
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
RUNTIME="${HEPU_TLS_RUNTIME:-/tmp/hepu-tls-e2e}"
PORT="${HEPU_PORT:-4433}"
BOOT_TIMEOUT="${HEPU_BOOT_TIMEOUT:-90}"
EXPECT_TLS="${HEPU_EXPECT_TLS:-0}"
MODE="${1:-positive}"
CERT_DIR="$RUNTIME/certs"

QEMU_SH="$ROOT/tools/tls_e2e/run_qemu_guest.sh"
SRV_SH="$ROOT/tools/tls_e2e/start_s_server.sh"
CERT_SH="$ROOT/tools/tls_e2e/gen_certs.sh"
BUILD_SH="$ROOT/tools/tls_e2e/build_image.sh"

SER_LOG="$RUNTIME/serial.log"
SRV_LOG="$RUNTIME/s_server.log"
SUMMARY="$RUNTIME/summary.txt"

mkdir -p "$RUNTIME"
FAIL=0

log() { echo "[tls-e2e] $*"; }

cleanup() {
  log "清理本实例进程..."
  bash "$QEMU_SH" stop || true
  if [ "$MODE" = "positive" ]; then
    bash "$SRV_SH" stop || true
  fi
}
trap cleanup EXIT

# ---- 0. 端口锁（整段会话持有）----
exec 8>/tmp/hepu-tls4433.lock
flock 8
log "已获取 4433 联调锁 /tmp/hepu-tls4433.lock"

# ---- 1. 证书（幂等重建，打印 SHA-256）----
log "生成/校验证书..."
bash "$CERT_SH"

# ---- 2. 镜像（缺省按需构建，副本落在运行时目录）----
bash "$BUILD_SH"

# ---- 3. 负向模式：不起 s_server，宿主侧验证端口无人监听 ----
if [ "$MODE" = "negative" ]; then
  log "负向对照：不启动 s_server（端口 $PORT 应无人监听）"
  if (echo > "/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then
    log "FAIL: 端口 $PORT 仍可连通（有残留监听？）"
    exit 1
  fi
  log "OK: 127.0.0.1:$PORT 连接被拒（无监听）"
  # 负向模式下启动客户机，仅取证串口；客户机侧命令失败断言待自检构建接入
  bash "$QEMU_SH" start
  bash "$QEMU_SH" wait-for "Desktop running|booting to lock" "$BOOT_TIMEOUT" || true
  exit 0
fi

# ---- 4. 正向：起 s_server（TLS1.2 + -www + 握手细节日志）----
bash "$SRV_SH" start good "$PORT"
if ! (echo > "/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then
  log "FAIL: s_server 端口 $PORT 不可连"
  exit 1
fi
log "宿主侧 s_server 监听 $PORT 正常"

# 宿主侧自握手：证明证书/密钥/TLS1.2 栈本身可完成 ClientHello→ServerHello
log "宿主侧 openssl s_client 自握手验证..."
if timeout 10 openssl s_client -connect "127.0.0.1:$PORT" -tls1_2 -brief </dev/null \
     >>"$RUNTIME/s_client.log" 2>&1; then
  log "OK: 宿主自握手完成（细节见 $RUNTIME/s_client.log）"
else
  log "WARN: 宿主自握手未通过（见 $RUNTIME/s_client.log）"
fi

# ---- 5. 起 QEMU（slirp e1000，串口落盘）----
bash "$QEMU_SH" start

# ---- 6. 等待启动标记 ----
if bash "$QEMU_SH" wait-for "Desktop running|booting to lock" "$BOOT_TIMEOUT"; then
  log "OK: 客户机进入桌面/锁屏阶段"
else
  log "FAIL: 未在 ${BOOT_TIMEOUT}s 内等到启动标记"
  FAIL=1
fi

# ---- 7. 串口断言 ----
# 7a. slirp 出站可达证据（ARP 解析到网关 10.0.2.2 = 0xA000202）
sleep 5
if grep -aqF "0xA000202" "$SER_LOG" 2>/dev/null; then
  log "OK: 串口日志含网关 ARP 解析 10.0.2.2"
else
  log "WARN: 未抓到网关 ARP 解析（slirp 证据不足，不据此判失败）"
fi

# 7b. TLS 握手断言：仅在 HEPU_EXPECT_TLS=1 时启用
if [ "$EXPECT_TLS" = "1" ]; then
  log "HEPU_EXPECT_TLS=1：等待 TLS12 ServerHello 断言..."
  bash "$QEMU_SH" assert-wait 30 \
    "[TLS12] sent ClientHello" \
    "[TLS12] ServerHello: version=0x0303" || FAIL=1
else
  log "基线模式：不要求客户机 TLS 握手断言（桌面构建无串口 shell）"
  log "  串口 TLS12 标记计数: $(grep -ac 'TLS12' "$SER_LOG" 2>/dev/null || true)"
fi

# ---- 8. 汇总 ----
{
  echo "==== TLS E2E 汇总 $(date '+%F %T') ===="
  echo "MODE=$MODE PORT=$PORT EXPECT_TLS=$EXPECT_TLS"
  echo "镜像: $RUNTIME/hepu64.img"
  echo "串口日志: $SER_LOG"
  echo "s_server 日志: $SRV_LOG"
  echo "s_client 日志: $RUNTIME/s_client.log"
  echo "--- 证书 SHA-256 ---"
  for f in ca.crt server.crt server-bad.crt; do
    [ -f "$CERT_DIR/$f" ] || continue
    fp=$(openssl x509 -in "$CERT_DIR/$f" -fingerprint -sha256 -noout | sed 's/.*=//')
    echo "  $CERT_DIR/$f  $fp"
  done
  echo "--- 串口关键计数 ---"
  echo "  hepu@os prompt 行数: $(grep -ac 'hepu@os' "$SER_LOG" 2>/dev/null || true)"
  echo "  TLS12 标记行数: $(grep -ac 'TLS12' "$SER_LOG" 2>/dev/null || true)"
  echo "  ARP 网关 0xA000202: $(grep -ac '0xA000202' "$SER_LOG" 2>/dev/null || true)"
} | tee "$SUMMARY"

[ "$FAIL" -eq 0 ] && log "TLS-E2E: 本阶段通过（基线桌面构建，客户机 TLS 命令未驱动）" \
                  || { log "TLS-E2E: FAIL"; exit 1; }
