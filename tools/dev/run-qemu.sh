#!/usr/bin/env bash
# 启动赫普OS（后台）并等待进入桌面。
# 用法: tools/dev/run-qemu.sh [--fresh]    --fresh 清空旧串口日志
set -u
here=$(cd "$(dirname "$0")" && pwd)
. "$here/qemu-common.sh"

[ "${1:-}" = "--fresh" ] && rm -f "$SERIAL"
stop_qemu
: > "$SERIAL" 2>/dev/null || true

# shellcheck disable=SC2046
qemu-system-x86_64 $(qemu_common_args) -daemonize -pidfile "$PID" 2>/dev/null

if wait_desktop 12; then
    echo "[ok] 桌面已启动  serial=${SERIAL}  mon=${MON}  qmp=${QMP}"
else
    echo "[fail] 未进入桌面, 末尾日志:"
    tail -5 "$SERIAL"
    echo "提示: 若停在 '[SMP] AP trampoline copied to 0x8000', 重跑一次即可(已知偶发)"
    exit 1
fi
