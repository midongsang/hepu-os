#!/bin/bash
# Hepu OS 快速启动验证脚本 — 打包 + 代理 + QEMU + 截图 一键完成
# 用法: bash tools/run_hepu.sh [wait_seconds]
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WAIT="${1:-110}"

echo "[1/5] 打包镜像..."
bash "$ROOT/tools/make_single_img64.sh" >/dev/null 2>&1
cp /tmp/hepu64_single.img /tmp/hepu64_run.img 2>/dev/null || { echo "打包失败"; exit 1; }

echo "[2/5] 重建 datadir..."
mkdir -p /tmp/hepu_qemu_dd
cp "$ROOT/tools/firmware/"* "$ROOT/tools/fwroms/"* /tmp/hepu_qemu_dd/ 2>/dev/null

echo "[3/5] 启动 NAT 代理..."
pkill -f "[m]ini_nat_proxy" 2>/dev/null; sleep 1
nohup python3 "$ROOT/tools/mini_nat_proxy.py" >/tmp/nat_console.log 2>&1 &
sleep 2
pgrep -f "[m]ini_nat" >/dev/null && echo "    代理 OK" || echo "    代理 FAIL"

echo "[4/5] 启动 QEMU..."
pkill -f "[q]emu-syste[m]" 2>/dev/null; sleep 1
rm -f /tmp/qemu_wx9.log /tmp/hmon.sock
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}:$ROOT/tools/qemu_libs/usr/lib/x86_64-linux-gnu"
nohup "$ROOT/tools/bin/qemu-system-x86_64" -L /tmp/hepu_qemu_dd -m 256 \
  -drive file=/tmp/hepu64_run.img,format=raw -display none -vga std \
  -monitor unix:/tmp/hmon.sock,server,nowait \
  -serial file:/tmp/qemu_wx9.log \
  -netdev socket,id=n1,connect=127.0.0.1:4000 -device e1000,netdev=n1 \
  -cpu max >/tmp/qemu_wx9.out 2>&1 &
echo "    QEMU bg pid=$!"

echo "[5/5] 等待 $WAIT 秒后截图..."
sleep "$WAIT"
echo "screendump /tmp/s0.ppm" | nc -U /tmp/hmon.sock
sleep 2
convert /tmp/s0.ppm /tmp/s0.png 2>/dev/null && echo "截图 OK: /tmp/s0.png" || echo "截图失败(monitor 未就绪?)"
grep -a "WX\|DESKTOP64\|EXCEPTION" /tmp/qemu_wx9.log | tail -5
echo "完成。"
