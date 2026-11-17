#!/bin/bash
# ============================================================================
# 加载屏推进取证：起机 -> 时间线第 4 秒经 monitor 选 64 位 -> 立即循环 screendump
# 录视频，抽帧证明阶段文字推进（Loading kernel image -> Verifying -> Preheating
# -> Preparing to hand off），不能只凭一张静态图下结论。
#
# 用法: bash tools/regress/record_loading.sh [DUR_SEC] [FPS]
# 产物: $EV/loading/loading.mp4 + $EV/loading/stage_*.png + frames/
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"
source "$REGRESS_DIR/lib_qemu.sh"

DUR=${1:-25}
FPS=${2:-4}
OUT="$EV/loading/loading.mp4"
FR="$EV/loading/frames"
SER="$EV/loading/ser.log"
MON="$INST/mon.sock"
PID="$INST/qemu.pid"
FD="$INST/V.fd"
mkdir -p "$FR"

exec {qfd}>"$QEMU_LOCK"
flock -w 2400 "$qfd" || { log "等不到 QEMU 锁"; exit 1; }

rm -rf "$FR" "$INST/frames"; mkdir -p "$FR" "$INST/frames"
rm -f "$SER" "$MON" "$PID"
cp -f "$V/OVMF_VARS_4M.fd" "$FD"
if [ -f "$PID" ]; then OLD=$(cat "$PID" 2>/dev/null); [ -n "${OLD:-}" ] && kill -9 "$OLD" 2>/dev/null; fi

qemu-system-x86_64 -L "$CHAT/.toolchain/pkg/datadir" \
  -rtc base=localtime \
  -drive if=pflash,format=raw,readonly=on,file="$V/OVMF_CODE_4M.fd" \
  -drive if=pflash,format=raw,file="$FD" \
  -drive format=raw,file="$IMG",file.locking=off,snapshot=on \
  -m 1024 -smp 2 -vnc 127.0.0.1:$VNCPORT \
  -monitor unix:$MON,server,nowait \
  -serial file:$SER \
  -device qemu-xhci -device usb-kbd -device usb-mouse -vga std \
  -nic user,model=e1000 -daemonize -pidfile "$PID" 2>&1 | head -2

for i in $(seq 1 30); do [ -S "$MON" ] && break; sleep 1; done

python3 - "$INST" "$DUR" "$FPS" <<'PY'
import os, socket, sys, time
from PIL import Image

inst, dur, fps = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
mon = os.path.join(inst, "mon.sock")
frames_dir = os.path.join(inst, "frames")
ev_frames = os.path.join(os.environ["EV"], "loading", "frames")
os.makedirs(ev_frames, exist_ok=True)
raw = os.path.join(inst, "frame.ppm")

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(mon)
time.sleep(0.3)

def send_key(k):
    s.sendall(("sendkey %s\n" % k).encode())

def grab():
    for attempt in range(3):
        try:
            if os.path.exists(raw): os.unlink(raw)
            s.sendall(('screendump %s\n' % raw).encode())
            prev = -1
            for _ in range(25):
                if os.path.exists(raw):
                    sz = os.path.getsize(raw)
                    if sz == prev and sz > 0:
                        return raw
                    prev = sz
                time.sleep(0.02)
        except Exception as e:
            print("grab attempt %d: %s" % (attempt, e))
    return None

idx = 0; times = []; t0 = time.time(); interval = 1.0 / fps
key_sent = False
while time.time() - t0 <= dur:
    # 固件 BDS 之后（约 9s）菜单已起，重复选 64 位（ConIn 采样窗口）
    if not key_sent and time.time() - t0 >= 9.0:
        for _ in range(3):
            send_key("1"); time.sleep(1.0)
        key_sent = True
    raw_path = grab()
    if raw_path is None:
        print("frame %d dropped" % idx); continue
    dst = os.path.join(frames_dir, "f%04d.ppm" % idx)
    os.system("cp %s %s" % (raw_path, dst))
    times.append(time.time() - t0)
    idx += 1
    nxt = t0 + idx * interval
    gap = nxt - time.time()
    if gap > 0: time.sleep(gap)
s.close()
print("captured %d frames over %.1fs" % (idx, times[-1] if times else 0))

# 出循环后批量 PPM -> PNG（热循环内不做解码，保证抓取帧率）
for i in range(idx):
    p = os.path.join(frames_dir, "f%04d.ppm" % i)
    if os.path.exists(p):
        Image.open(p).convert("RGB").save(os.path.join(frames_dir, "f%04d.png" % i))
        Image.open(p).convert("RGB").save(os.path.join(ev_frames, "f%04d.png" % i))
        os.remove(p)

lst = os.path.join(inst, "list.txt")
with open(lst, "w") as lf:
    for i in range(len(times)):
        d = (times[i+1]-times[i]) if i+1 < len(times) else (times[i]-times[i-1] if i else 0.25)
        lf.write("file '%s'\n" % os.path.join(frames_dir, "f%04d.png" % i))
        lf.write("duration %.4f\n" % max(d, 0.001))
PY

P=$(cat "$PID" 2>/dev/null); [ -n "${P:-}" ] && kill -9 "$P" 2>/dev/null
sleep 1

ffmpeg -y -f concat -safe 0 -i "$INST/list.txt" -vsync vfr \
  -vf "scale=1280:720:force_original_aspect_ratio=decrease,pad=1280:720:(ow-iw)/2:(oh-ih)/2" \
  -c:v libx264 -pix_fmt yuv420p -crf 20 "$OUT" 2>&1 | tail -3
ls -l "$OUT" | awk '{print "OUT:", $5, "bytes"}'
cp -f "$SER" "$EV/loading/ser.log" 2>/dev/null || true
log "loading video done -> $OUT"
