#!/bin/bash
# ============================================================================
# OPEN-05 取证启动脚本 A：默认构建（探针关闭）64 位桌面，验证 logd 周期拉起链路。
#
# 链路：菜单 1（64 位 GUI）-> 锁屏 -> 登录页 -> 夹具口令 -> 桌面；桌面停留约 70s，
# 覆盖多次 6s 周期触发，串口留存 `[LOGD] spawn ...` 与 worker 的
# `[LOGD] consumed=N wrote=<path> bytes=.. last_seq=..` 原文。
#
# 落盘写入需要持久化：q_start() 固定 snapshot=on（落盘写入会被丢弃），本脚本按
# evidence-log-hardening-2026-11-26 既有取证口径自启 snapshot=off，并把镜像复制到
# 本目录 build/ 下操作，不改动回归镜像 $IMG。
#
# 纪律：QEMU 全机串行走 /tmp/hepu-qemu.lock；只按 pidfile 精确退出；默认 HPET，
#       不加 -no-hpet；32 位 logd 未实现，本脚本只跑 64 位。
# ============================================================================
set -u

REG="$(cd "$(dirname "$0")/../../../tools/regress" && pwd)"
source "$REG/env.sh"
source "$REG/lib_qemu.sh"

EV="$WT/docs/status/evidence-open05-2026-11-26"
mkdir -p "$EV/build" "$EV/serial64"
WORK_IMG="$EV/build/logd.img"
cp -f "$IMG" "$WORK_IMG"
export IMG="$WORK_IMG"
SER="$EV/serial64/serial-64-default-raw.txt"
rm -f "$SER"

exec {qfd}>"$QEMU_LOCK"
flock -w 2400 "$qfd" || { echo "等不到 QEMU 锁"; exit 1; }

MON="$INST/mon.sock"
PID="$INST/qemu.pid"
FD="$INST/V.fd"
rm -f "$MON" "$PID"
cp -f "$V/OVMF_VARS_4M.fd" "$FD"
if [ -f "$PID" ]; then
    OLD=$(cat "$PID" 2>/dev/null)
    [ -n "${OLD:-}" ] && kill -9 "$OLD" 2>/dev/null
fi

qemu-system-x86_64 -L "$CHAT/.toolchain/pkg/datadir" \
    -rtc base=localtime \
    -drive if=pflash,format=raw,readonly=on,file="$V/OVMF_CODE_4M.fd" \
    -drive if=pflash,format=raw,file="$FD" \
    -drive format=raw,file="$IMG",file.locking=off,snapshot=off \
    -m 1024 -smp 2 -vnc 127.0.0.1:$VNCPORT \
    -monitor unix:$MON,server,nowait \
    -serial file:$SER \
    -device qemu-xhci -device usb-kbd -device usb-mouse -vga std \
    -nic user,model=e1000 -daemonize -pidfile "$PID" 2>&1 | head -2
sleep 1
echo "[logd] QEMU started, inst=$INST img=$WORK_IMG"

# ---- 引导菜单：选 1 = 64 位 GUI ----
sleep 5
q_sendkey 1

q_wait_for "step1 done" 30 "$SER" || echo "[logd] WARN: step1 未等到"
q_wait_for "booting to lock" 60 "$SER" || echo "[logd] WARN: 锁屏标记未等到"
sleep 6

# ---- 锁屏 -> 登录页 ----
q_sendkey ret; sleep 2.5
q_wait_for "lock -> login" 20 "$SER" || echo "[logd] WARN: lock -> login 未等到"
sleep 3

# ---- 空 Enter 探测：已配置账户应被拒；再输夹具口令 ----
q_sendkey ret; sleep 2
if q_wait_for "login: auth failed" 10 "$SER"; then
    q_send_text "$FIXTURE_PASSWORD"
    sleep 1.5
    q_sendkey ret; sleep 2
    q_wait_for "login -> desktop" 30 "$SER" || echo "[logd] WARN: 口令后未等到 desktop 标记"
elif q_wait_for "no account configured" 10 "$SER"; then
    :
else
    echo "[logd] WARN: 空 Enter 后分支不明"
fi

q_wait_for "Desktop running|login -> desktop" 60 "$SER" || echo "[logd] WARN: 桌面标记 60s 内未出现"

# ---- 桌面停留：覆盖多次 6s 周期触发 ----
echo "[logd] dwelling 70s on desktop for periodic logd spawns"
sleep 70

q_stop
echo "[logd] done -> $SER"
