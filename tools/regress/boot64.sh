#!/bin/bash
# ============================================================================
# 64 位全链路取证：菜单 -> 加载屏 -> 锁屏 -> 登录页 ->（空 Enter 探测）->（夹具密码）-> 桌面
# 每个状态 4 帧，串口日志与 NONBLACK 计数一并落 $EV/frames64。
#
# 认证三分支（hpdesk.c hs_login_submit，基线 a787a01）：
#   ① /account.conf 无 hash（未配置账户）-> 空 Enter 放行进桌面；
#   ② 已配置 -> auth64 sha256 比对；
#   ③ 比对失败 -> 留在登录页置错误提示，不退回锁屏。
# 夹具：/account.conf 写入 sha256($FIXTURE_PASSWORD)，口令仅本回归使用。
#
# 用法: bash tools/regress/boot64.sh
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"
source "$REGRESS_DIR/lib_qemu.sh"

OUT="$EV/frames64"
SER="$OUT/ser.log"
STATE="$OUT/state.txt"
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

ACCOUNT_LOADED=0
grep -aq "Account loaded" "$SER" && ACCOUNT_LOADED=1

# ---- 4) 锁屏 -> 登录页（任意键，Enter）----
q_sendkey ret; sleep 2.5
LOGIN_PAGE=0
if q_wait_for "lock -> login" 20 "$SER"; then
  LOGIN_PAGE=1
  log "lock -> login marker hit"
else
  log "WARN: lock -> login 标记未出现"
fi
sleep 3
for i in 1 2 3 4; do
  log "login frame $i"
  q_dump "$OUT/login_$i.ppm" "$OUT/login_$i.png"
  sleep 1.5
done

# ---- 5) 空 Enter 探测：不输入任何口令直接提交 ----
EMPTY_ENTER="UNVERIFIED"
AUTH_BRANCH="unknown"
q_sendkey ret; sleep 2

if q_wait_for "login: auth failed" 10 "$SER"; then
  EMPTY_ENTER="FAILED"
  AUTH_BRANCH="configured"
  log "empty Enter rejected: auth failed（已配置账户，空口令不通过）"
  q_dump "$OUT/login_reject_1.ppm" "$OUT/login_reject_1.png"

  # ---- 6) 输入夹具口令并提交 ----
  q_send_text "$FIXTURE_PASSWORD"
  sleep 1.5
  q_sendkey ret; sleep 2
  if q_wait_for "login -> desktop" 30 "$SER"; then
    log "fixture password accepted"
  else
    log "WARN: 夹具口令提交后未等到 desktop 迁移标记"
  fi
elif q_wait_for "no account configured" 10 "$SER"; then
  EMPTY_ENTER="PASSED"
  AUTH_BRANCH="noaccount"
  log "empty Enter passed: 未配置账户分支放行"
else
  log "WARN: 空 Enter 后既未 auth failed 也未 no account configured"
fi

# ---- 7) 桌面 ----
DESKTOP_MARKER=0
if q_wait_for "Desktop running|login -> desktop" 60 "$SER"; then
  DESKTOP_MARKER=1
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

# ---- 状态落盘（报告据此判定，不靠猜）----
DESKTOP_FRAMES=$(ls "$OUT"/desktop_*.png 2>/dev/null | wc -l)
{
  echo "AUTH_BRANCH=$AUTH_BRANCH"
  echo "EMPTY_ENTER=$EMPTY_ENTER"
  echo "FIXTURE_PASSWORD=$FIXTURE_PASSWORD"
  echo "ACCOUNT_LOADED=$ACCOUNT_LOADED"
  echo "LOGIN_PAGE=$LOGIN_PAGE"
  echo "DESKTOP_MARKER=$DESKTOP_MARKER"
  echo "DESKTOP_FRAMES=$DESKTOP_FRAMES"
} > "$STATE"

q_stop
cp -f "$SER" "$OUT/ser.log" 2>/dev/null || true
log "64-bit chain done -> $OUT"
