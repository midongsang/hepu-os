#!/bin/bash
# ============================================================================
# 32 位内核冒烟回归（Hepu OS 的第二套内核）
#
# 为什么需要它：32 位是**并行维护的第二套内核**，但在此之前它连构建都过不去，
# 而且没有任何回归 —— 改了没人知道坏没坏。这个脚本给一条最低限度的生命线。
#
# 判据（三条全成立才算过）：
#   ① [SERIAL] COM1 16550 UART ready —— 内核进到了串口初始化之后（最早的可观测点）
#   ② [INITRD] base=... head=0x55504548 —— initrd 正确加载（head = "HEPU" 魔数）
#   ③ 没有 PANIC / #GP / #PF / fatal —— 没崩
#   ④ [NET-TEST] === Self-Test Complete === —— 网络自检走完
#   ⑤ [DESKTOP] elastic free desktop init done —— 跑到桌面初始化
#
# 注意：32 位用 multiboot，`qemu-system-i386 -kernel kernel.elf`（**必须用 .elf**，
# 纯 .bin 没有 multiboot 头，QEMU 会静默不动）。日志零输出就是这个原因。
#
# 用法： ./tools/regress32.sh
# ============================================================================
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CHAT=$(dirname "$ROOT")
B=$CHAT/.toolchain; T=$B/pkg/root2; RUN=$B/run
export PATH=$T/sbin:$T/usr/bin:$PATH
export LD_LIBRARY_PATH=$T/usr/lib/x86_64-linux-gnu:$T/lib/x86_64-linux-gnu
export QEMU_MODULE_DIR=$T/usr/lib/x86_64-linux-gnu/qemu

echo "== 构建 32 位内核"
make -C "$ROOT" -j4 >/dev/null 2>&1
ELF="$ROOT/build_v250/kernel.elf"
[ -f "$ELF" ] || { echo "== FAIL: 32 位内核没编出来"; exit 1; }

echo "== 起 QEMU（i386, multiboot, 串口 $RUN/ser32.log）"
pkill -9 -x qemu-system-i386 2>/dev/null; sleep 1
rm -f "$RUN/ser32.log"
timeout 70 qemu-system-i386 -L $B/pkg/datadir \
  -kernel "$ELF" -m 256 -display none \
  -serial file:"$RUN/ser32.log" -no-reboot >/dev/null 2>&1
strings "$RUN/ser32.log" > "$RUN/ser32.txt" 2>/dev/null

have_serial=0; have_initrd=0; have_fault=0; have_net=0; have_desk=0
grep -qF '[SERIAL] COM1 16550 UART ready' "$RUN/ser32.txt" && have_serial=1
grep -qF 'head=0x55504548' "$RUN/ser32.txt" && have_initrd=1
grep -qE 'PANIC|#GP|#PF|fatal error' "$RUN/ser32.txt" && have_fault=1
# ④ 网络自检走完（曾因 32 位全程 IF=0、pit tick 恒 0 而死在 DHCP 等待循环里）
grep -qF '[NET-TEST] === Self-Test Complete ===' "$RUN/ser32.txt" && have_net=1
# ⑤ 桌面初始化返回（说明跑道已经延伸到 app framework 之后）
grep -qF '[DESKTOP] elastic free desktop init done' "$RUN/ser32.txt" && have_desk=1
lines=$(wc -l < "$RUN/ser32.txt")
echo "serial=$have_serial initrd=$have_initrd net=$have_net desk=$have_desk fault=$have_fault lines=$lines"
if [ "$have_serial" -eq 1 ] && [ "$have_initrd" -eq 1 ] && [ "$have_net" -eq 1 ] \
   && [ "$have_desk" -eq 1 ] && [ "$have_fault" -eq 0 ]; then
  echo "== 32 位内核冒烟 PASS"
  exit 0
fi
echo "== 32 位内核冒烟 FAIL"
exit 1
