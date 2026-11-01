#!/bin/bash
# HepuOS 开发环境初始化脚本
# 每次会话开始时运行：source env_setup.sh

echo "=== HepuOS 开发环境初始化 ==="

# 1. 设置阿里云apt源
echo "[1/5] 设置阿里云apt源..."
echo "deb https://mirrors.aliyun.com/ubuntu/ jammy main restricted universe multiverse" | sudo tee /etc/apt/sources.list > /dev/null
echo "deb https://mirrors.aliyun.com/ubuntu/ jammy-updates main restricted universe multiverse" | sudo tee -a /etc/apt/sources.list > /dev/null
echo "deb https://mirrors.aliyun.com/ubuntu/ jammy-security main restricted universe multiverse" | sudo tee -a /etc/apt/sources.list > /dev/null

# 2. 创建临时目录
echo "[2/5] 创建临时目录..."
mkdir -p /tmp/hepu_bios_boot
mkdir -p /tmp/esp_mount

# 3. 检查并安装QEMU
echo "[3/5] 检查QEMU..."
if ! which qemu-system-i386 >/dev/null 2>&1; then
    echo "  QEMU未安装，正在安装..."
    sudo apt-get update -qq
    sudo apt-get install -y -qq qemu-system-x86 ovmf
else
    echo "  QEMU已安装: $(which qemu-system-i386)"
fi

# 4. 检查gnu-efi
echo "[4/5] 检查gnu-efi..."
if [ ! -f /tmp/gnu-efi-3.0.13/inc/efi.h ]; then
    echo "  gnu-efi未编译，正在下载编译..."
    cd /tmp
    wget -q https://sourceforge.net/projects/gnu-efi/files/gnu-efi-3.0.13.tar.bz2/download -O gnu-efi.tar.bz2
    tar xjf gnu-efi.tar.bz2
    cd gnu-efi-3.0.13
    make -j4 > /dev/null 2>&1
    echo "  gnu-efi编译完成"
else
    echo "  gnu-efi已编译: /tmp/gnu-efi-3.0.13/"
fi

# 5. 设置项目路径
echo "[5/5] 设置项目路径..."
export HEPU_PROJECT="/home/user/Doubao/chats/38442532206397698/hepu_os_src/hepu-os"
export HEPU_BUILD="$HEPU_PROJECT/build_v250"
export HEPU_TOOLS="$HEPU_PROJECT/tools/hepboot"
export PATH="$HEPU_PROJECT/tools:$PATH"

echo ""
echo "=== 环境初始化完成 ==="
echo "项目目录: $HEPU_PROJECT"
echo "内核: $HEPU_BUILD/hepu-kernel.bin"
echo "QEMU: $(which qemu-system-i386 2>/dev/null || echo '未安装')"
echo ""
echo "常用命令："
echo "  编译内核: cd $HEPU_PROJECT && make"
echo "  编译引导器: cd $HEPU_TOOLS && make"
echo "  测试BIOS: qemu-system-i386 -drive if=ide,format=raw,file=$HEPU_PROJECT/build/hepu-os-hybrid-v0.10.5.img -m 512M"
