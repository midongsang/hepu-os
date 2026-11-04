#!/bin/bash
# ============================================================================
# 源码交付包打包脚本（V0.33.1）
# ----------------------------------------------------------------------------
# 交付准则要求：交付物必须能**从源码重建**，而不是只能从二进制反推。
# 这个脚本把剔除项写死，保证每次打出来的包内容一致、可重现。
#
# 用法：
#     bash tools/make_source_pkg.sh            # 输出 deliver/hepu-os-v0.33.1-src.tar.gz
#     VERIFY=1 bash tools/make_source_pkg.sh   # 打包后解包重建内核并与工作区产物逐字节比对
#
# 剔除项分两类：
#   ① 可再生成的产物（build_*/、logs/、user/build/、__pycache__、*.o …）
#   ② 与本项目无关的第三方二进制（tools/firmware/ 的 OVMF、tools/qemu/ 的 QEMU、
#      tools/gnu-efi/、历史交付物 tarball）
# ============================================================================
set -eu
cd "$(dirname "$0")/.."
PROJECT="$(pwd)"

VER=$(grep -oE 'HEPU_VERSION_STRING[[:space:]]+"V[0-9.]+"' kernel/include/hepu_brand.h \
      | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')
OUT_DIR="$PROJECT/deliver"
PKG="$OUT_DIR/hepu-os-v${VER}-src.tar.gz"

mkdir -p "$OUT_DIR"

echo "=== 打包源码：$PKG ==="

# --- 剔除清单 ---------------------------------------------------------------
EXCLUDES=(
    "./build_x86_64"          # 内核构建产物
    "./build_v250"            # 旧构建树
    "./logs"                  # 运行日志
    "./deliver"               # 交付物自身（避免自包含）
    "./test_results"          # 自动测试截图（体积大）
    "./user/build"            # 用户程序构建产物（可由 make -C user 重建）
    "./tools/share"           # 第三方共享资源
    "./tools/firmware"        # OVMF 固件（第三方，QEMU 自带 seabios 即可）
    "./tools/qemu"            # 打包的 QEMU 二进制（改用系统 qemu，见 tools/env_restore.sh）
    "./tools/gnu-efi"         # 第三方 UEFI 工具链
    "./hepu-os-v1.02-source-fix.tar.gz"   # 历史交付物
    "./__pycache__"
)

TAR_EXCLUDES=()
for e in "${EXCLUDES[@]}"; do
    TAR_EXCLUDES+=("--exclude=${e#/}")
done
# 通配剔除
TAR_EXCLUDES+=("--exclude=*.o" "--exclude=*.pyc" "--exclude=__pycache__")

tar czf "$PKG" "${TAR_EXCLUDES[@]}" -C "$PROJECT" .

echo "  大小：$(du -h "$PKG" | cut -f1)"
echo "  条目：$(tar tzf "$PKG" | wc -l)"

# --- 关键文件必须在内 --------------------------------------------------------
MISSING=0
for f in \
    kernel/arch/x86_64/user_process.c \
    kernel/arch/x86_64/user_stack64.c \
    kernel/arch/x86_64/hepu_smap.h \
    kernel/arch/x86_64/user_switch.S \
    kernel/arch/x86_64/elf64.c \
    kernel/arch/x86_64/syscall64_full.c \
    kernel/kernel_main_64.c \
    user/Makefile \
    user/user.ld \
    user/apps/comptest.c \
    user/apps/linux_pie_demo/linux_pie_demo.c \
    user/apps/pie_probe/pie_probe.c \
    user/apps/legacy/common_linux_abi.h \
    user/apps/legacy/hello_static.c \
    user/apps/legacy/hello_linux.c \
    user/apps/legacy/hello2.c \
    user/apps/legacy/hello3.c \
    user/apps/legacy/hello4.c \
    user/apps/legacy/hello_c2.c \
    user/apps/legacy/crash_jump.c \
    user/apps/legacy/glibc_test.c \
    user/apps/legacy/hello_libc.c \
    user/apps/legacy/dynhello.c \
    user/apps/legacy/dynhello2.c \
    user/apps/legacy/libtest.c \
    user/apps/legacy/libtest2.c \
    tools/make_single_img64.sh \
    tools/hepu_qemu_boot.sh \
    tools/env_restore.sh \
    tools/make_source_pkg.sh \
    tools/host-tests/run_host_tests.sh \
    docs/status/PROGRESS_V033.md \
    docs/status/OPEN_BUGS.md \
    docs/status/VERSIONING.md \
    docs/planning/ROADMAP.md \
    docs/planning/COMPAT_ROADMAP.md
do
    if ! tar tzf "$PKG" | grep -qx "./$f"; then
        echo "  ✗ 缺 $f"
        MISSING=1
    fi
done
[ "$MISSING" -eq 0 ] && echo "  ✓ 关键文件齐全"
[ "$MISSING" -eq 0 ] || exit 1

# --- 可选：解包重建内核并逐字节比对 -----------------------------------------
# 前置条件：**工作区的内核产物必须是干净全量重建的**。
#   构建输出目录在**顶层** build_x86_64/（不是 kernel/arch/x86_64/build_x86_64/），
#   删错路径会让 make 沿用旧 .o —— 实测踩过：交付内核混着 BOOT_SELFTEST=1 的对象，
#   与干净重建差 512 字节、哈希对不上。
#   正确做法：
#       cd "$PROJECT" && rm -f build_x86_64/*.o \
#           && (cd kernel/arch/x86_64 && make -j4)
if [ "${VERIFY:-0}" = "1" ]; then
    echo "=== 验证：解包 → 重建内核 → 逐字节比对 ==="
    TMP=$(mktemp -d)
    trap 'rm -rf "$TMP"' EXIT
    tar xzf "$PKG" -C "$TMP"
    ( cd "$TMP/kernel/arch/x86_64" && make -j4 >/dev/null 2>&1 )
    A="$TMP/build_x86_64/hepu-kernel64.bin"
    B="$PROJECT/build_x86_64/hepu-kernel64.bin"
    if [ ! -f "$A" ]; then
        echo "  ✗ 解包后重建失败"
        exit 1
    fi
    if cmp -s "$A" "$B"; then
        echo "  ✓ 内核产物逐字节一致（$(sha256sum "$A" | cut -c1-16)…）"
    else
        echo "  ✗ 内核产物不一致："
        sha256sum "$A" "$B"
        exit 1
    fi
fi

echo "=== 完成：$PKG ==="
