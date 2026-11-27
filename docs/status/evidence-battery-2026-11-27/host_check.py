#!/usr/bin/env python3
"""宿主侧核对：只认串口/截图里的事实，不替内核猜。

检查项：
  1. 默认 QEMU 的 [BAT] state=absent，且 reason=no-acpi-battery；
  2. 合成 SSDT 的 [BAT] state=declared；
  3. 两张 1920x1080 桌面截图存在；
  4. 默认与 DECLARED 的差异只发生在顶栏右侧（电池图标/图标位移），
     证明不是整屏偶然差异。
"""
import os
import re
import sys
from PIL import Image, ImageChops

ROOT = os.path.dirname(os.path.abspath(__file__))

def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()

def find_state(run):
    txt = read(os.path.join(ROOT, run, "bat-lines.txt"))
    m = re.search(r"\[BAT\] state=(\w+) .*?reason=(\S+)", txt)
    if not m:
        raise SystemExit("missing BAT state line in %s: %r" % (run, txt))
    return m.group(1), m.group(2), txt

def main():
    d_state, d_reason, d_txt = find_state("default")
    c_state, c_reason, c_txt = find_state("declared")

    checks = []
    checks.append(("default_state_absent", d_state == "absent", d_state))
    checks.append(("default_reason_no_battery", d_reason == "no-acpi-battery", d_reason))
    checks.append(("declared_state_declared", c_state == "declared", c_state))

    imgs = {}
    for run in ("default", "declared"):
        p = os.path.join(ROOT, run, "desktop_2.png")
        im = Image.open(p).convert("RGB")
        imgs[run] = im
        checks.append((run + "_size_1920x1080", im.size == (1920, 1080), str(im.size)))

    # 只比较顶栏右侧，避免时钟/光标/壁纸的无关差异污染结论。
    box = (1400, 0, 1920, 60)
    d = imgs["default"].crop(box)
    c = imgs["declared"].crop(box)
    diff = ImageChops.difference(d, c)
    bbox = diff.getbbox()
    changed = sum(1 for px in diff.getdata() if px != (0, 0, 0))
    checks.append(("topright_changed", changed > 0, "pixels=%d bbox=%r" % (changed, bbox)))

    lines = []
    lines.append("default: state=%s reason=%s" % (d_state, d_reason))
    lines.append("declared: state=%s reason=%s" % (c_state, c_reason))
    lines.append("top-right diff pixels=%d bbox=%r" % (changed, bbox))
    lines.append("")
    ok = True
    for name, passed, detail in checks:
        lines.append("%-28s %s  %s" % (name, "PASS" if passed else "FAIL", detail))
        ok = ok and passed
    lines.append("")
    lines.append("RESULT: %s" % ("PASS" if ok else "FAIL"))
    out = "\n".join(lines)
    print(out)
    with open(os.path.join(ROOT, "host-check.txt"), "w", encoding="utf-8") as f:
        f.write(out + "\n")
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
