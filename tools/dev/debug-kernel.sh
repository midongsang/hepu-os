#!/usr/bin/env bash
# 用 QEMU 的 gdb stub 调试内核: 启动即挂起(-S), 打印可直接粘贴的 gdb 命令。
#
# 生成符号信息: make -C kernel/arch/x86_64 DEBUG=1 (若 Makefile 支持), 产物
#   build_x86_64/hepu-kernel64.elf 本身带符号, 直接加载即可。
#
# 用法: tools/dev/debug-kernel.sh [端口]   默认 1234
set -u
here=$(cd "$(dirname "$0")" && pwd)
. "$here/qemu-common.sh"

PORT=${1:-1234}
stop_qemu
: > "$SERIAL" 2>/dev/null || true

# shellcheck disable=SC2046
qemu-system-x86_64 $(qemu_common_args) -s -S -daemonize -pidfile "$PID" 2>/dev/null
sleep 1

ELF="$(cd "$here/../.." && pwd)/build_x86_64/hepu-kernel64.elf"
cat <<EOF
QEMU 已在 ${PORT} 端口挂起等待 gdb。

    gdb -q ${ELF} \\
      -ex 'set architecture i386:x86-64' \\
      -ex 'target remote :${PORT}' \\
      -ex 'break kernel_main' \\
      -ex 'continue'

常用:
    info registers          看寄存器
    x/16i \$pc             看当前指令
    monitor screendump /tmp/g.ppm    仍可在 gdb 里截图
    layout src              有源码时打开源码视图

串口日志: ${SERIAL}
EOF
