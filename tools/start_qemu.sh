#!/bin/bash
# HEPU QEMU 稳定启动脚本
cd /home/user/.super_doubao/super-doubao-runtime/workspace/hepu-os || exit 1
rm -f /tmp/hmon.sock /tmp/hserial.log /tmp/qemu_out.log
export LD_LIBRARY_PATH=$PWD/tools/qemu/x/usr/lib/x86_64-linux-gnu:$PWD/tools/qemu/x/usr/lib:$PWD/tools/qemu/x/lib/x86_64-linux-gnu
setsid $PWD/tools/qemu/x/usr/bin/qemu-system-x86_64 \
  -L $PWD/tools/qemu/x/usr/share/qemu -m 256M -nic none -vga std -display none \
  -kernel build_v250/kernel.elf -hda build/ext2disk.img \
  -monitor unix:/tmp/hmon.sock,server,nowait \
  -serial file:/tmp/hserial.log >/tmp/qemu_out.log 2>&1 < /dev/null &
echo $! > /tmp/qemu.pid
sleep 3
pgrep -c qemu-system
