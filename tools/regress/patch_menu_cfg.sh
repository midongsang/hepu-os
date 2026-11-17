#!/bin/bash
# 把 ESP 里 HEPBOOT.CFG 的 timeout 调到 30s，留出菜单取证窗口（默认 5s 来不及截 4 帧）。
# 用法: bash tools/regress/patch_menu_cfg.sh
set -u
source "$(dirname "$0")/env.sh"

cat > "$EV/build/_cfg30.cfg" <<'EOF'
# HepuBoot UEFI Configuration v0.57.0
timeout=30
default=0
kernel_arch=64
cmdline=
EOF

mcopy -i "$IMG@@1M" -o "$EV/build/_cfg30.cfg" ::/HEPBOOT.CFG || exit 1
log "menu cfg patched:"
mtype -i "$IMG@@1M" ::/HEPBOOT.CFG
