#!/bin/bash
# ============================================================================
# 沙箱环境恢复脚本（V0.33）
# ----------------------------------------------------------------------------
# 为什么需要它：跑 HepuOS 的构建/回归需要 qemu-system-x86_64、nasm、e2fsprogs，
# 这三个都只能靠 apt 装。而这个沙箱会**周期性重置**，重置后：
#   * apt 安装的二进制消失（qemu / nasm 都没了）
#   * apt 的 package 索引也被清掉（apt-get install 报 "Unable to locate package"）
#   * /tmp 被清空（镜像、日志全没）
#   * 项目的 build_x86_64/ 产物目录有时也会没
#
# 一句话恢复：
#     bash tools/env_restore.sh
#
# 幂等：已经装好就直接跳过；缺什么补什么。
# ============================================================================
set -u

TUNA="http://mirrors.tuna.tsinghua.edu.cn/debian"
TUNA_SEC="http://mirrors.tuna.tsinghua.edu.cn/debian-security"

echo "=== [1/4] 检查 apt 源 ==="
SOURCES=/etc/apt/sources.list.d/debian.sources
if ! grep -q "tuna.tsinghua" "$SOURCES" 2>/dev/null; then
    echo "  源里没有清华源（沙箱默认的阿里云源在这个沙箱里解析到内网地址、不通），改写…"
    cat > "$SOURCES" <<EOF
Types: deb
URIs: $TUNA
Suites: bookworm bookworm-updates
Components: main
Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg

Types: deb
URIs: $TUNA_SEC
Suites: bookworm-security
Components: main
Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg
EOF
else
    echo "  已是清华源 ✓"
fi

echo "=== [2/4] 刷新 apt 索引 ==="
apt-get update -o Acquire::Retries=2 2>&1 | tail -1

echo "=== [3/4] 安装构建/回归所需工具 ==="
MISSING=""
for pkg_cmd in "qemu-system-x86:qemu-system-x86_64" "nasm:nasm" "e2fsprogs:mkfs.ext2"; do
    pkg="${pkg_cmd%%:*}"; cmd="${pkg_cmd##*:}"
    if command -v "$cmd" >/dev/null 2>&1; then
        echo "  $cmd ✓"
    else
        MISSING="$MISSING $pkg"
    fi
done
if [ -n "$MISSING" ]; then
    echo "  缺:$MISSING → 安装中…"
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends $MISSING 2>&1 | tail -2
else
    echo "  全部就位 ✓"
fi

echo "=== [4/4] 校验 ==="
RC=0
for cmd in qemu-system-x86_64 nasm mkfs.ext2 debugfs gcc; do
    if command -v "$cmd" >/dev/null 2>&1; then
        printf "  %-22s %s\n" "$cmd" "$(command -v $cmd)"
    else
        printf "  %-22s 缺失 ✗\n" "$cmd"
        RC=1
    fi
done
qemu-system-x86_64 --version 2>/dev/null | head -1
nasm -v 2>/dev/null

if [ "$RC" -eq 0 ]; then
    echo "=== 环境就绪 ✓ ==="
else
    echo "=== 仍有缺失 ✗ ==="
fi
exit $RC
