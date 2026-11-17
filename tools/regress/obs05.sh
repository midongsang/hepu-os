#!/bin/bash
# ============================================================================
# OBS-05 九宫格（3x3）非黑像素统计 + 网格叠加对照图。
# 覆盖：32 位直启桌面帧、64 位锁屏帧（32 位本树无锁屏，N/A）。
# 口径见 obs05_grid.py 头部注释；产物落 $EV/tests/obs05-*.json 与 $EV/obs05/。
#
# 用法: bash tools/regress/obs05.sh
# ============================================================================
set -u
source "$(dirname "$0")/env.sh"

OUT="$EV/tests"
mkdir -p "$OUT" "$EV/obs05"

run_grid() {
  local prefix="$1"; shift
  local json="$OUT/obs05-$prefix.json"
  # 展开失败（无帧）时传空列表，脚本内部跳过不存在路径
  local frames=("$@")
  if [ "${#frames[@]}" -eq 0 ]; then
    echo "[obs05] $prefix: 无截图可测"
    return 0
  fi
  python3 "$REGRESS_DIR/obs05_grid.py" "${frames[@]}" \
    --out-json "$json" --prefix "$prefix" --overlay-dir "$EV/obs05"
}

run_grid "32-desktop" $EV/frames32-direct/desktop_*.png
run_grid "64-lock"    $EV/frames64/lock_*.png
run_grid "64-desktop"  $EV/frames64/desktop_*.png

echo "[obs05] done -> $EV/obs05"
