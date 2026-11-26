#!/bin/bash
# ============================================================================
# OPEN-06 探针取证启动脚本（仅取证用，不进发布构建路径）。
#
# 用途：
#   1. 用 SVC_PROBE=1 编出的 64 位探针内核打包成 probe.img（见 README 步骤）；
#   2. OVMF 选保护模式（菜单 3，hepu.graphics=0）进 64 位文本 shell；
#   3. 串口日志保留 失败 -> 退避排程 -> 退避到期重试 -> 成功(healthy) 链路；
#   4. 文本 shell 里敲 `service list` / `service status svc-probe` 取 health 列。
#
# 纪律：QEMU 全机串行走 /tmp/hepu-qemu.lock；只按 pidfile 精确退出；
#       默认 HPET，不加 -no-hpet。
# ============================================================================
set -u

REG="$(cd "$(dirname "$0")/../../../tools/regress" && pwd)"
source "$REG/env.sh"
source "$REG/lib_qemu.sh"

EV="$WT/docs/status/evidence-open06-2026-11-26"
export IMG="$EV/build/probe.img"
SER="$EV/serial-64-probe.log"
OUT="$EV/frames64-probe"
mkdir -p "$OUT"
rm -f "$SER"

exec {qfd}>"$QEMU_LOCK"
flock -w 2400 "$qfd" || { echo "等不到 QEMU 锁"; exit 1; }

q_start "$SER"
echo "[probe] QEMU started, inst=$INST"

# 引导菜单：选 3 = 保护模式（文本 shell，图形子系统跳过）
sleep 5
q_sendkey 3

# 等文本 shell 就绪
q_wait_for "64-bit Shell" 60 "$SER" || echo "[probe] WARN: 未等到 shell 标记"
# 等探针退避重试链路落稳（前 3 次失败后第 4 次成功）
sleep 12

# 本地逐字符输入：空格->spc，连字符->minus（q_send_text 默认映射不含 minus）
send_text64() {
    local TXT="$1" CH
    local i
    for (( i=0; i<${#TXT}; i++ )); do
        CH=${TXT:i:1}
        case "$CH" in
            " ") q_sendkey spc ;;
            "-") q_sendkey minus ;;
            *)   q_sendkey "$CH" ;;
        esac
    done
}

# 取 health 列证据（第一轮：失败退避期间；第二轮：恢复 healthy 之后）
send_text64 "service list"
q_sendkey ret
sleep 2
send_text64 "service status svc-probe"
q_sendkey ret
sleep 3
send_text64 "service list"
q_sendkey ret
sleep 2
send_text64 "service status svc-probe"
q_sendkey ret
sleep 3

q_dump "$OUT/shell.ppm" "$OUT/shell.png" || true

# 多留一点时间让退避重试链路完整落盘
sleep 5
q_stop
echo "[probe] done -> $SER"
