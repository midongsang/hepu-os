#!/bin/bash
# ============================================================================
# HepuOS 回归测试 —— 公共环境（被 run_all.sh / 各单步脚本 source）
#
# 口径：
#   - 只操作本 worktree（WT），不碰 main、不碰其他 wt-* 目录；不 push。
#   - 构建串行：flock /tmp/hepu-build.lock
#   - QEMU 全机串行：flock /tmp/hepu-qemu.lock；实例目录 inst/regress，VNC 5902，
#     只按自己 pidfile 精确 kill，禁止 pkill 全局。
#   - 证据根目录 EV（可被环境变量 HEPU_EV 覆盖）。
# ============================================================================

# ---- 定位仓库与会话目录 ----
REGRESS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export WT="$(cd "$REGRESS_DIR/../.." && pwd)"
export CHAT="$(cd "$WT/.." && pwd)"

# ---- 工具链（三条环境变量缺一不可）----
export T="$CHAT/.toolchain/pkg/root2"
export PATH="$T/sbin:$T/usr/bin:$PATH"
export LD_LIBRARY_PATH="$T/usr/lib/x86_64-linux-gnu:$T/lib/x86_64-linux-gnu"
export QEMU_MODULE_DIR="$T/usr/lib/x86_64-linux-gnu/qemu"

# ---- 固件 / 运行实例 / 证据 ----
export V="$CHAT/.vm-assets"
export RUN="$CHAT/.toolchain/run"
export INST="$RUN/inst/regress"
export EV="${HEPU_EV:-$CHAT/evidence-regress-2026-11-17}"
export IMG="$EV/build/hepu.img"
export VNCPORT="${HEPU_VNC:-5902}"

# ---- 基线与夹具口令 ----
# 仓库代码里没有内置默认口令：OOBE 向导口令由用户首设（auth64_create_account），
# 测试夹具在 /account.conf 写入 sha256(hepu123)，口令 hepu123 仅为本回归 fixture 选择，
# 不是发行版默认口令。
export BASELINE="${HEPU_BASELINE:-61c53fa}"
export FIXTURE_PASSWORD="${HEPU_FIXTURE_PASS:-hepu123}"

# ---- 锁 ----
BUILD_LOCK=/tmp/hepu-build.lock
QEMU_LOCK=/tmp/hepu-qemu.lock

mkdir -p "$INST" "$EV/build" "$EV/frames64" "$EV/frames32" \
         "$EV/protect64" "$EV/protect32" "$EV/loading" "$EV/tests"

# 桌面判据阈值（1920x1080 = 2,073,600 像素）：
#   桌面：接近全量非黑才算真桌面，NONBLACK >= 2,073,597（沿用既往基线口径）
#   锁屏：米白底，全屏非黑属正常，不设桌面阈值；只记录计数与画面目视判据。
export DESKTOP_NONBLACK_MIN=2073597
export TOTAL_PIXELS=2073600

log() { echo "[regress] $*"; }
