#!/bin/bash
# 录制系统启动：QMP 循环 screendump -> PNG 帧 -> ffmpeg 按真实时间戳合成
#
# 用法: bash shots/.record.sh <TAG> [DUR_SEC] [FPS] [OUT.mp4]
#
# 为什么不用 VNC: 这台机器没有 X，装 vnc2mpg 一类工具不现实；QEMU 本身没有内建录屏。
# screendump 是唯一不依赖外部显示的抓帧通道，启动过程帧率要求低，够用。
# 关键点是按**真实时间戳**合成(concat + duration)，避免 TCG 下抓帧不稳导致视频快放。
set -u
CHAT=/home/user/Doubao/chats/38452875115179010
B=$CHAT/.toolchain; T=$B/pkg/root2; RUN=$B/run; V=$CHAT/.vm-assets

TAG=${1:-rec}
DUR=${2:-45}
FPS=${3:-4}
OUT=${4:-$CHAT/shots/boot_$TAG.mp4}
INST=$RUN/inst/$TAG
IMG=${HEPU_IMG:-$RUN/shot.img}

export PATH=$T/sbin:$T/usr/bin:$PATH
export LD_LIBRARY_PATH=$T/usr/lib/x86_64-linux-gnu:$T/lib/x86_64-linux-gnu
export QEMU_MODULE_DIR=$T/usr/lib/x86_64-linux-gnu/qemu

mkdir -p "$INST" "$INST/frames"
rm -rf "$INST/frames"; mkdir -p "$INST/frames"
rm -f "$INST/qmp.sock" "$INST/ser.log" "$INST/qemu.pid"
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
  flock -w 900 "$fd" || { echo "[record] 等不到 QEMU 额度" >&2; exit 1; }
  SLOT=1; SLOT_FD=$fd
fi
# 必须有真实 display 后端：-display none 时 QEMU 的 VGA surface 永远停在固件默认
# 640x480，screendump 抓出来的也是 640x480 —— 尽管 Guest 内部已经是 1920x1080
# (引导器日志里有 [GFX64] Mode set: 1920x1080 fb=0x80000000)。挂 VNC 之后 surface
# 才跟随 VGA 模式变化，screendump 才会是真实分辨率。
VNCPORT=$((5900 + SLOT))
echo "[record] tag=$TAG slot=$SLOT/$SLOTS dur=${DUR}s target=${FPS}fps vnc=:$VNCPORT -> $OUT"

qemu-system-x86_64 -L $B/pkg/datadir \
  -rtc base=localtime \
  -drive if=pflash,format=raw,readonly=on,file=$V/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=$INST/V.fd \
  -drive format=raw,file=$IMG,file.locking=off,snapshot=on \
  -m 1024 -smp 2 -vnc 127.0.0.1:${VNCPORT} \
  -qmp unix:$INST/qmp.sock,server,nowait \
  -serial file:$INST/ser.log \
  -device qemu-xhci -device usb-kbd -device usb-mouse -vga std \
  -nic user,model=e1000 -daemonize -pidfile $INST/qemu.pid 2>&1 | head -2

# 等 QMP 起来
for i in $(seq 1 30); do [ -S "$INST/qmp.sock" ] && break; sleep 1; done

python3 - "$INST" "$DUR" "$FPS" <<'PY'
import json, os, socket, sys, time
from PIL import Image

inst, dur, fps = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
sock_path = os.path.join(inst, "qmp.sock")
frames_dir = os.path.join(inst, "frames")
raw = os.path.join(inst, "frame.ppm")

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sock_path)
f = s.makefile("rwb")

def recv():
    line = f.readline()
    if not line:
        raise RuntimeError("qmp closed")
    return json.loads(line)

recv()
f.write(b'{"execute":"qmp_capabilities"}\n'); f.flush(); recv()

def grab():
    """抓一帧。QEMU 的 screendump 是异步写盘，而且分辨率切换(640x480 -> 1920x1080)
    的那一瞬间会抓到写了一半的 PPM(PIL 报 truncated)。所以: 等大小连续两次不变,
    再真正 decode; decode 失败就重新抓一次, 最多三轮。"""
    for attempt in range(3):
        try:
            if os.path.exists(raw):
                os.unlink(raw)
            f.write(('{"execute":"screendump","arguments":{"filename":"%s"}}\n' % raw).encode())
            f.flush()
            recv()
            prev = -1
            for _ in range(90):
                if os.path.exists(raw):
                    sz = os.path.getsize(raw)
                    if sz == prev and sz > 0:
                        time.sleep(0.06)
                        try:
                            im = Image.open(raw); im.load()
                            return im.convert("RGB")
                        except Exception:
                            pass
                    prev = sz
                time.sleep(0.02)
        except Exception as e:
            print("grab attempt %d: %s" % (attempt, e))
    return None

idx = 0
times = []
t0 = time.time()
interval = 1.0 / fps
while time.time() - t0 <= dur:
    im = grab()
    if im is None:
        print("frame %d dropped" % idx)
        continue
    # 启动途中分辨率会从 640x480(固件/文本阶段) 跳到 1920x1080(GOP 生效后),
    # 直接 thumbnail 会让不同尺寸的帧混进 concat, ffmpeg 要么报错要么退化到
    # 第一帧的尺寸。统一画布: 等比缩放后居中, 两边留黑边, 不变形。
    W, H = 1280, 720
    r = min(W / im.width, H / im.height)
    nw, nh = max(1, int(im.width * r)), max(1, int(im.height * r))
    im2 = im.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("RGB", (W, H), (0, 0, 0))
    canvas.paste(im2, ((W - nw) // 2, (H - nh) // 2))
    canvas.save(os.path.join(frames_dir, "f%04d.png" % idx))
    times.append(time.time() - t0)
    idx += 1
    nxt = t0 + idx * interval
    gap = nxt - time.time()
    if gap > 0: time.sleep(gap)

s.close()
print("captured %d frames over %.1fs" % (idx, times[-1] if times else 0))

lst = os.path.join(inst, "list.txt")
with open(lst, "w") as lf:
    for i in range(len(times)):
        if i + 1 < len(times):
            d = times[i + 1] - times[i]
        elif i:
            d = times[i] - times[i - 1]
        else:
            d = 0.25
        lf.write("file '%s'\n" % os.path.join(frames_dir, "f%04d.png" % i))
        lf.write("duration %.4f\n" % max(d, 0.001))
PY

P=$(cat "$INST/qemu.pid" 2>/dev/null)
[ -n "${P:-}" ] && kill -9 "$P" 2>/dev/null
sleep 1

ffmpeg -y -f concat -safe 0 -i "$INST/list.txt" \
  -vsync vfr -c:v libx264 -pix_fmt yuv420p -crf 20 "$OUT" 2>&1 | tail -3
ls -l "$OUT" 2>/dev/null | awk '{print "OUT:", $5, "bytes"}'

exec {SLOT_FD}>&-
exit 0
