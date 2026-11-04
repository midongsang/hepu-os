#!/bin/bash
# V0.13.8 P6: 鼠标点击自动化测试 — HMP(保持连接键盘+截图) + QMP(鼠标绝对定位点击)
set -u
PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TEST_NAME="${1:-p6_mouse}"
KEYSEQ="${2:-spc s ret}"
CLICK_X="${3:-}"
CLICK_Y="${4:-}"
CLICK2_X="${5:-}"
CLICK2_Y="${6:-}"
QEMU_BIN="$PROJECT_ROOT/tools/bin/qemu-system-x86_64"
BIOS="$PROJECT_ROOT/tools/firmware/bios-256k.bin"
HMP_SOCK="/tmp/hmp_mouse_$$.sock"
QMP_SOCK="/tmp/qmp_mouse_$$.sock"
SERIAL_LOG="/tmp/qemu_mouse_$$.log"
RESULT_DIR="$PROJECT_ROOT/test_results"
mkdir -p "$RESULT_DIR"

echo "[1/4] 构建单镜像..."
cd "$PROJECT_ROOT" && bash tools/make_single_img64.sh >/dev/null 2>&1 || { echo "❌ 镜像失败"; exit 1; }
echo "✅ 构建成功"
echo "[2/4] 启动QEMU (usb-tablet 绝对鼠标)..."
rm -f "$HMP_SOCK" "$QMP_SOCK" "$SERIAL_LOG"
"$QEMU_BIN" -L "$PROJECT_ROOT/tools/fwroms" -m 128 -smp 1 -bios "$BIOS" \
    -drive format=raw,file=/tmp/hepu64_single.img -vga std -display none \
    -usb -device usb-kbd -device usb-tablet \
    -device AC97 \
    -monitor "unix:$HMP_SOCK,server,nowait" \
    -qmp "unix:$QMP_SOCK,server,nowait" \
    -serial "file:$SERIAL_LOG" &
QPID=$!
sleep 7
if ! kill -0 $QPID 2>/dev/null; then echo "❌ QEMU崩溃"; tail -5 "$SERIAL_LOG"; exit 1; fi

echo "[3/4] 键盘: $KEYSEQ"
python3 - "$HMP_SOCK" "$KEYSEQ" "$RESULT_DIR/$TEST_NAME" <<'PYEOF'
import socket, time, sys
sock, keyseq, name = sys.argv[1], sys.argv[2].split(), sys.argv[3]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sock)
s.settimeout(1)
try: s.recv(4096)
except Exception: pass
for k in keyseq:
    s.send(('sendkey ' + k + '\n').encode())
    time.sleep(0.8)
time.sleep(1.5)
PYEOF

# ---- V0.13.8 P6: 自动定位模式 (窗口位置每次不同, 截图定位后点击) ----
resolve_click() {
  local cx="$1" cy="$2"
  case "$cx" in
    auto_menu|auto_cb|auto_close|auto_launcher)
      AUTO_OUT=$(python3 "$PROJECT_ROOT/tools/hepu_clickauto.py" "$HMP_SOCK" "$RESULT_DIR/$TEST_NAME" "$cx" "$cy")
      cx=$(echo "$AUTO_OUT" | cut -d' ' -f1)
      cy=$(echo "$AUTO_OUT" | cut -d' ' -f2)
      echo "[定位] 点击点: ($cx,$cy)";;
  esac
  [ "$cx" != "0" ] && [ -n "$cx" ] && {
    echo "[4/4] 鼠标点击 ($cx,$cy)"
    python3 - "$cx" "$cy" "$QMP_SOCK" <<'PYEOF'
import socket, sys, time
x, y, path = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(path)
def send(cmd):
    s.sendall((cmd+'\n').encode()); time.sleep(0.05)
s.sendall(b'{"execute":"qmp_capabilities"}\n'); s.recv(4096)
send('{"execute":"input-send-event","arguments":{"events":[{"type":"abs","data":{"axis":"x","value":%d}},{"type":"abs","data":{"axis":"y","value":%d}}]}}' % (x,y))
time.sleep(0.4)
send('{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"button":"left","down":true}}]}}')
time.sleep(0.3)
send('{"execute":"input-send-event","arguments":{"events":[{"type":"btn","data":{"button":"left","down":false}}]}}')
time.sleep(1.0)
s.close()
PYEOF
  }
}

resolve_click "$CLICK_X" "$CLICK_Y"

if [ -n "$CLICK2_X" ]; then
  resolve_click "$CLICK2_X" "$CLICK2_Y"
  sleep 0.5
fi

python3 - "$HMP_SOCK" "$RESULT_DIR/$TEST_NAME" <<'PYEOF'
import socket, time, sys
sock, name = sys.argv[1], sys.argv[2]
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sock)
s.settimeout(1)
try: s.recv(4096)
except Exception: pass
s.send(b'screendump ' + name.encode() + b'.ppm\n')
time.sleep(2.5)
s.close()
PYEOF
python3 -c "from PIL import Image; Image.open('$RESULT_DIR/$TEST_NAME.ppm').save('$RESULT_DIR/$TEST_NAME.png')" 2>/dev/null && echo "✅ 截图: $RESULT_DIR/$TEST_NAME.png"
kill $QPID 2>/dev/null
echo "DONE"
