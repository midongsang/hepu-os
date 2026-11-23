#!/bin/bash
# ============================================================================
# 跨架构 ext2 契约测试（D2）
#
# 目的：验证 32 位与 64 位两套完全独立的 ext2 实现，能在同一张盘上互相读写。
# 不比较 API（32 位是 VFS，64 位是 ext2_64 直调），只比较**盘上的结果**。
#
# 序列（同一张 /tmp/hepu64_single.img，ext2 分区 @ LBA 13000）：
#   1. 32 位启动，写 /c32.txt = "FROM_32BIT"，回读校验
#   2. 64 位启动，读 /c32.txt，写 /c64.txt = "FROM_64BIT"，回读校验
#   3. 32 位启动，读 /c64.txt 校验
#
# 判据：三段各自的 [CONFORM32]/[CONFORM64] 标记 + 最后 [CONFORM] RESULT: PASS
# 用法：bash tools/fs_conform.sh
# 退出码：0=PASS 1=FAIL
# ============================================================================
set -u
cd "$(dirname "$0")/.."

# 工具链（与其它验证脚本一致，均可用环境变量覆盖）
T=${T:-/home/user/Doubao/chats/38452875115179010/.toolchain/pkg/root2}
DD=${DD:-/home/user/Doubao/chats/38452875115179010/.toolchain/pkg/datadir}
export PATH=$T/sbin:$T/usr/bin:$PATH
export LD_LIBRARY_PATH=$T/usr/lib/x86_64-linux-gnu:$T/lib/x86_64-linux-gnu
export QEMU_MODULE_DIR=$T/usr/lib/x86_64-linux-gnu/qemu

IMG=/tmp/hepu64_single.img
PART=/tmp/conform_part.img
ELF32=build_v250/kernel.elf
LBA=13000
SECTORS=458752          # 224MB 系统分区（make_single_img64.sh: SYS_MB=224）
LOG32A=/tmp/conform32_a.log
LOG64=/tmp/conform64.log
LOG32D=/tmp/conform32_d.log

kill_qemu() {
    ps -eo pid,comm | awk '$2 ~ /qemu-system/ {print $1}' | while read -r p; do
        kill -9 "$p" 2>/dev/null
    done
}

# 从整盘镜像取出分区（debugfs 只能对分区镜像操作）
part_out() { dd if=$IMG of=$PART bs=512 skip=$LBA count=$SECTORS status=none; }
part_in()  { dd if=$PART of=$IMG bs=512 seek=$LBA count=$SECTORS conv=notrunc status=none; }

finish() {
    kill_qemu
    echo "[CONFORM] RESULT: $1"
    exit "$2"
}

echo "[CONFORM] 1/5 构建 32/64 位"
flock /tmp/hepu-build.lock -c 'make -j2' > /tmp/conform_build32.log 2>&1 || finish "FAIL (32 位构建)" 1
flock /tmp/hepu-build.lock -c 'make -C kernel/arch/x86_64 -j2' > /tmp/conform_build64.log 2>&1 || finish "FAIL (64 位构建)" 1
grep -Eqi "error" /tmp/conform_build32.log /tmp/conform_build64.log && finish "FAIL (构建有 error)" 1

echo "[CONFORM] 2/5 制作共享镜像"
bash tools/make_single_img64.sh > /tmp/conform_img.log 2>&1 || finish "FAIL (镜像制作)" 1
[ -f "$ELF32" ] || finish "FAIL (缺少 $ELF32)" 1

echo "[CONFORM] 3/5 32 位写 /c32.txt"
kill_qemu
part_out
printf 'c' > /tmp/conform_marker_c
debugfs -w -R "write /tmp/conform_marker_c /conform32_a" "$PART" >/dev/null 2>&1
part_in
rm -f "$LOG32A"
timeout 45s qemu-system-i386 -L "$DD" -kernel "$ELF32" -m 256 -display none \
    -drive file=$IMG,format=raw,if=ide -serial file:"$LOG32A" >/dev/null 2>&1
kill_qemu
grep -q "\[CONFORM32\] PHASE_C RESULT: PASS" "$LOG32A" \
    || { echo "[CONFORM] 32 位写阶段失败:"; grep -a "CONFORM32" "$LOG32A" || true; finish "FAIL" 1; }

echo "[CONFORM] 4/5 64 位读 c32 / 写 c64"
rm -f "$LOG64"
timeout 28s qemu-system-x86_64 -L "$DD" -drive format=raw,file=$IMG -m 1024 -smp 1 \
    -serial file:"$LOG64" -display none -vga std -nic user,model=e1000 >/dev/null 2>&1
kill_qemu
grep -q "\[CONFORM64\] read_c32=FROM_32BIT" "$LOG64" \
    || { echo "[CONFORM] 64 位读 c32 失败:"; grep -a "CONFORM64" "$LOG64" || true; finish "FAIL" 1; }
grep -q "\[CONFORM64\] verify_c64=FROM_64BIT" "$LOG64" \
    || { echo "[CONFORM] 64 位写 c64 失败"; finish "FAIL" 1; }

echo "[CONFORM] 5/5 32 位回读 /c64.txt"
# 关键：必须先把 64 位刚写的数据从盘上取回，再改标记，否则会覆盖掉
part_out
debugfs -w -R "rm /conform32_a" "$PART" >/dev/null 2>&1
printf 'd' > /tmp/conform_marker_d
debugfs -w -R "write /tmp/conform_marker_d /conform32_b" "$PART" >/dev/null 2>&1
part_in
rm -f "$LOG32D"
timeout 45s qemu-system-i386 -L "$DD" -kernel "$ELF32" -m 256 -display none \
    -drive file=$IMG,format=raw,if=ide -serial file:"$LOG32D" >/dev/null 2>&1
kill_qemu
grep -q "\[CONFORM32\] read_c64=FROM_64BIT" "$LOG32D" \
    || { echo "[CONFORM] 32 位读 c64 失败:"; grep -a "CONFORM32" "$LOG32D" || true; finish "FAIL" 1; }
grep -q "\[CONFORM32\] PHASE_D RESULT: PASS" "$LOG32D" \
    || { echo "[CONFORM] 32 位读阶段判定失败"; finish "FAIL" 1; }

echo "[CONFORM] 32->64: PASS (c32 被 64 位读出)"
echo "[CONFORM] 64->32: PASS (c64 被 32 位读出)"
finish "PASS" 0
