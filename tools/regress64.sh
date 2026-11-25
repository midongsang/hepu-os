#!/bin/bash
# ============================================================================
# HepuOS 64 位镜像级回归（P4 / S4）
#
# 做什么：
#   1. 构建 64 位内核并调用既有 tools/make_single_img64.sh 做真实 ext2 镜像
#   2. 启动 64 位 QEMU，抓串口日志
#   3. 断言 S4 自检（非根目录 getdents / mkdir / 写回读）与无异常复位
#
# 用法：
#   bash tools/regress64.sh            # 构建 + 镜像 + 启动 + 断言
#   HEPU_SKIP_BUILD=1 bash tools/regress64.sh   # 复用已有 build_x86_64
#   HEPU_BOOT_TIMEOUT=60 bash tools/regress64.sh
#
# 退出码：0 = PASS，1 = FAIL
# ============================================================================
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REL_ROOT="$(cd "$ROOT/.." && pwd)"
T="${HEPU_TOOLCHAIN:-$REL_ROOT/.toolchain/pkg/root2}"
DD="${HEPU_DATADIR:-$REL_ROOT/.toolchain/pkg/datadir}"
IMG_SRC=/tmp/hepu64_single.img
IMG_RUN=/tmp/hepu64_regress.img
LOG=/tmp/hepu64_regress.log
QEMU_OUT=/tmp/hepu64_regress_qemu.out
TIMEOUT_SEC="${HEPU_BOOT_TIMEOUT:-50}"
FAIL=0

say() { echo "[REGRESS64] $*"; }

pick_qemu() {
    if [ -n "${HEPU_QEMU:-}" ] && [ -x "$HEPU_QEMU" ]; then
        echo "$HEPU_QEMU"; return 0
    fi
    for q in "$ROOT/tools/bin/qemu-system-x86_64" \
             "$T/usr/bin/qemu-system-x86_64" \
             "$(command -v qemu-system-x86_64 2>/dev/null)"; do
        if [ -n "$q" ] && [ -x "$q" ]; then echo "$q"; return 0; fi
    done
    return 1
}

QEMU="$(pick_qemu)" || { say "FAIL (找不到 qemu-system-x86_64)"; exit 1; }
if [ ! -d "$DD" ]; then
    say "FAIL (datadir 不存在: $DD)"; exit 1
fi

export PATH="$T/sbin:$T/usr/bin:$PATH"
export LD_LIBRARY_PATH="$T/usr/lib/x86_64-linux-gnu:$T/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QEMU_MODULE_DIR="$T/usr/lib/x86_64-linux-gnu/qemu"

if [ "${HEPU_SKIP_BUILD:-0}" != "1" ]; then
    say "构建 64 位内核..."
    # S4_SELFTEST 是编译期宏；make 不因 CFLAGS 变化重编已有 .o。
    # 不 clean 会在普通构建之后误用旧 kernel_main_64.o，导致 [S4U] 段缺失。
    make -C "$ROOT/kernel/arch/x86_64" clean >/dev/null 2>&1
    mkdir -p "$ROOT/build_x86_64"
    make -C "$ROOT/kernel/arch/x86_64" -j2 S4_SELFTEST=1 \
        >"$ROOT/build_x86_64/regress64_build.log" 2>&1 \
        || { say "FAIL (64 位构建)"; tail -40 "$ROOT/build_x86_64/regress64_build.log"; exit 1; }
fi

say "制作镜像（复用 tools/make_single_img64.sh）..."
bash "$ROOT/tools/make_single_img64.sh" > /tmp/hepu64_regress_img.log 2>&1 \
    || { say "FAIL (镜像制作)"; tail -30 /tmp/hepu64_regress_img.log; exit 1; }
[ -f "$IMG_SRC" ] || { say "FAIL (镜像不存在: $IMG_SRC)"; exit 1; }

run_once() {
    cp "$IMG_SRC" "$IMG_RUN" || return 1
    : > "$LOG"
    timeout "$TIMEOUT_SEC" "$QEMU" -L "$DD" -m 256 \
        -drive file="$IMG_RUN",format=raw,if=ide \
        -display none -vga std -cpu max -no-reboot \
        -serial "file:$LOG" >"$QEMU_OUT" 2>&1
    # timeout 124 是正常路径：内核跑桌面不会自己退出
    return 0
}

run_once

# 极少数启动会停在 Stage2 后复位；允许一次干净重试。
if ! grep -aq "\[S4\] fs selftest" "$LOG"; then
    say "首次启动未进入 S4，重试一次..."
    run_once
fi

say "串口断言..."
for pat in \
    "[S4] mkdir PASS" \
    "[S4] write-read PASS" \
    "[S4] getdents-nonroot PASS" \
    "[S4U] getdents-nonroot PASS" \
    "[S4] fs selftest PASS" \
    "[S4] result=0x1"
do
    if grep -aqF "$pat" "$LOG"; then
        say "  PASS  $pat"
    else
        say "  FAIL  缺少: $pat"
        FAIL=1
    fi
done

if grep -aqE "\[EXCEPTION\]|\[PANIC\]|\[ASSERT\] FAILED" "$LOG"; then
    say "  FAIL  串口出现异常/断言"
    grep -aE "\[EXCEPTION\]|\[PANIC\]|\[ASSERT\] FAILED" "$LOG" | tail -5
    FAIL=1
else
    say "  PASS  无异常/断言"
fi

if [ "$FAIL" -eq 0 ]; then
    say "REGRESS64: PASS"
else
    say "REGRESS64: FAIL"
fi
say "日志: $LOG"
exit "$FAIL"
