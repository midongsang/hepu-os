#!/bin/bash
# ============================================================================
# OPEN-05 取证启动脚本 B：EMERG_PROBE=1 探针内核，文本 shell 验证 emergency 环
# 并入 `journal` 查询输出。
#
# 前置（手动步骤，见 README）：
#   make -C kernel/arch/x86_64 EMERG_PROBE=1
#   HEPU_IMG=<本目录>/build/probe.img python3 tools/build_uefi_dual.py
#   注入 system/account/display.conf（与 build_all.sh 同一套 debugfs 写入）
#
# 链路：菜单 3（hepu.graphics=0 文本 shell）-> 等 shell 就绪 -> 敲 `journal`，
# 串口留存 `---------- emergency ring ----------` 段与 `[EMERG]` 探针条目原文。
# 探针条目由 HEPU_EMERG_PROBE 宏在启动序列早期（图形/文本两路径都经过）写入；
# 默认构建不编入。
#
# 纪律：QEMU 全机串行走 /tmp/hepu-qemu.lock；只按 pidfile 精确退出；默认 HPET。
# ============================================================================
set -u

REG="$(cd "$(dirname "$0")/../../../tools/regress" && pwd)"
source "$REG/env.sh"
source "$REG/lib_qemu.sh"

EV="$WT/docs/status/evidence-open05-2026-11-26"
export IMG="$EV/build/probe.img"
SER="$EV/serial64-probe/serial-64-probe-raw.txt"
mkdir -p "$EV/serial64-probe"
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
    -drive format=raw,file="$IMG",file.locking=off,snapshot=on \
    -m 1024 -smp 2 -vnc 127.0.0.1:$VNCPORT \
    -monitor unix:$MON,server,nowait \
    -serial file:$SER \
    -device qemu-xhci -device usb-kbd -device usb-mouse -vga std \
    -nic user,model=e1000 -daemonize -pidfile "$PID" 2>&1 | head -2
sleep 1
echo "[probe] QEMU started, inst=$INST img=$IMG"

# 引导菜单：选 3 = 保护模式（hepu.graphics=0，64 位文本 shell）
sleep 5
q_sendkey 3

q_wait_for "64-bit Shell" 60 "$SER" || echo "[probe] WARN: 未等到 shell 标记"
sleep 3

send_text64() {
    local TXT="$1" CH i
    for (( i=0; i<${#TXT}; i++ )); do
        CH=${TXT:i:1}
        case "$CH" in
            " ") q_sendkey spc ;;
            "-") q_sendkey minus ;;
            *)   q_sendkey "$CH" ;;
        esac
    done
}

# journal 默认输出普通环 + emergency 环段
send_text64 "journal"
q_sendkey ret
sleep 4

# 再跑一次全级别（-l 0），确认 emerg 条目在最低级别阈值下也可见
send_text64 "journal -l 0"
q_sendkey ret
sleep 4

q_stop
echo "[probe] done -> $SER"
