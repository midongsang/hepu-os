#!/bin/bash
# 录制系统启动：QMP 循环 screendump -> PNG 帧 -> ffmpeg 按真实时间戳合成
#
# 用法: bash tools/vmshot/record.sh <TAG> [DUR_SEC] [FPS] [OUT.mp4] [KEYSPEC]
#
#   TAG      实例短名（独立实例目录 run/inst/<TAG>，重复跑只清自己）
#   DUR_SEC  录制总时长（秒，默认 45）
#   FPS      目标帧率（默认 4）
#   OUT.mp4  产物路径（默认 shots/boot_<TAG>.mp4）
#   KEYSPEC  可选：录制中途经 QMP send-key 定时注入按键，格式见下；留空则不注入。
#
# KEYSPEC 格式（多个事件用分号分隔，每个事件 "@<秒>=<键>[,<键>...]"）：
#   @12=ret                 录制到第 12 秒按一次回车
#   @10=down;@11=ret        10 秒按方向下，11 秒按回车（选菜单项）
#   @15=shift,2             组合键：先 shift 再按 2（keys 数组顺序即按下顺序）
#
# 支持的键名（QEMU QKeyCode 标准键名；脚本内置少量别名映射）：
#   回车 ret/enter/return；上 up/uparrow；下 down/downarrow；左 left/leftarrow；
#   右 right/rightarrow；空格 spc/space；制表 tab；ESC esc/escape；
#   退格 backspace/bs；字母 a-z；数字 0-9；修饰键 shift/alt/ctrl/altgr/meta。
#   未在映射表里的名字原样透传给 QEMU，QEMU 不认会在 QMP 回复里报错（脚本记一条
#   warning，不中断录制）。
#   内部按 QEMU 6.2 的 send-key 协议把每个键包成 KeyValue 对象
#   {"type":"qcode","data":<键名>}；调用方只写键名，不接触这个结构。
#
# 限制（必读）：
#   1. **时间是宿主机墙钟，不是 Guest 时钟**。TCG 软件模拟下 Guest 跑得比宿主机慢，
#      "@N 秒"对应的 Guest 进度（菜单/加载屏/锁屏）只是近似，不同轮次会漂移。
#      要稳妥推进界面，宁可把事件时刻留早一点、多排一个重复键。
#   2. 按键走 QMP `send-key`（USB/PS2 键盘都接在同一 QEMU 键盘设备上），不依赖
#      VNC 聚焦；Guest 侧是否消费取决于当时界面绑没绑这个键（与 OPEN-03 记录的
#      应用层边界一致）。
#   3. 组合键只发一次 keys 数组，不做按下-保持-释放的时序展开；修饰键用于单键
#      组合，连续文本输入请排多个独立事件。
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
KEYSPEC=${5:-}
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
# 挂 VNC 作为显示后端：screendump 从活动 VGA surface 抓帧，有显示后端时 surface
# 跟随 Guest 切到的 GOP 模式。早期曾把 "-display none 下抓到 640x480" 归因于
# "QEMU surface 被锁死"，核实后真实根因是引导器 cfg 加载顺序 bug（init_gop 早于
# load_config，resolution= 读不到），与显示后端无关；修复后 -display none 同样能
# 拿到 1920x1080（见 docs/status/VM_INSTANCES.md 的经验记录）。
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

python3 - "$INST" "$DUR" "$FPS" "$KEYSPEC" <<'PY'
import json, os, socket, sys, time
from PIL import Image

inst, dur, fps = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
keyspec = sys.argv[4] if len(sys.argv) > 4 else ""
sock_path = os.path.join(inst, "qmp.sock")
frames_dir = os.path.join(inst, "frames")
raw = os.path.join(inst, "frame.ppm")

# 别名 -> QEMU send-key 标准键名（见脚本头注释的键名集合）
KEY_ALIAS = {
    "enter": "ret", "return": "ret", "ret": "ret",
    "uparrow": "up", "up": "up",
    "downarrow": "down", "down": "down",
    "leftarrow": "left", "left": "left",
    "rightarrow": "right", "right": "right",
    "space": "spc", "spc": "spc",
    "escape": "esc", "esc": "esc",
    "bs": "backspace", "backspace": "backspace",
    "tab": "tab",
}

def parse_keyspec(spec):
    """解析 '@12=ret;@10=down,ret' -> [(12.0, ['ret']), (10.0, ['down','ret'])]，
    按时刻升序；空串返回 []。格式错的事件跳过并打印 warning，不抛异常。"""
    events = []
    if not spec:
        return events
    for ev in spec.split(";"):
        ev = ev.strip()
        if not ev:
            continue
        if "=" not in ev:
            print("[keys] 跳过格式错误的事件: %r" % ev); continue
        ts, keys = ev.split("=", 1)
        try:
            t = float(ts.lstrip("@").strip())
        except ValueError:
            print("[keys] 时刻不是数字: %r" % ev); continue
        names = [KEY_ALIAS.get(k.strip().lower(), k.strip()) for k in keys.split(",") if k.strip()]
        if names:
            events.append((t, names))
    events.sort(key=lambda e: e[0])
    return events

events = parse_keyspec(keyspec)
t0_global = time.time()
if events:
    print("[keys] 计划注入: " + ", ".join("@%.1fs=%s" % (t, "+".join(k)) for t, k in events))

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sock_path)
f = s.makefile("rwb")

def recv():
    line = f.readline()
    if not line:
        raise RuntimeError("qmp closed")
    return json.loads(line)

def recv_reply():
    """读到下一条命令回复；跳过中间夹杂的异步 event（dict 带 "event" 键）。"""
    while True:
        msg = recv()
        if "event" not in msg:
            return msg

recv()
f.write(b'{"execute":"qmp_capabilities"}\n'); f.flush(); recv_reply()

def send_key(keys):
    """经 QMP send-key 发一组键。QEMU 6.2 的 send-key 每个键是 KeyValue 对象
    {"type":"qcode","data":<键名>}，不能直接传字符串（否则报
    Invalid parameter type for 'keys[0]', expected: object）。
    读回 reply，error 只打 warning 不中断录制。"""
    kv = [{"type": "qcode", "data": k} for k in keys]
    cmd = {"execute": "send-key", "arguments": {"keys": kv}}
    f.write((json.dumps(cmd) + "\n").encode()); f.flush()
    reply = recv_reply()
    if "error" in reply:
        print("[keys] QMP 拒绝 %s: %s" % (keys, reply["error"].get("desc", reply["error"])))
    else:
        print("[keys] @%.1fs 发送 %s" % (time.time() - t0_global, "+".join(keys)))

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
            recv_reply()
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
ev_i = 0
while time.time() - t0 <= dur:
    # 到点的按键事件先注入（墙钟时刻；TCG 下 Guest 进度为近似，见头注释限制 1）
    now = time.time() - t0_global
    while ev_i < len(events) and events[ev_i][0] <= now:
        send_key(events[ev_i][1])
        ev_i += 1
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
