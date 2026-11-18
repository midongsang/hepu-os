#!/bin/bash
# ============================================================================
# OBS-07 32 位冒烟判据自测（私有复现版）
#
# 判据口径与 tools/regress32.sh 头部注释一致：
#   serial : 含 `[SERIAL] COM1 16550 UART ready`
#   initrd : 含 `head=0x55504548`（initrd "HEPU" 魔数）
#   fault  : 不含 `PANIC|#GP|#PF|fatal error`（=0 才算过）
#   net    : 含 `[NET-TEST] === Self-Test Complete ===`
#   desk   : 含 `[DESKTOP] elastic free desktop init done`
#   bound  : tools/check_gfx_boundary.sh 退出码 0
#
# 说明：本脚本只对指定串口日志做 grep 判定，不启动 QEMU、不 pkill、不写共享
# 目录（regress32.sh 内部 `pkill -9 -x qemu-system-i386` 会杀其他 agent 的
# QEMU，故按其判据在此私有路径复现，不直接执行该脚本）。
# 用法: bash evidence-linuxhello32/smoke_check.sh <serial.log>
# ============================================================================
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOG="${1:?usage: smoke_check.sh <serial.log>}"

grep_q() { grep -aqE "$2" "$1"; }
grep_f() { grep -aqF "$2" "$1"; }

have_serial=0; have_initrd=0; have_fault=0; have_net=0; have_desk=0
grep_f "$LOG" '[SERIAL] COM1 16550 UART ready' && have_serial=1
grep_f "$LOG" 'head=0x55504548' && have_initrd=1
grep_q "$LOG" 'PANIC|#GP|#PF|fatal error' && have_fault=1
grep_f "$LOG" '[NET-TEST] === Self-Test Complete ===' && have_net=1
grep_f "$LOG" '[DESKTOP] elastic free desktop init done' && have_desk=1

have_bound=0
if bash "$ROOT/tools/check_gfx_boundary.sh" >/dev/null 2>&1; then have_bound=1; fi

echo "log=$LOG"
echo "serial=$have_serial initrd=$have_initrd net=$have_net desk=$have_desk fault=$have_fault bound=$have_bound"

if [ "$have_serial" -eq 1 ] && [ "$have_initrd" -eq 1 ] && [ "$have_net" -eq 1 ] \
   && [ "$have_desk" -eq 1 ] && [ "$have_fault" -eq 0 ] && [ "$have_bound" -eq 1 ]; then
    echo "== 冒烟 PASS"
    exit 0
fi
echo "== 冒烟 FAIL"
exit 1
