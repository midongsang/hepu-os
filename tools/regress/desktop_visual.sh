#!/bin/bash
# ============================================================================
# 桌面固定态截图基线
#
# 用法:
#   bash tools/regress/desktop_visual.sh <输出目录> [--baseline <目录>]
#   bash tools/regress/desktop_visual.sh <输出目录> --update-baseline
#
# 固定条件:
#   - 1920x1080, UI scale=150（build_all.sh 注入的 display.conf）
#   - QEMU 固定 RTC, 无网络（天气显示空状态, 截图可重复）
#   - 依次采集: 锁屏 / 桌面 / Dock 悬停 / 应用抽屉 / 天气标签 / 控制中心 / Settings / Devices / Audio
#
# 说明: 这是视觉回归入口, 不是功能测试。锁屏解锁使用 fixture 口令,
# 与 battery 取证脚本同一套账号夹具。
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"
source "$REGRESS_DIR/lib_qemu.sh"

OUT="${1:?用法: desktop_visual.sh <输出目录> [--baseline <目录>]}"
shift || true
BASELINE=""
UPDATE=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --baseline) BASELINE="${2:?--baseline 需要目录}"; shift 2 ;;
    --update-baseline) UPDATE=1; shift ;;
    *) echo "未知参数: $1" >&2; exit 2 ;;
  esac
done
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
SER="$OUT/ser.log"

# 独立实例, 不和 battery / smoke 抢同一套 monitor socket。
INST="$RUN/inst/desktop-visual"
MON="$INST/mon.sock"
QMP="$INST/qmp.sock"
PID="$INST/qemu.pid"
FD="$INST/V.fd"
VNCPORT=5903
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

cp -f "$V/OVMF_VARS_4M.fd" "$FD"

qemu-system-x86_64 -L "$CHAT/.toolchain/pkg/datadir" \
  -rtc base=2026-11-27T12:00:00 \
  -drive if=pflash,format=raw,readonly=on,file="$V/OVMF_CODE_4M.fd" \
  -drive if=pflash,format=raw,file="$FD" \
  -drive format=raw,file="$IMG",file.locking=off,snapshot=on \
  -m 1024 -smp 2 -vnc 127.0.0.1:$VNCPORT \
  -monitor unix:$MON,server,nowait \
  -qmp unix:$QMP,server,nowait \
  -serial file:"$SER" \
  -usb -device usb-kbd -device usb-tablet \
  -vga std -nic none -daemonize -pidfile "$PID" 2>&1 | head -2
sleep 1
log "QEMU started, out=$OUT"

# ---- 01 锁屏 ----
q_wait_for "booting to lock screen" 60 "$SER" || { log "FAIL: 没到锁屏"; exit 3; }
sleep 3
q_dump "$OUT/01-lock.ppm" "$OUT/01-lock.png" || exit 3

# ---- 解锁到桌面 ----
sleep 2
q_sendkey ret
if ! q_wait_for "lock -> login" 20 "$SER"; then
  q_wait_for "login -> desktop" 20 "$SER" || true
fi
sleep 2
q_sendkey ret
q_wait_for "login: auth failed" 8 "$SER" || true
sleep 1
q_send_text "$FIXTURE_PASSWORD"
sleep 2
q_sendkey ret
q_wait_for "login -> desktop" 30 "$SER" || { log "FAIL: 解锁失败"; exit 3; }
q_wait_for "Desktop running" 30 "$SER" || { log "FAIL: 桌面未运行"; exit 3; }

# 欢迎通知会在右上角停留 6 秒, 等它消失再截固定态。
sleep 15
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 200 1920 1080 move
sleep 1
q_dump "$OUT/02-desktop.ppm" "$OUT/02-desktop.png" || exit 3

# ---- 08 Dock 悬停: 指针压到底部触发区, 确认图标不是字母占位 ----
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 960 1070 1920 1080 move
sleep 2
q_dump "$OUT/08-dock.ppm" "$OUT/08-dock.png" || exit 3
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 200 1920 1080 move
sleep 1

# ---- 03 应用抽屉: 点左侧把手 ----
# 150%: sidebar_x=15, sidebar_y=75, height=750, handle 高 144。
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 25 450 1920 1080 click
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 200 1920 1080 move
sleep 2
q_dump "$OUT/03-drawer.ppm" "$OUT/03-drawer.png" || exit 3

# ---- 09 天气标签: 无网络时必须显示空状态, 不得出现硬编码温度/湿度/预报 ----
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 286 112 1920 1080 click
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 200 1920 1080 move
sleep 2
q_dump "$OUT/09-weather.ppm" "$OUT/09-weather.png" || exit 3

# 收起抽屉: 150% 下关闭按钮在 (396..426, 99..129)
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 410 114 1920 1080 click
sleep 2

# ---- 04 控制中心: 顶栏齿轮 ----
# 150%: icon_size=36, margin=15, inset=21, slot1=1803..1839, y=18..54
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 1821 36 1920 1080 click
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 200 1920 1080 move
sleep 2
q_dump "$OUT/04-control-center.ppm" "$OUT/04-control-center.png" || exit 3

# ---- 控制中心真实性探针（不依赖截图判读）----
# 150% 几何: panel 1365..1905, rail 156, pad 24, rx=1545, card 104x104。
# 第一行 WiFi 中心 (1597,184), 第二行 DND (1597,300), Volume 轨道 y=406。
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 1597 184 1920 1080 click
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 1597 300 1920 1080 click
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 1597 300 1920 1080 click
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 1800 406 1920 1080 click
sleep 2
for needle in "toggle unavailable: No WiFi adapter" "dnd=off" "dnd=on" "volume unavailable: No AC"; do
  grep -aq "$needle" "$SER" || { log "FAIL: CC probe missing: $needle"; exit 3; }
done
log "CC probe: disabled WiFi / DND off-on / disabled volume all logged"

# ---- 06 Devices ----
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 1400 180 1920 1080 click
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 200 1920 1080 move
sleep 2
q_dump "$OUT/06-devices.ppm" "$OUT/06-devices.png" || exit 3

# ---- 07 Audio ----
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 1400 243 1920 1080 click
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 200 1920 1080 move
sleep 2
q_dump "$OUT/07-audio.ppm" "$OUT/07-audio.png" || exit 3
# Audio 页再验 DND / Torch 命中区: DND 卡片 1545..1665, y=525..645
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 1600 580 1920 1080 click
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 1600 580 1920 1080 click
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 1750 580 1920 1080 click
sleep 1
grep -aq "torch unavailable: No torch device" "$SER" || { log "FAIL: Torch 未记录不可用"; exit 3; }
log "CC audio page: DND hit + Torch disabled logged"

# ---- 05 Settings: 关掉控制中心, 保留启动时的 Settings 浮窗 ----
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 1821 36 1920 1080 click
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 20 200 1920 1080 move
sleep 2
q_dump "$OUT/05-settings.ppm" "$OUT/05-settings.png" || exit 3

log "screenshots done: 01-lock 02-desktop 03-drawer 04-control-center 05-settings 06-devices 07-audio 08-dock 09-weather"

# ---- 可选: 生成/比较基线 ----
if [ "$UPDATE" = "1" ]; then
  BASE="$WT/docs/status/desktop-visual-baseline-2026-11-27"
  mkdir -p "$BASE"
  cp "$OUT"/0*.png "$BASE"/
  log "baseline updated: $BASE"
elif [ -n "$BASELINE" ]; then
  python3 "$REGRESS_DIR/compare_visual.py" "$BASELINE" "$OUT" || exit 4
fi

log "OK"
