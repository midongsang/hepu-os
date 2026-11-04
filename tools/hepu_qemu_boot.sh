#!/bin/bash
# ============================================================================
# 赫普OS 64位 QEMU 启动回归助手 (V0.33)
# 用途: 拉起 64 位镜像, 抓串口日志, 可选注入按键/命令, 输出可判定结论
# 用法:
#   bash tools/hepu_qemu_boot.sh [--sec N] [--key KEY] [--cmd CMD]
#                               [--image IMG] [--log FILE] [--cpu CPU]
# 例:
#   bash tools/hepu_qemu_boot.sh --sec 30 --cmd "exec /bin/linux_pie_demo.elf"
# 退出码: 0=正常 1=QEMU提前挂掉
# ============================================================================
set -u
cd "$(dirname "$0")/.."

SEC=25
IMAGE=/tmp/hepu64_single.img
LOG=/tmp/hepu_serial.log
KEYS=""
CMD=""
CPU=""
MON=/tmp/hepu_mon.sock

while [ $# -gt 0 ]; do
    case "$1" in
        --sec)   SEC="$2"; shift 2 ;;
        --image) IMAGE="$2"; shift 2 ;;
        --log)   LOG="$2"; shift 2 ;;
        --key)   KEYS="$KEYS $2"; shift 2 ;;
        --cmd)   CMD="$2"; shift 2 ;;
        --cpu)   CPU="$2"; shift 2 ;;
        *) echo "unknown arg: $1"; exit 2 ;;
    esac
done

# QEMU 查找：优先系统安装的，其次项目内打包的。
# V0.33: 沙箱环境会周期性重置，apt 装的 qemu 与项目内 tools/qemu/ 打包副本
#        都可能被抹掉，所以这里逐个探测，并在都不可用时给出明确的恢复指引。
QEMU=""
for cand in "$(command -v qemu-system-x86_64 2>/dev/null)" \
            "$PWD/tools/qemu/x/usr/bin/qemu-system-x86_64"; do
    if [ -n "$cand" ] && [ -x "$cand" ]; then QEMU="$cand"; break; fi
done
if [ -z "$QEMU" ]; then
    echo "[BOOT] 找不到 qemu-system-x86_64。"
    echo "       沙箱重置会抹掉 apt 安装的包 —— 跑 'bash tools/env_restore.sh' 一键恢复。"
    exit 1
fi

# BIOS 固件：优先项目内的 bios-256k.bin；没有就让 QEMU 用自带的 seabios
# （QEMU 7.2 自带 /usr/share/seabios/bios-256k.bin，不指定 -bios 即可）。
FW=tools/firmware/bios-256k.bin
BIOS_ARG=""
[ -f "$FW" ] && BIOS_ARG="-bios $FW"

pkill -f "[q]emu-system-x86_64" 2>/dev/null
sleep 1
rm -f "$LOG" "$MON"

CPU_ARG=""
[ -n "$CPU" ] && CPU_ARG="-cpu $CPU"

setsid "$QEMU" \
    -m 512M \
    $CPU_ARG \
    $BIOS_ARG \
    -drive format=raw,file="$IMAGE" \
    -vga std -display none \
    -net none \
    -monitor unix:"$MON",server,nowait \
    -serial file:"$LOG" \
    >/tmp/hepu_qemu_out.log 2>&1 </dev/null &

sleep 1
QEMU_PID=$(pgrep -f "[q]emu-system-x86_64" | head -1)
[ -z "$QEMU_PID" ] && { echo "[BOOT] QEMU 启动失败"; cat /tmp/hepu_qemu_out.log; exit 1; }
echo "[BOOT] qemu pid=$QEMU_PID, 等待 ${SEC}s${CPU:+ (cpu=$CPU)}"

# 按键 / 命令注入（等 stage2 菜单过去、桌面起来之后再打）
if [ -n "$KEYS" ] || [ -n "$CMD" ]; then
    sleep 8
    python3 - "$MON" "$KEYS" "$CMD" <<'PYEOF'
import os, socket, sys, time
mon, keys, cmd = sys.argv[1], sys.argv[2], sys.argv[3]
try:
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(mon)
except Exception as e:
    print("[BOOT] monitor 连接失败:", e); sys.exit(0)

def sk(k, d=0.12):
    s.sendall(f'sendkey {k}\n'.encode()); time.sleep(d)

for k in keys.split():
    sk(k, 0.8)

if cmd:
    # 等桌面/终端起来再敲命令
    time.sleep(float(os.environ.get('HepuCmdDelay', '14')))
    for ch in cmd:
        sk('spc' if ch == ' ' else ch, 0.05)
    sk('ret', 1.2)
s.close()
PYEOF
fi

sleep "$SEC"

if ! pgrep -f "[q]emu-system-x86_64" >/dev/null; then
    echo "[BOOT] QEMU 已退出（异常）"
    tail -40 "$LOG" 2>/dev/null
    exit 1
fi
echo "[BOOT] QEMU 存活"
echo "===== 串口日志尾部 ====="
tail -${TAIL:-60} "$LOG" 2>/dev/null
echo "===== 日志文件: $LOG ====="
