#!/bin/bash
# 64 位桌面截图 —— 参数化、实例隔离、镜像只读共享版本
#
# 用法：bash shots/.shot.sh [TAG] [OUT.png]
#   TAG 默认为 d64；OUT 默认为 shots/desktop.png（兼容旧脚本的产物名）
#
# 为什么要隔离（2026-11-17）：
#   原来所有任务共用 $RUN/shot.img、ser_kill.log、mon_kill.sock、qemu_kill.pid 四个固定名字，
#   于是只能靠一把全局 device.lock 串行化，几个 agent 一排队就是十几分钟。
#   现在每个 TAG 有独立目录，镜像用 snapshot=on 只读共享（写进 QEMU 私有 overlay），
#   互相之间不再有任何可变文件冲突。
#
# 并发额度：TCG 软件模拟下每个 -smp 2 实例就吃满 2 核，这台机器 nproc=2、无 KVM，
#   所以默认额度 1（等价于原来的串行，但不再需要全局锁）；多核机器自动放宽到最多 3。
#   额度用 flock 的 slot 文件实现，等不到就阻塞排队，不会互相拖慢到不可用。
set -u
CHAT=/home/user/Doubao/chats/38452875115179010
B=$CHAT/.toolchain; T=$B/pkg/root2; RUN=$B/run; V=$CHAT/.vm-assets

TAG=${1:-d64}
OUT=${2:-$CHAT/shots/desktop.png}
INST=$RUN/inst/$TAG
IMG=${HEPU_IMG:-$RUN/shot.img}

export PATH=$T/sbin:$T/usr/bin:$PATH
export LD_LIBRARY_PATH=$T/usr/lib/x86_64-linux-gnu:$T/lib/x86_64-linux-gnu
export QEMU_MODULE_DIR=$T/usr/lib/x86_64-linux-gnu/qemu

mkdir -p "$INST"
rm -f "$INST/ser.log" "$INST/mon.sock" "$INST/qemu.pid"
cp -f "$V/OVMF_VARS_4M.fd" "$INST/V.fd"

# ---- 并发额度 ----
NP=$(nproc 2>/dev/null || echo 2)
# 每个实例 -smp 2 会占满 2 个核（TCG 下 vCPU 是并行翻译线程），
# 所以额度按 nproc/2 算：2 核 = 1（等价串行，但已不再需要全局锁），
# 8 核 = 4 但封顶 3，避免内存和 IO 一起挤爆。
SLOTS=${HEPU_SLOTS:-$(( NP / 2 ))}
[ "$SLOTS" -gt 3 ] && SLOTS=3
[ "$SLOTS" -lt 1 ] && SLOTS=1
SLOT=""
for i in $(seq 1 "$SLOTS"); do
  exec {fd}>"$RUN/.slot$i"
  if flock -n "$fd"; then SLOT=$i; SLOT_FD=$fd; break; fi
  exec {fd}>&-
done
if [ -z "$SLOT" ]; then
  # 全部占满：老实排队等第 1 个
  exec {fd}>"$RUN/.slot1"
  flock -w 900 "$fd" || { echo "[shot] 等不到 QEMU 额度，放弃" >&2; exit 1; }
  SLOT=1; SLOT_FD=$fd
fi
echo "[shot] tag=$TAG slot=$SLOT/$SLOTS img=$IMG"

# 只清自己这个 tag 上一次留下的实例，绝不 pkill -x 全局（那会杀掉别的 agent）
if [ -f "$INST/qemu.pid" ]; then
  OLD=$(cat "$INST/qemu.pid" 2>/dev/null)
  [ -n "${OLD:-}" ] && kill -9 "$OLD" 2>/dev/null
fi

qemu-system-x86_64 -L $B/pkg/datadir \
  -rtc base=localtime \
  -drive if=pflash,format=raw,readonly=on,file=$V/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=$INST/V.fd \
  -drive format=raw,file=$IMG,file.locking=off,snapshot=on \
  -m 1024 -smp 2 -display none -monitor unix:$INST/mon.sock,server,nowait \
  -serial file:$INST/ser.log \
  -device qemu-xhci -device usb-kbd -device usb-mouse -vga std \
  -nic user,model=e1000 -daemonize -pidfile $INST/qemu.pid 2>&1 | head -2

for i in $(seq 1 60); do
  sleep 1
  grep -aq 'hs_window\|frame=' "$INST/ser.log" 2>/dev/null && break
done
sleep 6

python3 - "$INST/mon.sock" "$CHAT/shots/_${TAG}_raw.ppm" "$OUT" <<'PY'
import socket, sys, time
from PIL import Image
sock_p, raw_p, out_p = sys.argv[1], sys.argv[2], sys.argv[3]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sock_p)
time.sleep(0.3)
s.sendall(("screendump %s\n" % raw_p).encode())
time.sleep(3)
try: s.recv(4096)
except Exception: pass
s.close()
im = Image.open(raw_p)
im.save(out_p)
print("W,H =", im.size)
PY

if [ -f "$INST/qemu.pid" ]; then
  P=$(cat "$INST/qemu.pid" 2>/dev/null)
  [ -n "${P:-}" ] && kill -9 "$P" 2>/dev/null
fi
exec {SLOT_FD}>&-
exit 0
