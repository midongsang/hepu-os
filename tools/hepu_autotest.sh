#!/bin/bash
# Hepu OS 自动化测试闭环脚本
# 用法: bash tools/hepu_autotest.sh [test_name]
# 功能: 编译→构建镜像→QEMU启动→截图→日志分析→结果报告

set -e

PROJECT_ROOT="/home/user/Doubao/chats/38442532206397698/hepu_os_src/hepu-os"
TEST_NAME="${1:-test_$(date +%H%M%S)}"
QEMU_BIN="$PROJECT_ROOT/tools/bin/qemu-system-x86_64"
BIOS="$PROJECT_ROOT/tools/firmware/bios-256k.bin"
IMG="/tmp/hepu64_single.img"
MON_SOCK="/tmp/qemu_mon_$$.sock"
SERIAL_LOG="/tmp/qemu_boot_$$.log"
SCREENSHOT="/tmp/${TEST_NAME}.ppm"
SCREENSHOT_PNG="/tmp/${TEST_NAME}.png"
RESULT_DIR="$PROJECT_ROOT/test_results"
SMP="${SMP:-1}"
MEM="${MEM:-128}"
BOOT_WAIT="${BOOT_WAIT:-8}"
KEY="${KEY:-f1}"

mkdir -p "$RESULT_DIR"

echo "========================================"
echo "Hepu OS 自动化测试: $TEST_NAME"
echo "========================================"

# 阶段1: 编译内核
echo "[1/5] 编译64位内核..."
cd "$PROJECT_ROOT/kernel/arch/x86_64"
COMPILE_LOG="$RESULT_DIR/${TEST_NAME}_compile.log"
if make 2>&1 | tee "$COMPILE_LOG" | grep -qE "error:|Error"; then
    echo "❌ 编译失败，详见 $COMPILE_LOG"
    exit 1
fi
KERNEL_SIZE=$(ls -lh "$PROJECT_ROOT/build_x86_64/hepu-kernel64.bin" | awk '{print $5}')
echo "✅ 编译成功，内核大小: $KERNEL_SIZE"

# 阶段2: 构建镜像
echo "[2/5] 构建单镜像..."
cd "$PROJECT_ROOT"
if ! bash tools/make_single_img64.sh > "$RESULT_DIR/${TEST_NAME}_build.log" 2>&1; then
    echo "❌ 镜像构建失败，详见 $RESULT_DIR/${TEST_NAME}_build.log"
    exit 1
fi
echo "✅ 镜像构建成功"

# 阶段3: 启动QEMU
echo "[3/5] 启动QEMU (smp=$SMP, mem=${MEM}M)..."
rm -f "$MON_SOCK" "$SERIAL_LOG"
"$QEMU_BIN" -m "$MEM" -smp "$SMP" -bios "$BIOS" \
    -drive format=raw,file="$IMG" \
    -vga std -display none \
    -monitor "unix:$MON_SOCK,server,nowait" \
    -serial "file:$SERIAL_LOG" &
QEMU_PID=$!
echo "QEMU PID: $QEMU_PID"

# 等待启动
sleep "$BOOT_WAIT"

# 检查QEMU是否还在运行
if ! kill -0 "$QEMU_PID" 2>/dev/null; then
    echo "❌ QEMU崩溃退出"
    echo "--- 串口日志最后30行 ---"
    tail -30 "$SERIAL_LOG"
    exit 1
fi

# 阶段4: 发送按键并截图
echo "[4/5] 发送按键 $KEY 并截图..."
python3 << PYEOF
import socket, time
try:
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect("$MON_SOCK")
    s.send(b'sendkey $KEY\n')
    time.sleep(2)
    s.send(b'screendump $SCREENSHOT\n')
    time.sleep(1)
    s.close()
    print("✅ 截图成功")
except Exception as e:
    print(f"❌ 截图失败: {e}")
PYEOF

# 转换为PNG
if [ -f "$SCREENSHOT" ]; then
    python3 -c "from PIL import Image; Image.open('$SCREENSHOT').save('$SCREENSHOT_PNG')"
    cp "$SCREENSHOT_PNG" "$RESULT_DIR/"
    echo "✅ 截图已保存: $RESULT_DIR/${TEST_NAME}.png"
fi

# 阶段5: 日志分析
echo "[5/5] 分析串口日志..."
cp "$SERIAL_LOG" "$RESULT_DIR/${TEST_NAME}_serial.log"

# 检查关键日志
echo "--- 关键启动日志 ---"
grep -E "\[TTF64\]|\[GFX64\]|\[HepuShell\]|Kernel|panic|error|fault|exception" "$SERIAL_LOG" | head -20 || echo "(无匹配)"

# 检查是否有panic或异常
if grep -qiE "panic|kernel panic|double fault|general protection|page fault" "$SERIAL_LOG"; then
    echo "❌ 检测到内核异常!"
    grep -iE "panic|fault|exception" "$SERIAL_LOG" | head -10
    TEST_RESULT="FAIL"
else
    echo "✅ 未检测到内核异常"
    TEST_RESULT="PASS"
fi

# 清理QEMU
kill "$QEMU_PID" 2>/dev/null || true
wait "$QEMU_PID" 2>/dev/null || true
rm -f "$MON_SOCK"

echo "========================================"
echo "测试结果: $TEST_RESULT"
echo "内核大小: $KERNEL_SIZE"
echo "截图: $RESULT_DIR/${TEST_NAME}.png"
echo "日志: $RESULT_DIR/${TEST_NAME}_serial.log"
echo "========================================"
