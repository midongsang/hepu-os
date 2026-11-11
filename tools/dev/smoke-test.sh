#!/usr/bin/env bash
# 启动冒烟测试: 启动一次 -> 等桌面 -> 断言串口没有致命错误 -> 截图 -> 断言画面非全黑。
# 把之前"启动完人工看截图"的动作变成可断言的回归。
#
# 用法: tools/dev/smoke-test.sh [--keep]     --keep 测试后不关闭 QEMU
set -u
here=$(cd "$(dirname "$0")" && pwd)
. "$here/qemu-common.sh"

KEEP=0
[ "${1:-}" = "--keep" ] && KEEP=1

FAIL=0
note() { printf '  [%s] %s\n' "$1" "$2"; }

echo "=== 赫普OS 启动冒烟测试 ==="
rm -f "$SERIAL"
stop_qemu
# shellcheck disable=SC2046
qemu-system-x86_64 $(qemu_common_args) -daemonize -pidfile "$PID" 2>/dev/null

if wait_desktop 12; then
    note ok "进入桌面 (Hepu Desktop started)"
else
    note fail "未进入桌面"
    tail -5 "$SERIAL"
    FAIL=1
fi

echo "--- 致命错误扫描 ---"
for pat in PANIC "triple fault" "KERNEL EXCEPTION" "Page Fault" "divide by zero"; do
    if grep -qi "$pat" "$SERIAL" 2>/dev/null; then
        note fail "串口出现 '$pat'"
        grep -i -m3 "$pat" "$SERIAL"
        FAIL=1
    fi
done
[ "$FAIL" = 0 ] && note ok "无致命错误关键字"

echo "--- 画面检查 ---"
if [ "$FAIL" = 0 ]; then
    SHOT=/tmp/smoke.png
    python3 "$here/shot.py" -o "$SHOT" >/dev/null 2>&1
    if [ -f "$SHOT" ]; then
        read -r uniq <<<"$(python3 - "$SHOT" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
cols = im.getcolors(maxcolors=1000000) or []
print(len(cols))
PY
)"
        if [ "${uniq:-0}" -lt 8 ]; then
            note fail "画面近似纯色 (颜色数=$uniq)，疑似黑屏"
            FAIL=1
        else
            note ok "画面正常 (颜色数=$uniq) -> $SHOT"
        fi
    else
        note fail "截图失败"
        FAIL=1
    fi
fi

[ "$KEEP" = 1 ] || stop_qemu

if [ "$FAIL" = 0 ]; then
    echo "=== 结果: 通过 ==="
else
    echo "=== 结果: 失败 ==="
fi
exit "$FAIL"
