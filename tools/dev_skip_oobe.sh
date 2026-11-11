#!/bin/bash
# 开发/测试辅助：在镜像 ext2 分区预置 OOBE 完成状态，跳过首设向导。
# 仅用于 QEMU 自动化验证，不参与正式发行流程。
IMG=${1:-/tmp/hepu64_single.img}
PART=/tmp/_part_dev.ext2
dd if=$IMG bs=512 skip=13000 of=$PART 2>/dev/null
printf 'oobe_done=1\nlang=zh\ntz_min=480\n' > /tmp/_system.conf
echo "write /tmp/_system.conf /system.conf" | debugfs -w $PART 2>/dev/null
printf 'zh\n' > /tmp/_lang.conf
echo "write /tmp/_lang.conf /lang.conf" | debugfs -w $PART 2>/dev/null
dd if=$PART bs=512 of=$IMG seek=13000 conv=notrunc 2>/dev/null
echo "oobe skipped"
# 预置 /account.conf 让 auth64_is_configured() 为真
dd if=$IMG bs=512 skip=13000 of=$PART 2>/dev/null
echo "write /tmp/_account.conf /account.conf" | debugfs -w $PART 2>/dev/null
dd if=$PART bs=512 of=$IMG seek=13000 conv=notrunc 2>/dev/null
echo "account preset"
