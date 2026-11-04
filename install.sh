#!/bin/bash
# ============================================================================
# 赫普OS 硬盘安装脚本 (V3.82)
#
# 用途: 把 combo 组合镜像安装到电脑硬盘
#   - combo 镜像 = MBR引导 + stage2 + 内核 + ext2数据分区, 单文件
#   - 安装 = 整盘写入目标硬盘(覆盖其原有分区表和数据)
#
# 用法: sudo bash install.sh /dev/sdX
#   例: sudo bash install.sh /dev/sdb
#
# 安全措施(双保险):
#   1. 拒绝安装到当前系统盘(/dev/sda 及挂载中的分区所在盘)
#   2. 要求输入目标盘符两次, 不一致则退出
#   3. 提示数据不可恢复, 需输入 YES 确认
# ============================================================================
set -e

COMBO="hepu-os-v1.02-combo.img"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ $# -ne 1 ]; then
    echo "用法: sudo bash install.sh /dev/sdX"
    echo "  例: sudo bash install.sh /dev/sdb"
    exit 1
fi
DEV="$1"

# 基本校验
[ -e "$COMBO" ] || COMBO="$SCRIPT_DIR/$COMBO"
[ -e "$COMBO" ] || { echo "找不到 $COMBO, 请放到脚本同目录"; exit 1; }
[ -b "$DEV" ] || { echo "$DEV 不是块设备"; exit 1; }

# 双保险1: 拒绝系统盘
if [ "$(basename "$DEV")" = "sda" ]; then
    echo "错误: 拒绝安装到 /dev/sda(通常为系统盘)。"
    echo "请确认目标盘后再执行。"; exit 1
fi
for part in $(lsblk -no NAME "$DEV" 2>/dev/null); do
    mnt=$(lsblk -no MOUNTPOINT "/dev/$part" 2>/dev/null | head -1)
    if [ -n "$mnt" ]; then
        echo "错误: $DEV 上存在已挂载分区($part → $mnt), 拒绝写入。"
        echo "请先卸载: sudo umount $mnt"; exit 1
    fi
done

echo "======================================================"
echo "  赫普OS 安装确认"
echo "  镜像: $COMBO ($(stat -c%s "$COMBO") 字节)"
echo "  目标: $DEV ($(lsblk -dno SIZE "$DEV") )"
echo "  ⚠️  整盘写入将覆盖 $DEV 上全部数据, 不可恢复!"
echo "======================================================"

# 双保险2: 输入两次目标盘符
read -p "请输入目标盘符(如 sdb): " V1
read -p "再次输入目标盘符确认: " V2
if [ "$V1" != "$V2" ] || [ "$V1" != "$(basename "$DEV")" ]; then
    echo "两次输入不一致或与参数不符, 已取消。"; exit 1
fi

# 双保险3: 键入 YES
read -p "输入 YES 确认覆盖安装: " V3
[ "$V3" = "YES" ] || { echo "未确认, 已取消。"; exit 1; }

echo "[1/3] 写入 combo 镜像到 $DEV ..."
dd if="$COMBO" of="$DEV" bs=4M status=progress conv=fsync

echo "[2/3] 刷新缓存 ..."
sync

echo "[3/3] 完成!"
echo "------------------------------------------------------"
echo "  安装完成。现在可以:"
echo "  1) 重启并从 $DEV 引导: sudo reboot"
echo "  2) 开机进入 BIOS 引导菜单, 选择该硬盘启动"
echo "  3) 系统启动后进入图形界面/终端"
echo "------------------------------------------------------"
