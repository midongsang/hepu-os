#!/bin/bash
# ============================================================================
# 壁纸五种布局截图证据
#
# 用法: bash tools/regress/wallpaper_layouts.sh <输出目录>
#
# 为什么用 1024x768：仓库自带壁纸全是 16:9（512x288 / 1600x900），
# 在 1920x1080 下 Fill/Fit/Stretch 会得到同一结果。4:3 屏幕能让五种布局
# 同时产生可肉眼区分的几何差异，且不修改发行镜像的显示配置。
#
# 固定条件：镜像副本 + display.conf scale=100 + HEPBOOT.CFG 1024x768，RTC 2026-11-27T12:00，
# 无网络，VNC 5904；Settings 在桌面启动时已打开，点击 Layout chip 切换。
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"
source "$REGRESS_DIR/lib_qemu.sh"

OUT="${1:?用法: wallpaper_layouts.sh <输出目录>}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
SER="$OUT/ser.log"

INST="$RUN/inst/wallpaper-layouts"
MON="$INST/mon.sock"
QMP="$INST/qmp.sock"
PID="$INST/qemu.pid"
FD="$INST/V.fd"
VNCPORT=5904
IMG="$INST/layout.img"
mkdir -p "$INST"
rm -f "$SER" "$MON" "$QMP" "$PID"

exec {qfd}>"$QEMU_LOCK"
flock -w 2400 "$qfd" || { log "等不到 QEMU 锁"; exit 1; }

cleanup() {
  if [ -f "$PID" ]; then
    local P; P=$(cat "$PID" 2>/dev/null)
    [ -n "${P:-}" ] && kill -9 "$P" 2>/dev/null
  fi
}
trap cleanup EXIT

# ---- 复制镜像并只改 display.conf；不动仓库产物 ----
cp -f "$EV/build/hepu.img" "$IMG"
EXT="$INST/_r.ext2"
DISP="$INST/_disp.conf"
printf 'scale=100\nresolution=1024x768\nshadow=1\ncursor=0\n' > "$DISP"
dd if="$IMG" of="$EXT" bs=512 skip=264192 count=204800 status=none
debugfs -w -R "rm /display.conf" "$EXT" >/dev/null 2>&1 || true
debugfs -w -R "write $DISP /display.conf" "$EXT" >/dev/null 2>&1
dd if="$EXT" of="$IMG" bs=512 seek=264192 conv=notrunc status=none
rm -f "$EXT" "$DISP"

# 分辨率由引导器 SetMode 决定，配置在 ESP 的 HEPBOOT.CFG，不在 /display.conf。
# 只改镜像副本里的第一处（ESP 区，约 8MB 处），保留 display.conf 供 scale 读取。
python3 - "$IMG" <<'PYIMG'
import sys
p = sys.argv[1]
data = bytearray(open(p, 'rb').read())
old = b'resolution=1920x1080'
new = b'resolution=1024x768 '          # 补一个空格保持字节长度不变
pos = data.find(old)
assert pos >= 0, 'HEPBOOT.CFG resolution not found'
assert pos < 100 * 1024 * 1024, 'first resolution line is not in ESP'
data[pos:pos + len(old)] = new
open(p, 'wb').write(data)
print('[layout-test] HEPBOOT.CFG resolution patched at', pos)
PYIMG

cp -f "$V/OVMF_VARS_4M.fd" "$FD"
qemu-system-x86_64 -L "$CHAT/.toolchain/pkg/datadir" \
  -rtc base=2026-11-27T12:00:00 \
  -drive if=pflash,format=raw,readonly=on,file="$V/OVMF_CODE_4M.fd" \
  -drive if=pflash,format=raw,file="$FD" \
  -drive format=raw,file="$IMG",file.locking=off \
  -m 1024 -smp 2 -vnc 127.0.0.1:$VNCPORT \
  -monitor unix:$MON,server,nowait \
  -qmp unix:$QMP,server,nowait \
  -serial file:"$SER" \
  -usb -device usb-kbd -device usb-tablet \
  -vga std -nic none -daemonize -pidfile "$PID" 2>&1 | head -2
sleep 1
log "QEMU started, out=$OUT"

# ---- 解锁到桌面 ----
q_wait_for "booting to lock screen" 60 "$SER" || { log "FAIL: 没到锁屏"; exit 3; }
sleep 2
q_sendkey ret
q_wait_for "lock -> login" 20 "$SER" || q_wait_for "login -> desktop" 20 "$SER" || true
sleep 1
q_sendkey ret
sleep 1
q_send_text "$FIXTURE_PASSWORD"
sleep 1
q_sendkey ret
q_wait_for "login -> desktop" 30 "$SER" || { log "FAIL: 解锁失败"; exit 3; }
q_wait_for "Desktop running" 30 "$SER" || { log "FAIL: 桌面未运行"; exit 3; }
sleep 15
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 20 1024 768 move

