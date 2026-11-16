#!/bin/bash
# 32 位图形验证：起 QEMU + qmp screendump + 非黑像素统计
# 用法: tools/check32.sh <VNC端口> <输出png绝对路径> [等待秒数]
PORT=$1; OUT=$2; SEC=${3:-95}
CHAT=/home/user/Doubao/chats/38452875115179010
T=$CHAT/.toolchain/pkg/root2
export LD_LIBRARY_PATH=$T/usr/lib/x86_64-linux-gnu:$T/lib/x86_64-linux-gnu
export QEMU_MODULE_DIR=$T/usr/lib/x86_64-linux-gnu/qemu
pkill -9 -x qemu-system-i386 2>/dev/null; sleep 1
rm -f /tmp/c32.qmp /tmp/c32.ser
# -L 必带: 否则 QEMU 找不到 BIOS 直接退出（串口 0 行）
# -qmp 而非 -monitor: 本环境 QEMU monitor socket 不响应
$T/usr/bin/qemu-system-i386 -L $CHAT/.toolchain/pkg/datadir \
  -kernel $CHAT/hepu-os/build_v250/kernel.elf -m 256 -display none \
  -vnc :$PORT -qmp unix:/tmp/c32.qmp,server,nowait \
  -serial file:/tmp/c32.ser -no-reboot >/dev/null 2>&1 &
sleep $SEC
python3 - "$OUT" <<'PY'
import socket,json,time,sys
from PIL import Image
out=sys.argv[1]
s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM); s.settimeout(20)
s.connect("/tmp/c32.qmp"); time.sleep(0.5)
try: s.recv(4096)
except Exception: pass
for c in [{"execute":"qmp_capabilities"},{"execute":"screendump","arguments":{"filename":out}}]:
    s.sendall((json.dumps(c)+"\n").encode()); time.sleep(1.2)
    try: s.recv(8192)
    except Exception: pass
im=Image.open(out).convert('RGB')
print("NONBLACK=", sum(1 for p in im.getdata() if p!=(0,0,0)))
im.resize((1280,720)).save(out.replace('.png','_view.png'))
PY
pkill -9 -x qemu-system-i386 2>/dev/null
