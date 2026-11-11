#!/usr/bin/env bash
# 统一的 QEMU 启动参数 —— 所有调试/截图/回归脚本都从这里取，避免"某次手敲漏参数"。
#
# 已知约束(踩过的坑, 改动前请先读):
#   -m 1024      : -m 2048 会卡 AP trampoline, 截图全黑
#   -smp 1       : -smp 4 必卡在 0x8000
#   -device qemu-xhci + usb-mouse : 输入走 XHCI; HMP mouse_button 对 USB 鼠标无效,
#                                   注入按钮必须用 QMP input-send-event
#   偶发卡在 "[SMP] AP trampoline copied to 0x8000" : 重试一次即可

IMG=${IMG:-/tmp/hepu64_single.img}
SERIAL=${SERIAL:-/tmp/hepu_serial.log}
MON=${MON:-/tmp/hepu_mon.sock}
QMP=${QMP:-/tmp/hepu_qmp.sock}
PID=${PID:-/tmp/qemu.pid}

qemu_common_args() {
    # 注意: 必须是单行输出 —— 带反斜杠续行的 echo 结果在 $(...) 里会变成字面参数
    echo "-drive format=raw,file=${IMG} -m 1024 -smp 1 -monitor unix:${MON},server,nowait -qmp unix:${QMP},server,nowait -serial file:${SERIAL} -device qemu-xhci -device usb-kbd -device usb-mouse -vga std"
}

stop_qemu() {
    if [ -f "$PID" ]; then kill "$(cat "$PID")" 2>/dev/null; sleep 3; rm -f "$PID"; fi
}

wait_desktop() {
    local limit=${1:-12}
    for _ in $(seq 1 "$limit"); do
        sleep 5
        grep -q 'Hepu Desktop started' "$SERIAL" 2>/dev/null && return 0
    done
    return 1
}
