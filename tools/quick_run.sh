#!/bin/bash
# 快速重建并启动 Hepu OS 测试环境
# 运行时文件放在项目 .run/ 目录(避免 /tmp 被环境清理)
# 用法: bash tools/quick_run.sh [wait_sec]
set -u
cd "$(dirname "$0")/.."
ROOT="$PWD"
RUN="$ROOT/.run"
mkdir -p "$RUN"

echo "[1/4] 构建镜像..."
bash tools/make_single_img64.sh >/dev/null 2>&1
cp /tmp/hepu64_single.img "$RUN/hepu64_run.img"
echo "  镜像 OK"

echo "[2/4] 重建 datadir..."
rm -rf "$RUN/datadir" && mkdir -p "$RUN/datadir"
cp tools/firmware/* tools/fwroms/* "$RUN/datadir/" 2>/dev/null
echo "  datadir OK"

echo "[3/4] 启动 NAT 代理..."
pkill -f mini_nat_proxy >/dev/null 2>&1
sleep 1
(nohup python3 tools/mini_nat_proxy.py >"$RUN/proxy.log" 2>&1 &)
sleep 1
echo "  proxy OK (port 4000)"

echo "[4/4] 启动 QEMU..."
pkill -f "[q]emu-syste[m]" >/dev/null 2>&1
sleep 2
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}:$ROOT/tools/qemu_libs/usr/lib/x86_64-linux-gnu"
rm -f "$RUN/qemu_wx9.log" "$RUN/hmon.sock"
nohup $ROOT/tools/bin/qemu-system-x86_64 -L "$RUN/datadir" -m 256 \
  -drive file="$RUN/hepu64_run.img",format=raw,snapshot=on -display none -vga std \
  -monitor unix:"$RUN/hmon.sock",server,nowait \
  -serial file:"$RUN/qemu_wx9.log" \
  -netdev socket,id=n1,connect=127.0.0.1:4000 -device e1000,netdev=n1 \
  -cpu max >"$RUN/qemu_wx9.out" 2>&1 &
echo "  QEMU 已启动 (等待 ${1:-100}s 进入桌面)"
echo "完成: printf 'screendump \$RUN/x.ppm\\n' | timeout 8 nc -U \$RUN/hmon.sock"