# Layout chip 几何（1024x768, scale=100）：
# Settings 460x470 水平居中 => x=282, 窗口 y 固定 125; sw_x=x+156=438。
# chip y=y+260=385..405，宽 50 / 间距 4，中心 x = 463,517,571,625,679，点击 y=395。
click_chip() {
  python3 "$REGRESS_DIR/qmp_click.py" "$QMP" "$1" 395 1024 768 click
  python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 20 1024 768 move
  sleep 2
}

FIRST_DUMP=$(q_dump "$OUT/01-fill.ppm" "$OUT/01-fill.png") || exit 3
echo "$FIRST_DUMP"
case "$FIRST_DUMP" in *"W,H = (1024, 768)"*) ;; *) log "FAIL: 期望 1024x768，实际 $FIRST_DUMP"; exit 3 ;; esac
click_chip 517
q_dump "$OUT/02-fit.ppm" "$OUT/02-fit.png" || exit 3
click_chip 571
q_dump "$OUT/03-stretch.ppm" "$OUT/03-stretch.png" || exit 3
click_chip 625
q_dump "$OUT/04-tile.ppm" "$OUT/04-tile.png" || exit 3
click_chip 679
q_dump "$OUT/05-center.ppm" "$OUT/05-center.png" || exit 3

# 串口是状态事实源：必须看到五次布局日志，且顺序正确。
for name in Fit Stretch Tile Center; do
  grep -aq "\[WALLPAPER\] layout=$name" "$SER" || { log "FAIL: 缺少 layout=$name 日志"; exit 3; }
done
log "layout logs: initial Fill -> Fit -> Stretch -> Tile -> Center"

# 持久化必须真的落盘：本次镜像是副本且不 snapshot，停 QEMU 后直接读 ext2。
q_stop
EXT="$INST/_r.ext2"
CFG="$INST/config-after.txt"
dd if="$IMG" of="$EXT" bs=512 skip=264192 count=204800 status=none
debugfs -R "cat /etc/hepushell/config.hsqml" "$EXT" > "$CFG" 2>/dev/null || true
rm -f "$EXT"
if ! grep -aq 'wallpaper_layout: 4' "$CFG"; then
  log "FAIL: config.hsqml 未持久化 Center(4)"; grep -a 'wallpaper' "$CFG" || true; exit 3
fi
log "persisted: config.hsqml wallpaper_layout: 4"

# ---- 二次启动：证明 hpdesk_init 真的读回了 Center，而不是只写不读 ----
SER2="$OUT/ser-reboot.log"
rm -f "$SER2" "$MON" "$QMP" "$PID"
cp -f "$V/OVMF_VARS_4M.fd" "$FD"
qemu-system-x86_64 -L "$CHAT/.toolchain/pkg/datadir" \
  -rtc base=2026-11-27T12:00:00 \
  -drive if=pflash,format=raw,readonly=on,file="$V/OVMF_CODE_4M.fd" \
  -drive if=pflash,format=raw,file="$FD" \
  -drive format=raw,file="$IMG",file.locking=off \
  -m 1024 -smp 2 -vnc 127.0.0.1:$VNCPORT \
  -monitor unix:$MON,server,nowait \
  -qmp unix:$QMP,server,nowait \
  -serial file:"$SER2" \
  -usb -device usb-kbd -device usb-tablet \
  -vga std -nic none -daemonize -pidfile "$PID" 2>&1 | head -2
sleep 1
q_wait_for "booting to lock screen" 60 "$SER2" || { log "FAIL: 二次启动没到锁屏"; exit 3; }
sleep 2
q_sendkey ret
q_wait_for "lock -> login" 20 "$SER2" || q_wait_for "login -> desktop" 20 "$SER2" || true
sleep 1
q_sendkey ret
sleep 1
q_send_text "$FIXTURE_PASSWORD"
sleep 1
q_sendkey ret
q_wait_for "login -> desktop" 30 "$SER2" || { log "FAIL: 二次启动解锁失败"; exit 3; }
q_wait_for "Desktop running" 30 "$SER2" || { log "FAIL: 二次启动桌面未运行"; exit 3; }
sleep 15
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 20 1024 768 move
q_dump "$OUT/06-reboot-center.ppm" "$OUT/06-reboot-center.png" || exit 3
grep -aq "\[WALLPAPER\] layout loaded=Center" "$SER2" || { log "FAIL: 二次启动未读回 Center"; exit 3; }
log "reboot reads back: layout loaded=Center"
log "OK: 01-fill 02-fit 03-stretch 04-tile 05-center 06-reboot-center"
