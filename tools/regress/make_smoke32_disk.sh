#!/bin/bash
# ============================================================================
# 确定性生成 32 位冒烟用 ext2 测试盘（MBR + 0x83 分区 @ LBA2048 + ext2 分区）。
#
# 用途：tools/regress/smoke32.sh 默认带盘启动时调用，让 p3-security 两个用例
#       有真实 ext2 后端可跑（不再退化为 KUNIT_SKIP）。
#
# 口径（与 tools/verify_p1p3.sh 的造盘逻辑保持一致，仅去掉其全局 pkill）：
#   - 64MB 整盘镜像；ext2 分区 58368 块 × 1024B，类型 0x83，起始 LBA 2048。
#   - 每次运行从干净盘开始：先删旧盘与旧分区文件。mke2fs -F 只重写元数据，
#     不会清空旧文件残留的块内容/目录项；复用旧分区会让 mkdir/mv 误判
#     “已存在”（verify_p1p3.sh 里有同样注释）。
#   - 本脚本只造镜像、不起 QEMU，不做任何 pkill；实例起停由调用方按 pidfile 管理。
#
# 环境变量：
#   HEPU_SMOKE32_DISK  整盘镜像路径（默认 /tmp/hepu-smoke32-test.img）
#
# 注意：镜像必须放在 /tmp（overlay/ext4）一类本地文件系统上。/home/user 下的
# 网络文件系统在 QEMU 被 SIGKILL 后会残留写锁，导致下一次启动报
# "Failed to get write lock"；既有 verify_p1p3.sh / gate.sh 的测试盘也都放 /tmp。
#
# 用法：bash tools/regress/make_smoke32_disk.sh
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"

DISK="${HEPU_SMOKE32_DISK:-/tmp/hepu-smoke32-test.img}"
PART="${DISK%.img}.part.img"
BLOCKS=58368          # ext2 块数（1024B/块，约 58MB），与 verify_p1p3.sh 同口径
DISK_MB=64
START_LBA=2048

mkdir -p "$(dirname "$DISK")"

# 干净起点：删除旧分区文件与整盘镜像，避免旧目录项残留干扰后续 mkdir/mv 判定。
rm -f "$DISK" "$PART"

log "生成确定性 ext2 测试盘: $DISK (${DISK_MB}MB, ext2 ${BLOCKS} 块 @ LBA ${START_LBA})"

python3 - <<PY
import struct, subprocess
disk = "$DISK"; part = "$PART"
blocks = $BLOCKS; disk_mb = $DISK_MB; start = $START_LBA

# 分区文件刚被删除，这里从全零空白 ext2 开始构建。
subprocess.run(["mke2fs", "-q", "-t", "ext2", "-b", "1024", "-F", part, str(blocks)],
               check=True)

with open(disk, "wb") as f:
    f.truncate(disk_mb * 1024 * 1024)

sectors = blocks * 1024 // 512
mbr = bytearray(512)
# 单分区：起始 LBA 2048，类型 0x83（Linux ext2）
mbr[446:462] = struct.pack("<BBBBBBBBII",
    0x00, 0xFE, 0xFF, 0xFF, 0x83, 0xFE, 0xFF, 0xFF, start, sectors)
mbr[510] = 0x55
mbr[511] = 0xAA
with open(disk, "r+b") as f:
    f.seek(0)
    f.write(bytes(mbr))

with open(part, "rb") as src, open(disk, "r+b") as dst:
    dst.seek(start * 512)
    dst.write(src.read())

print(f"  MBR + ext2 分区 @ LBA {start} 就绪（{sectors} 扇区）")
PY

# 落盘后校验：超级块可读，避免把坏镜像交给冒烟用例。
if ! debugfs -R "stats" "$PART" >/dev/null 2>&1; then
    log "✗ ext2 分区校验失败（debugfs 无法读取超级块）"
    exit 1
fi

ls -l "$DISK"
log "测试盘就绪: $DISK"
