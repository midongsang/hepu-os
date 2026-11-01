#!/bin/bash
# 赫普OS QEMU启动脚本
# 用法: ./run_qemu.sh [32|64] [镜像文件]

ARCH=${1:-32}
IMAGE=${2:-build/hepu-os-hybrid-v0.10.5.img}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$SCRIPT_DIR/qemu_libs/usr/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"

if [ "$ARCH" = "64" ]; then
    QEMU="$SCRIPT_DIR/bin/qemu-system-x86_64"
    MEMORY=512
else
    QEMU="$SCRIPT_DIR/bin/qemu-system-i386"
    MEMORY=256
fi

echo "启动赫普OS..."
echo "架构: $ARCH 位"
echo "内存: ${MEMORY}MB"
echo "镜像: $IMAGE"
echo ""

$QEMU \
    -m $MEMORY \
    -hda "$IMAGE" \
    -boot c \
    -vga std \
    -net none \
    -serial stdio \
    -display gtk \
    "$@"
