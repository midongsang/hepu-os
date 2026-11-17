#!/bin/bash
# ============================================================================
# 64 位全链路取证：菜单 -> 加载屏 -> 锁屏 ->（Enter 登录）->（Enter 桌面）
# 每个状态 4 帧，串口日志与 NONBLACK 计数一并落 $EV/frames64。
# 用法: bash tools/regress/boot64.sh
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"
source "$REGRESS_DIR/lib_qemu.sh"

OUT="$EV/frames64"
SER="$OUT/ser.log"
mkdir -p "$OUT"

# 完整起停生命周期包在全局 QEMU 锁内；只 kill 自己 pidfile
exec {qfd}>"$QEMU_LOCK"
flock -w 2400 "$qfd" || { log "等不到 QEMU 锁"; exit 1; }

q_start "$SER"
log "QEMU started, inst=$INST vnc=:$VNCPORT"

# ---- 1) 引导菜单（timeout=30 窗口内截 4 帧）----
sleep 5
for i in 1 2 3 4; do
  log "menu frame $i"
  q_dump "$OUT/menu_$i.ppm" "$OUT/menu_$i.png"
  sleep 1.2
done

# 显式选 64 位（菜单默认项即 64，按 1 固定口径）
q_sendkey 1
sleep 1

# ---- 2) 加载屏（4 帧，阶段推进由 loading 视频 + 抽帧交叉佐证）----
for i in 1 2 3 4; do
  log "loading frame $i"
  q_dump "$OUT/loading_$i.ppm" "$OUT/loading_$i.png"
  sleep 1.5
done

# ---- 3) 锁屏 ----
if q_wait_for "booting to lock" 60 "$SER"; then
  log "lock marker hit"
else
  log "WARN: lock marker 60s 内未出现"
fi
sleep 6
for i in 1 2 3 4; do
  log "lock frame $i"
  q_dump "$OUT/lock_$i.ppm" "$OUT/lock_$i.png"
  sleep 2
done

# ---- 4) 解锁（Enter 路径）：锁屏 -> 登录页 -> 桌面（ebf438c 新状态机）----
# 第一次 Enter: g_locked 1 -> 2（lock -> login）；第二次 Enter: 2 -> 0（login -> desktop）
q_sendkey ret; sleep 2.5
if q_wait_for "lock -> login" 20 "$SER"; then
  log "lock -> login marker hit"
else
  log "WARN: lock -> login 标记未出现"
fi
q_sendkey ret; sleep 2

if q_wait_for "Desktop running" 60 "$SER"; then
  log "desktop marker hit"
else
  log "WARN: desktop marker 60s 内未出现"
fi
sleep 8
for i in 1 2 3 4; do
  log "desktop frame $i"
  q_dump "$OUT/desktop_$i.ppm" "$OUT/desktop_$i.png"
  sleep 2
done

q_stop
cp -f "$SER" "$OUT/ser.log" 2>/dev/null || true
log "64-bit chain done -> $OUT"
