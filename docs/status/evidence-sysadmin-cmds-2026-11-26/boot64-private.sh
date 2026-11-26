#!/bin/bash
# 本路 64 位定向取证：私有 UEFI 镜像（含本轮新构建 kernel64.bin），
# 只采集启动串口标记；不做登录流程。只按自己 pidfile 回收 QEMU。
set -u
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$DIR/../../../tools/regress/env.sh"
source "$REGRESS_DIR/lib_qemu.sh"

export IMG="$DIR/hepu64.img"
export INST="/tmp/hepu-admin64-inst"
rm -rf "$INST"; mkdir -p "$INST"
SER="$DIR/ser64-boot.log"

exec {qfd}>"$QEMU_LOCK"
flock -w 2400 "$qfd" || { echo "等不到 QEMU 锁"; exit 1; }

q_start "$SER"
echo "QEMU started, inst=$INST"

q_sendkey 1
q_wait_for "step1 done" 30 "$SER" && echo "marker: step1 done" || echo "WARN: step1 done 未出现"
if q_wait_for "booting to lock" 60 "$SER"; then
  echo "marker: booting to lock"
else
  echo "WARN: booting to lock 未出现"
fi
sleep 6
q_stop
cp -f "$SER" "$DIR/ser64-boot.log" 2>/dev/null || true
echo "64 boot capture done -> $SER"
