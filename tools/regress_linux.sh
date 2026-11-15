#!/bin/bash
# ============================================================================
# Linux 兼容层端到端回归（DESIGN.md 约束一 第 1 步的验证兜底）
#
# 为什么单独一个脚本：
#   .toolchain/regress.sh 只跑原生 child（PASS: child ran），**从不跑 Linux ELF**。
#   于是"翻译表搬出 syscall64_full.c 之后 Linux 程序还跑不跑"这件事没有实测依据，
#   只有"同表同判定"的等价性推理。这个脚本把那一格补上。
#
# 做法：
#   用 BOOT_SELFTEST=1 重建内核（启动自检电池里有 /bin/hello_linux.elf 一条），
#   QEMU 起来后抓串口，要求同时满足三条：
#     ① [LINUX] Running /bin/hello_linux.elf   —— exec 没被拒
#     ② Hello Linux!                            —— 程序真的跑到了 write(1)
#     ③ [LINUX] returned                        —— 正常退出，没崩
#   只有 ① 不算通过：exec 成功但程序跑挂一样会打 ①。
#
# 用法： ./.toolchain/regress_linux.sh
# ============================================================================
set -u
# 路径按脚本自身位置推导: <CHAT>/hepu-os/tools/regress_linux.sh
ROOT=$(cd "$(dirname "$0")/.." && pwd)      # <CHAT>/hepu-os
CHAT=$(dirname "$ROOT")                      # <CHAT>
B=$CHAT/.toolchain; T=$B/pkg/root2; RUN=$B/run; V=$CHAT/.vm-assets
export PATH=$T/sbin:$T/usr/bin:$PATH
export LD_LIBRARY_PATH=$T/usr/lib/x86_64-linux-gnu:$T/lib/x86_64-linux-gnu
export QEMU_MODULE_DIR=$T/usr/lib/x86_64-linux-gnu/qemu

echo "== 构建 BOOT_SELFTEST=1 内核"
make -C "$ROOT/kernel/arch/x86_64" clean >/dev/null 2>&1
make -C "$ROOT/kernel/arch/x86_64" -j4 BOOT_SELFTEST=1 >/dev/null 2>&1
BIN="$ROOT/build_x86_64/hepu-kernel64.bin"
[ -f "$BIN" ] || { echo "== FAIL: 内核没编出来"; exit 1; }

echo "== 重建镜像"
bash "$CHAT/shots/.mkimg.sh" >/dev/null 2>&1

echo "== 起 QEMU（串口 $RUN/ser_linux.log）"
pkill -9 -x qemu-system-x86 2>/dev/null; sleep 1
cp -f $V/OVMF_VARS_4M.fd $RUN/V_linux.fd
rm -f $RUN/ser_linux.log
qemu-system-x86_64 -L $B/pkg/datadir \
  -drive if=pflash,format=raw,readonly=on,file=$V/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=$RUN/V_linux.fd \
  -drive format=raw,file=$RUN/shot.img,file.locking=off \
  -m 1024 -smp 2 -display none -serial file:$RUN/ser_linux.log \
  -nic user,model=e1000 -daemonize -pidfile $RUN/qemu_linux.pid 2>&1 | head -2

for i in $(seq 1 60); do
  sleep 1
  grep -aq '[LINUX] returned' $RUN/ser_linux.log 2>/dev/null && break
done
sleep 2
pkill -9 -x qemu-system-x86 2>/dev/null

# ---- 判据 ----
# 串口文件是二进制的，且多核下日志行会被别的核插进来撕开（实测
# "[LINUX] Running /bin/hello_linux.elf..." 在 strings 里完整，直接 grep 却匹配不上）。
# 所以先 strings 转成纯文本再判；且必须用 grep -F，
# 否则 "[LINUX]" 会被当成字符类，一辈子匹配不上。
L=$RUN/ser_linux.log
strings $L > $RUN/ser_linux.txt 2>/dev/null
have_exec=0; have_hello=0; have_ret=0; have_exc=0
grep -qF '[LINUX] Running /bin/hello_linux.elf' $RUN/ser_linux.txt && have_exec=1
grep -qF 'Hello Linux!' $RUN/ser_linux.txt && have_hello=1
grep -qF '[LINUX] returned' $RUN/ser_linux.txt && have_ret=1
# 注意：BOOT_SELFTEST 电池里本来就有"故意的坏地址测试"(crash_jump)，
# 出现 EXCEPTION 不代表失败 —— 只统计"未被用户态异常处理接住"的那种。
grep -qF 'EXCEPTION' $RUN/ser_linux.txt && have_exc=1
unhandled=$(grep -c 'EXCEPTION' $RUN/ser_linux.txt 2>/dev/null || echo 0)
handled=$(grep -c 'user-mode fault handled' $RUN/ser_linux.txt 2>/dev/null || echo 0)
echo "exec=$have_exec hello=$have_hello returned=$have_ret exc=$have_exc(handled=$handled)"
if [ $have_exec -eq 1 ] && [ $have_hello -eq 1 ] && [ $have_ret -eq 1 ]; then
  echo "== Linux 兼容端到端 PASS"
  exit 0
fi
echo "== Linux 兼容端到端 FAIL"
exit 1
