#!/bin/bash
# ============================================================================
# TLS 端到端测试 —— 64 位镜像构建（基线内核，不改 kernel/ 源码）
#
# 用法：
#   bash tools/tls_e2e/build_image.sh            # 缺镜像才构建
#   HEPU_REBUILD=1 bash tools/tls_e2e/build_image.sh   # 强制重建
#   HEPU_SKIP_BUILD=1 bash tools/tls_e2e/build_image.sh# # 只复用已有镜像
#
# 并发纪律：
#   - /tmp/hepu64_single.img 与 /tmp/hepu64_build 是全机固定路径，用
#     flock /tmp/hepu-img64.lock 包住整个“构建 + 出图”段；
#   - 出图后立刻把镜像复制到本测试运行时目录 $RUNTIME/hepu64.img，
#     QEMU 只跑这个副本，不直接占用 /tmp 上的固定镜像。
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
RUNTIME="${HEPU_TLS_RUNTIME:-/tmp/hepu-tls-e2e}"
IMG="$RUNTIME/hepu64.img"
mkdir -p "$RUNTIME"

if [ "${HEPU_SKIP_BUILD:-0}" = "1" ]; then
  [ -f "$IMG" ] || { echo "HEPU_SKIP_BUILD=1 但镜像不存在: $IMG"; exit 2; }
  echo "[build] 复用已有镜像: $IMG"
  exit 0
fi

if [ -f "$IMG" ] && [ "${HEPU_REBUILD:-0}" != "1" ]; then
  echo "[build] 镜像已存在: $IMG（HEPU_REBUILD=1 可强制重建）"
  exit 0
fi

exec 9>/tmp/hepu-img64.lock
flock 9

echo "[build] $(date '+%F %T') 获取镜像构建锁，开始 stage2"
make -C "$ROOT/tools/hepboot/x86_64" >"$RUNTIME/build_stage2.log" 2>&1

echo "[build] $(date '+%F %T') 构建 64 位内核"
make -C "$ROOT/kernel/arch/x86_64" clean >/dev/null 2>&1 || true
make -C "$ROOT/kernel/arch/x86_64" -j2 >"$RUNTIME/build_kernel.log" 2>&1

echo "[build] $(date '+%F %T') 制作单镜像"
bash "$ROOT/tools/make_single_img64.sh" >"$RUNTIME/build_img.log" 2>&1

cp /tmp/hepu64_single.img "$IMG"
echo "[build] $(date '+%F %T') 镜像就绪: $IMG ($(stat -c%s "$IMG") 字节)"
