#!/bin/bash
# 赫普OS QEMU 测试脚本 (-kernel 直载)
cd "$(dirname "$0")/.."
export LD_LIBRARY_PATH=./tools/qemu/x/usr/lib/x86_64-linux-gnu:./tools/qemu/x/usr/lib:./tools/qemu/x/lib/x86_64-linux-gnu
rm -f /tmp/hmon.sock
exec ./tools/qemu/x/usr/bin/qemu-system-x86_64 -L ./tools/qemu/x/usr/share/qemu -m 256M -kernel build/hepu-kernel.elf -nic none -vga std -display none -monitor unix:/tmp/hmon.sock,server,nowait -serial file:/tmp/hserial.log "$@"
