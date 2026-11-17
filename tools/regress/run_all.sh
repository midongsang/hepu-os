#!/bin/bash
# ============================================================================
# HepuOS 回归测试一条命令入口。
#
# 用法（在任意基于本仓库的 worktree 内）:
#   bash tools/regress/run_all.sh
#
# 可选环境变量:
#   SKIP_BUILD=1   跳过内核/镜像构建（复用已有 build_v250 / build_x86_64 / 镜像）
#   HEPU_EV=<dir>  证据输出目录（默认 <chat>/evidence-regress-2026-11-17）
#   HEPU_VNC=5902  本机 VNC 端口
#
# 步骤: 构建 -> 菜单 cfg -> 加载屏视频 -> 64 位链路 -> 32 位链路
#       -> 64 保护模式 -> 32 保护模式 -> 主机/冒烟测试 -> 帧度量 -> 报告
# 每个 QEMU 实例完整生命周期都在 /tmp/hepu-qemu.lock 内；编译在 /tmp/hepu-build.lock 内。
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"
cd "$WT"

STEPS="$EV/tests/steps.log"
mkdir -p "$EV/tests"
: > "$STEPS"

run_step() {
  local name="$1"; shift
  echo "==== STEP $name : $* ====" | tee -a "$STEPS"
  bash "$@" >> "$EV/tests/$name.log" 2>&1
  local rc=$?
  echo "[$name] exit=$rc" | tee -a "$STEPS"
  return $rc
}

[ "${SKIP_BUILD:-0}" != "1" ] && run_step build_all "$REGRESS_DIR/build_all.sh"
run_step patch_cfg  "$REGRESS_DIR/patch_menu_cfg.sh"
run_step loading    "$REGRESS_DIR/record_loading.sh" 30 6
run_step boot64     "$REGRESS_DIR/boot64.sh"
run_step boot32     "$REGRESS_DIR/boot32.sh"
run_step desktop32 "$REGRESS_DIR/desktop32_direct.sh"
run_step protect64 "$REGRESS_DIR/protect64.sh"
run_step protect32 "$REGRESS_DIR/protect32.sh"
run_step tests      "$REGRESS_DIR/run_tests.sh"

echo "==== 帧度量汇总 ====" | tee -a "$STEPS"
python3 "$REGRESS_DIR/analyze_frames.py" 2>&1 | tee -a "$STEPS"

echo "==== OBS-05 九宫格度量 ====" | tee -a "$STEPS"
bash "$REGRESS_DIR/obs05.sh" >> "$EV/tests/obs05.log" 2>&1
echo "[obs05] exit=$?" | tee -a "$STEPS"

echo "==== 生成报告 ====" | tee -a "$STEPS"
python3 "$REGRESS_DIR/make_report.py" 2>&1 | tee -a "$STEPS"

log "回归全链路结束，证据目录: $EV"
