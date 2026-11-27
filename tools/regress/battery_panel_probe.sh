#!/bin/bash
# ============================================================================
# 电池面板取证：点击顶栏电池图标 → 展开面板 → 点右下角"电池和电源计划"
#                → Settings 电池页
#
# 用法:
#   bash tools/regress/battery_panel_probe.sh <输出目录> [acpitable文件]
#
# 与 battery_state_probe.sh 的区别:
#   - 额外挂 usb-tablet(UHCI 绝对指针), 用 QMP input-send-event 做绝对点击;
#   - 不靠"点完看一眼", 每个动作都在串口留一行 [DESK] 日志, 截图再复核。
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"
source "$REGRESS_DIR/lib_qemu.sh"

OUT="${1:?用法: battery_panel_probe.sh <输出目录> [acpitable文件]}"
ACPITABLE="${2:-}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
SER="$OUT/ser.log"

exec {qfd}>"$QEMU_LOCK"
flock -w 2400 "$qfd" || { log "等不到 QEMU 锁"; exit 1; }

MON="$INST/mon.sock"
QMP="$INST/qmp.sock"
PID="$INST/qemu.pid"
FD="$INST/V.fd"
rm -f "$SER" "$MON" "$QMP" "$PID"
cp -f "$V/OVMF_VARS_4M.fd" "$FD"

extra=()
[ -n "$ACPITABLE" ] && extra=(-acpitable "file=$ACPITABLE")

qemu-system-x86_64 -L "$CHAT/.toolchain/pkg/datadir" \
  -rtc base=localtime \
  -drive if=pflash,format=raw,readonly=on,file="$V/OVMF_CODE_4M.fd" \
  -drive if=pflash,format=raw,file="$FD" \
  -drive format=raw,file="$IMG",file.locking=off,snapshot=on \
  -m 1024 -smp 2 -vnc 127.0.0.1:$VNCPORT \
  -monitor unix:$MON,server,nowait \
  -qmp unix:$QMP,server,nowait \
  -serial file:"$SER" \
  -usb -device usb-tablet \
  -vga std -nic user,model=e1000 "${extra[@]}" -daemonize -pidfile "$PID" 2>&1 | head -2
sleep 1
log "QEMU started, inst=$INST acpitable=${ACPITABLE:-none}"

if [ "${HEPU_NO_LOCK:-0}" = "1" ]; then
  log "lock disabled image; skip unlock"
  q_wait_for "Desktop running" 60 "$SER" || log "WARN: Desktop running 未出现"
else
  sleep 5
  q_sendkey 1
  q_wait_for "step1 done" 30 "$SER" || log "WARN: step1 未出现"
  q_wait_for "booting to lock" 60 "$SER" || log "WARN: lock 未出现"
  sleep 5
  q_sendkey ret
  q_wait_for "lock -> login" 20 "$SER" || log "WARN: login 未出现"
  sleep 2
  q_sendkey ret
  if q_wait_for "login: auth failed" 10 "$SER"; then
    log "empty Enter rejected; typing fixture password"
    sleep 2
    q_send_text "$FIXTURE_PASSWORD"
    sleep 2
    q_sendkey ret
    if ! q_wait_for "login -> desktop" 30 "$SER"; then
      log "WARN: first password attempt failed; retry"
      q_send_text "$FIXTURE_PASSWORD"
      sleep 2
      q_sendkey ret
    fi
  fi
  q_wait_for "login -> desktop" 30 "$SER" || log "WARN: desktop 迁移未出现"
  q_wait_for "Desktop running" 60 "$SER" || log "WARN: Desktop running 未出现"

  # 启动流程偶发停在锁屏(键盘 Enter 未落到锁屏窗口): 显式点一下再输密码
  if ! grep -aq "login -> desktop" "$SER"; then
    log "still locked after boot; clicking center to wake"
    python3 "$REGRESS_DIR/qmp_click.py" "$QMP" 960 540 1920 1080 click
    sleep 3
    q_wait_for "lock -> login" 10 "$SER" || log "WARN: 锁屏没有进入 login"
    sleep 3
    q_send_text "$FIXTURE_PASSWORD"
    sleep 3
    q_sendkey ret
    if ! q_wait_for "login -> desktop" 30 "$SER"; then
      log "WARN: 补解锁第一次失败，重试"
      q_send_text "$FIXTURE_PASSWORD"
      sleep 3
      q_sendkey ret
      q_wait_for "login -> desktop" 30 "$SER" || log "WARN: 补解锁仍失败"
    fi
    sleep 3
  fi
fi

# 通知卡会盖住顶栏右侧(面板也在这一带), 等它自己超时消失再动鼠标
sleep 14

W=1920
H=1080
# 150% 缩放下的几何(与 hpdesk.c 中 hs_bar_icon_x / hs_battery_panel_geom 同源):
#   icon_size=36, bar_h=54, bar_top=12, margin=15, pad=21, gap=9
#   电池 = power(1848) - 3*(9+36) = 1713
BAT_X=$(( 1713 + 18 ))
BAT_Y=$(( 12 + (54 - 36) / 2 + 18 ))
# 面板: 右边对齐电池右缘(1749), 宽 375, 高 195, y=78
LINK_X=$(( 1374 + 375 - 18 - 225 + 112 ))
LINK_Y=$(( 78 + 195 - 15 - 36 + 18 ))

q_dump "$OUT/01-desktop.ppm" "$OUT/01-desktop.png"

log "click battery @ ($BAT_X,$BAT_Y)"
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" "$BAT_X" "$BAT_Y" "$W" "$H" click
sleep 2
q_dump "$OUT/02-panel.ppm" "$OUT/02-panel.png"
q_wait_for "\[DESK\] battery panel open" 5 "$SER" || log "WARN: 串口没有 panel open"

log "click link @ ($LINK_X,$LINK_Y)"
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" "$LINK_X" "$LINK_Y" "$W" "$H" click
sleep 3
q_dump "$OUT/03-settings.ppm" "$OUT/03-settings.png"
q_wait_for "settings page=battery" 5 "$SER" || log "WARN: 串口没有 settings page=battery"

# 顺带回归 1: 加了第 7 页后, 老页还能点(About = 第 4 项)
# 导航列从窗口内容原点开始, 实测 150% 下首项盒顶 y=232, 每项 42 + 间距 6
NAV_X=$(( 615 + 24 + 40 ))
ABOUT_Y=$(( 232 + 3 * 48 + 21 ))
log "click About nav @ ($NAV_X,$ABOUT_Y)"
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" "$NAV_X" "$ABOUT_Y" "$W" "$H" click
sleep 3
q_dump "$OUT/04-about.ppm" "$OUT/04-about.png"

# 顺带回归 2: 导航里第 7 项 Battery 本身也能进电池页
BAT_NAV_Y=$(( 232 + 6 * 48 + 21 ))
log "click Battery nav @ ($NAV_X,$BAT_NAV_Y)"
python3 "$REGRESS_DIR/qmp_click.py" "$QMP" "$NAV_X" "$BAT_NAV_Y" "$W" "$H" click
sleep 3
q_dump "$OUT/05-battery-nav.ppm" "$OUT/05-battery-nav.png"

q_stop

log "== [DESK]/[BAT] lines =="
grep -aE '\[AML\]|\[DESK\]|\[BAT\]|\[SETTINGS\]' "$SER" | tee "$OUT/desk-lines.txt" || true
