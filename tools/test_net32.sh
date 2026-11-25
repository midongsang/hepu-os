#!/bin/bash
# B-4 32 位网络回归：DHCP -> ifconfig -> ICMP -> TCP 三次握手。
# 依赖 build_v250/kernel.elf 已构建；QEMU user-net 的 10.0.2.2 指向宿主机。
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC="${HEPU_TOOLCHAIN:-/home/user/Doubao/chats/38452875115179010/.toolchain/pkg/root2}"
DD="${HEPU_DATADIR:-/home/user/Doubao/chats/38452875115179010/.toolchain/pkg/datadir}"
PORT="${1:-19091}"
SER=/tmp/hepu_net32_ser.log
MON=/tmp/hepu_net32_mon.sock
QPID=/tmp/hepu_net32_qemu.pid
HOSTLOG=/tmp/hepu_net32_host.log
LISTENER_PID=""
fail=0

export LD_LIBRARY_PATH="$TC/usr/lib/x86_64-linux-gnu:$TC/lib/x86_64-linux-gnu"
export QEMU_MODULE_DIR="$TC/usr/lib/x86_64-linux-gnu/qemu"

cleanup() {
    if [ -f "$QPID" ]; then kill -9 "$(cat "$QPID")" 2>/dev/null || true; fi
    if [ -n "$LISTENER_PID" ]; then kill -9 "$LISTENER_PID" 2>/dev/null || true; fi
    wait "$LISTENER_PID" 2>/dev/null || true
}
trap cleanup EXIT

[ -f "$ROOT/build_v250/kernel.elf" ] || { echo "缺少 build_v250/kernel.elf"; exit 2; }

rm -f "$SER" "$MON" "$QPID" "$HOSTLOG"
python3 - "$HOSTLOG" "$PORT" <<'PY' &
import socket,sys,datetime
log=open(sys.argv[1],"w")
def w(m):
    log.write("%s %s\n"%(datetime.datetime.now().strftime("%H:%M:%S"),m)); log.flush()
srv=socket.socket(); srv.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
srv.bind(("127.0.0.1",int(sys.argv[2]))); srv.listen(2); srv.settimeout(120)
w("listener up on %s" % sys.argv[2])
try:
    c,a=srv.accept(); w("accepted %s" % (a,)); c.settimeout(3)
    try: w("recv %r" % c.recv(256))
    except Exception as e: w("recv %s" % e)
    c.close()
except Exception as e: w("listener error %s" % e)
srv.close()
PY
LISTENER_PID=$!

"$TC/usr/bin/qemu-system-i386" -L "$DD" -kernel "$ROOT/build_v250/kernel.elf" \
  -append "hepu.graphics=0" -m 256 -smp 1 -display none \
  -monitor unix:"$MON",server,nowait -serial file:"$SER" -no-reboot \
  -nic user,model=e1000 -daemonize -pidfile "$QPID" || { echo "QEMU 启动失败"; exit 2; }

prompt=0
for i in $(seq 1 70); do
  if grep -aq "hepu@os:~" "$SER" 2>/dev/null; then prompt=1; break; fi
  if [ -f "$QPID" ] && ! kill -0 "$(cat "$QPID")" 2>/dev/null; then break; fi
  sleep 1
done
if [ "$prompt" -eq 0 ]; then echo "FAIL: 未看到 shell 提示符"; exit 1; fi
sleep 6

send_text() {
    local TXT="$1" CH K
    for (( i=0; i<${#TXT}; i++ )); do
        CH=${TXT:i:1}
        case "$CH" in
            " ") K=spc;;
            ".") K=dot;;
            *) K="$CH";;
        esac
        python3 - "$MON" "$K" <<'PY'
import socket,time,sys
s=socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); time.sleep(0.12)
s.sendall(("sendkey %s\n"%sys.argv[2]).encode()); time.sleep(0.10); s.close()
PY
    done
    python3 - "$MON" <<'PY'
import socket,time,sys
s=socket.socket(socket.AF_UNIX); s.connect(sys.argv[1]); time.sleep(0.15)
s.sendall(b"sendkey ret\n"); time.sleep(0.12); s.close()
PY
}

echo "=== dhcp ==="; send_text "dhcp"; sleep 15
echo "=== ifconfig ==="; send_text "ifconfig"; sleep 3
echo "=== ping ==="; send_text "ping 10.0.2.2"; sleep 14
echo "=== tcp ==="; send_text "tcp connect 10 0 2 2 $PORT"; sleep 10

check() {
    if ! grep -aqF "$1" "$SER"; then echo "  FAIL: 缺少 $1"; fail=1; else echo "  OK: $1"; fi
}
check "DHCP configuration complete"
check "UP"
check "[IP] ARP resolved"
check "[PING] reply"
check "[TCP] connect established"
if ! grep -aqF "accepted" "$HOSTLOG"; then echo "  FAIL: 主机未 accept"; fail=1; else echo "  OK: 主机 accept"; fi
if [ "$fail" -ne 0 ]; then
    echo "NET32: FAIL"; grep -aE "ARP|ICMP|PING|TCP|FAIL" "$SER" | tail -25
    exit 1
fi
echo "NET32: PASS"
