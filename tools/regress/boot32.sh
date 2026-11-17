#!/bin/bash
# ============================================================================
# 32 位全链路取证：菜单 -> down+ret 选 Hepu OS 32-bit -> 加载屏 -> 锁屏(若有) -> 桌面
# 每个状态 4 帧；32 位在本树兜底关闭锁屏，若确实无锁屏页，如实记为 N/A，不伪装。
# 用法: bash tools/regress/boot32.sh
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"
source "$REGRESS_DIR/lib_qemu.sh"

OUT="$EV/frames32"
SER="$OUT/ser.log"
mkdir -p "$OUT"

exec {qfd}>"$QEMU_LOCK"
flock -w 2400 "$qfd" || { log "等不到 QEMU 锁"; exit 1; }

q_start "$SER"
log "QEMU started, inst=$INST vnc=:$VNCPORT"

# ---- 1) 菜单 4 帧 ----
sleep 5
for i in 1 2 3 4; do
  log "menu frame $i"
  q_dump "$OUT/menu_$i.ppm" "$OUT/menu_$i.png"
  sleep 1.2
done

# ---- 2) 选择 32 位：down 一项 + ret ----
q_sendkey down; sleep 1.5
q_sendkey ret;   sleep 1

# ---- 3) 加载屏 4 帧 ----
for i in 1 2 3 4; do
  log "loading frame $i"
  q_dump "$OUT/loading_$i.ppm" "$OUT/loading_$i.png"
  sleep 1.5
done

# ---- 4) 等待 32 位内核早期标记 ----
if q_wait_for "SERIAL.*16550|INITRD|elastic free" 70 "$SER"; then
  log "32-bit kernel marker hit"
else
  log "WARN: 32-bit 内核标记 70s 内未出现（可能菜单选择未生效或内核未起）"
fi

# ---- 5) 锁屏：32 位本树兜底关闭锁屏；有则截，无则记 N/A ----
LOCK_HIT=0
if q_wait_for "booting to lock" 25 "$SER"; then
  LOCK_HIT=1
  log "32-bit lock marker hit"
  sleep 6
  for i in 1 2 3 4; do
    log "lock frame $i"
    q_dump "$OUT/lock_$i.ppm" "$OUT/lock_$i.png"
    sleep 2
  done
  q_sendkey space; sleep 2.5
  q_sendkey ret;   sleep 1.5
  q_sendkey ret;   sleep 2
else
  log "32-bit 无锁屏页（lock marker 缺席），lock 状态记 N/A"
fi

# ---- 6) 桌面 ----
if q_wait_for "elastic free desktop init done|Desktop running" 60 "$SER"; then
  log "32-bit desktop marker hit"
else
  log "WARN: 32-bit 桌面标记 60s 内未出现"
fi
sleep 8
for i in 1 2 3 4; do
  log "desktop frame $i"
  q_dump "$OUT/desktop_$i.ppm" "$OUT/desktop_$i.png"
  sleep 2
done

echo "LOCK_HIT=$LOCK_HIT" > "$OUT/state.txt"
q_stop
cp -f "$SER" "$OUT/ser.log" 2>/dev/null || true
log "32-bit chain done -> $OUT"
