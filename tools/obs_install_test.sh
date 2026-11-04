#!/bin/bash
# V0.13.44: OBS 兼容层安装测试 (debugfs 批量直写)
# 把 OBS Studio deb 解出的文件 (usr/local 布局) 写入 Hepu OS ext2 根分区,
# 验证"外部软件安装"机制; 运行测试由 QEMU 内 run /usr/local/bin/obs 完成
# 用法: bash tools/obs_install_test.sh <obs_root> [img]

PROJECT="$(cd "$(dirname "$0")/.." && pwd)"
OBS_ROOT=${1:-/tmp/obs_test/obs_root}
IMG=${2:-/tmp/hepu64_obs.img}
EXT2_LBA=13000
FS=/tmp/hepu_obs_fs.ext2

if [ ! -d "$OBS_ROOT" ]; then
    echo "ERROR: OBS root 不存在: $OBS_ROOT"
    exit 1
fi
if [ ! -f "$IMG" ]; then
    echo "ERROR: 目标镜像不存在: $IMG"
    exit 1
fi
if ! command -v debugfs &>/dev/null; then
    echo "ERROR: 需要 debugfs"
    exit 1
fi

# ext2 分区大小: 固定 500MB (留出 MBR+内核区, 且不越过镜像尾)
EXT2_SIZE_MB=500
EXT2_OFFSET=$((EXT2_LBA * 512))
rm -f $FS
dd if=/dev/zero of=$FS bs=1M count=$EXT2_SIZE_MB 2>/dev/null
mkfs.ext2 -F -L "HEPU_ROOT" $FS 2>/dev/null

# 生成 debugfs 命令文件: mkdir 所有目录 + write 所有文件
CMDF=/tmp/hepu_obs_cmds.txt
: > $CMDF
cd $OBS_ROOT
find . -type d | while read d; do
    rel="${d#.}"
    echo "mkdir $rel" >> $CMDF
done
find . -type f | while read f; do
    rel="${f#./}"
    echo "write $OBS_ROOT/$rel /$rel" >> $CMDF
done
echo "debugfs 命令数: $(wc -l < $CMDF)"

# 执行批量写入 (单进程, 顺序执行)
debugfs -w -f $CMDF $FS 2>/dev/null | tail -2

# 验证
echo "=== 安装验证 ==="
debugfs -R "ls /usr/local/bin" $FS 2>&1 | head -5
debugfs -R "stat /usr/local/bin/obs" $FS 2>&1 | grep -E "Size|Blocks" | head -2

# 写回镜像
dd if=$FS of=$IMG bs=512 seek=$EXT2_LBA conv=notrunc 2>/dev/null
echo "OK: OBS 已写入 $IMG (ext2 @ LBA$EXT2_LBA, ${EXT2_SIZE_MB}MB)"
