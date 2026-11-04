#!/bin/bash
# ============================================================================
# 赫普OS - QEMU环境修复脚本
# 用于解决会话切换后QEMU依赖库丢失的问题
# ============================================================================

set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
QEMU_LIBS="$PROJECT_DIR/tools/qemu_libs"

echo "=== HepuOS QEMU环境修复 ==="

# 1. 检查QEMU二进制
if [ ! -f "$PROJECT_DIR/tools/bin/qemu-system-x86_64" ]; then
    echo "错误: QEMU二进制不存在"
    exit 1
fi
echo "✓ QEMU二进制存在"

# 2. 创建库目录
mkdir -p "$QEMU_LIBS/usr/lib/x86_64-linux-gnu"
mkdir -p "$QEMU_LIBS/lib/x86_64-linux-gnu"

# 3. 下载依赖库
download_lib() {
    local url="$1"
    local name="$2"
    if [ ! -f "$QEMU_LIBS/usr/lib/x86_64-linux-gnu/$name" ]; then
        echo "下载 $name..."
        wget -q "$url" -O /tmp/lib.deb 2>/dev/null
        if [ -s /tmp/lib.deb ]; then
            dpkg-deb -x /tmp/lib.deb "$QEMU_LIBS/" 2>/dev/null
            echo "  ✓ $name"
        else
            echo "  ✗ $name (下载失败)"
        fi
    fi
}

BASE_URL="https://mirrors.aliyun.com/ubuntu/pool"

# 下载已知可用的库
download_lib "$BASE_URL/main/p/pmdk/libpmem1_1.11.1-3build1_amd64.deb" "libpmem.so.1"
download_lib "$BASE_URL/main/d/device-tree-compiler/libfdt1_1.6.1-1_amd64.deb" "libfdt.so.1"
download_lib "$BASE_URL/main/liba/libaio/libaio1_0.3.112-13build1_amd64.deb" "libaio.so.1"
download_lib "$BASE_URL/main/libu/liburing/liburing2_2.1-2build1_amd64.deb" "liburing.so.2"
download_lib "$BASE_URL/main/f/fuse3/libfuse3-3_3.10.5-1build1_amd64.deb" "libfuse3.so.3"

# 4. 创建stub库（用于缺失的库）
create_stub() {
    local libname="$1"
    local soname="$2"
    if [ ! -f "$QEMU_LIBS/usr/lib/x86_64-linux-gnu/$soname" ]; then
        echo "创建stub: $soname"
        echo "void __stub(void) {}" > /tmp/stub.c
        gcc -shared -fPIC -o "$QEMU_LIBS/usr/lib/x86_64-linux-gnu/$soname" /tmp/stub.c 2>/dev/null
    fi
}

create_stub "librdmacm" "librdmacm.so.1"
create_stub "libibverbs" "libibverbs.so.1"
create_stub "libslirp" "libslirp.so.0"
create_stub "libndctl" "libndctl.so.6"
create_stub "libdaxctl" "libdaxctl.so.1"

# 5. 设置库路径
export LD_LIBRARY_PATH="$QEMU_LIBS/usr/lib/x86_64-linux-gnu:$QEMU_LIBS/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH"

# 6. 测试QEMU
echo ""
echo "测试QEMU..."
if "$PROJECT_DIR/tools/bin/qemu-system-x86_64" --version >/dev/null 2>&1; then
    echo "✓ QEMU可以正常运行"
    "$PROJECT_DIR/tools/bin/qemu-system-x86_64" --version 2>&1 | head -1
else
    echo "⚠ QEMU仍有问题，尝试运行查看错误:"
    "$PROJECT_DIR/tools/bin/qemu-system-x86_64" --version 2>&1 | head -5
fi

# 7. 检查其他工具
echo ""
echo "检查其他工具..."
for tool in mtools dosfstools gdisk parted; do
    if which $tool >/dev/null 2>&1; then
        echo "✓ $tool"
    else
        echo "✗ $tool (需要安装)"
    fi
done

echo ""
echo "=== 环境修复完成 ==="
echo "使用以下命令设置环境变量:"
echo "export LD_LIBRARY_PATH=$QEMU_LIBS/usr/lib/x86_64-linux-gnu:$QEMU_LIBS/lib/x86_64-linux-gnu:\$LD_LIBRARY_PATH"
