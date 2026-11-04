#!/bin/bash
# ============================================================================
# 赫普OS ext2 主机端测试夹具生成器（V0.33）
#
# tools/host-tests/ext2_test.c 需要两个 ext2 镜像（1024 / 4096 字节块）以及
# 一份内容完全一致的主机端目录 fsroot/，用来交叉校验 ext2 共享算法读出来的
# 字节与源文件一致。这些夹具此前没有进仓库，测试因此一直无法运行。
#
# 本脚本生成：
#   fsroot/hello.txt      小文件
#   fsroot/big.bin        40KB（超过 12 个直接块，必须走间接块）
#   fsroot/etc/cfg        子目录
#   fsroot/bin/file1      单行
#   fsroot/bin/file40     40 行
#   img1024.img / img4096.img  上述内容的 ext2 镜像（两种块大小）
# ============================================================================
set -eu
DEST=${1:?用法: gen_ext2_fixtures.sh <目标目录>}

rm -rf "$DEST"
mkdir -p "$DEST/fsroot/etc" "$DEST/fsroot/bin"

printf 'hello from ext2\n' > "$DEST/fsroot/hello.txt"
printf 'cfg=1\nmode=rw\n' > "$DEST/fsroot/etc/cfg"
printf 'one\n' > "$DEST/fsroot/bin/file1"
seq 1 40 > "$DEST/fsroot/bin/file40"
# big.bin：确定性内容，且大于 12*1024 字节，必须走一级间接块
python3 - "$DEST/fsroot/big.bin" <<'PYEOF'
import sys
with open(sys.argv[1], "wb") as f:
    for i in range(40):
        f.write(bytes(((i * 7 + j) & 0xFF) for j in range(1000)))
PYEOF

for BS in 1024 4096; do
    IMG="$DEST/img${BS}.img"
    truncate -s 16M "$IMG"
    mkfs.ext2 -q -F -b "$BS" -I 128 -L HEPU "$IMG"
    debugfs -w "$IMG" >/dev/null 2>&1 <<EOF
mkdir /etc
mkdir /bin
EOF
    debugfs -w "$IMG" >/dev/null 2>&1 <<EOF
write $DEST/fsroot/hello.txt /hello.txt
write $DEST/fsroot/big.bin /big.bin
write $DEST/fsroot/etc/cfg /etc/cfg
write $DEST/fsroot/bin/file1 /bin/file1
write $DEST/fsroot/bin/file40 /bin/file40
EOF
done

echo "[fixtures] ext2 测试夹具已生成: $DEST"
