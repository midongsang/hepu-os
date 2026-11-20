#!/bin/bash
# verify_p1p3.sh — shell FS→VFS 改造（P1/P2/P3）落盘验证，一条命令跑完
#
# 验的是什么：
#   P1  shell 的 ls/stat 走 vfs_readdir / vfs_find，不再是 ide_ext2_list_dir
#   P2  shell 的 cat/write 走 vfs_open+read/write+close，write 后 vfs_sync 真落盘
#   P3  vfs_mkdir / vfs_rmdir 接 ext2 后端，重启后目录还在
#
# 怎么验：
#   同一张 ext2 盘，两次独立 QEMU 进程。
#   第一次标记 /vfs_selftest='w' 跑写阶段（写文件、建目录）；
#   进程退出后把标记改成 'r'，第二次启动跑读阶段（看东西还在不在、再删掉）。
#   两次都 RESULT: PASS 才算过。
#
# 为什么用两次启动而不是 system_reset：
#   无头 QEMU 下 system_reset 之后第二次启动不产日志（这轮踩过四个路子都失败），
#   两次独立进程最稳，而且证据链更干净——第一次的进程根本不知道第二次存在。
#
# 用法：bash tools/verify_p1p3.sh

set -u
cd "$(dirname "$0")/.." || exit 1
ROOT=$(pwd)

# ---- 工具链 ----
TC=${HEPU_TOOLCHAIN:-$(dirname "$ROOT")/.toolchain/pkg/root2}
DD=${HEPU_DATADIR:-$(dirname "$ROOT")/.toolchain/pkg/datadir}
export PATH=$TC/sbin:$TC/usr/bin:$PATH
export LD_LIBRARY_PATH=$TC/usr/lib/x86_64-linux-gnu:$TC/lib/x86_64-linux-gnu
export QEMU_MODULE_DIR=$TC/usr/lib/x86_64-linux-gnu/qemu

ELF=build_v250/kernel.elf
[ -f "$ELF" ] || { echo "缺少 $ELF，先 make"; exit 1; }

DISK=/tmp/vp32disk.img
PART=/tmp/vp32part.img
MWORK=/tmp/vpwork
LOGW=/tmp/vp_write.log
LOGR=/tmp/vp_read.log
rm -rf "$MWORK"; mkdir -p "$MWORK"

cleanup() { pkill -9 -f "qemu-system-i386.*$DISK" 2>/dev/null; }
trap cleanup EXIT

# ---- 1. 造盘：MBR(0x55AA + 0x83 分区 @ LBA2048) + ext2 分区 ----
echo "[1/5] 造盘 ${DISK}"
python3 - <<PY
import struct
disk="$DISK"; part="$PART"
# ext2 分区 58MB
import subprocess, os
subprocess.run(["mke2fs","-q","-t","ext2","-b","1024","-F",part,"58368"],check=True)
with open(disk,"wb") as f: f.truncate(64*1024*1024)
mbr=bytearray(512)
# 一个分区：起始 LBA 2048，类型 0x83，长度按 512B 扇区算
start=2048; sectors=58368*1024//512
mbr[446+0:446+16]=struct.pack("<BBBBBBBBII",0x00,0xFE,0xFF,0xFF,0x83,0xFE,0xFF,0xFF,start,sectors)
mbr[510]=0x55; mbr[511]=0xAA
with open(disk,"r+b") as f:
    f.seek(0); f.write(bytes(mbr))
with open(part,"rb") as src, open(disk,"r+b") as dst:
    dst.seek(start*512)
    dst.write(src.read())
print("  MBR + ext2 分区 @ LBA 2048 就绪")
PY

# ---- 2. 写阶段标记 ----
echo "[2/5] 注入标记 /vfs_selftest='w'"
printf 'w' > "$MWORK/mark_w"
debugfs -w -R "write $MWORK/mark_w /vfs_selftest" "$PART" >/dev/null 2>&1
# 把改过的分区写回盘
dd if="$PART" of="$DISK" bs=512 seek=2048 conv=notrunc status=none

# ---- 3. 第一次启动：写阶段 ----
echo "[3/5] 第一次启动（写阶段）"
pkill -9 -f qemu-system-i386 2>/dev/null; sleep 2
rm -f "$LOGW"
timeout 70 qemu-system-i386 -L "$DD" -kernel "$ELF" -m 256 -display none \
    -drive file="$DISK",format=raw,if=ide -serial file:"$LOGW" >/dev/null 2>&1
W_RC=$?
[ -f "$LOGW" ] || { echo "  ✗ 写阶段没产出日志（QEMU 被杀/起不来，rc=$W_RC）"; exit 2; }
grep -a "VFS-TEST" "$LOGW" | sed 's/^/  /'
W_PASS=$(grep -ac "VFS-TEST. RESULT: PASS" "$LOGW")

# ---- 4. 换标记为读阶段 ----
echo "[4/5] 标记改成 /vfs_selftest='r'"
# 关键：先把盘上的分区取回来（含第一次 QEMU 写进去的数据），再改标记，
# 否则用旧的 $PART 覆盖回去会把刚写的数据全抹掉 —— 那读阶段必然"盘上没有"。
SECTORS=$((58368 * 1024 / 512))
dd if="$DISK" of="$PART" bs=512 skip=2048 count=$SECTORS conv=notrunc status=none
debugfs -w -R "rm /vfs_selftest" "$PART" >/dev/null 2>&1
printf 'r' > "$MWORK/mark_r"
debugfs -w -R "write $MWORK/mark_r /vfs_selftest" "$PART" >/dev/null 2>&1
dd if="$PART" of="$DISK" bs=512 seek=2048 conv=notrunc status=none

echo "[4/5] 第二次启动（读阶段，同一张盘）"
pkill -9 -f qemu-system-i386 2>/dev/null; sleep 2
rm -f "$LOGR"
timeout 70 qemu-system-i386 -L "$DD" -kernel "$ELF" -m 256 -display none \
    -drive file="$DISK",format=raw,if=ide -serial file:"$LOGR" >/dev/null 2>&1
R_RC=$?
[ -f "$LOGR" ] || { echo "  ✗ 读阶段没产出日志（rc=$R_RC）"; exit 2; }
grep -a "VFS-TEST" "$LOGR" | sed 's/^/  /'
R_PASS=$(grep -ac "VFS-TEST. RESULT: PASS" "$LOGR")

# ---- 5. 判定 ----
echo
echo "[5/5] 结论"
if [ "$W_PASS" -ge 1 ] && [ "$R_PASS" -ge 1 ]; then
    echo "  ✓ P1/P2/P3 全过：写阶段 PASS + 读阶段 PASS（跨启动存活）"
    echo "    日志：$LOGW  $LOGR"
    exit 0
else
    echo "  ✗ 未过（写阶段 PASS=$W_PASS，读阶段 PASS=$R_PASS）"
    echo "    日志：$LOGW  $LOGR"
    exit 1
fi
