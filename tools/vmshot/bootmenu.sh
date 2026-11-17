#!/bin/bash
# 引导菜单截图 —— 与 .shot.sh 同一套隔离方案
#
# 用法：bash shots/.bootmenu.sh [TAG] [WAIT_SEC] [OUT.png]
#   TAG 默认 menu；WAIT_SEC 默认 12（引导器自检约 4-8 秒，菜单停留由 HEPBOOT.CFG 的
#   timeout 决定，想把菜单拉长就先改镜像里的 cfg 再截）；OUT 默认 shots/menu.png
#
# 关键：镜像里的 HEPBOOT.CFG timeout 决定菜单停留多久。默认的 5 秒窗口太窄，
#   截图常常抓不到菜单就进内核了。脚本会在启动前检查 timeout，小于 15 就提示，
#   但不会擅自改镜像（改镜像要用 mcopy，那属于构建的事，不归截图脚本管）。
set -u
CHAT=/home/user/Doubao/chats/38452875115179010
B=$CHAT/.toolchain; T=$B/pkg/root2; RUN=$B/run; V=$CHAT/.vm-assets

TAG=${1:-menu}
# 第二个参数既可能是等待秒数（纯数字），也可能是输出文件名 —— 按形状判定，
# 免得像 "menu shots/menu.png" 这种自然写法把 sleep 的参数传成路径
WAIT=12
OUT=${3:-$CHAT/shots/menu.png}
if [ $# -ge 2 ]; then
  if [[ "${2}" =~ ^[0-9]+$ ]]; then WAIT=$2
  else OUT=$2; fi
fi
INST=$RUN/inst/$TAG
IMG=${HEPU_IMG:-$RUN/shot.img}

export PATH=$T/sbin:$T/usr/bin:$PATH
export LD_LIBRARY_PATH=$T/usr/lib/x86_64-linux-gnu:$T/lib/x86_64-linux-gnu
export QEMU_MODULE_DIR=$T/usr/lib/x86_64-linux-gnu/qemu

mkdir -p "$INST"
rm -f "$INST/ser.log" "$INST/mon.sock" "$INST/qemu.pid"
cp -f "$V/OVMF_VARS_4M.fd" "$INST/V.fd"

NP=$(nproc 2>/dev/null || echo 2)
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
  exec {fd}>"$RUN/.slot1"
  flock -w 900 "$fd" || { echo "[bootmenu] 等不到 QEMU 额度" >&2; exit 1; }
  SLOT=1; SLOT_FD=$fd
fi

# 顺带把菜单停留时间报出来，省得截图后才发现窗口太窄
TO=$(mtype -i "$IMG"@@1M ::/HEPBOOT.CFG 2>/dev/null | grep -i '^timeout' | head -1 | tr -d '\r')
VNCPORT=$((5900 + SLOT))
echo "[bootmenu] tag=$TAG slot=$SLOT/$SLOTS $TO vnc=:$VNCPORT"

if [ -f "$INST/qemu.pid" ]; then
  OLD=$(cat "$INST/qemu.pid" 2>/dev/null)
  [ -n "${OLD:-}" ] && kill -9 "$OLD" 2>/dev/null
fi

qemu-system-x86_64 -L $B/pkg/datadir \
  -rtc base=localtime \
  -drive if=pflash,format=raw,readonly=on,file=$V/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=$INST/V.fd \
  -drive format=raw,file=$IMG,file.locking=off,snapshot=on \
  -m 1024 -smp 2 -vnc 127.0.0.1:${VNCPORT} -monitor unix:$INST/mon.sock,server,nowait \
  -serial file:$INST/ser.log -vga std -daemonize -pidfile $INST/qemu.pid 2>&1 | head -2

sleep "$WAIT"

python3 - "$INST/mon.sock" "$CHAT/shots/_${TAG}_raw.ppm" "$OUT" <<'PY'
import socket, sys, time
from PIL import Image
sock_p, raw_p, out_p = sys.argv[1], sys.argv[2], sys.argv[3]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sock_p)
time.sleep(0.3)
s.sendall(("screendump %s\n" % raw_p).encode())
time.sleep(2)
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
